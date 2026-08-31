// 当前阶段：第 1 阶段 —— 天文引擎 + 星表，GPS 已接入
// 主程序（胶水层）：setup() 初始化驱动；loop() 轮询 GPS、按键、刷新可见目标表并打印。
// GPS 首次定位前等待最多 30s，超时后从 NVS 加载上次定位；定位后信号丢失 30s 标注陈旧。
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
#include "GpsManager.h"
#include "ButtonDriver.h"
#include "DisplayDriver.h"
#include "AstronomyEngine.h"
#include "StarCatalog.h"
#include "VisibilityRanker.h"
#include "TimeTravelManager.h"

static const unsigned long REFRESH_INTERVAL_MS = 1000; // 目标表刷新节奏（低频足够）
static const int PRINT_TOP_N = 10;                     // 每次打印前 N 个目标

// ---------- 运行期状态 ----------
static astro_observer_t s_observer = {0.0, 0.0, 0.0};
static astro_utc_t s_nowUtc = {0, 0, 0, 0, 0, 0.0};
static unsigned long s_lastRefreshMs = 0;
static bool s_forceRefresh = false;

// ---------- 串口行输入缓冲 ----------
static char s_lineBuf[128];
static size_t s_lineLen = 0;

namespace {

// 把引擎时间转回 UTC 日历值并打印（用于核对有效时刻）
void printEffectiveTime(astro_time_t t)
{
	astro_utc_t utc = Astronomy_UtcFromTime(t);
	Serial.printf("%04d-%02d-%02d %02d:%02d:%02d UTC", utc.year, utc.month, utc.day,
		      utc.hour, utc.minute, (int)utc.second);
}

// 打印一次可见目标表头 + Top N 行
void reportTargets(astro_time_t effectiveTime, astro_observer_t effectiveObs)
{
	Serial.println("------------------------------------------------------------");
	printEffectiveTime(effectiveTime);
	Serial.printf(" [%s]", gpsManagerTimeTag());
	if (timetravelHasLocationOverride()) {
		Serial.printf(" [%s]", gpsManagerLocTag());
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

// 从一行命令里依次取出数字参数（用 strtod，避开 newlib 的 scanf 浮点裁剪问题）
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

// 处理一条完整命令行（已含结尾 \\0）
void handleCommand(char *line)
{
	char *saveptr = NULL;
	char *cmd = strtok_r(line, " \t", &saveptr);
	if (cmd == NULL)
		return;
	double v[6];

	switch (cmd[0]) {
	case 'P': { // 设置观察点覆盖：纬度 经度
		if (parseDoubles(saveptr, v, 2) == 2) {
			timetravelOverrideObserver(v[0], v[1], 0.0);
			s_forceRefresh = true;
			Serial.printf("位置覆盖已生效: lat=%.4f lon=%.4f\n", v[0], v[1]);
		} else {
			Serial.println("用法: P <纬度> <经度>");
		}
		break;
	}
	case 'T': {
		if (cmd[1] == 'T') { // 时空旅行：TT ... 或 TT OFF
			if (parseDoubles(saveptr, v, 6) == 6) {
				timetravelSet((int)v[0], (int)v[1], (int)v[2], (int)v[3],
					      (int)v[4], v[5]);
				s_forceRefresh = true;
				astro_utc_t o = timetravelGetOverride();
				Serial.printf("时空旅行生效: %04d-%02d-%02d %02d:%02d:%02d UTC\n",
					      o.year, o.month, o.day, o.hour, o.minute,
					      (int)o.second);
			} else if (strtok_r(NULL, " \t", &saveptr) != NULL) {
				timetravelClear(); // OFF 或任意非数字参数都视为取消
				s_forceRefresh = true;
				Serial.println("时空旅行已取消，恢复实时时钟");
			} else {
				Serial.println("用法: TT <年 月 日 时 分 秒> 或 TT OFF");
			}
		} else { // 设置当前 UTC 时刻
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
	case 'R': // 立即重算一次
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
	delay(300); // 等串口就绪，避免开头几行日志丢失
	Serial.println("\n===== 观星AR眼镜 固件启动 =====");
	Serial.println("阶段: 第1阶段 天文引擎+星表+GPS");

	// 1. 初始化各驱动
	buttonInit();
	oledInit();
	gpsManagerInit();

	// 2. 数据层自检：目录规模应非零
	Serial.printf("目录自检: 恒星 %d 颗, 太阳系天体 %d 个\n", STAR_CATALOG_COUNT,
		      BODY_CATALOG_COUNT);

	printHelp();
}

void loop()
{
	// 0. GPS 管理器（驱动 + 状态机 + NVS）
	gpsManagerTick();

	// 1. 从 GpsManager 获取当前时间/位置
	astro_utc_t utc;
	if (gpsManagerGetUtc(&utc)) {
		s_nowUtc = utc;
	}
	astro_observer_t obs;
	if (gpsManagerGetObserver(&obs)) {
		s_observer = obs;
	}

	// 2. 读取串口命令行（非阻塞，逐字符累积到换行为止）
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

		if (!gpsManagerHasValidData()) {
			Serial.println("[GPS] 等待定位...");
			return;
		}

		astro_time_t baseTime = astroMakeTime(s_nowUtc.year, s_nowUtc.month,
						      s_nowUtc.day, s_nowUtc.hour,
						      s_nowUtc.minute, s_nowUtc.second);
		reportTargets(timetravelResolve(baseTime),
			      timetravelResolveObserver(s_observer));
	}

	// 4. 按键事件（消抖在 ButtonDriver 内完成）
	ButtonId btn = buttonRead();
	if (btn != BTN_NONE) {
		const char* names[] = {"A", "B", "UP", "DOWN"};
		Serial.printf("按键[%s] 按下\n", names[btn]);
		buttonClear();
	}

	// 5. OLED 实时刷新：数据侧仅置脏标志，此处统一拉起绘制
	oledTick();
}