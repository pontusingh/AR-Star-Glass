// 当前阶段：第 2 阶段 —— BNO085 硬件接入暂缓，相关活跃代码已全部注释（搜"BNO085 暂缓"恢复）；
// 第 1 阶段天文引擎+星表+GPS 已就绪
// 主程序（胶水层）：setup() 初始化驱动；loop() 按 1-5 顺序执行，GPS 状态机非阻塞轮询。
//
// 串口命令（115200，行尾换行）：
//   H                          帮助
//   P <纬度> <经度> [高度米]    设置观察点覆盖（时空旅行位置覆盖）
//   T <年 月 日 时 分 秒>      设置当前 UTC 时刻（覆盖 GPS 时间）
//   TT <年 月 日 时 分 秒>     开启时空旅行覆盖；TT OFF 取消
//   O <yaw> <pitch> <roll>     注入朝向（覆盖真实 IMU）；O OFF 取消
//   I                          打印当前朝向（注入/IMU）
//   A / R                      打印一次可见 Top10（含高度、方位角），不连续打印
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

// ---------- 运行期状态 ----------
static astro_observer_t s_observer = {0.0, 0.0, 0.0};
static astro_utc_t s_nowUtc = {0, 0, 0, 0, 0, 0.0};
static bool s_forceRefresh = false;
static GpsStatus s_lastGpsStatus = GPS_STATUS_SEARCHING;


void setup()
{
	Serial.begin(115200);
	delay(300);
	Serial.println("\n===== 观星AR眼镜 固件启动 =====");
	Serial.println("阶段: 第2阶段 (BNO085 硬件接入暂缓，IMU 代码已注释)");

	// 1. 初始化各驱动
	buttonInit();
	oledInit();
	gpsManagerBegin();      // init NVS+GPS → 进入 SEARCHING（非阻塞）
	// imuInit(); // 【BNO085 暂缓】

	Serial.printf("目录自检: 恒星 %d 颗, 太阳系天体 %d 个\n", STAR_CATALOG_COUNT,
		      BODY_CATALOG_COUNT);

	// 2. 初始化串口控制台
	serialConsoleInit();

	// 3. 首次刷新 OLED
	oledTick();
}

void loop()
{
	// 1. 串口命令处理（可能通过引用修改 s_nowUtc / s_observer / s_forceRefresh）
	serialConsoleTick(s_nowUtc, s_forceRefresh);

	// 2. GPS 状态机（非阻塞）；状态离开 SEARCHING 时拉取时间/位置并触发一次打印
	gpsManagerTick();
	GpsStatus gpsStatus = gpsManagerGetStatus();
	if (gpsStatus != s_lastGpsStatus) {
		s_lastGpsStatus = gpsStatus;
		if (gpsStatus != GPS_STATUS_SEARCHING) {
			astro_utc_t utc;
			if (gpsManagerGetUtc(&utc)) s_nowUtc = utc;
			astro_observer_t obs;
			if (gpsManagerGetLocation(&obs)) s_observer = obs;
			s_forceRefresh = true;
		}
	}

	// imuTick(); // 【BNO085 暂缓】

	// 3. 按需打印可见目标表（A/R 命令或注入变更后触发，不自动连续打印）
	if (s_forceRefresh) {
		s_forceRefresh = false;

		astro_time_t baseTime = astroMakeTime(s_nowUtc.year, s_nowUtc.month,
						      s_nowUtc.day, s_nowUtc.hour,
						      s_nowUtc.minute, s_nowUtc.second);
		serialReportTargets(timetravelResolve(baseTime),
			      timetravelResolveObserver(s_observer));
	}

	// 4. 按键事件（消抖在 ButtonDriver 内完成）
	ButtonId btn = buttonRead();
	if (btn != BTN_NONE) {
		const char* names[] = {"A", "B", "UP", "DOWN"};
		Serial.printf("按键[%s] 按下\n", names[btn]);
		buttonClear();
		// TODO: 待 InputManager 状态机（phase 5）接管菜单；此处临时用 BTN_B 触发重新定位
		if (btn == BTN_B) {
			gpsManagerRequestFreshFix();
			s_lastGpsStatus = gpsManagerGetStatus(); // 立即同步，避免误判跳变
		} else {
			s_forceRefresh = true;
		}
	}

	// 5. OLED 实时刷新
	oledTick();
}
