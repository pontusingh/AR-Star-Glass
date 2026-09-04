#ifndef GPS_MANAGER_H
#define GPS_MANAGER_H

#include <Arduino.h>
#include "AstronomyEngine.h"

// 行为层：GPS 管理器（非阻塞状态机）。
// 上电或触发后进入 SEARCHING，60s 内等待实时定位；成功 → FIXED；
// 超时且有 NVS 回退 → FALLBACK；超时且无 NVS → NONE（占位 0,0,0）。
// loop() 每圈调用 gpsManagerTick()，无任何阻塞。

typedef enum {
    GPS_STATUS_SEARCHING,   // 正在搜索定位（60s 内）
    GPS_STATUS_FIXED,       // 实时定位已获取
    GPS_STATUS_FALLBACK,    // 60s 超时，回退到 NVS 上次位置
    GPS_STATUS_NONE         // 超时且 NVS 空，无位置信息（占位 0,0,0）
} GpsStatus;

void         gpsManagerBegin();                     // init NVS + GPS 驱动 → 加载 NVS 到回退缓存 → 进入 SEARCHING
void         gpsManagerTick();                      // 每 loop 调用一次，非阻塞，状态机实际工作在此
void         gpsManagerRequestFreshFix();           // 菜单触发：重置回 SEARCHING，立即返回
GpsStatus    gpsManagerGetStatus();                 // 供 OLED/串口读取当前状态
const char*  gpsManagerStatusTag();                 // 状态标签："GPS"/"NVS"/"无位置"/"搜索中"
bool         gpsManagerGetLocation(astro_observer_t *out); // FIXED→实时；FALLBACK/NONE→回退/占位
bool         gpsManagerGetUtc(astro_utc_t *out);           // 时间（天文引擎需要）

#endif // GPS_MANAGER_H