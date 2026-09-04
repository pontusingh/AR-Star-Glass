#include "GpsManager.h"
#include "GpsDriver.h"
#include <Preferences.h>

static const unsigned long GPS_SEARCH_TIMEOUT_MS = 60000;

static Preferences s_prefs;
static GpsStatus s_status = GPS_STATUS_SEARCHING;
static unsigned long s_searchStartMs = 0;

// 回退缓存：NVS 上次定位（无 NVS 时为占位 0,0,0）
static astro_observer_t s_observer = {0.0, 0.0, 0.0};
static astro_utc_t s_utc = {0, 0, 0, 0, 0, 0.0};
static bool s_fallbackValid = false;

// 把当前 GPS 数据写入 NVS（仅在 fix 落地瞬间调用，频率极低，无闪存磨损顾虑）。
static void saveNvsGpsData()
{
    GpsData d;
    if (!gpsGetData(&d)) return;

    s_prefs.putDouble("gps_lat", d.lat);
    s_prefs.putDouble("gps_lon", d.lon);
    s_prefs.putDouble("gps_alt", d.altitude_m);
    s_prefs.putInt("gps_yr", d.year);
    s_prefs.putInt("gps_mon", d.month);
    s_prefs.putInt("gps_day", d.day);
    s_prefs.putInt("gps_hr", d.hour);
    s_prefs.putInt("gps_min", d.minute);
    s_prefs.putDouble("gps_sec", d.second);
}

// 从 NVS 加载上次定位到回退缓存，成功返回 true。
static bool loadNvsGpsData()
{
    if (!s_prefs.isKey("gps_yr")) return false;
    if (!s_prefs.isKey("gps_lat")) return false;
    if (!s_prefs.isKey("gps_lon")) return false;

    int yr = s_prefs.getInt("gps_yr", 0);
    if (yr < 2000) return false;

    s_observer.latitude  = s_prefs.getDouble("gps_lat", 0.0);
    s_observer.longitude = s_prefs.getDouble("gps_lon", 0.0);
    s_observer.height    = s_prefs.getDouble("gps_alt", 0.0);

    s_utc.year   = yr;
    s_utc.month  = s_prefs.getInt("gps_mon", 1);
    s_utc.day    = s_prefs.getInt("gps_day", 1);
    s_utc.hour   = s_prefs.getInt("gps_hr", 0);
    s_utc.minute = s_prefs.getInt("gps_min", 0);
    s_utc.second = s_prefs.getDouble("gps_sec", 0.0);

    return true;
}

void gpsManagerBegin()
{
    s_prefs.begin("gps_data", false);
    gpsInit();

    s_fallbackValid = loadNvsGpsData();

    s_status = GPS_STATUS_SEARCHING;
    s_searchStartMs = millis();
}

void gpsManagerTick()
{
    if (s_status != GPS_STATUS_SEARCHING) return;

    // 1. 拉取并解析新到达的 NMEA 字节（非阻塞，只读当前 UART 缓冲）
    gpsTick();

    // 2. 有效定位出现 → FIXED，写 NVS 一次
    if (gpsHasFix()) {
        GpsData d;
        if (gpsGetData(&d) && d.lat != 0.0 && d.lon != 0.0) {
            s_observer.latitude  = d.lat;
            s_observer.longitude = d.lon;
            s_observer.height    = d.altitude_m;
            s_utc.year   = d.year;
            s_utc.month  = d.month;
            s_utc.day    = d.day;
            s_utc.hour   = d.hour;
            s_utc.minute = d.minute;
            s_utc.second = d.second;
            s_fallbackValid = true;
            saveNvsGpsData();
            s_status = GPS_STATUS_FIXED;
            Serial.printf("[GPS] 定位成功: lat=%.4f lon=%.4f h=%.1fm\n",
                          s_observer.latitude, s_observer.longitude, s_observer.height);
            return;
        }
    }

    // 3. 超时 → FALLBACK（有 NVS）或 NONE（无 NVS）
    if (millis() - s_searchStartMs >= GPS_SEARCH_TIMEOUT_MS) {
        if (s_fallbackValid) {
            s_status = GPS_STATUS_FALLBACK;
            Serial.printf("[NVS] 定位超时，使用上次位置: lat=%.4f lon=%.4f\n",
                          s_observer.latitude, s_observer.longitude);
        } else {
            s_status = GPS_STATUS_NONE;
            Serial.println("[GPS] 定位超时且无 NVS 数据，无位置信息");
        }
    }
}

void gpsManagerRequestFreshFix()
{
    s_status = GPS_STATUS_SEARCHING;
    s_searchStartMs = millis();
}

GpsStatus gpsManagerGetStatus()
{
    return s_status;
}

const char* gpsManagerStatusTag()
{
    switch (s_status) {
        case GPS_STATUS_FIXED:     return "GPS";
        case GPS_STATUS_FALLBACK:  return "NVS";
        case GPS_STATUS_NONE:      return "无位置";
        default:                   return "搜索中";
    }
}

bool gpsManagerGetLocation(astro_observer_t *out)
{
    if (out == NULL) return false;
    if (s_status == GPS_STATUS_SEARCHING) return false;
    *out = s_observer;
    return true;
}

bool gpsManagerGetUtc(astro_utc_t *out)
{
    if (out == NULL) return false;
    if (s_status == GPS_STATUS_SEARCHING) return false;
    *out = s_utc;
    return true;
}