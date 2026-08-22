// 驱动层：板载按键的去抖读取
#include <Arduino.h>

#include "ButtonDriver.h"

// 消抖状态变量（自 main.cpp 迁入，逻辑不变）
static unsigned long lastDebounceMs = 0;    // BOOT 键消抖计时
static bool    lastButtonReading = HIGH;    // 上一次读到原始引脚电平
static bool    buttonStable = HIGH;         // 消抖后稳定的按键状态

void buttonInit() {
	pinMode(PIN_BUTTON_BOOT, INPUT_PULLUP); // 板载 BOOT 键，按下为 LOW
}

bool buttonWasPressed() {
	bool pressed = false;
	unsigned long now = millis();

	// 非阻塞消抖（取代原 delay/wait），松手后才允许再次触发
	bool reading = digitalRead(PIN_BUTTON_BOOT);
	if (reading != lastButtonReading) {
		lastDebounceMs = now;          // 状态翻转即重置计时
		lastButtonReading = reading;
	}
	if ((now - lastDebounceMs) > 50) { // 50ms 稳定确认
		if (reading != buttonStable) {
			buttonStable = reading;
			if (buttonStable == LOW) { // 确认按下 → 上报一次按键事件
				pressed = true;
			}
		}
	}
	return pressed;
}
