#ifndef GPS_MANAGER_H
#define GPS_MANAGER_H

#include <Arduino.h>
#include "AstronomyEngine.h"

typedef enum {
    GPS_SRC_NONE,          // 无数据
    GPS_SRC_GPS,           // 来自实时 GPS
    GPS_SRC_NVS_FALLBACK,  // 来自 NVS 回退
    GPS_SRC_PLACEHOLDER    // 占位坐标 (0,0,0)
} GpsDataSource;

void         gpsManagerInit();                // 初始化 NVS + GPS 驱动
void         gpsManagerWaitForFix();           // 阻塞等待最多 60s：GPS → NVS → 占位
bool         gpsManagerHasFix();               // 是否有有效定位（GPS 或 NVS）
GpsDataSource gpsManagerDataSource();         // 返回当前数据来源
const char*  gpsManagerDataSourceTag();        // 数据来源标签字符串
bool         gpsManagerGetUtc(astro_utc_t *out);
bool         gpsManagerGetObserver(astro_observer_t *out);

#endif // GPS_MANAGER_H