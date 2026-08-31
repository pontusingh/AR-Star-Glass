#ifndef GPS_DRIVER_H
#define GPS_DRIVER_H

#include <Arduino.h>
#include "AstronomyEngine.h"

typedef struct {
    double     lat;              // 十进制度（北正南负）
    double     lon;              // 十进制度（东正西负）
    double     altitude_m;       // 海拔（米）
    int        year, month, day; // UTC 日期
    int        hour, minute;     // UTC 时分
    double     second;           // UTC 秒（小数）
    double     speed_kn;         // 速度（节）
    double     course_deg;       // 航向（度）
    bool       fix_valid;        // 定位是否有效
    int        satellites;       // 参与解算的卫星数
    unsigned long last_update_ms; // 上次更新时间
} GpsData;

void     gpsInit();                          // 初始化 UART1 (GPIO17/18, 9600)
void     gpsTick();                          // 读取/解析一帧 NMEA
bool     gpsHasFix();                        // 是否有有效定位
bool     gpsGetData(GpsData *out);           // 复制当前 GPS 数据
bool     gpsGetUtc(astro_utc_t *out);        // 填充 UTC 结构体
bool     gpsGetObserver(astro_observer_t *out); // 填充观察点结构体

// 强制覆盖 GPS 数据（供时空旅行管理器使用）
void     gpsOverride(double lat, double lon, double height_m,
                     int year, int month, int day,
                     int hour, int minute, double second);
void     gpsClearOverride();

#endif // GPS_DRIVER_H