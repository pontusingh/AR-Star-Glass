#ifndef CALIBRATION_MANAGER_H
#define CALIBRATION_MANAGER_H

#include <Arduino.h>

// 行为层：单点验证校准。
// 瞄准一颗已知天体，读取当前朝向，与该天体计算出的地平坐标（方位/高度）比对，
// 得到残差并持久化到 NVS（仅在校准提交时写一次，节约闪存读写）。

// 单点验证：用当前朝向比对排序表第 targetIdx 个目标（rankerAt 下标）。
// 成功：残差写入 NVS 并返回 true；无朝向数据等失败返回 false（不写 NVS）。
bool calibrationVerify(int targetIdx, double *azResidual, double *altResidual);

// 读取上次持久化的残差；无记录返回 false。
bool calibrationLoadLast(double *azResidual, double *altResidual);

#endif // CALIBRATION_MANAGER_H
