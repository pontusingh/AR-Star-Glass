//当前阶段:测试BLE功能+OLED实时显示
#include <Arduino.h>
// 引 入 官 方BLE库
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <BLESecurity.h>

// OLED 显示库 (U8g2)
#include <U8g2lib.h>

// ====================== 在 这 里 修 改 参 数 ======================
#define DEVICE_NAME "TH-Insecure" // 设 备 名 称， 手 机 上 搜 索 到 的 名 字
#define PAIRING_PIN "" // 配 对 密 码， 设 置 为 空 字 符 串 则 不 需 要 密 码

// 通 用Nordic UART Service UUID （无 需 修 改！ 所 有APP都 支 持 这 个 标 准）
#define SERVICE_UUID "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define CHARACTERISTIC_UUID_RX "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"
#define CHARACTERISTIC_UUID_TX "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"

// 全 局 变 量 （不 需 要 修 改）
BLEServer *pServer = NULL; // 创 建 服 务 器
BLECharacteristic *pTxCharacteristic; // 创 建 特 征 值， 控 制 蓝 牙 读 写 模 式
bool isConnected = false; // 连 接 状 态 标 志： true=已 连 接， false=未 连 接
std::string receivedData = ""; // 自 动 存 储 手 机 发 送 过 来 的 字 符 串

// 非阻塞计时/消抖变量（取代 delay）
unsigned long lastHeartbeatMs = 0;   // 心跳上次发送时间
unsigned long lastDebounceMs = 0;    // BOOT 键消抖计时
bool    lastButtonReading = HIGH;    // 上一次读到原始引脚电平
bool    buttonStable = HIGH;         // 消抖后稳定的按键状态

// ---------- OLED 显示部分 ----------
// 引脚由 U8g2 构造器传入，内部完成唯一一次 I2C 总线初始化。
// 教训：绝不能在这里再次调用 Wire.begin()，否则 ESP32-S3 I2C 总线锁死（见 test/oled.cpp）。
#define PIN_OLED_SDA 8
#define PIN_OLED_SCL 9
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE, PIN_OLED_SCL, PIN_OLED_SDA);
const int MAX_TEXT_LINES = 4;
String screenLines[MAX_TEXT_LINES];
int lineCount = 0;
volatile bool oledDirty = false;     // 蓝牙回调置 1，loop() 里统一刷新
String pendingText = "";            // 待入队的文本

void oledInit();
void oledPushLines();
void oledRedraw();

void sendBLEString(String str);
void disconnectBLE();

// ============= 连 接 状 态 回 调 函 数 （不 需 要 修 改） =============
// 当 手 机 连 接 或 断 开 时， 这 个 函 数 会 自 动 执 行
class MyServerCallbacks: public BLEServerCallbacks {
	void onConnect(BLEServer* pServer) {
		isConnected = true;
		Serial.println("手 机 已 成 功 连 接"); // 调 试 信 息， 可 删
		// 连接成功立即通知手机（取代等 2 秒心 跳的首次发送）
		sendBLEString("ESP32 已连接");
	};

	void onDisconnect(BLEServer* pServer) {
		isConnected = false;
		Serial.println("手 机 已 断 开 连 接");
		// 断 开 后 自 动 重 新 开 始 广 播， 等 待 下 一 次 连 接
		pServer ->startAdvertising();
	}
};

// ============= 数 据 接 收 回 调 函 数 =============
// 当 手 机 发 送 数 据 时， 这 个 函 数 会 自 动 执 行
class MyCallbacks: public BLECharacteristicCallbacks {
	void onWrite(BLECharacteristic *pCharacteristic) {
		receivedData = pCharacteristic ->getValue();

		if (receivedData.length() > 0) {
			Serial.print("收 到 手 机 数 据: "); // 调 试 信 息， 可 删
			Serial.println(receivedData.c_str());

			// 实时显示：拷贝到待处理缓冲区 + 打脏标志，实际绘制留给 loop() 执行
			// (蓝牙回调运行于协议栈任务，避免于此直接绘制屏幕)
			pendingText = receivedData.c_str();
			oledDirty = true;

			// ==============================================
			// 在 这 里 添 加 你 收 到 数 据 后 的 处 理 逻 辑
			// ==============================================
			// 示 例： 如 果 收 到"ON"就 点 亮 板 载LED
			// if(receivedData == "ON") digitalWrite(LED_BUILTIN , HIGH);
			// 示 例： 如 果 收 到"OFF"就 熄 灭 板 载LED
			// if(receivedData == "OFF") digitalWrite(LED_BUILTIN , LOW);
		}
	}
};


