#ifndef TIME_TRAVEL_MANAGER_H
#define TIME_TRAVEL_MANAGER_H

// 行为层：时空旅行管理器。
// 持有一个可选的「用户设定时刻」；激活后，天文计算一律使用该时刻，
// 而不是实时时钟（未来为 GPS 时间）。本阶段（第 1 阶段）先通过串口命令
// 验证「同一引擎在任意时刻都能给出正确星空」，按键调节在第 5 阶段接入。

#include <Arduino.h>
#include "AstronomyEngine.h"

// 设定并立即生效一个 UTC 覆盖时刻
void timetravelSet(int year, int month, int day, int hour, int minute, double second);

// 取消覆盖，恢复跟随实时时钟
void timetravelClear();

// 是否处于覆盖状态
bool timetravelIsActive();

// 读取当前覆盖时刻（仅当 timetravelIsActive() 为真时有效）
astro_utc_t timetravelGetOverride();

// 核心接口：把基础时刻（实时时钟）解析为实际参与天文计算的时刻。
// 覆盖激活时返回用户设定时刻，否则原样透传 baseTime。
astro_time_t timetravelResolve(astro_time_t baseTime);

#endif // TIME_TRAVEL_MANAGER_H
