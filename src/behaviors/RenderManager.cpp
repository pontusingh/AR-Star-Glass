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

// FOV（单目，plan.md 15–30° 取下限）；屏幕 128×64 → 均匀 8 px/°
const double FOV_H_DEG = 16.0;
const double FOV_V_DEG = 8.0;
const double FOV_HALF_H = FOV_H_DEG / 2.0;
const double FOV_HALF_V = FOV_V_DEG / 2.0;
const double PX_PER_DEG = 8.0;
const int SCREEN_W = 128;
const int SCREEN_H = 64;
const int SCREEN_CX = 64;
const int SCREEN_CY = 32;

// ---------- 共享工具 ----------
const char *targetName(const RankedTarget *t)
{
	if (t->kind == TARGET_STAR)
		return STAR_CATALOG[t->catalogIndex].nameAscii;
	return BODY_CATALOG[t->catalogIndex].nameAscii;
}

const char *targetNameCn(const RankedTarget *t)
{
	if (t->kind == TARGET_STAR)
		return STAR_CATALOG[t->catalogIndex].nameCn;
	return BODY_CATALOG[t->catalogIndex].nameCn;
}

double wrapDeg(double deg)
{
	while (deg > 180.0) deg -= 360.0;
	while (deg < -180.0) deg += 360.0;
	return deg;
}

// FOV 过滤：返回目标是否在视场内，并输出 Δaz / Δalt（度）
bool targetInFov(const RankedTarget *t, double yaw, double pitch, double *daz, double *dalt)
{
	double a = wrapDeg(t->azimuth - yaw);
	double e = t->altitude - pitch;
	if (daz) *daz = a;
	if (dalt) *dalt = e;
	return fabs(a) <= FOV_HALF_H && fabs(e) <= FOV_HALF_V;
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
	ImuOrientation o;
	if (!orientationGet(&o)) {
		Serial.println("（无朝向数据，请先 O 注入朝向或连接 IMU）");
	} else {
		Serial.printf("朝向: yaw=%+.1f pitch=%+.1f\n", o.yaw, o.pitch);
		int shown = 0;
		for (int i = 0; i < rankerCount(); i++) {
			const RankedTarget *t = rankerAt(i);
			double daz, dalt;
			if (targetInFov(t, o.yaw, o.pitch, &daz, &dalt)) {
				Serial.printf("  %s  方位%+.1f 高度%+.1f (Δaz%+.1f Δalt%+.1f)\n",
					      targetName(t), t->azimuth, t->altitude, daz, dalt);
				shown++;
			}
		}
		if (shown == 0)
			Serial.println("  （视场内无可显示目标）");
	}
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
			lines[k++] = String(i == s.listCursor ? "> " : "  ") + targetNameCn(rankerAt(i));
		oledShowScreen(lines, k);
	} else if (s.calPhase == CAL_SELECTED) {
		String lines[2];
		int n = 0;
		const RankedTarget *t = rankerAt(s.selectedTarget);
		if (t != NULL)
			lines[n++] = String("目标: ") + targetNameCn(t);
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
	oledStartFrame();

	ImuOrientation o;
	if (!orientationGet(&o)) {
		oledDrawStr(0, 16, "无朝向数据");
		oledDrawStr(0, 32, "请注入朝向(O)或接IMU");
		oledEndFrame();
		return;
	}

	// 中心十字（视线方向）
	oledDrawLine(SCREEN_CX - 5, SCREEN_CY, SCREEN_CX + 5, SCREEN_CY);
	oledDrawLine(SCREEN_CX, SCREEN_CY - 5, SCREEN_CX, SCREEN_CY + 5);

	for (int i = 0; i < rankerCount(); i++) {
		const RankedTarget *t = rankerAt(i);
		double daz, dalt;
		if (!targetInFov(t, o.yaw, o.pitch, &daz, &dalt))
			continue;
		int x = SCREEN_CX + (int)lround(daz * PX_PER_DEG);
		int y = SCREEN_CY - (int)lround(dalt * PX_PER_DEG);
		if (x < 0) x = 0;
		if (x > SCREEN_W - 1) x = SCREEN_W - 1;
		if (y < 0) y = 0;
		if (y > SCREEN_H - 1) y = SCREEN_H - 1;
		oledDrawDisc(x, y, 1);                       // 位置标记点
		oledDrawStr(x + 3, y, targetNameCn(t)); // 星名
	}

	oledEndFrame();
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
			lines[k++] = String(i == s.listCursor ? "> " : "  ") + targetNameCn(rankerAt(i));
	oledShowScreen(lines, k);
}

void oledGuide(const InputSnapshot &s)
{
	oledStartFrame();

	const RankedTarget *t = rankerAt(s.selectedTarget);
	if (t == NULL) {
		oledDrawStr(0, SCREEN_CY, "（无选中目标）");
		oledEndFrame();
		return;
	}

	// 顶部目标名
	oledDrawStr(0, 10, targetNameCn(t));

	// 中心瞄准圆圈（reticle）
	oledDrawCircle(SCREEN_CX, SCREEN_CY, 18);

	ImuOrientation o;
	if (!orientationGet(&o)) {
		oledDrawStr(0, SCREEN_H - 12, "无朝向数据");
		oledEndFrame();
		return;
	}

	double daz, dalt;
	bool inFov = targetInFov(t, o.yaw, o.pitch, &daz, &dalt);

	// 目标屏幕偏移（x 右正，y 下正）
	double sx = daz * PX_PER_DEG;
	double sy = -dalt * PX_PER_DEG;
	double len = sqrt(sx * sx + sy * sy);

	// 箭头：从中心指向目标方向，长度固定 16px
	double ux = 0.0, uy = 0.0;
	int ax = SCREEN_CX, ay = SCREEN_CY;
	if (len > 0.5) {
		ux = sx / len;
		uy = sy / len;
		ax = SCREEN_CX + (int)lround(ux * 16.0);
		ay = SCREEN_CY + (int)lround(uy * 16.0);
	}
	oledDrawLine(SCREEN_CX, SCREEN_CY, ax, ay);

	if (len > 0.5) {
		// 箭头三角头：顶点=尖端，底边垂直方向
		double px = -uy, py = ux;
		int bx1 = (int)lround(ax - ux * 6 + px * 4);
		int by1 = (int)lround(ay - uy * 6 + py * 4);
		int bx2 = (int)lround(ax - ux * 6 - px * 4);
		int by2 = (int)lround(ay - uy * 6 - py * 4);
		oledDrawTriangle(ax, ay, bx1, by1, bx2, by2);
	}

	// 目标进入 FOV → 画实心点标记
	if (inFov) {
		int tx = SCREEN_CX + (int)lround(sx);
		int ty = SCREEN_CY + (int)lround(sy);
		oledDrawDisc(tx, ty, 2);
	}

	oledEndFrame();
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

// 每帧 OLED 刷新（仅直接观星/转向指导），内部 50ms 节流，避免串口/重绘开销。
void renderManagerRenderOled()
{
	InputSnapshot s;
	inputManagerGetSnapshot(&s);
	if (s.mode != IM_OBSERVE_LIVE && s.mode != IM_OBSERVE_GUIDE)
		return;

	static unsigned long lastMs = 0;
	unsigned long now = millis();
	if (now - lastMs < 50)
		return;
	lastMs = now;

	if (s.mode == IM_OBSERVE_LIVE)
		oledObserveLive(s);
	else
		oledGuide(s);
}
