// 行为层实现：模式状态机与按键路由（第 5 阶段）。
// 反馈全部走串口：每次显示变化打印当前页面 + 提示行；数据页的 ranker 由 main 刷新后重绘。
#include "InputManager.h"

#include "GpsManager.h"
#include "VisibilityRanker.h"
#include "StarCatalog.h"
#include "TimeTravelManager.h"
#include "CalibrationManager.h"
#include "OrientationManager.h"

namespace {

// ---------- 内部状态 ----------
typedef enum { CAL_BROWSE, CAL_SELECTED, CAL_DONE } CalPhase;

const int MENU_COUNT = 4;                 // 定位/校准/观星/时空旅行
const int CAL_TOP_N = 5;                  // 校准页展示前 N 颗好找天体
const int TT_FIELD_COUNT = 5;             // 年/月/日/时/分
const int TT_CONFIRM_INDEX = TT_FIELD_COUNT; // 第 6 项 = 确认启动

InputMode s_mode = IM_MENU;
CalPhase  s_calPhase = CAL_BROWSE;
int       s_menuCursor = 0;
int       s_listCursor = 0;
int       s_selectedTarget = -1;
int       s_ttFieldCursor = 0;
astro_utc_t s_ttBuffer = {2000, 1, 1, 0, 0, 0.0};
astro_utc_t s_baseTime = {2000, 1, 1, 0, 0, 0.0};
unsigned long s_doneMs = 0;

bool s_dataDirty = false;
bool s_needRender = false;

// ---------- 小工具 ----------
void markRender()    { s_needRender = true; }
void markDataDirty() { s_dataDirty = true; }

int clampInt(int v, int lo, int hi)
{
	if (v < lo) return lo;
	if (v > hi) return hi;
	return v;
}

double wrapDeg(double deg)
{
	while (deg > 180.0) deg -= 360.0;
	while (deg < -180.0) deg += 360.0;
	return deg;
}

void printTargetName(const RankedTarget *t)
{
	if (t->kind == TARGET_STAR)
		Serial.print(STAR_CATALOG[t->catalogIndex].nameAscii);
	else
		Serial.print(BODY_CATALOG[t->catalogIndex].nameAscii);
}

void printTargetLine(int idx, const RankedTarget *t, bool cursor)
{
	Serial.printf("%c %2d  ", cursor ? '>' : ' ', idx + 1);
	printTargetName(t);
	Serial.printf("  方位%+6.1f 高度%+6.1f\n", t->azimuth, t->altitude);
}

void gotoMenu()
{
	s_mode = IM_MENU;
	s_menuCursor = 0;
	markRender();
}

// ---------- 进入各页 ----------
void enterLocation()
{
	s_mode = IM_LOCATION;
	markRender();
}

void enterCalibrate()
{
	s_mode = IM_CALIBRATE;
	s_calPhase = CAL_BROWSE;
	s_listCursor = 0;
	s_selectedTarget = -1;
	markDataDirty(); // 需要 fresh ranker 取 Top5
	markRender();
}

void enterObserveLive()
{
	s_mode = IM_OBSERVE_LIVE;
	s_selectedTarget = -1;
	markRender();
}

void enterObserveList()
{
	s_mode = IM_OBSERVE_LIST;
	s_listCursor = 0;
	s_selectedTarget = -1;
	markDataDirty();
	markRender();
}

void enterTimeTravel()
{
	s_mode = IM_TT_SELECT;
	s_ttFieldCursor = 0;
	s_ttBuffer = timetravelIsActive() ? timetravelGetOverride() : s_baseTime;
	markRender();
}

// ---------- 页面渲染 ----------
void renderMenu()
{
	Serial.println("===== 模式选择 =====");
	static const char *items[] = {"定位", "校准", "观星", "时空旅行"};
	for (int i = 0; i < MENU_COUNT; i++)
		Serial.printf("%c %s\n", (i == s_menuCursor) ? '>' : ' ', items[i]);
	Serial.println("UP/DOWN 选择   A 进入");
}

void renderLocation()
{
	Serial.println("===== 定位 =====");
	Serial.printf("状态: %s\n", gpsManagerStatusTag());
	astro_observer_t obs;
	if (gpsManagerGetLocation(&obs))
		Serial.printf("位置: lat=%.4f lon=%.4f h=%.0fm\n", obs.latitude, obs.longitude, obs.height);
	else
		Serial.println("位置: 搜索中...");
	astro_utc_t utc;
	if (gpsManagerGetUtc(&utc))
		Serial.printf("时间: %04d-%02d-%02d %02d:%02d:%02d UTC\n",
			      utc.year, utc.month, utc.day, utc.hour, utc.minute, (int)utc.second);
	Serial.println("A 重新定位   B 返回");
}

void renderCalibrate()
{
	if (s_calPhase == CAL_BROWSE) {
		Serial.println("===== 校准（单点验证） =====");
		Serial.println("选择一颗好找的天体:");
		int n = rankerCount();
		int topN = n < CAL_TOP_N ? n : CAL_TOP_N;
		if (topN == 0) {
			Serial.println("  （当前无可选天体）");
		} else {
			for (int i = 0; i < topN; i++)
				printTargetLine(i, rankerAt(i), i == s_listCursor);
		}
		double azRes, altRes;
		if (calibrationLoadLast(&azRes, &altRes))
			Serial.printf("上次校准残差: 方位%+.1f 高度%+.1f\n", azRes, altRes);
		Serial.println("UP/DOWN 切换   A 选择   B 返回");
	} else if (s_calPhase == CAL_SELECTED) {
		Serial.println("===== 校准（已选目标） =====");
		const RankedTarget *t = rankerAt(s_selectedTarget);
		if (t != NULL) {
			Serial.print("目标: ");
			printTargetName(t);
			Serial.printf("  方位%+.1f 高度%+.1f\n", t->azimuth, t->altitude);
		}
		Serial.println("A 校准（记录瞄准并比对）   B 返回");
	} else { // CAL_DONE
		Serial.println("===== 校准 =====");
		Serial.println("已校准（残差已写入闪存）");
		Serial.println("3 秒后返回主菜单，按 B 立即返回");
	}
}

void renderObserveLive()
{
	Serial.println("===== 观星 · 直接观星 =====");
	Serial.println("（FOV 内星名渲染待阶段3 RenderManager）");
	Serial.println("A 推荐名录   B 返回");
}

void renderObserveList()
{
	Serial.println("===== 观星 · 推荐名录 =====");
	int n = rankerCount();
	if (n == 0) {
		Serial.println("  （当前无可选目标）");
	} else {
		for (int i = 0; i < n; i++)
			printTargetLine(i, rankerAt(i), i == s_listCursor);
	}
	Serial.println("UP/DOWN 浏览   A 选中   B 返回");
}

void renderGuide()
{
	Serial.println("===== 观星 · 转向指导 =====");
	const RankedTarget *t = rankerAt(s_selectedTarget);
	if (t == NULL) {
		Serial.println("（无选中目标）");
	} else {
		Serial.print("目标: ");
		printTargetName(t);
		Serial.printf("  方位%+.1f 高度%+.1f\n", t->azimuth, t->altitude);
		ImuOrientation o;
		if (orientationGet(&o)) {
			double daz = wrapDeg(t->azimuth - o.yaw);
			Serial.printf("当前 yaw=%+.1f, 水平需转 %+.1f（%s）\n",
				      o.yaw, daz, daz >= 0 ? "向右" : "向左");
		} else {
			Serial.println("（无朝向数据，无法计算箭头）");
		}
	}
	Serial.println("B 返回");
}

void renderTtSelect()
{
	Serial.println("===== 时空旅行 · 选择字段 =====");
	static const char *fields[] = {"年", "月", "日", "时", "分"};
	int vals[] = {s_ttBuffer.year, s_ttBuffer.month, s_ttBuffer.day,
		      s_ttBuffer.hour, s_ttBuffer.minute};
	for (int i = 0; i < TT_FIELD_COUNT; i++)
		Serial.printf("%c %s: %d\n", (i == s_ttFieldCursor) ? '>' : ' ', fields[i], vals[i]);
	Serial.printf("%c 确认启动\n", (s_ttFieldCursor == TT_CONFIRM_INDEX) ? '>' : ' ');
	Serial.println("UP/DOWN 选字段   A 进入/确认   B 返回");
}

void renderTtEdit()
{
	static const char *fields[] = {"年", "月", "日", "时", "分"};
	int vals[] = {s_ttBuffer.year, s_ttBuffer.month, s_ttBuffer.day,
		      s_ttBuffer.hour, s_ttBuffer.minute};
	Serial.println("===== 时空旅行 · 编辑 =====");
	for (int i = 0; i < TT_FIELD_COUNT; i++)
		Serial.printf("%c %s: %d\n", (i == s_ttFieldCursor) ? '*' : ' ', fields[i], vals[i]);
	Serial.println("UP/DOWN 加减   A 确认   B 取消");
}

// ---------- 校准动作 ----------
void doCalibrate()
{
	double azRes, altRes;
	if (calibrationVerify(s_selectedTarget, &azRes, &altRes)) {
		s_calPhase = CAL_DONE;
		s_doneMs = millis();
		Serial.printf("校准完成: 方位残差%+.1f 高度残差%+.1f\n", azRes, altRes);
	} else {
		Serial.println("校准失败: 无朝向数据（请先串口 O 注入朝向或连接 IMU）");
	}
	markRender();
}

// ---------- 时空旅行字段加减 ----------
void ttAdjust(int delta)
{
	switch (s_ttFieldCursor) {
	case 0: s_ttBuffer.year   = clampInt(s_ttBuffer.year + delta, 2000, 2100); break;
	case 1: s_ttBuffer.month  = clampInt(s_ttBuffer.month + delta, 1, 12);    break;
	case 2: s_ttBuffer.day    = clampInt(s_ttBuffer.day + delta, 1, 31);      break;
	case 3: s_ttBuffer.hour   = clampInt(s_ttBuffer.hour + delta, 0, 23);     break;
	case 4: s_ttBuffer.minute = clampInt(s_ttBuffer.minute + delta, 0, 59);   break;
	}
	markRender();
}

// ---------- 各状态按键处理 ----------
void handleMenu(ButtonId btn)
{
	switch (btn) {
	case BTN_UP:
		s_menuCursor = (s_menuCursor + MENU_COUNT - 1) % MENU_COUNT;
		markRender();
		break;
	case BTN_DOWN:
		s_menuCursor = (s_menuCursor + 1) % MENU_COUNT;
		markRender();
		break;
	case BTN_A:
		switch (s_menuCursor) {
		case 0: enterLocation();    break;
		case 1: enterCalibrate();   break;
		case 2: enterObserveLive(); break;
		case 3: enterTimeTravel();  break;
		}
		break;
	case BTN_B:
	default:
		break; // 顶层无操作
	}
}

void handleLocation(ButtonId btn)
{
	switch (btn) {
	case BTN_A:
		gpsManagerRequestFreshFix();
		markRender();
		break;
	case BTN_B:
		gotoMenu();
		break;
	default:
		break;
	}
}

void handleCalibrate(ButtonId btn)
{
	if (s_calPhase == CAL_BROWSE) {
		int n = rankerCount();
		int topN = n < CAL_TOP_N ? n : CAL_TOP_N;
		switch (btn) {
		case BTN_UP:
			if (topN > 0) {
				s_listCursor = (s_listCursor + topN - 1) % topN;
				markRender();
			}
			break;
		case BTN_DOWN:
			if (topN > 0) {
				s_listCursor = (s_listCursor + 1) % topN;
				markRender();
			}
			break;
		case BTN_A:
			if (topN > 0) {
				s_calPhase = CAL_SELECTED;
				s_selectedTarget = s_listCursor;
				markRender();
			}
			break;
		case BTN_B:
			gotoMenu();
			break;
		default:
			break;
		}
	} else if (s_calPhase == CAL_SELECTED) {
		switch (btn) {
		case BTN_A:
			doCalibrate();
			break;
		case BTN_B:
			gotoMenu();
			break;
		default:
			break; // UP/DOWN 无操作
		}
	} else { // CAL_DONE
		if (btn == BTN_B)
			gotoMenu();
	}
}

void handleObserveLive(ButtonId btn)
{
	switch (btn) {
	case BTN_A:
		enterObserveList();
		break;
	case BTN_B:
		gotoMenu();
		break;
	default:
		break;
	}
}

void handleObserveList(ButtonId btn)
{
	int n = rankerCount();
	switch (btn) {
	case BTN_UP:
		if (s_listCursor > 0) {
			s_listCursor--;
			markRender();
		}
		break;
	case BTN_DOWN:
		if (s_listCursor < n - 1) {
			s_listCursor++;
			markRender();
		}
		break;
	case BTN_A:
		if (n > 0) {
			s_selectedTarget = s_listCursor;
			s_mode = IM_OBSERVE_GUIDE;
			markRender();
		}
		break;
	case BTN_B:
		enterObserveLive();
		break;
	default:
		break;
	}
}

void handleGuide(ButtonId btn)
{
	if (btn == BTN_B) {
		s_mode = IM_OBSERVE_LIST;
		markRender();
	}
	// A 无操作
}

void handleTtSelect(ButtonId btn)
{
	switch (btn) {
	case BTN_UP:
		s_ttFieldCursor = (s_ttFieldCursor + TT_FIELD_COUNT) % (TT_FIELD_COUNT + 1);
		markRender();
		break;
	case BTN_DOWN:
		s_ttFieldCursor = (s_ttFieldCursor + 1) % (TT_FIELD_COUNT + 1);
		markRender();
		break;
	case BTN_A:
		if (s_ttFieldCursor == TT_CONFIRM_INDEX) {
			timetravelSet(s_ttBuffer.year, s_ttBuffer.month, s_ttBuffer.day,
				      s_ttBuffer.hour, s_ttBuffer.minute, 0.0);
			Serial.printf("时空旅行生效: %04d-%02d-%02d %02d:%02d:00 UTC\n",
				      s_ttBuffer.year, s_ttBuffer.month, s_ttBuffer.day,
				      s_ttBuffer.hour, s_ttBuffer.minute);
			enterObserveLive();
		} else {
			s_mode = IM_TT_EDIT;
			markRender();
		}
		break;
	case BTN_B:
		gotoMenu();
		break;
	default:
		break;
	}
}

void handleTtEdit(ButtonId btn)
{
	switch (btn) {
	case BTN_UP:
		ttAdjust(+1);
		break;
	case BTN_DOWN:
		ttAdjust(-1);
		break;
	case BTN_A:
		s_mode = IM_TT_SELECT;
		markRender();
		break;
	case BTN_B:
		s_mode = IM_TT_SELECT;
		markRender();
		break;
	default:
		break;
	}
}

} // namespace

