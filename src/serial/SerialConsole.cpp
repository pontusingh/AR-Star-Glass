#include "SerialConsole.h"
#include <string.h>
#include <stdlib.h>

static const int PRINT_TOP_N = 10;

static char s_lineBuf[128];
static size_t s_lineLen = 0;

static void printEffectiveTime(astro_time_t t)
{
	astro_utc_t utc = Astronomy_UtcFromTime(t);
	Serial.printf("%04d-%02d-%02d %02d:%02d:%02d UTC", utc.year, utc.month, utc.day,
		      utc.hour, utc.minute, (int)utc.second);
}

void serialReportTargets(astro_time_t effectiveTime, astro_observer_t effectiveObs)
{
	Serial.println("------------------------------------------------------------");
	printEffectiveTime(effectiveTime);
	Serial.printf(" [%s]", timetravelIsActive() ? "时空旅行" : gpsManagerTimeTag());
	Serial.printf(" [%s]",
		      timetravelHasLocationOverride() ? "位置覆盖" : gpsManagerLocTag());
	Serial.printf("  观察点 lat=%.4f lon=%.4f h=%.0fm\n", effectiveObs.latitude,
		      effectiveObs.longitude, effectiveObs.height);

	rankerUpdate(effectiveTime, effectiveObs);

	int n = rankerCount();
	Serial.printf("地平线以上共 %d 个目标，按亮度+高度角排序 Top%d:\n", n, PRINT_TOP_N);
	Serial.println("  #  名称          HIP/体  星等    方位角   高度角");
	for (int i = 0; i < n && i < PRINT_TOP_N; i++) {
		const RankedTarget *t = rankerAt(i);
		if (t == NULL)
			break;
		if (t->kind == TARGET_STAR) {
			const CatalogStar &s = STAR_CATALOG[t->catalogIndex];
			Serial.printf(" %2d  %-12s %-6u %+5.2f   %6.1f   %+6.1f\n", i + 1,
				      s.nameAscii, s.hip, s.mag, t->azimuth, t->altitude);
		} else {
			const CatalogBody &b = BODY_CATALOG[t->catalogIndex];
			Serial.printf(" %2d  %-12s %-6s   --    %6.1f   %+6.1f\n", i + 1,
				      b.nameAscii, "-", t->azimuth, t->altitude);
		}
	}
}

static int parseDoubles(char *saveptr, double *out, int maxCount)
{
	int count = 0;
	while (count < maxCount) {
		char *tok = strtok_r(NULL, " \t", &saveptr);
		if (tok == NULL)
			break;
		out[count++] = strtod(tok, NULL);
	}
	return count;
}

static void printHelp()
{
	Serial.println("命令: H=帮助 P=<纬度> <经度> T=<年 月 日 时 分 秒> "
		       "TT=<年 月 日 时 分 秒|OFF> R=立即刷新");
}

static void handleCommand(char *line, astro_utc_t &nowUtc, bool &forceRefresh)
{
	char *saveptr = NULL;
	char *cmd = strtok_r(line, " \t", &saveptr);
	if (cmd == NULL)
		return;
	double v[6];

	switch (cmd[0]) {
	case 'P': {
		if (parseDoubles(saveptr, v, 2) == 2) {
			timetravelOverrideObserver(v[0], v[1], 0.0);
			forceRefresh = true;
			Serial.printf("位置覆盖已生效: lat=%.4f lon=%.4f\n", v[0], v[1]);
		} else {
			Serial.println("用法: P <纬度> <经度>");
		}
		break;
	}
	case 'T': {
		if (cmd[1] == 'T') {
			if (parseDoubles(saveptr, v, 6) == 6) {
				timetravelSet((int)v[0], (int)v[1], (int)v[2], (int)v[3],
					      (int)v[4], v[5]);
				forceRefresh = true;
				astro_utc_t o = timetravelGetOverride();
				Serial.printf("时空旅行生效: %04d-%02d-%02d %02d:%02d:%02d UTC\n",
					      o.year, o.month, o.day, o.hour, o.minute,
					      (int)o.second);
			} else if (strtok_r(NULL, " \t", &saveptr) != NULL) {
				timetravelClear();
				forceRefresh = true;
				Serial.println("时空旅行已取消，恢复实时时钟");
			} else {
				Serial.println("用法: TT <年 月 日 时 分 秒> 或 TT OFF");
			}
		} else {
			if (parseDoubles(saveptr, v, 6) == 6) {
				nowUtc.year = (int)v[0];
				nowUtc.month = (int)v[1];
				nowUtc.day = (int)v[2];
				nowUtc.hour = (int)v[3];
				nowUtc.minute = (int)v[4];
				nowUtc.second = v[5];
				forceRefresh = true;
				Serial.printf("当前 UTC 已设为 %04d-%02d-%02d %02d:%02d:%02d\n",
					      nowUtc.year, nowUtc.month, nowUtc.day,
					      nowUtc.hour, nowUtc.minute, (int)nowUtc.second);
			} else {
				Serial.println("用法: T <年 月 日 时 分 秒>");
			}
		}
		break;
	}
	case 'R':
		forceRefresh = true;
		break;
	case 'H':
	default:
		printHelp();
		break;
	}
}

void serialConsoleInit()
{
	printHelp();
}

void serialConsoleTick(astro_utc_t &nowUtc, bool &forceRefresh)
{
	while (Serial.available() > 0) {
		char c = (char)Serial.read();
		if (c == '\r')
			continue;
		if (c == '\n') {
			s_lineBuf[s_lineLen] = '\0';
			handleCommand(s_lineBuf, nowUtc, forceRefresh);
			s_lineLen = 0;
		} else if (s_lineLen < sizeof(s_lineBuf) - 1) {
			s_lineBuf[s_lineLen++] = c;
		}
	}
}