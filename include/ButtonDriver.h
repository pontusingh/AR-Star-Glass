#ifndef BUTTON_DRIVER_H
#define BUTTON_DRIVER_H

#include <Arduino.h>

// 驱动层：板载按键的去抖读取（纯硬件封装，无逻辑）
// TODO: 按键动作的路由（模式切换/校准/浏览等）待第 5 阶段由 InputManager 接管

#define PIN_BUTTON_BOOT 0 // 板载 BOOT 键（GPIO0，按下为 LOW，内部上拉）

void buttonInit();        // 配置按键引脚，注册中断
bool buttonWasPressed();  // 检查是否有新按下事件（边沿触发，需配合 buttonClearFlag 使用）
void buttonClearFlag();   // 清除按下标志，供主循环消费后调用

#endif // BUTTON_DRIVER_H