void inputManagerInit()
{
	s_mode = IM_MENU;
	s_menuCursor = 0;
	s_calPhase = CAL_BROWSE;
	s_listCursor = 0;
	s_selectedTarget = -1;
	s_ttFieldCursor = 0;
	markRender();
}

void inputManagerSetBaseTime(astro_utc_t utc)
{
	s_baseTime = utc;
}

void inputManagerUpdate(ButtonId btn)
{
	// 校准 DONE 的 3s 自动返回（非阻塞）
	if (s_mode == IM_CALIBRATE && s_calPhase == CAL_DONE) {
		if (millis() - s_doneMs >= 3000) {
			gotoMenu();
			return;
		}
	}

	if (btn == BTN_NONE)
		return;

	switch (s_mode) {
	case IM_MENU:          handleMenu(btn);         break;
	case IM_LOCATION:      handleLocation(btn);     break;
	case IM_CALIBRATE:     handleCalibrate(btn);    break;
	case IM_OBSERVE_LIVE:  handleObserveLive(btn);  break;
	case IM_OBSERVE_LIST:  handleObserveList(btn);  break;
	case IM_OBSERVE_GUIDE: handleGuide(btn);        break;
	case IM_TT_SELECT:     handleTtSelect(btn);     break;
	case IM_TT_EDIT:       handleTtEdit(btn);       break;
	}
}

InputMode inputManagerMode()
{
	return s_mode;
}

int inputManagerListCursor()
{
	return s_listCursor;
}

int inputManagerSelectedTarget()
{
	return s_selectedTarget;
}

bool inputManagerConsumeDataDirty()
{
	bool d = s_dataDirty;
	s_dataDirty = false;
	return d;
}

bool inputManagerConsumeRender()
{
	bool r = s_needRender;
	s_needRender = false;
	return r;
}

void inputManagerRender()
{
	switch (s_mode) {
	case IM_MENU:          renderMenu();         break;
	case IM_LOCATION:      renderLocation();     break;
	case IM_CALIBRATE:     renderCalibrate();    break;
	case IM_OBSERVE_LIVE:  renderObserveLive();  break;
	case IM_OBSERVE_LIST:  renderObserveList();  break;
	case IM_OBSERVE_GUIDE: renderGuide();        break;
	case IM_TT_SELECT:     renderTtSelect();     break;
	case IM_TT_EDIT:       renderTtEdit();       break;
	}
}
