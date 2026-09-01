#ifndef IMU_DRIVER_H
#define IMU_DRIVER_H

#include <Arduino.h>

// 驱动层：BNO085 IMU（Adafruit BNO08x 库，SH-2 协议），纯硬件封装。
// 只负责初始化、取数、四元数转欧拉角；校准偏移在行为层 CalibrationManager。
//
// 引脚（ESP32-S3，走第二 I2C 外设 Wire1，与 OLED 的 Wire(GPIO8/9) 互不干扰）：
//   SDA=GPIO1, SCL=GPIO2, INT=GPIO10, RST=GPIO11
//   - I2C 地址 0x4A（BNO085 默认，ADDR 脚拉高时为 0x4B）
//   - INT 在 I2C 模式下库用不到（轮询总线），接线留作以后"数据就绪"提示
//   - 总线只允许 Wire1.begin() 一次（重复 begin 锁死总线的教训见 test/oled.cpp）

#define PIN_IMU_SDA 1
#define PIN_IMU_SCL 2
#define PIN_IMU_INT 10
#define PIN_IMU_RST 11

typedef struct {
    float yaw;                 // 航向角（度），磁北基准（与真北差当地磁偏角，校准吸收）
    float pitch;               // 俯仰角（度）
    float roll;                // 横滚角（度）
    uint8_t accuracy;          // SH2 精度状态：0=不可靠 1=低 2=中 3=高
    unsigned long lastUpdateMs; // 上次收到有效数据的 millis() 时刻
} ImuOrientation;

bool imuInit();                            // 初始化 Wire1 + BNO085，使能旋转矢量；成功返回 true
void imuTick();                            // loop 中每圈调用，取走新到的传感器事件
bool imuGetOrientation(ImuOrientation *out); // 收到过有效数据则拷贝输出并返回 true

#endif // IMU_DRIVER_H