void setup() { // Serial输 出 的 调 试 信 息 可 删
	Serial.begin(115200);
	Serial.println("\n===== ESP32 BLE 串 口 透 传 初 始 化 =====");

	// 1. 初 始 化BLE设 备， 设 置 蓝 牙 名 称
	BLEDevice::init(DEVICE_NAME);

	// 2. 设 置 配 对 密 码 （如 果 PAIRING_PIN 不 为 空）
	if(strlen(PAIRING_PIN) > 0) {
		BLESecurity *pSecurity = new BLESecurity();
		pSecurity ->setAuthenticationMode(ESP_LE_AUTH_REQ_SC_MITM_BOND);
		pSecurity ->setCapability(ESP_IO_CAP_OUT);
		pSecurity ->setStaticPIN(strtoul(PAIRING_PIN, NULL, 10));
		Serial.print("配 对 密 码 已 设 置 为: ");
		Serial.println(PAIRING_PIN);
	} else {
		Serial.println("未 设 置 配 对 密 码， 开 放 连 接");
	}

	// 3. 创 建BLE服 务 器 （ESP32作 为 从 机 等 待 连 接）
	pServer = BLEDevice::createServer();
	pServer ->setCallbacks(new MyServerCallbacks());

	// 4. 创 建 通 用 串 口 服 务
	BLEService *pService = pServer ->createService(SERVICE_UUID);

	// 5. 创 建TX通 道 （ESP32 → 手 机）
	pTxCharacteristic = pService ->createCharacteristic(
		CHARACTERISTIC_UUID_TX ,
		BLECharacteristic::PROPERTY_NOTIFY
	);
	pTxCharacteristic ->addDescriptor(new BLE2902());

	// 6. 创 建RX通 道 （手 机 → ESP32）
	BLECharacteristic *pRxCharacteristic = pService ->
	createCharacteristic(
		CHARACTERISTIC_UUID_RX , BLECharacteristic::PROPERTY_WRITE);
	pRxCharacteristic ->setCallbacks(new MyCallbacks());

	// 7. 启 动 服 务 并 开 始 广 播 （让 手 机 能 搜 索 到）
	pService ->start();
	pServer ->getAdvertising()->start();

	Serial.print("蓝 牙 名 称: ");
	Serial.println(DEVICE_NAME);
	Serial.println("初 始 化 完 成， 等 待 手 机 连 接...");
	Serial.println("----------------------------------------");

	// 8. 初始化 OLED 显示
	oledInit();
	Serial.println("OLED 初始化完成");
}

void loop() {
	unsigned long now = millis();

	// 1. 心跳 — 非阻塞：连接后每 2 秒发送一次（首次立即发送，行为不变）
	if (isConnected && (now - lastHeartbeatMs >= 2000)) {
		lastHeartbeatMs = now;
		sendBLEString("Hello from ESP32!");
	}

	// 2. BOOT 键断开 — 非阻塞消抖（取代原 delay/wait），松手后才允许再次断开
	bool reading = digitalRead(0);
	if (reading != lastButtonReading) {
		lastDebounceMs = now;          // 状态翻转即重置计时
		lastButtonReading = reading;
	}
	if ((now - lastDebounceMs) > 50) { // 50ms 稳定确认
		if (reading != buttonStable) {
			buttonStable = reading;
			if (buttonStable == LOW) { // 确认按下 → 断开连接
				disconnectBLE();
				Serial.println("主 动 断 开 了 蓝 牙 连 接");
			}
		}
	}

	// 3. OLED 实时刷新：蓝牙回调仅做标记，此处统一拉起绘制
	if (oledDirty) {
		oledDirty = false;
		oledPushLines();
		oledRedraw();
	}
}

// ============= 工 具 函 数 （封 装 完 成， 直 接 调 用 即 可） =============

/**
 * @brief 向 手 机 发 送 任 意 字 符 串
 * @param str 要 发 送 的 字 符 串 内 容
 */
void sendBLEString(String str) {
	if(isConnected) { // 只 有 连 接 上 了 才 发 送， 避 免 报 错
		pTxCharacteristic ->setValue(str.c_str());
		pTxCharacteristic ->notify();
		Serial.print("发 送 数 据: ");
		Serial.println(str);
	}
}

/**
 * @brief 主 动 断 开 当 前 的 蓝 牙 连 接
 */
void disconnectBLE() {
	if(isConnected) {
		pServer ->disconnect(pServer ->getConnId());
		isConnected = false;
	}
}

/**
 * @brief 初始化 OLED 显示
 *        引脚由 U8g2 构造器传入，内部完成唯一一次 I2C 总线初始化。
 *        务必不要在此额外调用 Wire.begin()，否则 ESP32-S3 I2C 总线锁死。
 */
void oledInit() {
	u8g2.begin();              // 初始化 OLED，顺便完成 I2C 总线初始化
	u8g2.enableUTF8Print();    // 支持中文 UTF-8 显示
	u8g2.setFont(u8g2_font_wqy12_t_gb2312); // 中文字体
}

/**
 * @brief 把 pendingText 按换行拆分，推入滚动缓冲区。
 *        缓冲区满（4 行）时，滚动顶掉最旧一行，永远只显示最近内容。
 */
void oledPushLines() {
	String chunk;
	for (int i = 0; i <= pendingText.length(); i++) {
		char c = (i < pendingText.length()) ? pendingText.charAt(i) : '\n';
		if (c == '\n') {
			if (chunk.length() > 0) {
				if (lineCount >= MAX_TEXT_LINES) {                  // 满 → 滚动
					for (int j = 0; j < MAX_TEXT_LINES - 1; j++) screenLines[j] = screenLines[j + 1];
					screenLines[MAX_TEXT_LINES - 1] = chunk;
				} else {
					screenLines[lineCount++] = chunk;
				}
			}
			chunk = "";
		} else if (c != '\r') {
			chunk += c;
		}
	}
}

/**
 * @brief 刷新整屏：清屏并把滚动缓冲区的内容逐行绘制到 OLED。
 */
void oledRedraw() {
	u8g2.clearBuffer();
	u8g2.setFont(u8g2_font_wqy12_t_gb2312);
	for (int i = 0; i < lineCount; i++) {
		u8g2.setCursor(0, (i + 1) * 13); // wqy12 行高 ≈ 12，行间 13
		u8g2.print(screenLines[i]);
	}
	u8g2.sendBuffer();            // 把缓冲区像素一次性推到 OLED
}