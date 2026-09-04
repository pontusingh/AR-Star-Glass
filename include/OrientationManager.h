#ifndef ORIENTATION_MANAGER_H
#define ORIENTATION_MANAGER_H

#include <Arduino.h>
#include "ImuDriver.h"

// 行为层：当前视线方向来源。
// - 读取真实 IMU（BNO085），或串口 O 注入的合成朝向（注入优先）。
// - 应用校准残差（单点验证）作为朝向修正：磁偏角 + 安装误差一并由
//   「残差 = 原始朝向 - 目标计算方位/高度」修正，使星位对齐真北。
// - 残差由 CalibrationManager 计算并写 NVS("calib")；本模块负责读取/应用。

// 上电初始化：从 NVS 加载已存校准残差到缓存（main setup 调用）
void orientationInit();

// 校准提交后立即更新缓存的残差（CalibrationManager 调用）
void orientationSetCalibrationOffset(double azDeg, double altDeg);

// 注入一个合成朝向（覆盖真实 IMU），供串口调试（不参与残差修正）
void orientationSetOverride(float yaw, float pitch, float roll);

// 取消注入，回退真实 IMU
void orientationClearOverride();

// 是否处于注入状态
bool orientationHasOverride();

// 读取当前朝向（已应用校准残差）：注入优先；否则 imuGetOrientation 后修正。
bool orientationGet(ImuOrientation *out);

// 读取「原始」朝向（未应用校准残差）：供 CalibrationManager 计算残差用。
bool orientationGetRaw(ImuOrientation *out);

#endif // ORIENTATION_MANAGER_H
