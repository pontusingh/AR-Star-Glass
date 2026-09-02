// 行为层实现：可见性排序器。
// 计算路径分两条（与 AstronomyEngine 封装层对应）：
//   - 太阳系天体：逐个调用 astroBodyHorizon()；
//   - 恒星批量：每次更新只算一次 EQJ→HOR 旋转矩阵，60 颗星各做一次矩阵乘。
// 排序评分：score = -视星等 + 高度角加权。ALT_WEIGHT_PER_DEG = 0.05 意为
// 「高度角每升高 5 度，相当于变亮 1 个星等」——保证亮星优先，但压得很低的
// 亮星会自然排到升得较高的稍暗星之后，符合肉眼观星的推荐直觉。
#include "VisibilityRanker.h"

#include "StarCatalog.h"
#include "astronomy.h"

namespace {

const double ALT_WEIGHT_PER_DEG = 0.05;

RankedTarget s_targets[RANKER_MAX_TARGETS];
int s_count = 0;
double s_horizonMinDeg = 10.0; // 默认高度角 > 10° 才算可见（避开地平线附近大气/遮挡）

// 插入排序：n 很小（≤72），无需引入 qsort 的函数指针开销
void sortTargets(RankedTarget *arr, int n)
{
	for (int i = 1; i < n; i++) {
		RankedTarget key = arr[i];
		double keyScore = -key.magnitude + ALT_WEIGHT_PER_DEG * key.altitude;
		int j = i - 1;
		while (j >= 0) {
			double js = -arr[j].magnitude + ALT_WEIGHT_PER_DEG * arr[j].altitude;
			if (js >= keyScore)
				break;
			arr[j + 1] = arr[j];
			j--;
		}
		arr[j + 1] = key;
	}
}

} // namespace

void rankerSetHorizonMin(double degrees)
{
	s_horizonMinDeg = degrees;
}

double rankerHorizonMin()
{
	return s_horizonMinDeg;
}

void rankerUpdate(astro_time_t time, astro_observer_t observer)
{
	s_count = 0;

	// 恒星批量路径：整批目标共用一个 J2000→地平 旋转矩阵
	astro_rotation_t rotEqjToHor = astroEqjToHor(time, observer);

	// 1) 目录恒星
	for (int i = 0; i < STAR_CATALOG_COUNT && s_count < RANKER_MAX_TARGETS; i++) {
		const CatalogStar &star = STAR_CATALOG[i];
		AstroAltAz altAz;
		if (!astroStarHorizon(&rotEqjToHor, star.raHours, star.decDeg, &altAz))
			continue; // 单颗失败不拖垮整表
		if (altAz.altitude <= s_horizonMinDeg)
			continue;
		s_targets[s_count].kind = TARGET_STAR;
		s_targets[s_count].catalogIndex = i;
		s_targets[s_count].magnitude = star.mag;
		s_targets[s_count].altitude = altAz.altitude;
		s_targets[s_count].azimuth = altAz.azimuth;
		s_count++;
	}

	// 2) 太阳系天体（引擎原生逐体路径）
	for (int i = 0; i < BODY_CATALOG_COUNT && s_count < RANKER_MAX_TARGETS; i++) {
		const CatalogBody &entry = BODY_CATALOG[i];
		AstroAltAz altAz;
		if (!astroBodyHorizon(entry.body, time, observer, &altAz))
			continue;
		if (altAz.altitude <= s_horizonMinDeg)
			continue;
		s_targets[s_count].kind = TARGET_BODY;
		s_targets[s_count].catalogIndex = i;
		s_targets[s_count].magnitude = Astronomy_Illumination(entry.body, time).mag;
		s_targets[s_count].altitude = altAz.altitude;
		s_targets[s_count].azimuth = altAz.azimuth;
		s_count++;
	}

	sortTargets(s_targets, s_count);
}

int rankerCount()
{
	return s_count;
}

const RankedTarget *rankerAt(int index)
{
	if (index < 0 || index >= s_count)
		return NULL;
	return &s_targets[index];
}
