#include "GpsDriver.h"

// ---------- GPS 原始数据缓冲 ----------
static HardwareSerial GPS_Serial(1);

// ---------- NMEA 行缓冲 ----------
static char s_lineBuf[96];
static size_t s_lineLen = 0;
static bool s_lineStarted = false;

// ---------- 解析后的 GPS 数据 ----------
static GpsData s_gps = {0};

// ---------- NMEA 工具函数 ----------
// 校验和校验：返回 true 表示校验通过
static bool nmeaChecksum(const char *line)
{
    if (line[0] != '$') return false;
    const char *p = line + 1;
    uint8_t crc = 0;
    while (*p && *p != '*') {
        crc ^= (uint8_t)*p;
        ++p;
    }
    if (*p != '*') return false;
    ++p; // skip '*'
    if (!p[0] || !p[1]) return false;
    char hex[3] = {p[0], p[1], '\0'};
    uint8_t expected = (uint8_t)strtol(hex, NULL, 16);
    return crc == expected;
}

// NMEA 纬度格式 ddmm.mmmm → 十进制度
static double nmeaLatToDeg(double raw, char ns)
{
    int deg = (int)(raw / 100.0);
    double min = raw - deg * 100.0;
    double dec = deg + min / 60.0;
    return (ns == 'S') ? -dec : dec;
}

// NMEA 经度格式 dddmm.mmmm → 十进制度
static double nmeaLonToDeg(double raw, char ew)
{
    int deg = (int)(raw / 100.0);
    double min = raw - deg * 100.0;
    double dec = deg + min / 60.0;
    return (ew == 'W') ? -dec : dec;
}

// 从字符串中提取第 n 个逗号分隔字段（0-indexed），返回指向该字段的指针
// 如果字段为空或不存在，返回 NULL
static const char* nmeaField(const char *line, int idx)
{
    const char *p = line;
    int count = 0;
    while (*p) {
        if (count == idx) {
            // 跳过前导空格（不应有，但预防）
            while (*p == ' ') ++p;
            return p;
        }
        if (*p == ',') ++count;
        ++p;
    }
    return NULL;
}

// 解析一个逗号分隔字段中的数值（直到下一个逗号或 * 或结尾）
// 成功返回 true，out 被赋值；失败返回 false，out 不变
static bool parseFieldDouble(const char *field, double *out)
{
    if (field == NULL || *field == ',' || *field == '*' || *field == '\0')
        return false;
    char *end = NULL;
    double v = strtod(field, &end);
    if (end == field) return false;
    *out = v;
    return true;
}

// 解析一个逗号分隔字段中的整数
static bool parseFieldInt(const char *field, int *out)
{
    if (field == NULL || *field == ',' || *field == '*' || *field == '\0')
        return false;
    char *end = NULL;
    long v = strtol(field, &end, 10);
    if (end == field) return false;
    *out = (int)v;
    return true;
}

// 获取字段中指定位置的字符
static char fieldChar(const char *field, int offset)
{
    if (field == NULL) return '\0';
    int i = 0;
    while (i < offset && field[i] != '\0' && field[i] != ',' && field[i] != '*')
        ++i;
    if (i < offset) return '\0';
    return field[i];
}

// ---------- 各语句解析 ----------

// $GPGGA / $GNGGA
// 0: $GPGGA  1: time  2: lat  3: NS  4: lon  5: EW  6: fix  7: sats  8: HDOP  9: alt  10: M
static void parseGga(const char *line)
{
    const char *f_time = nmeaField(line, 1);
    const char *f_lat  = nmeaField(line, 2);
    const char *f_ns   = nmeaField(line, 3);
    const char *f_lon  = nmeaField(line, 4);
    const char *f_ew   = nmeaField(line, 5);
    const char *f_fix  = nmeaField(line, 6);
    const char *f_sats = nmeaField(line, 7);
    const char *f_alt  = nmeaField(line, 9);

    double rawLat, rawLon, alt;
    int fix;
    double timeVal;

    if (!parseFieldInt(f_fix, &fix)) return;
    if (fix == 0) {
        s_gps.fix_valid = false;
        return;
    }

    if (!parseFieldDouble(f_lat, &rawLat)) return;
    if (!parseFieldDouble(f_lon, &rawLon)) return;
    if (!parseFieldDouble(f_alt, &alt)) return;

    char ns = fieldChar(f_ns, 0);
    char ew = fieldChar(f_ew, 0);
    if (ns != 'N' && ns != 'S') return;
    if (ew != 'E' && ew != 'W') return;

    s_gps.lat = nmeaLatToDeg(rawLat, ns);
    s_gps.lon = nmeaLonToDeg(rawLon, ew);
    s_gps.altitude_m = alt;
    s_gps.fix_valid = true;

    if (parseFieldDouble(f_time, &timeVal)) {
        int hhmmss = (int)timeVal;
        s_gps.hour = hhmmss / 10000;
        s_gps.minute = (hhmmss / 100) % 100;
        s_gps.second = timeVal - (int)(timeVal / 100.0) * 100.0;
    }
}

