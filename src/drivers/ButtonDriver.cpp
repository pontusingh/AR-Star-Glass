// 驱动层：板载按键的去抖读取（ISR 中断方案）
// 参考 THUEE-MCU-2026 Major-Assignment-Desktop-Pet 的 button.cpp 实现
#include <Arduino.h>
#include "ButtonDriver.h"

static volatile bool pressFlag = false;
static volatile unsigned long lastPressMs = 0;
static const unsigned long DEBOUNCE_MS = 200;

void ARDUINO_ISR_ATTR onButtonPress() {
    unsigned long now = millis();
    if (now - lastPressMs > DEBOUNCE_MS) {
        pressFlag = true;
        lastPressMs = now;
    }
}

void buttonInit() {
    pinMode(PIN_BUTTON_BOOT, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(PIN_BUTTON_BOOT), onButtonPress, FALLING);
}

bool buttonWasPressed() {
    return pressFlag;
}

void buttonClearFlag() {
    pressFlag = false;
}
