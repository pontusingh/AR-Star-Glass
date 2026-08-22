#include <Arduino.h>
// 引 入 官 方BLE库
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

// ====================== 在 这 里 修 改 参 数 ======================
#define DEVICE_NAME "ESP32_BLE_2026" // 设 备 名 称， 手 机 上 搜 索 到 的 名 字
#define PAIRING_PIN "123456" // 配 对 密 码， 设 置 为 空 字 符 串 则 不 需 要 密 码

// 通 用Nordic UART Service UUID （无 需 修 改！ 所 有APP都 支 持 这 个 标 准）
#define SERVICE_UUID "6E400001 -B5A3-F393-E0A9-E50E24DCCA9E"
#define CHARACTERISTIC_UUID_RX "6E400002 -B5A3-F393-E0A9-E50E24DCCA9E"
#define CHARACTERISTIC_UUID_TX "6E400003 -B5A3-F393-E0A9-E50E24DCCA9E"

// 全 局 变 量 （不 需 要 修 改）
BLEServer *pServer = NULL; // 创 建 服 务 器
BLECharacteristic *pTxCharacteristic; // 创 建 特 征 值， 控 制 蓝 牙 读 写 模 式
bool isConnected = false; // 连 接 状 态 标 志： true=已 连 接， false=未 连 接
std::string receivedData = ""; // 自 动 存 储 手 机 发 送 过 来 的 字 符 串

// ============= 连 接 状 态 回 调 函 数 （不 需 要 修 改） =============
// 当 手 机 连 接 或 断 开 时， 这 个 函 数 会 自 动 执 行
class MyServerCallbacks: public BLEServerCallbacks {
	void onConnect(BLEServer* pServer) {
		isConnected = true;
		Serial.println("手 机 已 成 功 连 接"); // 调 试 信 息， 可 删
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
		BLEDevice::setSecurityAuth(true , true , true);
		BLEDevice::setPin(PAIRING_PIN);
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
}

void loop() {
	// 示 例： 如 果 有 设 备 连 接， 每 隔2秒 发 送 一 次 心 跳 数 据
	if(isConnected) {
		sendBLEString("Hello from ESP32!");
		delay(2000);
	}

	// 示 例： 按 下ESP32上 的BOOT键 主 动 断 开 连 接
	if(digitalRead(0) == LOW) {
		delay(50); // 按 键 消 抖
		if(digitalRead(0) == LOW) {
			disconnectBLE();
			Serial.println("主 动 断 开 了 蓝 牙 连 接");
			while(digitalRead(0) == LOW); // 等 待 按 键 释 放
		}
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