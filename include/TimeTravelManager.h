#ifndef TIME_TRAVEL_MANAGER_H
#define TIME_TRAVEL_MANAGER_H

// 行为层：时空旅行管理器。
// 持有一个可选的「用户设定时刻」和一个「用户设定位置」；
// 激活后，天文计算一律使用用户设定值，而不是实时 GPS 数据。
// 供 main.cpp 串口验证台和第 5 阶段按键调节使用。

#include <Arduino.h>
#include "AstronomyEngine.h"

// 时间覆盖
void timetravelSet(int year, int month, int day, int hour, int minute, double second);
void timetravelClear();
bool timetravelIsActive();
astro_utc_t timetravelGetOverride();
astro_time_t timetravelResolve(astro_time_t baseTime);

// 位置覆盖
void timetravelOverrideObserver(double lat, double lon, double height_m);
void timetravelClearObserver();
bool timetravelHasLocationOverride();
astro_observer_t timetravelGetObserverOverride();

// 核心解析：把 GPS 数据解析为参与天文计算的实际数据
astro_observer_t timetravelResolveObserver(astro_observer_t gpsObs);

#endif // TIME_TRAVEL_MANAGER_H
