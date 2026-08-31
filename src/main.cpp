// 当前阶段：第 1 阶段 —— 天文引擎 + 星表，GPS 已接入
// 主程序（胶水层）：setup() 初始化驱动；loop() 轮询 GPS、按键、刷新可见目标表并打印。
//
// 串口命令（115200，行尾换行）：
//   H                          帮助
//   P <纬度> <经度>            设置观察点覆盖（时空旅行位置覆盖）
//   T <年 月 日 时 分 秒>      设置当前 UTC 时刻（覆盖 GPS 时间）
//   TT <年 月 日 时 分 秒>     开启时空旅行覆盖；TT OFF 取消
//   R                          立即重算并打印一次
#include <Arduino.h>
#include <string.h>
#include <stdlib.h>
#include "GpsDriver.h"
#include "ButtonDriver.h"
#include "DisplayDriver.h"
#include "AstronomyEngine.h"
#include "StarCatalog.h"
#include "VisibilityRanker.h"
#include "TimeTravelManager.h"

// ---------- 默认配置（GPS 无定位时的回退值） ----------
static const double DEFAULT_LAT = 39.9042;
static const double DEFAULT_LON = 116.4074;
static const double DEFAULT_HEIGHT_M = 40.0;
static const int DEF_Y = 2026, DEF_MO = 8, DEF_D = 25;
static const int DEF_H = 12, DEF_MI = 0;
static const double DEF_S = 0.0;

static const unsigned long REFRESH_INTERVAL_MS = 1000;
static const int PRINT_TOP_N = 10;

// ---------- 运行期状态 ----------
static astro_observer_t s_observer;
static astro_utc_t s_nowUtc;
static unsigned long s_lastRefreshMs = 0;
static bool s_forceRefresh = true;

// ---------- 串口行输入缓冲 ----------
static char s_lineBuf[128];
static size_t s_lineLen = 0;

namespace {

// 打印时间来源标签
const char* timeSourceTag()
{
	if (timetravelIsActive()) return "TT";
	return "GPS";
}

// 打印位置来源标签
const char* locSourceTag()
{
	if (timetravelHasLocationOverride()) return "P-CMD";
	return "GPS";
}

void printEffectiveTime(astro_time_t t)
{
	astro_utc_t utc = Astronomy_UtcFromTime(t);
	Serial.printf("%04d-%02d-%02d %02d:%02d:%02d UTC", utc.year, utc.month, utc.day,
		      utc.hour, utc.minute, (int)utc.second);
}

void reportTargets(astro_time_t effectiveTime, astro_observer_t effectiveObs)
{
	Serial.println("------------------------------------------------------------");
	printEffectiveTime(effectiveTime);
	Serial.printf(" [%s]", timeSourceTag());
	if (timetravelHasLocationOverride()) {
		Serial.printf(" [%s]", locSourceTag());
	}
	Serial.printf("  观察点 lat=%.4f lon=%.4f h=%.0fm\n", effectiveObs.latitude,
		      effectiveObs.longitude, effectiveObs.height);

	rankerUpdate(effectiveTime, effectiveObs);

	int n = rankerCount();
	Serial.printf("地平线以上共 %d 个目标，按亮度+高度角排序 Top%d:\n", n, PRINT_TOP_N);
	Serial.println("  #  名称          HIP/体  星等    方位角   高度角");
	for (int i = 0; i < n && i < PRINT_TOP_N; i++) {
		const RankedTarget *t = rankerAt(i);
		if (t == NULL)
			break;
		if (t->kind == TARGET_STAR) {
			const CatalogStar &s = STAR_CATALOG[t->catalogIndex];
			Serial.printf(" %2d  %-12s %-6u %+5.2f   %6.1f   %+6.1f\n", i + 1,
				      s.nameAscii, s.hip, s.mag, t->azimuth, t->altitude);
		} else {
			const CatalogBody &b = BODY_CATALOG[t->catalogIndex];
			Serial.printf(" %2d  %-12s %-6s   --    %6.1f   %+6.1f\n", i + 1,
				      b.nameAscii, "-", t->azimuth, t->altitude);
		}
	}
}

int parseDoubles(char *saveptr, double *out, int maxCount)
{
	int count = 0;
	while (count < maxCount) {
		char *tok = strtok_r(NULL, " \t", &saveptr);
		if (tok == NULL)
			break;
		out[count++] = strtod(tok, NULL);
	}
	return count;
}

void printHelp()
{
	Serial.println("命令: H=帮助 P=<纬度> <经度> T=<年 月 日 时 分 秒> "
		       "TT=<年 月 日 时 分 秒|OFF> R=立即刷新");
}

void handleCommand(char *line)
{
	char *saveptr = NULL;
	char *cmd = strtok_r(line, " \t", &saveptr);
	if (cmd == NULL)
		return;
	double v[6];

	switch (cmd[0]) {
	case 'P': {
		if (parseDoubles(saveptr, v, 2) == 2) {
			timetravelOverrideObserver(v[0], v[1], DEFAULT_HEIGHT_M);
			s_forceRefresh = true;
			Serial.printf("位置覆盖已生效: lat=%.4f lon=%.4f\n", v[0], v[1]);
		} else {
			Serial.println("用法: P <纬度> <经度>");
		}
		break;
	}
	case 'T': {
		if (cmd[1] == 'T') {
			if (parseDoubles(saveptr, v, 6) == 6) {
				timetravelSet((int)v[0], (int)v[1], (int)v[2], (int)v[3],
					      (int)v[4], v[5]);
				s_forceRefresh = true;
				astro_utc_t o = timetravelGetOverride();
				Serial.printf("时空旅行生效: %04d-%02d-%02d %02d:%02d:%02d UTC\n",
					      o.year, o.month, o.day, o.hour, o.minute,
					      (int)o.second);
			} else if (strtok_r(NULL, " \t", &saveptr) != NULL) {
				timetravelClear();
				s_forceRefresh = true;
				Serial.println("时空旅行已取消，恢复实时时钟");
			} else {
				Serial.println("用法: TT <年 月 日 时 分 秒> 或 TT OFF");
			}
		} else {
			if (parseDoubles(saveptr, v, 6) == 6) {
				s_nowUtc.year = (int)v[0];
				s_nowUtc.month = (int)v[1];
				s_nowUtc.day = (int)v[2];
				s_nowUtc.hour = (int)v[3];
				s_nowUtc.minute = (int)v[4];
				s_nowUtc.second = v[5];
				s_forceRefresh = true;
				Serial.printf("当前 UTC 已设为 %04d-%02d-%02d %02d:%02d:%02d\n",
					      s_nowUtc.year, s_nowUtc.month, s_nowUtc.day,
					      s_nowUtc.hour, s_nowUtc.minute,
					      (int)s_nowUtc.second);
			} else {
				Serial.println("用法: T <年 月 日 时 分 秒>");
			}
		}
		break;
	}
	case 'R':
		s_forceRefresh = true;
		break;
	case 'H':
	default:
		printHelp();
		break;
	}
}

} // namespace

