//OLED显示的代码示例，其中包含其他项目的头文件，这里不做理会。

#include <Wire.h>
#include <U8g2lib.h>
#include "pin_config.h"
#include "oled.h"
#include "pet_state.h"

/*
 * 经验记录:
 *
 * 第一次上电 OLED 无显示 + 串口无输出,
 * 原因是 oledInit() 里调用了 Wire.begin(SDA, SCL)
 * 但 U8g2 构造器传了引脚 (U8G2_R0, U8X8_PIN_NONE, SCL, SDA),
 * u8g2.begin() 内部会再次调用 Wire.begin(),
 * 重复初始化导致 ESP32-S3 I2C 总线锁死.
 */
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE, PIN_OLED_SCL, PIN_OLED_SDA);

void oledInit() {
    u8g2.begin();
    u8g2.enableUTF8Print();
    u8g2.setFont(u8g2_font_wqy12_t_gb2312);
}

void oledShowStatus(PetState& s, unsigned long nowMs) {
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_wqy12_t_gb2312);
    if (s.sleepMode) {
        u8g2.sendBuffer();
        return;
    }

    u8g2.setCursor(0, 13);
    u8g2.print("EMOTION: ");
    u8g2.print(s.moodValue);

    u8g2.setCursor(0, 28);
    if (s.isFull) {
        unsigned long elapsed = nowMs - s.fullStateStartMs;
        unsigned long rem = (elapsed >= PetConfig::FULL_DURATION_MS)
                            ? 0 : PetConfig::FULL_DURATION_MS - elapsed;
        u8g2.print("FULL ");
        if (rem / 60000 < 10) u8g2.print("0");
        u8g2.print(rem / 60000);
        u8g2.print(" Min ");
        if ((rem % 60000) / 1000 < 10) u8g2.print("0");
        u8g2.print((rem % 60000) / 1000);
        u8g2.print(" S");
    } else {
        u8g2.print("HUNGRY");
    }

    u8g2.setCursor(0, 43);
    switch (s.currentAction) {
        case ActionMode::NONE:    u8g2.print("NULL"); break;
        case ActionMode::PLAY:    u8g2.print("PLAY"); break;
        case ActionMode::IDLE:    u8g2.print("IDLE"); break;
        case ActionMode::TIRED:   u8g2.print("TIRE"); break;
        case ActionMode::FORWARD: u8g2.print("FOWD"); break;
    }

    u8g2.setCursor(0, 58);
    u8g2.print(s.cachedWeatherText);

    u8g2.sendBuffer();
}