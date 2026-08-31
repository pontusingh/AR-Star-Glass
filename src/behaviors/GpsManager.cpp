#include "GpsManager.h"
#include "GpsDriver.h"
#include <Preferences.h>

static const unsigned long GPS_STALE_MS = 30000;
static const unsigned long NVS_SAVE_INTERVAL_MS = 60000;

static Preferences s_prefs;
static unsigned long s_bootMs = 0;
static unsigned long s_lastNvsSaveMs = 0;

static bool s_gpsFirstFix = false;
static bool s_gpsStale = false;
static bool s_gpsNvsFallback = false;
static bool s_staleWarned = false;

static astro_observer_t s_cachedObs = {0.0, 0.0, 0.0};
static astro_utc_t s_cachedUtc = {0, 0, 0, 0, 0, 0.0};
static bool s_cacheValid = false;
static unsigned long s_cacheTimestampMs = 0;

void saveNvsGpsData()
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
    s_lastNvsSaveMs = millis();
}

bool loadNvsGpsData(astro_observer_t *obs, astro_utc_t *utc)
{
    if (!s_prefs.isKey("gps_yr")) return false;
    if (!s_prefs.isKey("gps_lat")) return false;
    if (!s_prefs.isKey("gps_lon")) return false;

    int yr = s_prefs.getInt("gps_yr", 0);
    if (yr < 2000) return false;

    obs->latitude  = s_prefs.getDouble("gps_lat", 0.0);
    obs->longitude = s_prefs.getDouble("gps_lon", 0.0);
    obs->height    = s_prefs.getDouble("gps_alt", 0.0);

    utc->year   = yr;
    utc->month  = s_prefs.getInt("gps_mon", 1);
    utc->day    = s_prefs.getInt("gps_day", 1);
    utc->hour   = s_prefs.getInt("gps_hr", 0);
    utc->minute = s_prefs.getInt("gps_min", 0);
    utc->second = s_prefs.getDouble("gps_sec", 0.0);

    s_cachedObs = *obs;
    s_cachedUtc = *utc;
    s_cacheValid = true;

    return true;
}

void gpsManagerInit()
{
    s_prefs.begin("gps_data", false);
    gpsInit();
    s_bootMs = millis();
}

void gpsManagerTick()
{
    gpsTick();

    astro_utc_t gpsUtc;
    (void)gpsGetUtc(&gpsUtc);
    astro_observer_t gpsObs;
    bool hasObs = gpsGetObserver(&gpsObs);

    if (hasObs) {
        s_cachedObs = gpsObs;
        s_cachedUtc = gpsUtc;
        s_cacheValid = true;
        s_cacheTimestampMs = millis();
        bool wasNvsOrStale = s_gpsNvsFallback || s_gpsStale;
        if (!s_gpsFirstFix) {
            s_gpsFirstFix = true;
            Serial.printf("[GPS] 首次定位: lat=%.4f lon=%.4f h=%.1fm\n",
                          gpsObs.latitude, gpsObs.longitude, gpsObs.height);
        } else if (wasNvsOrStale) {
            Serial.printf("[GPS] 定位恢复: lat=%.4f lon=%.4f h=%.1fm\n",
                          gpsObs.latitude, gpsObs.longitude, gpsObs.height);
        }
        s_gpsStale = false;
        s_gpsNvsFallback = false;
        s_staleWarned = false;

        if (millis() - s_lastNvsSaveMs >= NVS_SAVE_INTERVAL_MS) {
            saveNvsGpsData();
        }
        return;
    }

    // 无定位
    if (!s_gpsFirstFix) {
        if (millis() - s_bootMs > GPS_STALE_MS) {
            astro_observer_t nvsObs;
            astro_utc_t nvsUtc;
            if (loadNvsGpsData(&nvsObs, &nvsUtc)) {
                s_gpsFirstFix = true;
                s_gpsNvsFallback = true;
                s_gpsStale = true;
                Serial.printf("[NVS] 加载上次定位: lat=%.4f lon=%.4f\n",
                              nvsObs.latitude, nvsObs.longitude);
            }
        }
    } else if (!s_gpsStale) {
        unsigned long lastUp = gpsGetLastUpdateMs();
        if (lastUp > 0 && millis() - lastUp > GPS_STALE_MS) {
            s_gpsStale = true;
            s_staleWarned = true;
            Serial.println("[GPS] 定位信号丢失超过30s，使用上次数据");
        }
    }
}

bool gpsManagerHasValidData()
{
    return s_gpsFirstFix;
}

bool gpsManagerIsStale()
{
    return s_gpsStale;
}

const char* gpsManagerTimeTag()
{
    if (s_gpsStale) return "GPS-陈旧";
    return "GPS";
}

const char* gpsManagerLocTag()
{
    if (s_gpsNvsFallback) return "NVS";
    if (s_gpsStale) return "GPS-陈旧";
    return "GPS";
}

bool gpsManagerGetUtc(astro_utc_t *out)
{
    if (out == NULL) return false;
    if (!s_gpsStale) {
        astro_utc_t gpsUtc;
        if (gpsGetUtc(&gpsUtc)) {
            *out = gpsUtc;
            return true;
        }
    }
    if (s_gpsFirstFix && s_cacheValid) {
        unsigned long elapsedMs = millis() - s_cacheTimestampMs;
        astro_time_t t = Astronomy_TimeFromUtc(s_cachedUtc);
        t = Astronomy_AddDays(t, elapsedMs / 86400000.0);
        *out = Astronomy_UtcFromTime(t);
        return true;
    }
    return false;
}

bool gpsManagerGetObserver(astro_observer_t *out)
{
    if (out == NULL) return false;
    astro_observer_t gpsObs;
    if (gpsGetObserver(&gpsObs)) {
        *out = gpsObs;
        return true;
    }
    if (s_gpsFirstFix) {
        if (s_cacheValid) {
            *out = s_cachedObs;
            return true;
        }
        astro_utc_t dummy;
        astro_observer_t nvsObs;
        if (loadNvsGpsData(&nvsObs, &dummy)) {
            *out = nvsObs;
            return true;
        }
    }
    return false;
}