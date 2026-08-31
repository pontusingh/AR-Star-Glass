// 行为层实现：时空旅行管理器。
// 状态：时间覆盖 + 位置覆盖，各有独立的激活标志。
#include "TimeTravelManager.h"

namespace {

astro_utc_t      s_timeOverride = {2000, 1, 1, 0, 0, 0.0};
bool             s_timeActive = false;
astro_observer_t s_locOverride = {0.0, 0.0, 0.0};
bool             s_locActive = false;

} // namespace

// ---------- 时间覆盖 ----------

void timetravelSet(int year, int month, int day, int hour, int minute, double second)
{
	s_timeOverride.year = year;
	s_timeOverride.month = month;
	s_timeOverride.day = day;
	s_timeOverride.hour = hour;
	s_timeOverride.minute = minute;
	s_timeOverride.second = second;
	s_timeActive = true;
}

void timetravelClear()
{
	s_timeActive = false;
}

bool timetravelIsActive()
{
	return s_timeActive;
}

astro_utc_t timetravelGetOverride()
{
	return s_timeOverride;
}

astro_time_t timetravelResolve(astro_time_t baseTime)
{
	if (!s_timeActive)
		return baseTime;
	return astroMakeTime(s_timeOverride.year, s_timeOverride.month, s_timeOverride.day,
			     s_timeOverride.hour, s_timeOverride.minute, s_timeOverride.second);
}

// ---------- 位置覆盖 ----------

void timetravelOverrideObserver(double lat, double lon, double height_m)
{
	s_locOverride.latitude = lat;
	s_locOverride.longitude = lon;
	s_locOverride.height = height_m;
	s_locActive = true;
}

void timetravelClearObserver()
{
	s_locActive = false;
}

bool timetravelHasLocationOverride()
{
	return s_locActive;
}

astro_observer_t timetravelGetObserverOverride()
{
	return s_locOverride;
}

astro_observer_t timetravelResolveObserver(astro_observer_t gpsObs)
{
	if (s_locActive)
		return s_locOverride;
	return gpsObs;
}

astro_time_t timetravelResolveTime(astro_time_t gpsTime)
{
	return timetravelResolve(gpsTime);
}
