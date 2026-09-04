// 行为层实现：当前视线方向来源。
// 阶段2硬件暂缓：真实 IMU 轮询暂不接入；本阶段仅支持串口注入覆盖，
// 供隔离硬件（无 BNO085）调试。后续接入 imuTick()/单点验证后，
// orientationGet 仍保持「注入优先，否则回退真实 IMU」。
#include "OrientationManager.h"

namespace {

ImuOrientation s_override = {0.0f, 0.0f, 0.0f, 0, 0};
bool s_hasOverride = false;

} // namespace

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
	// 真实 IMU 暂缓接入（BNO085 暂缓），始终无数据
	return imuGetOrientation(out);
}
