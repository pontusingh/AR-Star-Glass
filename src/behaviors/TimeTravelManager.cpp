// 行为层实现：时空旅行管理器。
// 状态极简：一个 astro_utc_t 覆盖值 + 一个激活标志。
// 不持有实时时钟本身——「现在几点」由上层（本阶段是 main.cpp 的串口验证台，
// 未来是 GpsDriver）提供，这里只负责「要不要替换成用户时刻」这一个决策，
// 因此 GPS 接入时本模块无需任何改动。
#include "TimeTravelManager.h"

namespace {

astro_utc_t s_override = {2000, 1, 1, 0, 0, 0.0}; // 未激活时的占位值
bool s_active = false;

} // namespace

void timetravelSet(int year, int month, int day, int hour, int minute, double second)
{
	s_override.year = year;
	s_override.month = month;
	s_override.day = day;
	s_override.hour = hour;
	s_override.minute = minute;
	s_override.second = second;
	s_active = true;
}

void timetravelClear()
{
	s_active = false;
}

bool timetravelIsActive()
{
	return s_active;
}

astro_utc_t timetravelGetOverride()
{
	return s_override;
}

astro_time_t timetravelResolve(astro_time_t baseTime)
{
	if (!s_active)
		return baseTime;
	// 引擎移植版没有 CurrentTime()，覆盖时刻同样要经 astroMakeTime 构造
	return astroMakeTime(s_override.year, s_override.month, s_override.day,
			     s_override.hour, s_override.minute, s_override.second);
}
