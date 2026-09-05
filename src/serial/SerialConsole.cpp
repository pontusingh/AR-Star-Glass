#include "SerialConsole.h"
#include "OrientationManager.h"
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
	Serial.printf(" [%s]", timetravelIsActive() ? "时空旅行" : gpsManagerStatusTag());
	Serial.printf(" [%s]",
		      timetravelHasLocationOverride() ? "位置覆盖" : gpsManagerStatusTag());
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

// 把行内剩余字段拆成 token 数组，返回个数（strtok_r 状态由 saveptr 维护）
static int splitTokens(char **saveptr, char *tokens[], int maxTokens)
{
	int n = 0;
	while (n < maxTokens) {
		char *tok = strtok_r(NULL, " \t", saveptr);
		if (tok == NULL)
			break;
		tokens[n++] = tok;
	}
	return n;
}

// 大小写不敏感比较
static bool tokenEqualsCi(const char *a, const char *b)
{
	while (*a && *b) {
		char ca = *a, cb = *b;
		if (ca >= 'A' && ca <= 'Z') ca += 32;
		if (cb >= 'A' && cb <= 'Z') cb += 32;
		if (ca != cb) return false;
		a++;
		b++;
	}
	return *a == *b;
}

// 严格解析一个 token 为 double：必须整体可解析（不允许 "OFF" 之类被 strtod 当 0.0）
static bool parseTokenDouble(const char *tok, double *out)
{
	if (tok == NULL || *tok == '\0')
		return false;
	char *end = NULL;
	double v = strtod(tok, &end);
	if (end == tok || *end != '\0')
		return false;
	*out = v;
	return true;
}

static void printHelp()
{
	Serial.println("命令:");
	Serial.println("  H                                  帮助");
	Serial.println("  P <纬度> <经度> [高度米]            位置覆盖");
	Serial.println("  T <年 月 日 时 分 秒>              设置当前 UTC");
	Serial.println("  TT <年 月 日 时 分 秒>             时空旅行覆盖；TT OFF 取消");
	Serial.println("  O <yaw> <pitch> <roll>             注入朝向；O OFF 取消");
	Serial.println("  I                                  打印当前朝向");
	Serial.println("  R                                  打印一次可见 Top10（含高度/方位角）");
}

static void handleCommand(char *line, bool &forceRefresh)
{
	char *saveptr = NULL;
	char *cmd = strtok_r(line, " \t", &saveptr);
	if (cmd == NULL)
		return;

	char *tokens[10];
	int ntok = splitTokens(&saveptr, tokens, 10);

	switch (cmd[0]) {
	case 'P': {
		double v[3];
		bool ok = ntok >= 2 && parseTokenDouble(tokens[0], &v[0]) &&
			  parseTokenDouble(tokens[1], &v[1]) &&
			  (ntok < 3 || parseTokenDouble(tokens[2], &v[2]));
		if (ok) {
			double h = (ntok >= 3) ? v[2] : 0.0;
			timetravelOverrideObserver(v[0], v[1], h);
			forceRefresh = true;
			Serial.printf("位置覆盖已生效: lat=%.4f lon=%.4f h=%.0fm\n", v[0], v[1], h);
		} else {
			Serial.println("用法: P <纬度> <经度> [高度米]");
		}
		break;
	}
	case 'T': {
		if (cmd[1] == 'T') {
			if (ntok == 1 && tokenEqualsCi(tokens[0], "OFF")) {
				timetravelClear();
				forceRefresh = true;
				Serial.println("时空旅行已取消，恢复实时时钟");
			} else if (ntok == 6) {
				double v[6];
				bool ok = true;
				for (int i = 0; i < 6; i++)
					ok = ok && parseTokenDouble(tokens[i], &v[i]);
				if (ok) {
					timetravelSet((int)v[0], (int)v[1], (int)v[2],
						      (int)v[3], (int)v[4], v[5]);
					forceRefresh = true;
					astro_utc_t o = timetravelGetOverride();
					Serial.printf("时空旅行生效: %04d-%02d-%02d %02d:%02d:%02d UTC\n",
						      o.year, o.month, o.day, o.hour, o.minute,
						      (int)o.second);
				} else {
					Serial.println("用法: TT <年 月 日 时 分 秒> 或 TT OFF");
				}
			} else {
				Serial.println("用法: TT <年 月 日 时 分 秒> 或 TT OFF");
			}
		} else {
			if (ntok == 6) {
				double v[6];
				bool ok = true;
				for (int i = 0; i < 6; i++)
					ok = ok && parseTokenDouble(tokens[i], &v[i]);
				if (ok) {
					astro_utc_t utc;
					utc.year = (int)v[0];
					utc.month = (int)v[1];
					utc.day = (int)v[2];
					utc.hour = (int)v[3];
					utc.minute = (int)v[4];
					utc.second = v[5];
					gpsManagerSetUtc(utc);
					forceRefresh = true;
					Serial.printf("当前 UTC 已设为 %04d-%02d-%02d %02d:%02d:%02d\n",
						      utc.year, utc.month, utc.day,
						      utc.hour, utc.minute, (int)utc.second);
				} else {
					Serial.println("用法: T <年 月 日 时 分 秒>");
				}
			} else {
				Serial.println("用法: T <年 月 日 时 分 秒>");
			}
		}
		break;
	}
	case 'O': {
		if (ntok == 1 && tokenEqualsCi(tokens[0], "OFF")) {
			orientationClearOverride();
			Serial.println("朝向注入已取消");
		} else if (ntok == 3) {
			double v[3];
			bool ok = true;
			for (int i = 0; i < 3; i++)
				ok = ok && parseTokenDouble(tokens[i], &v[i]);
			if (ok) {
				orientationSetOverride((float)v[0], (float)v[1], (float)v[2]);
				Serial.printf("朝向注入已生效: yaw=%.1f pitch=%.1f roll=%.1f\n",
					      v[0], v[1], v[2]);
			} else {
				Serial.println("用法: O <yaw> <pitch> <roll> 或 O OFF");
			}
		} else {
			Serial.println("用法: O <yaw> <pitch> <roll> 或 O OFF");
		}
		break;
	}
	case 'I': {
		ImuOrientation o;
		if (orientationGet(&o)) {
			Serial.printf("朝向: yaw=%+.1f pitch=%+.1f roll=%+.1f 精度=%u [%s]\n",
				      o.yaw, o.pitch, o.roll, o.accuracy,
				      orientationHasOverride() ? "注入" : "IMU");
			if (!orientationHasOverride())
				Serial.printf("IMU 链路: 帧 %lu, 复位 %u（应约 50 帧/秒）\n",
					      (unsigned long)imuGetFrameCount(), imuGetResetCount());
		} else {
			Serial.println("朝向: 无数据（IMU 未接，请用 O <yaw> <pitch> <roll> 注入）");
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

void serialConsoleTick(bool &forceRefresh)
{
	while (Serial.available() > 0) {
		char c = (char)Serial.read();
		if (c == '\r')
			continue;
		if (c == '\n') {
			s_lineBuf[s_lineLen] = '\0';
			handleCommand(s_lineBuf, forceRefresh);
			s_lineLen = 0;
		} else if (s_lineLen < sizeof(s_lineBuf) - 1) {
			s_lineBuf[s_lineLen++] = c;
		}
	}
}
