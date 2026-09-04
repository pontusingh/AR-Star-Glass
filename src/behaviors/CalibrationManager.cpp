// 行为层实现：单点验证校准。
// 用当前朝向（注入或真实 IMU）比对目标天体计算出的方位/高度，得残差并写 NVS。
// 只在「校准提交」时写一次，读写频率极低，无闪存磨损顾虑（见 plan.md 单点验证）。
#include "CalibrationManager.h"
#include "VisibilityRanker.h"
#include "OrientationManager.h"
#include <Preferences.h>

namespace {

Preferences s_prefs;
bool s_prefsOpen = false;

// 角度折到 [-180, +180]
double wrapDeg(double deg)
{
	while (deg > 180.0)
		deg -= 360.0;
	while (deg < -180.0)
		deg += 360.0;
	return deg;
}

} // namespace

bool calibrationVerify(int targetIdx, double *azResidual, double *altResidual)
{
	const RankedTarget *t = rankerAt(targetIdx);
	if (t == NULL)
		return false;

	ImuOrientation o;
	// 用「原始」朝向计算残差，避免把已应用的修正再算进去（否则重复校准会抹掉修正）。
	if (!orientationGetRaw(&o))
		return false;

	// 磁北基准的 yaw 与真北基准的方位角差当地磁偏角 + 安装误差，残差即修正量；
	// pitch 与高度角直接对应。
	double azRes = wrapDeg(o.yaw - t->azimuth);
	double altRes = o.pitch - t->altitude;

	if (!s_prefsOpen) {
		s_prefs.begin("calib", false);
		s_prefsOpen = true;
	}
	s_prefs.putDouble("az_res", azRes);
	s_prefs.putDouble("alt_res", altRes);

	// 立即生效：更新 OrientationManager 缓存的修正量
	orientationSetCalibrationOffset(azRes, altRes);

	if (azResidual)
		*azResidual = azRes;
	if (altResidual)
		*altResidual = altRes;
	return true;
}

bool calibrationLoadLast(double *azResidual, double *altResidual)
{
	if (!s_prefsOpen) {
		s_prefs.begin("calib", false);
		s_prefsOpen = true;
	}
	if (!s_prefs.isKey("az_res"))
		return false;
	if (azResidual)
		*azResidual = s_prefs.getDouble("az_res", 0.0);
	if (altResidual)
		*altResidual = s_prefs.getDouble("alt_res", 0.0);
	return true;
}
