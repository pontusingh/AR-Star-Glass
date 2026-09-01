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
static uint32_t s_frameCount = 0;          // 累计收到旋转矢量帧数（链路健康指标）
static uint16_t s_resetCount = 0;          // 传感器意外复位次数

static const uint32_t REPORT_INTERVAL_US = 20000; // 旋转矢量报告间隔：50Hz

bool imuInit()
{
	// INT 开漏线：BNO085 有数据待发时拉低，imuTick 靠它做"有数据才读"门控
	pinMode(PIN_IMU_INT, INPUT_PULLUP);

	// 防御性硬复位必须先于地址探测：盲读被主机中止会让 BNO085 发送队列卡死，
	// 卡死状态可跨固件重启残留（传感器并没断电）；而 Adafruit 库 begin_I2C 是
	// 先探测地址、后硬件复位——卡死的传感器让探测失败，复位就永远轮不到。
	// 第二 I2C 外设，只允许 begin 一次（OLED 独占 Wire，见 test/oled.cpp 教训）
	Wire1.begin(PIN_IMU_SDA, PIN_IMU_SCL);

	for (int attempt = 1; attempt <= 2; attempt++) {
		pinMode(PIN_IMU_RST, OUTPUT);
		digitalWrite(PIN_IMU_RST, LOW);
		delay(10);
		digitalWrite(PIN_IMU_RST, HIGH);
		delay(200); // 等 BNO085 完成启动

		if (s_bno.begin_I2C(BNO08x_I2CADDR_DEFAULT, &Wire1))
			break;
		if (attempt == 2) {
			Serial.println("[IMU] 初始化失败：未找到 BNO085 (0x4A)，检查接线 / "
				       "PS0/PS1 是否 I2C 模式；曾卡死时请断电重插");
			return false;
		}
		Serial.println("[IMU] 首次探测失败，复位后重试...");
	}

	// 绝对朝向（融合磁力计，磁北基准）。
	// 不能用 SH2_GAME_ROTATION_VECTOR：它是相对朝向，没有绝对方位。
	if (!s_bno.enableReport(SH2_ROTATION_VECTOR, REPORT_INTERVAL_US)) {
		Serial.println("[IMU] 使能旋转矢量报告失败");
		return false;
	}

	Serial.printf("[IMU] BNO085 就绪：旋转矢量 50Hz（磁北基准），INT=%s\n",
		      digitalRead(PIN_IMU_INT) == LOW ? "低(有数据待发)" : "高");

	// 清掉本次上电复位留下的标志，避免 imuTick 把它误报为"意外复位"
	(void)s_bno.wasReset();

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

	// 意外复位自愈：电源瞬断/干扰会让传感器复位且报告使能丢失。
	// 复位完成通告会拉低 INT，下面的门控不会挡住它；一旦读到，这里重新使能。
	if (s_bno.wasReset()) {
		s_resetCount++;
		s_bno.enableReport(SH2_ROTATION_VECTOR, REPORT_INTERVAL_US);
		Serial.printf("[IMU] 传感器意外复位，已重新使能旋转矢量报告（第 %u 次）\n",
			      s_resetCount);
	}

	// INT 门控：仅在有数据待发（INT 低）时读取。
	// Adafruit 库的 I2C 路径盲读不看 INT，ESP32 上盲读空事务会触发
	// 时钟延展→主机中止→传感器发送队列卡死，必须自己挡（见 imuInit 注释）。
	if (digitalRead(PIN_IMU_INT) != LOW) {
		// 就绪 1s 后 INT 从未拉低且无数据：一次性告警，帮助排查 INT 接线
		static unsigned long readyMs = 0;
		static bool warned = false;
		if (readyMs == 0)
			readyMs = millis();
		if (!warned && !s_hasData && millis() - readyMs > 1000) {
			warned = true;
			Serial.println("[IMU] 警告：INT 一直为高且无数据，检查 INT 接线 (GPIO10)");
		}
		return;
	}

	sh2_SensorValue_t v;
	while (s_bno.getSensorEvent(&v)) { // 一圈可能积多条事件，全部取走
		if (v.sensorId == SH2_ROTATION_VECTOR) {
			quatToEuler(v.un.rotationVector.real, v.un.rotationVector.i,
				    v.un.rotationVector.j, v.un.rotationVector.k,
				    &s_latest.yaw, &s_latest.pitch, &s_latest.roll);
			s_latest.accuracy = v.status & 0x03; // 低 2 位为精度
			s_latest.lastUpdateMs = millis();
			s_hasData = true;
			s_frameCount++;
		}
	}
}

uint32_t imuGetFrameCount() { return s_frameCount; }
uint16_t imuGetResetCount() { return s_resetCount; }

bool imuGetOrientation(ImuOrientation *out)
{
	if (!s_hasData || out == NULL)
		return false;
	*out = s_latest;
	return true;
}
