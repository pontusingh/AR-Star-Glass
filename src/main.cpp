// 当前阶段:OLED 显示测试（新架构：全本地独立设备，无手机/BLE 参与）
// 主程序（胶水层）：setup() 初始化所有驱动；loop() 只做周期性触发。
// OLED 细节在 src/drivers/DisplayDriver.cpp；后续按 plan.md 接入 IMU/GPS/天文引擎与各管理器。
#include <Arduino.h>
#include "ButtonDriver.h"
#include "DisplayDriver.h"

void setup() { // Serial输 出 的 调 试 信 息 可 删
	Serial.begin(115200);
	Serial.println("\n===== 观星AR眼镜 固件启动 =====");

	// 1. 初始化各驱动
	buttonInit();
	oledInit();
	Serial.println("驱动初始化完成");
}

void loop() {
	// 1. 按键事件（消抖在 ButtonDriver 内完成）
	if (buttonWasPressed()) {
		Serial.println("BOOT 键按下"); // 占位：动作路由待第 5 阶段由 InputManager 接管
	}

	// 2. OLED 实时刷新：数据侧仅置脏标志，此处统一拉起绘制
	oledTick();
}
