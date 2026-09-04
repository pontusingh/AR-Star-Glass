// 行为层实现：统一渲染（串口全量 + OLED 精简）。
// 串口/OLED 共用同一页面内容逻辑（条目、光标、窗口），仅排版不同。
#include "RenderManager.h"

#include "InputManager.h"
#include "DisplayDriver.h"
#include "GpsManager.h"
#include "VisibilityRanker.h"
#include "StarCatalog.h"
#include "CalibrationManager.h"
#include "OrientationManager.h"
#include <stdio.h>
#include <math.h>

namespace {

const int OLED_ROWS = 4;
const int CAL_TOP_N = 5;
const int TT_FIELD_COUNT = 5;
const int TT_CONFIRM_INDEX = TT_FIELD_COUNT;

// ---------- 共享工具 ----------
String targetName(const RankedTarget *t)
{
	if (t->kind == TARGET_STAR)
		return String(STAR_CATALOG[t->catalogIndex].nameAscii);
	return String(BODY_CATALOG[t->catalogIndex].nameAscii);
}

double wrapDeg(double deg)
{
	while (deg > 180.0) deg -= 360.0;
	while (deg < -180.0) deg += 360.0;
	return deg;
}

// 4 行窗口起始下标（光标尽量居中偏上）
int windowStart(int cursor, int count, int rows)
{
	if (count <= rows) return 0;
	int s = cursor - 1;
	if (s < 0) s = 0;
	if (s > count - rows) s = count - rows;
	return s;
}

// ---------- 串口渲染（全量） ----------
void serialTargetLine(int idx, const RankedTarget *t, bool cursor)
{
	Serial.printf("%c %2d  ", cursor ? '>' : ' ', idx + 1);
	Serial.print(targetName(t));
	Serial.printf("  方位%+6.1f 高度%+6.1f\n", t->azimuth, t->altitude);
}

void serialMenu(const InputSnapshot &s)
{
	Serial.println("===== 模式选择 =====");
	static const char *items[] = {"定位", "校准", "观星", "时空旅行"};
	for (int i = 0; i < 4; i++)
		Serial.printf("%c %s\n", (i == s.menuCursor) ? '>' : ' ', items[i]);
	Serial.println("UP/DOWN 选择   A 进入");
}

void serialLocation(const InputSnapshot &s)
{
	(void)s;
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

void serialCalibrate(const InputSnapshot &s)
{
	if (s.calPhase == CAL_BROWSE) {
		Serial.println("===== 校准（单点验证） =====");
		Serial.println("选择一颗好找的天体:");
		int n = rankerCount();
		int topN = n < CAL_TOP_N ? n : CAL_TOP_N;
		if (topN == 0) {
			Serial.println("  （当前无可选天体）");
		} else {
			for (int i = 0; i < topN; i++)
				serialTargetLine(i, rankerAt(i), i == s.listCursor);
		}
		double azRes, altRes;
		if (calibrationLoadLast(&azRes, &altRes))
			Serial.printf("上次校准残差: 方位%+.1f 高度%+.1f\n", azRes, altRes);
		Serial.println("UP/DOWN 切换   A 选择   B 返回");
	} else if (s.calPhase == CAL_SELECTED) {
		Serial.println("===== 校准（已选目标） =====");
		const RankedTarget *t = rankerAt(s.selectedTarget);
		if (t != NULL) {
			Serial.print("目标: ");
			Serial.print(targetName(t));
			Serial.printf("  方位%+.1f 高度%+.1f\n", t->azimuth, t->altitude);
		}
		Serial.println("A 校准（记录瞄准并比对）   B 返回");
	} else { // CAL_DONE
		Serial.println("===== 校准 =====");
		Serial.println("已校准（残差已写入闪存）");
		Serial.println("3 秒后返回主菜单，按 B 立即返回");
	}
}

void serialObserveLive(const InputSnapshot &s)
{
	(void)s;
	Serial.println("===== 观星 · 直接观星 =====");
	Serial.println("（FOV 内星名渲染待阶段3 RenderManager）");
	Serial.println("A 推荐名录   B 返回");
}

void serialObserveList(const InputSnapshot &s)
{
	Serial.println("===== 观星 · 推荐名录 =====");
	int n = rankerCount();
	if (n == 0) {
		Serial.println("  （当前无可选目标）");
	} else {
		for (int i = 0; i < n; i++)
			serialTargetLine(i, rankerAt(i), i == s.listCursor);
	}
	Serial.println("UP/DOWN 浏览   A 选中   B 返回");
}

void serialGuide(const InputSnapshot &s)
{
	Serial.println("===== 观星 · 转向指导 =====");
	const RankedTarget *t = rankerAt(s.selectedTarget);
	if (t == NULL) {
		Serial.println("（无选中目标）");
	} else {
		Serial.print("目标: ");
		Serial.print(targetName(t));
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

void serialTtSelect(const InputSnapshot &s)
{
	Serial.println("===== 时空旅行 · 选择字段 =====");
	static const char *fields[] = {"年", "月", "日", "时", "分"};
	int vals[] = {s.ttBuffer.year, s.ttBuffer.month, s.ttBuffer.day,
		      s.ttBuffer.hour, s.ttBuffer.minute};
	for (int i = 0; i < TT_FIELD_COUNT; i++)
		Serial.printf("%c %s: %d\n", (i == s.ttFieldCursor) ? '>' : ' ', fields[i], vals[i]);
	Serial.printf("%c 确认启动\n", (s.ttFieldCursor == TT_CONFIRM_INDEX) ? '>' : ' ');
	Serial.println("UP/DOWN 选字段   A 进入/确认   B 返回");
}

void serialTtEdit(const InputSnapshot &s)
{
	Serial.println("===== 时空旅行 · 编辑 =====");
	static const char *fields[] = {"年", "月", "日", "时", "分"};
	int vals[] = {s.ttBuffer.year, s.ttBuffer.month, s.ttBuffer.day,
		      s.ttBuffer.hour, s.ttBuffer.minute};
	for (int i = 0; i < TT_FIELD_COUNT; i++)
		Serial.printf("%c %s: %d\n", (i == s.ttFieldCursor) ? '*' : ' ', fields[i], vals[i]);
	Serial.println("UP/DOWN 加减   A 确认   B 取消");
}

// ---------- OLED 渲染（精简 + 窗口） ----------
void oledMenu(const InputSnapshot &s)
{
	String lines[4];
	static const char *items[] = {"定位", "校准", "观星", "时空旅行"};
	for (int i = 0; i < 4; i++)
		lines[i] = String(i == s.menuCursor ? "> " : "  ") + items[i];
	oledShowScreen(lines, 4);
}

void oledLocation(const InputSnapshot &s)
{
	(void)s;
	String lines[4];
	int n = 0;
	char buf[32];

	lines[n++] = String("状态: ") + gpsManagerStatusTag();

	astro_observer_t obs;
	if (gpsManagerGetLocation(&obs)) {
		snprintf(buf, sizeof(buf), "%.2f %.2f", obs.latitude, obs.longitude);
		lines[n++] = String(buf);
		snprintf(buf, sizeof(buf), "h %.0fm", obs.height);
		lines[n++] = String(buf);
	} else {
		lines[n++] = "搜索中...";
	}

	astro_utc_t utc;
	if (gpsManagerGetUtc(&utc) && n < OLED_ROWS) {
		snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d",
			 utc.year, utc.month, utc.day, utc.hour, utc.minute);
		lines[n++] = String(buf);
	}

	oledShowScreen(lines, n);
}

void oledCalibrate(const InputSnapshot &s)
{
	if (s.calPhase == CAL_BROWSE) {
		int count = rankerCount();
		int topN = count < CAL_TOP_N ? count : CAL_TOP_N;
		if (topN == 0) {
			String l[] = {"（无可选天体）"};
			oledShowScreen(l, 1);
			return;
		}
		String lines[4];
		int start = windowStart(s.listCursor, topN, OLED_ROWS);
		int k = 0;
		for (int i = start; i < topN && k < OLED_ROWS; i++)
			lines[k++] = String(i == s.listCursor ? "> " : "  ") + targetName(rankerAt(i));
		oledShowScreen(lines, k);
	} else if (s.calPhase == CAL_SELECTED) {
		String lines[2];
		int n = 0;
		const RankedTarget *t = rankerAt(s.selectedTarget);
		if (t != NULL)
			lines[n++] = String("目标: ") + targetName(t);
		lines[n++] = "A 校准  B 返回";
		oledShowScreen(lines, n);
	} else { // CAL_DONE
		String lines[2];
		lines[0] = "已校准";
		lines[1] = "3s 后返回主菜单";
		oledShowScreen(lines, 2);
	}
}

void oledObserveLive(const InputSnapshot &s)
{
	(void)s;
	String lines[2];
	lines[0] = "直接观星";
	lines[1] = "(FOV 待阶段3)";
	oledShowScreen(lines, 2);
}

void oledObserveList(const InputSnapshot &s)
{
	int count = rankerCount();
	if (count == 0) {
		String l[] = {"（无可选目标）"};
		oledShowScreen(l, 1);
		return;
	}
	String lines[4];
	int start = windowStart(s.listCursor, count, OLED_ROWS);
	int k = 0;
	for (int i = start; i < count && k < OLED_ROWS; i++)
		lines[k++] = String(i == s.listCursor ? "> " : "  ") + targetName(rankerAt(i));
	oledShowScreen(lines, k);
}

void oledGuide(const InputSnapshot &s)
{
	String lines[4];
	int n = 0;
	char buf[32];

	const RankedTarget *t = rankerAt(s.selectedTarget);
	if (t == NULL) {
		lines[n++] = "（无选中目标）";
	} else {
		lines[n++] = String("目标: ") + targetName(t);
		snprintf(buf, sizeof(buf), "方位%.0f 高度%.0f", t->azimuth, t->altitude);
		lines[n++] = String(buf);
		ImuOrientation o;
		if (orientationGet(&o)) {
			double daz = wrapDeg(t->azimuth - o.yaw);
			snprintf(buf, sizeof(buf), "%s %.0f度", daz >= 0 ? "向右" : "向左", fabs(daz));
			lines[n++] = String(buf);
		} else {
			lines[n++] = "(无朝向数据)";
		}
	}
	oledShowScreen(lines, n);
}

void oledTtSelect(const InputSnapshot &s)
{
	static const char *fields[] = {"年", "月", "日", "时", "分"};
	int vals[] = {s.ttBuffer.year, s.ttBuffer.month, s.ttBuffer.day,
		      s.ttBuffer.hour, s.ttBuffer.minute};

	String all[TT_FIELD_COUNT + 1];
	for (int i = 0; i < TT_FIELD_COUNT; i++)
		all[i] = String(fields[i]) + ": " + String(vals[i]);
	all[TT_CONFIRM_INDEX] = "确认启动";

	String lines[4];
	int start = windowStart(s.ttFieldCursor, TT_FIELD_COUNT + 1, OLED_ROWS);
	int k = 0;
	for (int i = start; i < TT_FIELD_COUNT + 1 && k < OLED_ROWS; i++)
		lines[k++] = String(i == s.ttFieldCursor ? "> " : "  ") + all[i];
	oledShowScreen(lines, k);
}

void oledTtEdit(const InputSnapshot &s)
{
	static const char *fields[] = {"年", "月", "日", "时", "分"};
	int vals[] = {s.ttBuffer.year, s.ttBuffer.month, s.ttBuffer.day,
		      s.ttBuffer.hour, s.ttBuffer.minute};

	String all[TT_FIELD_COUNT];
	for (int i = 0; i < TT_FIELD_COUNT; i++)
		all[i] = String(fields[i]) + ": " + String(vals[i]);

	String lines[4];
	int start = windowStart(s.ttFieldCursor, TT_FIELD_COUNT, OLED_ROWS);
	int k = 0;
	for (int i = start; i < TT_FIELD_COUNT && k < OLED_ROWS; i++)
		lines[k++] = String(i == s.ttFieldCursor ? "* " : "  ") + all[i];
	oledShowScreen(lines, k);
}

} // namespace

void renderManagerRender()
{
	InputSnapshot s;
	inputManagerGetSnapshot(&s);

	switch (s.mode) {
	case IM_MENU:          serialMenu(s);          oledMenu(s);         break;
	case IM_LOCATION:      serialLocation(s);      oledLocation(s);     break;
	case IM_CALIBRATE:     serialCalibrate(s);     oledCalibrate(s);    break;
	case IM_OBSERVE_LIVE:  serialObserveLive(s);   oledObserveLive(s);  break;
	case IM_OBSERVE_LIST:  serialObserveList(s);   oledObserveList(s);  break;
	case IM_OBSERVE_GUIDE: serialGuide(s);         oledGuide(s);        break;
	case IM_TT_SELECT:     serialTtSelect(s);      oledTtSelect(s);     break;
	case IM_TT_EDIT:       serialTtEdit(s);        oledTtEdit(s);       break;
	}
}
