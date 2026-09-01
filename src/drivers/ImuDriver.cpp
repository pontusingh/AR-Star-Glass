// 驱动层：BNO085 IMU（Adafruit BNO08x 库，SH-2 协议，Wire1 第二 I2C 总线）
// 纯硬件封装：初始化 + 取数 + 四元数转欧拉角，不含任何校准逻辑（校准在行为层）。
#include <Arduino.h>
#include <Adafruit_BNO08x.h>
#include <math.h>

#include "ImuDriver.h"

static Adafruit_BNO08x s_bno(PIN_IMU_RST); // RST 脚交给库做上电硬复位
static ImuOrientation s_latest;            // 最新一帧朝向
static bool s_hasData = false;             // 是否收到过有效数据
static bool s_ready = false;               // 初始化是否成功

static const uint32_t REPORT_INTERVAL_US = 20000; // 旋转矢量报告间隔：50Hz

bool imuInit()
{
	// 第二 I2C 外设，只允许 begin 一次（OLED 独占 Wire，见 test/oled.cpp 教训）
	Wire1.begin(PIN_IMU_SDA, PIN_IMU_SCL);

	if (!s_bno.begin_I2C(BNO08x_I2CADDR_DEFAULT, &Wire1)) {
		Serial.println("[IMU] 初始化失败：未找到 BNO085 (0x4A)，检查接线 / PS0/PS1 是否 I2C 模式");
		return false;
	}

	// 绝对朝向（融合磁力计，磁北基准）。
	// 不能用 SH2_GAME_ROTATION_VECTOR：它是相对朝向，没有绝对方位。
	if (!s_bno.enableReport(SH2_ROTATION_VECTOR, REPORT_INTERVAL_US)) {
		Serial.println("[IMU] 使能旋转矢量报告失败");
		return false;
	}

	Serial.println("[IMU] BNO085 就绪：旋转矢量 50Hz（磁北基准）");
	s_ready = true;
	return true;
}

// 四元数 → 欧拉角（度），公式来自 Adafruit 官方例程 quaternion_yaw_pitch_roll
static void quatToEuler(float qr, float qi, float qj, float qk,
			float *yaw, float *pitch, float *roll)
{
	float sqr = qr * qr, sqi = qi * qi, sqj = qj * qj, sqk = qk * qk;

	*yaw = atan2f(2.0f * (qi * qj + qk * qr), (sqi - sqj - sqk + sqr));
	*pitch = asinf(-2.0f * (qi * qk - qj * qr) / (sqi + sqj + sqk + sqr));
	*roll = atan2f(2.0f * (qj * qk + qi * qr), (-sqi - sqj + sqk + sqr));

	*yaw *= RAD_TO_DEG;
	*pitch *= RAD_TO_DEG;
	*roll *= RAD_TO_DEG;
}

void imuTick()
{
	if (!s_ready)
		return;

	sh2_SensorValue_t v;
	while (s_bno.getSensorEvent(&v)) { // 一圈可能积多条事件，全部取走
		if (v.sensorId == SH2_ROTATION_VECTOR) {
			quatToEuler(v.un.rotationVector.real, v.un.rotationVector.i,
				    v.un.rotationVector.j, v.un.rotationVector.k,
				    &s_latest.yaw, &s_latest.pitch, &s_latest.roll);
			s_latest.accuracy = v.status & 0x03; // 低 2 位为精度
			s_latest.lastUpdateMs = millis();
			s_hasData = true;
		}
	}
}

bool imuGetOrientation(ImuOrientation *out)
{
	if (!s_hasData || out == NULL)
		return false;
	*out = s_latest;
	return true;
}
