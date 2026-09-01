// 当前阶段：第 2 阶段 —— BNO085 硬件接入暂缓，相关活跃代码已全部注释（搜"BNO085 暂缓"恢复）；
// 第 1 阶段天文引擎+星表+GPS 已就绪
// 主程序（胶水层）：setup() 初始化驱动并等待 GPS 定位；loop() 轮询按键、刷新可见目标表并打印。
//
// 串口命令（115200，行尾换行）：
//   H                          帮助
//   P <纬度> <经度>            设置观察点覆盖（时空旅行位置覆盖）
//   T <年 月 日 时 分 秒>      设置当前 UTC 时刻（覆盖 GPS 时间）
//   TT <年 月 日 时 分 秒>     开启时空旅行覆盖；TT OFF 取消
//   R                          立即重算并打印一次
//   （I 命令=IMU 朝向打印，随 BNO085 暂缓一并注释）
#include <Arduino.h>
#include "GpsManager.h"
#include "ButtonDriver.h"
#include "DisplayDriver.h"
#include "ImuDriver.h"
#include "AstronomyEngine.h"
#include "StarCatalog.h"
#include "VisibilityRanker.h"
#include "TimeTravelManager.h"
#include "SerialConsole.h"

static const unsigned long REFRESH_INTERVAL_MS = 1000;

// ---------- 运行期状态 ----------
static astro_observer_t s_observer = {0.0, 0.0, 0.0};
static astro_utc_t s_nowUtc = {0, 0, 0, 0, 0, 0.0};
static unsigned long s_lastRefreshMs = 0;
static bool s_forceRefresh = false;


void setup()
{
	Serial.begin(115200);
	delay(300); // 等串口就绪，避免开头几行日志丢失
	Serial.println("\n===== 观星AR眼镜 固件启动 =====");
	Serial.println("阶段: 第2阶段 (BNO085 硬件接入暂缓，IMU 代码已注释)");

	// 1. 初始化各驱动
	buttonInit();
	oledInit();
	gpsManagerInit();
	// imuInit(); // 【BNO085 暂缓】

	Serial.printf("目录自检: 恒星 %d 颗, 太阳系天体 %d 个\n", STAR_CATALOG_COUNT,
		      BODY_CATALOG_COUNT);

	serialConsoleInit();

	Serial.print("[GPS] 正在获取定位...");
	Serial.flush();
	gpsManagerWaitForFix();
	Serial.printf(" 数据来源: %s\n", gpsManagerDataSourceTag());

	astro_utc_t utc;
	if (gpsManagerGetUtc(&utc)) {
		s_nowUtc = utc;
	}
	astro_observer_t obs;
	if (gpsManagerGetObserver(&obs)) {
		s_observer = obs;
	}

	astro_time_t baseTime = astroMakeTime(s_nowUtc.year, s_nowUtc.month,
					      s_nowUtc.day, s_nowUtc.hour,
					      s_nowUtc.minute, s_nowUtc.second);
	serialReportTargets(timetravelResolve(baseTime),
			    timetravelResolveObserver(s_observer));

	oledTick();
}

void loop()
{
	serialConsoleTick(s_nowUtc, s_forceRefresh);

	// 3. 低频刷新可见目标表
	unsigned long nowMs = millis();
	if (s_forceRefresh || nowMs - s_lastRefreshMs >= REFRESH_INTERVAL_MS) {
		s_lastRefreshMs = nowMs;
		s_forceRefresh = false;

		astro_time_t baseTime = astroMakeTime(s_nowUtc.year, s_nowUtc.month,
						      s_nowUtc.day, s_nowUtc.hour,
						      s_nowUtc.minute, s_nowUtc.second);
		serialReportTargets(timetravelResolve(baseTime),
			      timetravelResolveObserver(s_observer));
	}

	// imuTick(); // 【BNO085 暂缓】

	// 4. 按键事件（消抖在 ButtonDriver 内完成）
	ButtonId btn = buttonRead();
	if (btn != BTN_NONE) {
		const char* names[] = {"A", "B", "UP", "DOWN"};
		Serial.printf("按键[%s] 按下，重新获取GPS定位...\n", names[btn]);
		buttonClear();
		gpsManagerWaitForFix();
		Serial.printf("[GPS] 数据来源: %s\n", gpsManagerDataSourceTag());
		astro_utc_t utc;
		if (gpsManagerGetUtc(&utc)) s_nowUtc = utc;
		astro_observer_t obs;
		if (gpsManagerGetObserver(&obs)) s_observer = obs;
		s_forceRefresh = true;
	}

	// 5. OLED 实时刷新：数据侧仅置脏标志，此处统一拉起绘制
	oledTick();
}