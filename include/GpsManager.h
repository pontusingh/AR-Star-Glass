#ifndef GPS_MANAGER_H
#define GPS_MANAGER_H

// 行为层：GPS 管理器。
// 持有 GpsDriver 的 NMEA 解析 + NVS 持久化 + 30s 超时回退 + 陈旧检测。
// 向上层统一提供「当前位置/时间」及「数据来源标签」。

#include <Arduino.h>
#include "AstronomyEngine.h"

void     gpsManagerInit();              // 初始化 NVS + 驱动
void     gpsManagerTick();              // 内含 gpsTick() + 状态机
bool     gpsManagerHasValidData();      // 是否已有可用数据（定位或 NVS 回退）
bool     gpsManagerIsStale();           // 当前数据是否陈旧（丢失 >30s）
const char* gpsManagerTimeTag();        // 时间来源标签
const char* gpsManagerLocTag();         // 位置来源标签
bool     gpsManagerGetUtc(astro_utc_t *out);
bool     gpsManagerGetObserver(astro_observer_t *out);

#endif // GPS_MANAGER_H