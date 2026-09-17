// 驱动层：4 个物理按键的去抖读取（单 ISR + 数组化，便于扩展）
// 参考 THUEE-MCU-2026 Major-Assignment-Desktop-Pet 的 button.cpp 实现
#include <Arduino.h>
#include "ButtonDriver.h"

static const unsigned long DEBOUNCE_MS = 200;

// 引脚表：顺序对应 ButtonId 枚举（新增按键只需在此追加）
static const uint8_t BTN_PINS[] = { PIN_BTN_A, PIN_BTN_B, PIN_BTN_UP, PIN_BTN_DOWN };
static const int     BTN_COUNT  = sizeof(BTN_PINS) / sizeof(BTN_PINS[0]);

// 消抖状态数组（按 ButtonId 索引）
static volatile unsigned long  s_lastPress[BTN_COUNT];
static volatile ButtonId       s_lastButton = BTN_NONE;

// 单 ISR：遍历所有引脚，判断哪个触发并通过消抖
void ARDUINO_ISR_ATTR onAnyButton() {
    unsigned long now = millis();
    for (int i = 0; i < BTN_COUNT; i++) {
        if (digitalRead(BTN_PINS[i]) == HIGH
            && (now - s_lastPress[i]) > DEBOUNCE_MS) {
            s_lastPress[i] = now;
            s_lastButton = (ButtonId)i;
        }
    }
}

void buttonInit() {
    for (int i = 0; i < BTN_COUNT; i++) {
        pinMode(BTN_PINS[i], INPUT_PULLDOWN);
    }
    for (int i = 0; i < BTN_COUNT; i++) {
        attachInterrupt(digitalPinToInterrupt(BTN_PINS[i]), onAnyButton, RISING);
    }
}

ButtonId buttonRead() {
    return s_lastButton;
}

void buttonClear() {
    s_lastButton = BTN_NONE;
}