//当前阶段:测试BLE功能+OLED实时显示
// 主程序（胶水层）：setup() 初始化各模块；loop() 只做定时触发。
// BLE 收发细节在 src/ble/BleService.cpp，OLED 细节在 src/drivers/DisplayDriver.cpp。
#include <Arduino.h>
#include "BleService.h"
#include "DisplayDriver.h"

// 非阻塞计时/消抖变量（取代 delay）
unsigned long lastHeartbeatMs = 0;   // 心跳上次发送时间
unsigned long lastDebounceMs = 0;    // BOOT 键消抖计时
bool    lastButtonReading = HIGH;    // 上一次读到原始引脚电平
bool    buttonStable = HIGH;         // 消抖后稳定的按键状态

// ============= 数 据 接 收 回 调 =============
// 手机发来数据时由 BleService 在协议栈任务中调用
void onBleData(const String &text) {
	// 实时显示到 OLED（仅拷贝+置脏标志，绘制留给 loop() 执行）
	oledShowText(text);

	// ==============================================
	// 在 这 里 添 加 你 收 到 数 据 后 的 处 理 逻 辑
	// ==============================================
	// 示 例： 如 果 收 到"ON"就 点 亮 板 载LED
	// if(text == "ON") digitalWrite(LED_BUILTIN , HIGH);
	// 示 例： 如 果 收 到"OFF"就 熄 灭 板 载LED
	// if(text == "OFF") digitalWrite(LED_BUILTIN , LOW);
}

void setup() { // Serial输 出 的 调 试 信 息 可 删
	Serial.begin(115200);
	Serial.println("\n===== ESP32 BLE 串 口 透 传 初 始 化 =====");

	// 1. 初始化 BLE 通信层（设备名/配对密码/服务与广播都在 BleService 内配置）
	bleInit();

	// 2. 注册手机数据接收回调 → OLED 显示等处理
	bleOnDataReceived(onBleData);

	// 3. 初始化 OLED 显示
	oledInit();
	Serial.println("OLED 初始化完成");
}

void loop() {
	unsigned long now = millis();

	// 1. 心跳 — 非阻塞：连接后每 2 秒发送一次（首次立即发送，行为不变）
	if (bleIsConnected() && (now - lastHeartbeatMs >= 2000)) {
		lastHeartbeatMs = now;
		sendBLEString("Hello from ESP32!");
	}

	// 2. BOOT 键断开 — 非阻塞消抖（取代原 delay/wait），松手后才允许再次断开
	bool reading = digitalRead(0);
	if (reading != lastButtonReading) {
		lastDebounceMs = now;          // 状态翻转即重置计时
		lastButtonReading = reading;
	}
	if ((now - lastDebounceMs) > 50) { // 50ms 稳定确认
		if (reading != buttonStable) {
			buttonStable = reading;
			if (buttonStable == LOW) { // 确认按下 → 断开连接
				disconnectBLE();
				Serial.println("主 动 断 开 了 蓝 牙 连 接");
			}
		}
	}

	// 3. OLED 实时刷新：蓝牙回调仅做标记，此处统一拉起绘制
	oledTick();
}
