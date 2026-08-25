/*
 * 第 1 阶段数值验证程序（PC 端，无需硬件）。
 *
 * 目的：用与固件完全相同的代码路径（lib/AstronomyEngine/src/astronomy.c +
 *       AstronomyEngine.cpp 的恒星批量矩阵乘法），在固定时刻/地点打印
 *       太阳系天体与亮星的地平坐标，供对照官方 astronomy-engine(Python) 输出。
 *
 * 构建（仓库根目录执行）：
 *   gcc -DASTRONOMY_ENGINE_NO_CURRENT_TIME -Ilib/AstronomyEngine/src \
 *       tools/host_verify/host_verify.c src/data/AstronomyEngine.cpp \
 *       -o tools/host_verify/host_verify -lm
 * 运行：tools/host_verify/host_verify.exe
 */
#include <stdio.h>
#include "astronomy.h"
#include "AstronomyEngine.h"

/* 测试用的恒星样本（J2000 坐标取自星表生成脚本） */
typedef struct {
	const char *name;
	double raHours;
	double decDeg;
} TestStar;

static const TestStar TEST_STARS[] = {
	{"Vega",       18.61560, 38.78300},
	{"Sirius",      6.75260, -16.71300},
	{"Polaris",     2.52973, 89.26400},
	{"Betelgeuse",  5.91953,   7.40700},
};

int main(void)
{
	const astro_time_t t = Astronomy_MakeTime(2026, 8, 25, 12, 0, 0.0);
	const astro_observer_t obs = Astronomy_MakeObserver(39.9042, 116.4074, 40.0);

	printf("TIME 2026-08-25T12:00:00Z  OBS lat=%.4f lon=%.4f h=%.0f\n",
	       obs.latitude, obs.longitude, obs.height);

	/* ---- 太阳系天体（与固件 astroBodyHorizon 相同的两步路径） ---- */
	const astro_body_t bodies[] = {BODY_SUN, BODY_MOON, BODY_JUPITER, BODY_MARS};
	for (unsigned i = 0; i < sizeof(bodies) / sizeof(bodies[0]); i++) {
		AstroAltAz r;
		if (!astroBodyHorizon(bodies[i], t, obs, &r)) {
			printf("BODY %d FAIL\n", (int)bodies[i]);
			continue;
		}
		printf("BODY %d az=%.6f alt=%.6f\n", (int)bodies[i], r.azimuth, r.altitude);
	}

	/* ---- 恒星批量路径（与固件 astroStarHorizon 相同的矩阵乘法） ---- */
	const astro_rotation_t rot = astroEqjToHor(t, obs);
	if (rot.status != ASTRO_SUCCESS) {
		printf("ROTATION FAIL\n");
		return 1;
	}
	for (unsigned i = 0; i < sizeof(TEST_STARS) / sizeof(TEST_STARS[0]); i++) {
		AstroAltAz r;
		if (!astroStarHorizon(&rot, TEST_STARS[i].raHours, TEST_STARS[i].decDeg, &r)) {
			printf("STAR %s FAIL\n", TEST_STARS[i].name);
			continue;
		}
		printf("STAR %s az=%.6f alt=%.6f\n", TEST_STARS[i].name, r.azimuth, r.altitude);
	}

	return 0;
}
