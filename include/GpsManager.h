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

typedef enum {
    GPS_WAIT_IDLE,         // 未启动
    GPS_WAIT_IN_PROGRESS,  // 正在尝试定位
    GPS_WAIT_DONE          // 已完成（成功/超时/回退）
} GpsWaitState;

void         gpsManagerInit();                // 初始化 NVS + GPS 驱动
void         gpsManagerStartWait();            // 启动定位（非阻塞），记录起始时间戳，设状态 IN_PROGRESS
bool         gpsManagerCheckFix();            // 每帧调用：gpsTick() + 检查是否定位成功或超时；完成后更新内部数据并设状态 DONE；返回 true 表示完成
GpsWaitState gpsManagerGetWaitState();        // 返回当前等待状态
bool         gpsManagerHasFix();             // 是否有有效定位（GPS 或 NVS）
GpsDataSource gpsManagerDataSource();         // 返回当前数据来源
const char*  gpsManagerDataSourceTag();       // 数据来源标签字符串
bool         gpsManagerGetUtc(astro_utc_t *out);
bool         gpsManagerGetObserver(astro_observer_t *out);

#endif // GPS_MANAGER_H