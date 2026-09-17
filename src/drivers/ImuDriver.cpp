// 驱动层：BNO085 IMU（Adafruit BNO08x 库，SH-2 协议，与 OLED 共享 Wire 总线）
// 纯硬件封装：初始化 + 取数 + 四元数转欧拉角，不含任何校准逻辑（校准在行为层）。
#include <Arduino.h>
#include <Adafruit_BNO08x.h>
#include <math.h>

#include "ImuDriver.h"

// 调试开关：1 = 每 1s 在串口打印朝向/帧数/复位数；稳定后改 0 关闭
#define IMU_DEBUG 1
// 诊断开关：1 = 额外使能原始加速度计报告，验证 MEMS 核心是否出数（定位「融合坏」vs「芯片坏」）
#define IMU_DIAG_RAW 1

static Adafruit_BNO08x s_bno(PIN_IMU_RST); // RST 脚交给库做上电硬复位
static ImuOrientation s_latest;            // 最新一帧朝向
static bool s_hasData = false;             // 是否收到过有效数据
static bool s_ready = false;               // 初始化是否成功
static uint32_t s_frameCount = 0;          // 累计收到旋转矢量帧数（链路健康指标）
static uint32_t s_rawAccelCount = 0;       // 累计收到的原始加速度计帧数（诊断：验证 MEMS 出数）
static uint16_t s_resetCount = 0;          // 传感器意外复位次数

static const uint32_t REPORT_INTERVAL_US = 20000; // 旋转矢量报告间隔：50Hz
static const uint32_t IMU_STALL_TIMEOUT_MS = 2000; // 数据停摆阈值：2s 无新帧触发重使能
static const uint32_t REENABLE_RETRY_INTERVAL_MS = 100; // 重使能重试间隔（非阻塞）
static const uint16_t REENABLE_RETRY_MAX = 50;          // 重使能重试上限（约 5s）

// 非阻塞重使能状态（复位/停摆后不 delay、不重置 s_hasData，静默沿用旧朝向）
static bool s_pendingReenable = false;
static uint16_t s_reenableAttempts = 0;
static unsigned long s_reenableLastTryMs = 0;

// 单地址探测：只发一次 beginTransmission/endTransmission 看是否 ACK（不碰 SH2 协议）
static bool i2cProbe(uint8_t addr)
{
	Wire.beginTransmission(addr);
	return Wire.endTransmission() == 0;
}

// 全总线扫描：打印所有应答地址，用于区分「地址漂移(0x4B)/整总线掉/仅 BNO085 掉」
static void imuI2cScan()
{
	Serial.print("[IMU] I2C 扫描:");
	for (uint8_t a = 0x08; a < 0x78; a++) {
		if (i2cProbe(a)) {
			Serial.printf(" 0x%02X", a);
		}
	}
	Serial.println();
}

