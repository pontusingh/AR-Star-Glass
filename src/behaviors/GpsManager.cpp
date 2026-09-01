#include "GpsManager.h"
#include "GpsDriver.h"
#include <Preferences.h>

static const unsigned long GPS_WAIT_TIMEOUT_MS = 60000;

static Preferences s_prefs;
static GpsDataSource s_dataSource = GPS_SRC_NONE;
static astro_observer_t s_observer = {0.0, 0.0, 0.0};
static astro_utc_t s_utc = {0, 0, 0, 0, 0, 0.0};

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

void gpsManagerInit()
{
    s_prefs.begin("gps_data", false);
    gpsInit();
}

void gpsManagerWaitForFix()
{
    unsigned long startMs = millis();
    s_dataSource = GPS_SRC_NONE;

    while (millis() - startMs < GPS_WAIT_TIMEOUT_MS) {
        gpsTick();

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
                s_dataSource = GPS_SRC_GPS;
                saveNvsGpsData();
                Serial.printf("[GPS] 定位成功: lat=%.4f lon=%.4f h=%.1fm\n",
                              s_observer.latitude, s_observer.longitude, s_observer.height);
                return;
            }
        }
        delay(10);
    }

    Serial.println("[GPS] GPS 定位超时，尝试加载 NVS...");

    if (loadNvsGpsData()) {
        s_dataSource = GPS_SRC_NVS_FALLBACK;
        Serial.printf("[NVS] 加载上次定位: lat=%.4f lon=%.4f\n",
                      s_observer.latitude, s_observer.longitude);
    } else {
        s_dataSource = GPS_SRC_PLACEHOLDER;
        Serial.println("[GPS] 无 NVS 数据，使用占位坐标 (0,0,0)");
    }
}

bool gpsManagerHasFix()
{
    return s_dataSource == GPS_SRC_GPS || s_dataSource == GPS_SRC_NVS_FALLBACK;
}

GpsDataSource gpsManagerDataSource()
{
    return s_dataSource;
}

const char* gpsManagerDataSourceTag()
{
    switch (s_dataSource) {
        case GPS_SRC_GPS:          return "GPS";
        case GPS_SRC_NVS_FALLBACK: return "NVS";
        case GPS_SRC_PLACEHOLDER:   return "无位置";
        default:                    return "无数据";
    }
}

bool gpsManagerGetUtc(astro_utc_t *out)
{
    if (out == NULL) return false;
    if (s_dataSource == GPS_SRC_NONE) return false;
    *out = s_utc;
    return true;
}

bool gpsManagerGetObserver(astro_observer_t *out)
{
    if (out == NULL) return false;
    if (s_dataSource == GPS_SRC_NONE) return false;
    *out = s_observer;
    return true;
}