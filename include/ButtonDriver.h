#ifndef BUTTON_DRIVER_H
#define BUTTON_DRIVER_H

#include <Arduino.h>

// 驱动层：4 个物理按键的去抖读取（纯硬件封装，无逻辑）
// A=GPIO4 确认，B=GPIO5 返回，UP=GPIO6 上/加，DOWN=GPIO7 下/减

#define PIN_BTN_A     4
#define PIN_BTN_B     5
#define PIN_BTN_UP    6
#define PIN_BTN_DOWN  7

typedef enum {
    BTN_NONE = -1,
    BTN_A    = 0,
    BTN_B    = 1,
    BTN_UP   = 2,
    BTN_DOWN = 3
} ButtonId;

void buttonInit();         // 配置 4 个按键引脚，注册中断
ButtonId buttonRead();     // 返回最新按下的按键 ID（BTN_NONE 表示无）
void buttonClear();        // 清除所有按键标志

#endif // BUTTON_DRIVER_H