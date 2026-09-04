// 行为层实现：模式状态机与按键路由（第 5 阶段）。
// 只负责状态与路由，不渲染（渲染在 RenderManager）。
#include "InputManager.h"

#include "GpsManager.h"
#include "VisibilityRanker.h"
#include "TimeTravelManager.h"
#include "CalibrationManager.h"

namespace {

// ---------- 内部状态 ----------
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

void inputManagerGetSnapshot(InputSnapshot *out)
{
	if (out == NULL)
		return;
	out->mode = s_mode;
	out->calPhase = s_calPhase;
	out->menuCursor = s_menuCursor;
	out->listCursor = s_listCursor;
	out->selectedTarget = s_selectedTarget;
	out->ttFieldCursor = s_ttFieldCursor;
	out->ttBuffer = s_ttBuffer;
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
