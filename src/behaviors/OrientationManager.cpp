// 行为层实现：当前视线方向来源。
// 读取真实 IMU（BNO085）或串口注入覆盖，并应用校准残差作朝向修正。
#include "OrientationManager.h"
#include "CalibrationManager.h"

namespace {

ImuOrientation s_override = {0.0f, 0.0f, 0.0f, 0, 0};
bool s_hasOverride = false;

double s_calAz = 0.0;    // 校准残差：方位
double s_calAlt = 0.0;   // 校准残差：高度
bool   s_calValid = false;

double wrapDeg(double deg)
{
	while (deg > 180.0)
		deg -= 360.0;
	while (deg < -180.0)
		deg += 360.0;
	return deg;
}

} // namespace

void orientationInit()
{
	s_calValid = calibrationLoadLast(&s_calAz, &s_calAlt);
}

void orientationSetCalibrationOffset(double azDeg, double altDeg)
{
	s_calAz = azDeg;
	s_calAlt = altDeg;
	s_calValid = true;
}

void orientationSetOverride(float yaw, float pitch, float roll)
{
	s_override.yaw = yaw;
	s_override.pitch = pitch;
	s_override.roll = roll;
	s_override.accuracy = 3; // 注入值视为高精度
	s_override.lastUpdateMs = millis();
	s_hasOverride = true;
}

void orientationClearOverride()
{
	s_hasOverride = false;
}

bool orientationHasOverride()
{
	return s_hasOverride;
}

bool orientationGet(ImuOrientation *out)
{
	if (out == NULL)
		return false;
	if (s_hasOverride) {
		*out = s_override;
		return true;
	}
	if (!imuGetOrientation(out))
		return false;
	if (s_calValid) {
		out->yaw = (float)wrapDeg((double)out->yaw - s_calAz);
		out->pitch -= (float)s_calAlt;
	}
	return true;
}

bool orientationGetRaw(ImuOrientation *out)
{
	if (out == NULL)
		return false;
	if (s_hasOverride) {
		*out = s_override;
		return true;
	}
	return imuGetOrientation(out);
}