// $GNRMC
// 0: $GNRMC  1: time  2: status  3: lat  4: NS  5: lon  6: EW  7: speed  8: course  9: date
static void parseRmc(const char *line)
{
    const char *f_time   = nmeaField(line, 1);
    const char *f_status = nmeaField(line, 2);
    const char *f_lat    = nmeaField(line, 3);
    const char *f_ns     = nmeaField(line, 4);
    const char *f_lon    = nmeaField(line, 5);
    const char *f_ew     = nmeaField(line, 6);
    const char *f_date   = nmeaField(line, 9);

    // 状态 A = 有效
    if (fieldChar(f_status, 0) != 'A') return;

    double rawLat, rawLon, timeVal;
    int dateVal;

    if (!parseFieldDouble(f_lat, &rawLat)) return;
    if (!parseFieldDouble(f_lon, &rawLon)) return;
    char ns = fieldChar(f_ns, 0);
    char ew = fieldChar(f_ew, 0);
    if (ns != 'N' && ns != 'S') return;
    if (ew != 'E' && ew != 'W') return;

    s_gps.lat = nmeaLatToDeg(rawLat, ns);
    s_gps.lon = nmeaLonToDeg(rawLon, ew);
    s_gps.fix_valid = true;

    if (parseFieldDouble(f_time, &timeVal)) {
        int hhmmss = (int)timeVal;
        s_gps.hour = hhmmss / 10000;
        s_gps.minute = (hhmmss / 100) % 100;
        s_gps.second = timeVal - (int)(timeVal / 100.0) * 100.0;
    }

    if (parseFieldInt(f_date, &dateVal)) {
        s_gps.day = dateVal / 10000;
        s_gps.month = (dateVal / 100) % 100;
        s_gps.year = 2000 + (dateVal % 100);
    }
}

// $GNZDA
// 0: $GNZDA  1: time  2: day  3: month  4: year  5: lz_hour  6: lz_min
static void parseZda(const char *line)
{
    const char *f_time = nmeaField(line, 1);
    const char *f_day  = nmeaField(line, 2);
    const char *f_mon  = nmeaField(line, 3);
    const char *f_year = nmeaField(line, 4);

    double timeVal;
    int day, month, year;

    if (!parseFieldDouble(f_time, &timeVal)) return;
    if (!parseFieldInt(f_day, &day)) return;
    if (!parseFieldInt(f_mon, &month)) return;
    if (!parseFieldInt(f_year, &year)) return;

    int hhmmss = (int)timeVal;
    s_gps.hour = hhmmss / 10000;
    s_gps.minute = (hhmmss / 100) % 100;
    s_gps.second = timeVal - (int)(timeVal / 100.0) * 100.0;
    s_gps.year = year;
    s_gps.month = month;
    s_gps.day = day;

    // ZDA 本身不包含位置信息，不修改 fix_valid
}

// ---------- 路由函数 ----------
static void parseNmeaLine(const char *line)
{
    if (!nmeaChecksum(line)) return;

    // 根据语句类型路由
    if (strncmp(line, "$GPGGA", 6) == 0 || strncmp(line, "$GNGGA", 6) == 0 ||
        strncmp(line, "$BDGGA", 6) == 0) {
        parseGga(line);
    } else if (strncmp(line, "$GPRMC", 6) == 0 || strncmp(line, "$GNRMC", 6) == 0 ||
               strncmp(line, "$BDRMC", 6) == 0) {
        parseRmc(line);
    } else if (strncmp(line, "$GNZDA", 6) == 0 || strncmp(line, "$GPZDA", 6) == 0 ||
               strncmp(line, "$BDZDA", 6) == 0) {
        parseZda(line);
    }
    // 其他语句忽略
}

// ---------- 公开接口 ----------

void gpsInit()
{
    GPS_Serial.begin(9600, SERIAL_8N1, PIN_GPS_RX, PIN_GPS_TX);
    Serial.println("[GPS] UART1 started @9600 on GPIO17/18");
}

void gpsTick()
{
    while (GPS_Serial.available()) {
        char c = (char)GPS_Serial.read();

        // 寻找行首 $
        if (!s_lineStarted) {
            if (c == '$') {
                s_lineStarted = true;
                s_lineLen = 0;
                s_lineBuf[s_lineLen++] = c;
            }
            continue;
        }

        // 换行 → 一条完整语句
        if (c == '\n') {
            s_lineBuf[s_lineLen] = '\0';
            if (s_lineLen > 3) {
                parseNmeaLine(s_lineBuf);
            }
            s_lineStarted = false;
            s_lineLen = 0;
            continue;
        }

        // 回车忽略
        if (c == '\r') continue;

        // 累积到缓冲
        if (s_lineLen < sizeof(s_lineBuf) - 1) {
            s_lineBuf[s_lineLen++] = c;
        } else {
            // 缓冲溢出，丢弃当前行
            s_lineStarted = false;
            s_lineLen = 0;
        }
    }
}

bool gpsHasFix()
{
    return s_gps.fix_valid;
}

bool gpsGetData(GpsData *out)
{
    if (out == NULL) return false;
    *out = s_gps;
    return s_gps.fix_valid;
}

bool gpsGetUtc(astro_utc_t *out)
{
    if (out == NULL) return false;
    if (!s_gps.fix_valid && s_gps.year == 0) return false;
    out->year   = s_gps.year;
    out->month  = s_gps.month;
    out->day    = s_gps.day;
    out->hour   = s_gps.hour;
    out->minute = s_gps.minute;
    out->second = s_gps.second;
    return s_gps.fix_valid || s_gps.year > 2000;
}

bool gpsGetObserver(astro_observer_t *out)
{
    if (out == NULL) return false;
    if (!s_gps.fix_valid) return false;
    out->latitude  = s_gps.lat;
    out->longitude = s_gps.lon;
    out->height    = s_gps.altitude_m;
    return true;
}