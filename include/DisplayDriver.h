#ifndef DISPLAY_DRIVER_H
#define DISPLAY_DRIVER_H

#include <Arduino.h>

// ---------- OLED 显示部分 ----------
// 引脚由 U8g2 构造器传入，内部完成唯一一次 I2C 总线初始化。
// 教训：绝不能在这里再次调用 Wire.begin()，否则 ESP32-S3 I2C 总线锁死（见 test/oled.cpp）。
#define PIN_OLED_SDA 8
#define PIN_OLED_SCL 9

void oledInit();                        // 初始化 OLED（唯一一次 I2C 总线初始化）
void oledShowScreen(const String lines[], int n); // 整屏替换：清屏按 n 行绘制（n>4 截断）
void oledTick();                        // loop 中每圈调用：有脏标志时统一刷新整屏

// ---------- 图形原语（阶段3 FOV/箭头渲染；在 oledStartFrame/oledEndFrame 之间调用） ----------
void oledStartFrame();                           // 清屏，开始一帧
void oledEndFrame();                             // 提交一帧到 OLED
void oledDrawStr(int x, int y, const char *s);   // 文本（UTF-8），(x,y) 为基线
void oledDrawCircle(int x, int y, int r);        // 空心圆
void oledDrawDisc(int x, int y, int r);          // 实心圆
void oledDrawLine(int x0, int y0, int x1, int y1);
void oledDrawTriangle(int x0, int y0, int x1, int y1, int x2, int y2); // 实心三角

#endif // DISPLAY_DRIVER_H