bool imuInit()
{
	// INT 开漏线：BNO085 有数据待发时拉低，imuTick 靠它做"有数据才读"门控
	pinMode(PIN_IMU_INT, INPUT_PULLUP);

	// 防御性硬复位必须先于地址探测：盲读被主机中止会让 BNO085 发送队列卡死，
	// 卡死状态可跨固件重启残留（传感器并没断电）；而 Adafruit 库 begin_I2C 是
	// 先探测地址、后硬件复位——卡死的传感器让探测失败，复位就永远轮不到。
	// 总线已由 oledInit() 的 U8g2 构造器初始化（共享 Wire，SDA/SCL 同 OLED），
	// 这里绝不能再次 Wire.begin()，否则重复初始化锁死总线（见 test/oled.cpp 教训）。

	// 复位前先扫一次总线，记录当前所有应答地址（0x3C=OLED 0x4A/0x4B=BNO085）
	imuI2cScan();

	for (int attempt = 1; attempt <= 3; attempt++) {
		pinMode(PIN_IMU_RST, OUTPUT);
		digitalWrite(PIN_IMU_RST, LOW);
		delay(50);   // 放宽复位低脉冲（原 10ms）
		digitalWrite(PIN_IMU_RST, HIGH);
		delay(500);  // 放宽启动等待（原 200ms）

		// 复位后单独看 0x4A/0x4B 是否 ACK，先于 begin_I2C 判定地址是否到位
		Serial.printf("[IMU] 尝试 %d：复位后 0x4A=%s 0x4B=%s (t=%lums)\n", attempt,
			      i2cProbe(BNO08x_I2CADDR_DEFAULT) ? "ACK" : "无",
			      i2cProbe(0x4B) ? "ACK" : "无", (unsigned long)millis());

		uint8_t addr = BNO08x_I2CADDR_DEFAULT;
		bool ok = false;

		Serial.printf("[IMU] begin_I2C(0x%02X) 开始...\n", addr);
		unsigned long t0 = millis();
		if (s_bno.begin_I2C(addr, &Wire)) {
			ok = true;
		} else if (i2cProbe(0x4B)) {
			// 0x4A 失败但 0x4B ACK → ADDR 脚悬空/虚焊导致地址漂移，改试 0x4B
			Serial.println("[IMU] 0x4A 失败但 0x4B 有应答，改用备用地址 0x4B...");
			addr = 0x4B;
			if (s_bno.begin_I2C(addr, &Wire))
				ok = true;
		}
		Serial.printf("[IMU] begin_I2C 返回 %s（耗时 %lums）\n",
			      ok ? "成功" : "失败", (unsigned long)(millis() - t0));

		if (ok)
			break;
		if (attempt == 3) {
			Serial.println("[IMU] 初始化失败：未找到 BNO085 (0x4A/0x4B)，检查接线 / "
				       "PS0/PS1 是否 I2C 模式；曾卡死时请断电重插");
			return false;
		}
		Serial.printf("[IMU] 第 %d 次探测失败，复位后重试...\n", attempt);
	}

	// 绝对朝向（融合磁力计，磁北基准）。
	// 不能用 SH2_GAME_ROTATION_VECTOR：它是相对朝向，没有绝对方位。
	if (!s_bno.enableReport(SH2_ROTATION_VECTOR, REPORT_INTERVAL_US)) {
		Serial.println("[IMU] 使能旋转矢量报告失败");
		return false;
	}

	// 打印产品 ID：识别芯片型号/固件版本/复位原因（定位「芯片坏」vs「融合坏」）
	Serial.printf("[IMU] 产品 ID 共 %d 项:\n", s_bno.prodIds.numEntries);
	for (int i = 0; i < s_bno.prodIds.numEntries && i < 5; i++) {
		const sh2_ProductId_t &p = s_bno.prodIds.entry[i];
		Serial.printf("[IMU]   #%d partNum=0x%08lX ver=%u.%u.%u build=%lu resetCause=0x%02X\n",
			      i, (unsigned long)p.swPartNumber, p.swVersionMajor,
			      p.swVersionMinor, p.swVersionPatch,
			      (unsigned long)p.swBuildNumber, p.resetCause);
	}

#if IMU_DIAG_RAW
	// 诊断：额外使能原始加速度计（最基础 MEMS 报告，不依赖融合/磁力计）
	if (s_bno.enableReport(SH2_RAW_ACCELEROMETER, REPORT_INTERVAL_US))
		Serial.println("[IMU] 诊断：已使能原始加速度计报告 (0x14)");
	else
		Serial.println("[IMU] 诊断：使能原始加速度计报告失败");
#endif

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

// 请求非阻塞重使能：复位/停摆后置位，由 imuTick 里限流重试 enableReport。
static void requestReenable()
{
	if (!s_pendingReenable)
		s_resetCount++;
	s_pendingReenable = true;
	s_reenableAttempts = 0;
	s_reenableLastTryMs = 0;
}

// 处理单个传感器事件：旋转矢量 → 欧拉角 + 更新 s_latest（INT 门控与诊断轮询共用）
static void processSensorEvent(const sh2_SensorValue_t &v)
{
	if (v.sensorId == SH2_ROTATION_VECTOR) {
		quatToEuler(v.un.rotationVector.real, v.un.rotationVector.i,
			    v.un.rotationVector.j, v.un.rotationVector.k,
			    &s_latest.yaw, &s_latest.pitch, &s_latest.roll);
		s_latest.accuracy = v.status & 0x03; // 低 2 位为精度
		s_latest.lastUpdateMs = millis();
		s_hasData = true;
		s_frameCount++;
	} else if (v.sensorId == SH2_RAW_ACCELEROMETER) {
		s_rawAccelCount++;
		if (s_rawAccelCount <= 3)
			Serial.printf("[IMU] 收到原始加速度计帧 x=%d y=%d z=%d（MEMS 核心在出数）\n",
				      v.un.rawAccelerometer.x, v.un.rawAccelerometer.y,
				      v.un.rawAccelerometer.z);
	} else if (s_frameCount + s_rawAccelCount <= 3) {
		Serial.printf("[IMU] 收到其它事件 sensorId=0x%02X\n", v.sensorId);
	}
}

void imuTick()
{
	if (!s_ready)
		return;

#if IMU_DEBUG
	// 1Hz 调试打印：观察朝向是否持续更新、帧数是否递增、复位是否发生
	{
		static unsigned long lastDbgMs = 0;
		if (millis() - lastDbgMs >= 1000) {
			lastDbgMs = millis();
			if (s_hasData)
				Serial.printf("[IMU] yaw=%+7.1f pitch=%+6.1f roll=%+6.1f acc=%u 帧=%lu 复位=%u\n",
					      s_latest.yaw, s_latest.pitch, s_latest.roll, s_latest.accuracy,
					      (unsigned long)s_frameCount, s_resetCount);
			else
				Serial.printf("[IMU] 无数据 帧=%lu 原始加速=%lu 复位=%u\n",
					      (unsigned long)s_frameCount,
					      (unsigned long)s_rawAccelCount, s_resetCount);
		}
	}
#endif

	// 意外复位自愈：电源瞬断/干扰会让传感器复位且报告使能丢失。
	if (s_bno.wasReset()) {
		Serial.printf("[IMU] 传感器意外复位（第 %u 次）\n", s_resetCount + 1);
		requestReenable();
	}

	// 数据停摆看门狗：曾收到数据、如今 2s 无新帧 → 漏检的复位/卡死，主动请求重使能。
	if (s_hasData && (millis() - s_latest.lastUpdateMs) > IMU_STALL_TIMEOUT_MS) {
		Serial.printf("[IMU] 数据停摆，请求重使能（第 %u 次）\n", s_resetCount + 1);
		requestReenable();
	}

	// INT 门控：仅在有数据待发（INT 低）时读取。
	// Adafruit 库的 I2C 路径盲读不看 INT，ESP32 上盲读空事务会触发
	// 时钟延展→主机中止→传感器发送队列卡死，必须自己挡（见 imuInit 注释）。
	// 这一读同时驱动 sh2_service 处理复位包/重新 advert，从而重建 control 通道。
	if (digitalRead(PIN_IMU_INT) == LOW) {
		sh2_SensorValue_t v;
		while (s_bno.getSensorEvent(&v)) { // 一圈可能积多条事件，全部取走
			processSensorEvent(v);
		}
	} else {
		// 就绪 1s 后 INT 从未拉低且无数据：一次性告警，帮助排查 INT 接线
		static unsigned long readyMs = 0;
		static bool warned = false;
		if (readyMs == 0)
			readyMs = millis();
		if (!warned && !s_hasData && millis() - readyMs > 1000) {
			warned = true;
			Serial.println("[IMU] 警告：INT 一直为高且无数据，检查 INT 接线 (GPIO6)");
		}

		// 诊断：INT 长期为高且无数据 → 有限次无条件读，区分「芯片在出数(INT断线)」vs「芯片未出数」
		static int diagPolls = 0;
		static unsigned long diagLastMs = 0;
		if (!s_hasData && diagPolls < 6 && millis() - readyMs > 2500) {
			if (millis() - diagLastMs >= 250) {
				diagLastMs = millis();
				sh2_SensorValue_t v;
				diagPolls++;
				if (s_bno.getSensorEvent(&v)) {
					Serial.printf("[IMU] 诊断轮询 #%d 命中 sensorId=%d → 芯片在出数，INT 线断/未接！\n",
						      diagPolls, v.sensorId);
					processSensorEvent(v);
					diagPolls = 999; // 已确认，停止诊断轮询
				} else {
					Serial.printf("[IMU] 诊断轮询 #%d 空读（芯片未出数或读数被吞）\n",
						      diagPolls);
				}
			}
		}
	}

	// 非阻塞重使能重试：放在读取之后，control 通道已尽量重建；限流 + 上限，零 delay。
	if (s_pendingReenable) {
		unsigned long now = millis();
		if (now - s_reenableLastTryMs >= REENABLE_RETRY_INTERVAL_MS) {
			s_reenableLastTryMs = now;
			if (s_bno.enableReport(SH2_ROTATION_VECTOR, REPORT_INTERVAL_US)) {
				s_pendingReenable = false;
				Serial.println("[IMU] 旋转矢量报告已重新使能");
			} else if (++s_reenableAttempts >= REENABLE_RETRY_MAX) {
				s_pendingReenable = false;
				Serial.println("[IMU] 重使能失败（已达重试上限）");
			}
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