void setup()
{
	Serial.begin(115200);
	delay(300);
	Serial.println("\n===== 观星AR眼镜 固件启动 =====");
	Serial.println("阶段: 第1阶段 天文引擎+星表+GPS");

	// 1. 初始化各驱动
	buttonInit();
	oledInit();
	gpsInit();

	// 2. 默认观察点与时钟（GPS 定位前使用的回退值）
	s_observer = Astronomy_MakeObserver(DEFAULT_LAT, DEFAULT_LON, DEFAULT_HEIGHT_M);
	s_nowUtc.year = DEF_Y;
	s_nowUtc.month = DEF_MO;
	s_nowUtc.day = DEF_D;
	s_nowUtc.hour = DEF_H;
	s_nowUtc.minute = DEF_MI;
	s_nowUtc.second = DEF_S;

	// 3. 数据层自检
	Serial.printf("目录自检: 恒星 %d 颗, 太阳系天体 %d 个\n", STAR_CATALOG_COUNT,
		      BODY_CATALOG_COUNT);

	printHelp();
}

void loop()
{
	// 0. GPS 数据读取（非阻塞 NMEA 解析）
	gpsTick();

	// 1. GPS 更新基础时间与位置（无定位时保留上次有效值或默认值）
	astro_utc_t gpsUtc;
	if (gpsGetUtc(&gpsUtc)) {
		s_nowUtc = gpsUtc;
	}
	astro_observer_t gpsObs;
	if (gpsGetObserver(&gpsObs)) {
		s_observer = gpsObs;
	}

	// 2. 读取串口命令行（非阻塞）
	while (Serial.available() > 0) {
		char c = (char)Serial.read();
		if (c == '\r')
			continue;
		if (c == '\n') {
			s_lineBuf[s_lineLen] = '\0';
			handleCommand(s_lineBuf);
			s_lineLen = 0;
		} else if (s_lineLen < sizeof(s_lineBuf) - 1) {
			s_lineBuf[s_lineLen++] = c;
		}
	}

	// 3. 低频刷新可见目标表
	unsigned long nowMs = millis();
	if (s_forceRefresh || nowMs - s_lastRefreshMs >= REFRESH_INTERVAL_MS) {
		s_lastRefreshMs = nowMs;
		s_forceRefresh = false;
		astro_time_t baseTime = astroMakeTime(s_nowUtc.year, s_nowUtc.month,
						      s_nowUtc.day, s_nowUtc.hour,
						      s_nowUtc.minute, s_nowUtc.second);
		reportTargets(timetravelResolveTime(baseTime),
			      timetravelResolveObserver(s_observer));
	}

	// 4. 按键事件
	ButtonId btn = buttonRead();
	if (btn != BTN_NONE) {
		const char* names[] = {"A", "B", "UP", "DOWN"};
		Serial.printf("按键[%s] 按下\n", names[btn]);
		buttonClear();
	}

	// 5. OLED 实时刷新
	oledTick();
}