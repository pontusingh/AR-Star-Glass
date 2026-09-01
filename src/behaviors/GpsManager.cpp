// 行为层：GPS 管理器。
// 持有 GpsDriver 的 NMEA 解析 + NVS 持久化 + 30s 超时回退 + 陈旧检测。
// 向上层统一提供「当前位置/时间」及「数据来源标签」。
#include "GpsManager.h"
#include "GpsDriver.h"
#include <Preferences.h>

// ---------- 常量 ----------
static const unsigned long GPS_STALE_MS = 30000;       // GPS 信号丢失超时阈值（30 秒）
static const unsigned long NVS_SAVE_INTERVAL_MS = 60000; // NVS 写入间隔（防闪存磨损）

// ---------- 运行期状态 ----------
static Preferences s_prefs;                // Preferences 对象，命名空间 "gps_data"
static unsigned long s_bootMs = 0;         // 上电时刻（毫秒），用于首次 30s 超时计时
static unsigned long s_lastNvsSaveMs = 0;  // 上次写入 NVS 的时刻（毫秒）

static bool s_gpsFirstFix = false;   // 是否已有可用数据（GPS 定位或 NVS 回退）
static bool s_gpsStale = false;      // 当前数据是否陈旧（GPS 丢失 >30s）
static bool s_gpsNvsFallback = false;// 当前数据是否来自 NVS 回退（首次 30s 超时加载）
static bool s_staleWarned = false;   // 陈旧警告是否已打印（防每帧刷屏）

// 内存缓存：NVS 或 GPS 的上一次有效数据，避免每帧重读 NVS
static astro_observer_t s_cachedObs = {0.0, 0.0, 0.0};
static astro_utc_t s_cachedUtc = {0, 0, 0, 0, 0, 0.0};
static bool s_cacheValid = false;
static unsigned long s_cacheTimestampMs = 0; // 缓存记录时的毫秒时间戳，用于时间外推

// ---------- NVS 存储 ----------

// 把当前 GPS 数据写入 NVS（节流写入，由调用方控制间隔）。
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

// 从 NVS 加载上次定位数据，成功返回 true。
// 校验 gps_yr >= 2000 防止读取到未初始化的脏数据。
// 加载成功后同时更新内存缓存，加速后续 Getter 访问。
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

    // 同步更新内存缓存
    s_cachedObs = *obs;
    s_cachedUtc = *utc;
    s_cacheValid = true;

    return true;
}

// ---------- 公开接口 ----------

void gpsManagerInit()
{
    s_prefs.begin("gps_data", false);
    gpsInit();
    s_bootMs = millis();
}

// 核心状态机：每帧由 main.cpp 调用，内含 gpsTick() + 超时回退 + 陈旧检测。
void gpsManagerTick()
{
    gpsTick();

    astro_utc_t gpsUtc;
    (void)gpsGetUtc(&gpsUtc);
    astro_observer_t gpsObs;
    bool hasObs = gpsGetObserver(&gpsObs);

    // ---- 有 GPS 定位 ----
    if (hasObs) {
        // 更新内存缓存（位置不随时间变化，时间后续由 Getter 外推）
        s_cachedObs = gpsObs;
        s_cachedUtc = gpsUtc;
        s_cacheValid = true;
        s_cacheTimestampMs = millis();

        bool wasNvsOrStale = s_gpsNvsFallback || s_gpsStale;
        if (!s_gpsFirstFix) {
            // 真正首次定位（上电后第一次或 NVS 回退后第一次）
            s_gpsFirstFix = true;
            Serial.printf("[GPS] 首次定位: lat=%.4f lon=%.4f h=%.1fm\n",
                          gpsObs.latitude, gpsObs.longitude, gpsObs.height);
        } else if (wasNvsOrStale) {
            // 从 NVS 回退或缺信号陈旧状态恢复定位
            Serial.printf("[GPS] 定位恢复: lat=%.4f lon=%.4f h=%.1fm\n",
                          gpsObs.latitude, gpsObs.longitude, gpsObs.height);
        }
        s_gpsStale = false;
        s_gpsNvsFallback = false;
        s_staleWarned = false;

        // 节流写入 NVS（60s 间隔，避免频繁擦写闪存）
        if (millis() - s_lastNvsSaveMs >= NVS_SAVE_INTERVAL_MS) {
            saveNvsGpsData();
        }
        return;
    }

    // ---- 无 GPS 定位 ----
    if (!s_gpsFirstFix) {
        // 首次上电 30s 超时 → 尝试从 NVS 加载上次定位
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
        // 已有定位，检查信号丢失是否超过阈值
        unsigned long lastUp = gpsGetLastUpdateMs();
        if (lastUp > 0 && millis() - lastUp > GPS_STALE_MS) {
            s_gpsStale = true;
            s_staleWarned = true;
            Serial.println("[GPS] 定位信号丢失超过30s，使用上次数据");
        }
    }
    // 若 s_gpsStale 已为 true，不再重复打印警告（s_staleWarned 防止刷屏）
}

bool gpsManagerHasValidData()
{
    return s_gpsFirstFix;
}

bool gpsManagerIsStale()
{
    return s_gpsStale;
}

// 时间来源标签（"GPS" / "GPS-陈旧"）。
// 注意：时空旅行覆盖（TT 命令）的标签由 TimeTravelManager 提供，不在本层处理。
const char* gpsManagerTimeTag()
{
    if (s_gpsStale) return "GPS-陈旧";
    return "GPS";
}

// 位置来源标签（"GPS" / "GPS-陈旧" / "NVS"）。
// 注意：位置覆盖（P 命令）的标签由 TimeTravelManager 提供，不在本层处理。
const char* gpsManagerLocTag()
{
    if (s_gpsNvsFallback) return "NVS";
    if (s_gpsStale) return "GPS-陈旧";
    return "GPS";
}

// 获取当前 UTC 时间。
// 有 GPS 定位时返回实时数据；无定位但有缓存时，将缓存的 UTC 时间按经过的毫秒数外推。
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
    // GPS 无定位时，从缓存外推当前时间
    if (s_gpsFirstFix && s_cacheValid) {
        unsigned long elapsedMs = millis() - s_cacheTimestampMs;
        astro_time_t t = Astronomy_TimeFromUtc(s_cachedUtc);
        t = Astronomy_AddDays(t, elapsedMs / 86400000.0);
        *out = Astronomy_UtcFromTime(t);
        return true;
    }
    return false;
}

// 获取当前观察点位置（经纬度、海拔）。
// 有 GPS 定位时返回实时数据；无定位时从内存缓存或 NVS 回退。
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