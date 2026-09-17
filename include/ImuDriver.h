#ifndef IMU_DRIVER_H
#define IMU_DRIVER_H

#include <Arduino.h>

// 驱动层：BNO085 IMU（Adafruit BNO08x 库，SH-2 协议），纯硬件封装。
// 只负责初始化、取数、四元数转欧拉角；校准残差由行为层 OrientationManager 应用。
//
// 引脚（ESP32-C3，与 OLED 共享唯一一条 I2C 总线 Wire，SDA/SCL 相同）：
//   SDA=GPIO8, SCL=GPIO9, INT=GPIO6, RST=GPIO5
//   - I2C 地址 0x4A（BNO085 默认，ADDR 脚拉高时为 0x4B），与 OLED 0x3C 不冲突
//   - INT 用作"数据就绪"门控：低电平=有数据待发，imuTick 只在 INT 低时读
//     （Adafruit 库 I2C 路径盲读不看 INT，ESP32 上盲读会卡死传感器发送队列）
//   - 总线由 oledInit() 的 U8g2 构造器初始化唯一一次，imuInit 里绝不能再次
//     Wire.begin()（重复 begin 锁死总线的教训见 test/oled.cpp）

#define PIN_IMU_SDA 8
#define PIN_IMU_SCL 9
#define PIN_IMU_INT 6
#define PIN_IMU_RST 5

typedef struct {
    float yaw;                 // 航向角（度），磁北基准（与真北差当地磁偏角，校准吸收）
    float pitch;               // 俯仰角（度）
    float roll;                // 横滚角（度）
    uint8_t accuracy;          // SH2 精度状态：0=不可靠 1=低 2=中 3=高
    unsigned long lastUpdateMs; // 上次收到有效数据的 millis() 时刻
} ImuOrientation;

bool imuInit();                            // 初始化 BNO085（共享 Wire），使能旋转矢量；成功返回 true
void imuTick();                            // loop 中每圈调用：复位自愈 + INT 门控取数
bool imuGetOrientation(ImuOrientation *out); // 收到过有效数据则拷贝输出并返回 true
uint32_t imuGetFrameCount();               // 累计旋转矢量帧数（链路健康指标，应约 50 帧/秒）
uint16_t imuGetResetCount();               // 传感器意外复位次数（>0 说明发生过电源/总线瞬断）

#endif // IMU_DRIVER_H
