// 驱动层：SSD1306 OLED 显示（U8g2，硬件 I2C），只管画屏不管数据来源
#include <Arduino.h>
#include <U8g2lib.h>

#include "DisplayDriver.h"

// 引脚由 U8g2 构造器传入，内部完成唯一一次 I2C 总线初始化。
// 教训：绝不能在这里再次调用 Wire.begin()，否则 ESP32-S3 I2C 总线锁死（见 test/oled.cpp）。
static U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE, PIN_OLED_SCL, PIN_OLED_SDA);

static const int MAX_TEXT_LINES = 4;
static String screenLines[MAX_TEXT_LINES];
static int lineCount = 0;
static volatile bool oledDirty = false;     // 有脏标志时 loop() 里统一刷新

static void oledRedraw();

/**
 * @brief 初始化 OLED 显示
 *        引脚由 U8g2 构造器传入，内部完成唯一一次 I2C 总线初始化。
 *        务必不要在此额外调用 Wire.begin()，否则 ESP32-S3 I2C 总线锁死。
 */
void oledInit() {
	u8g2.begin();              // 初始化 OLED，顺便完成 I2C 总线初始化
	u8g2.enableUTF8Print();    // 支持中文 UTF-8 显示
	u8g2.setFont(u8g2_font_wqy12_t_gb2312); // 中文字体
}

/**
 * @brief 整屏替换显示：清掉滚动历史，按 n 行直接绘制（最多 4 行）。
 *        供 RenderManager 渲染页面（菜单/列表等固定布局）使用。
 */
void oledShowScreen(const String lines[], int n) {
	if (n > MAX_TEXT_LINES) n = MAX_TEXT_LINES;
	lineCount = 0;
	for (int i = 0; i < n; i++) {
		screenLines[lineCount++] = lines[i];
	}
	oledDirty = true;
}

/**
 * @brief 开始一帧图形绘制：清空显示缓冲。
 */
void oledStartFrame() {
	u8g2.clearBuffer();
}

/**
 * @brief 结束一帧：把缓冲推到 OLED。
 */
void oledEndFrame() {
	u8g2.sendBuffer();
}

void oledDrawStr(int x, int y, const char *s) {
	u8g2.drawUTF8(x, y, s);
}

void oledDrawCircle(int x, int y, int r) {
	u8g2.drawCircle(x, y, r);
}

void oledDrawDisc(int x, int y, int r) {
	u8g2.drawDisc(x, y, r);
}

void oledDrawLine(int x0, int y0, int x1, int y1) {
	u8g2.drawLine(x0, y0, x1, y1);
}

void oledDrawTriangle(int x0, int y0, int x1, int y1, int x2, int y2) {
	u8g2.drawTriangle(x0, y0, x1, y1, x2, y2);
}

/**
 * @brief loop 中每圈调用：有脏标志时整屏刷新。
 */
void oledTick() {
	if (oledDirty) {
		oledDirty = false;
		oledRedraw();
	}
}

/**
 * @brief 刷新整屏：清屏并把缓冲区的内容逐行绘制到 OLED。
 */
static void oledRedraw() {
	u8g2.clearBuffer();
	for (int i = 0; i < lineCount; i++) {
		u8g2.setCursor(0, (i + 1) * 13); // wqy12 行高 ≈ 12，行间 13
		u8g2.print(screenLines[i]);
	}
	u8g2.sendBuffer();            // 把缓冲区像素一次性推到 OLED
}
