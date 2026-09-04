#ifndef ORIENTATION_MANAGER_H
#define ORIENTATION_MANAGER_H

#include <Arduino.h>
#include "ImuDriver.h"

// 行为层：当前视线方向来源。
// 阶段2硬件暂缓：真实 IMU 轮询暂不接入；本阶段仅支持串口注入覆盖，
// 供隔离硬件（无 BNO085）调试。后续接入 imuTick()/单点验证后，
// orientationGet 仍保持「注入优先，否则回退真实 IMU」。

// 注入一个合成朝向（覆盖真实 IMU），供串口调试
void orientationSetOverride(float yaw, float pitch, float roll);

// 取消注入，回退真实 IMU
void orientationClearOverride();

// 是否处于注入状态
bool orientationHasOverride();

// 读取当前朝向：注入优先，否则回退 imuGetOrientation()；成功返回 true
bool orientationGet(ImuOrientation *out);

#endif // ORIENTATION_MANAGER_H
