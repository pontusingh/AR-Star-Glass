#ifndef DISPLAY_DRIVER_H
#define DISPLAY_DRIVER_H

#include <Arduino.h>

// ---------- OLED 显示部分 ----------
// 引脚由 U8g2 构造器传入，内部完成唯一一次 I2C 总线初始化。
// 教训：绝不能在这里再次调用 Wire.begin()，否则 ESP32-S3 I2C 总线锁死（见 test/oled.cpp）。
#define PIN_OLED_SDA 8
#define PIN_OLED_SCL 9

void oledInit();                        // 初始化 OLED（唯一一次 I2C 总线初始化）
void oledShowText(const String &text);  // 入队待显示文本（蓝牙回调中调用安全：仅拷贝+置脏标志）
void oledTick();                        // loop 中每圈调用：有脏标志时统一刷新整屏

#endif // DISPLAY_DRIVER_H
