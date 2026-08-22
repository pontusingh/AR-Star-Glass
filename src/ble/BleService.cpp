// 通信层：Nordic UART 串口透传（GATT 服务器 + 收发特征），只管收发不做业务逻辑
#include <Arduino.h>
// 引 入 官 方BLE库
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <BLESecurity.h>

#include "BleService.h"

// 全 局 变 量 （不 需 要 修 改）
static BLEServer *pServer = NULL; // 创 建 服 务 器
static BLECharacteristic *pTxCharacteristic; // 创 建 特 征 值， 控 制 蓝 牙 读 写 模 式
static bool isConnected = false; // 连 接 状 态 标 志： true=已 连 接， false=未 连 接
static std::string receivedData = ""; // 自 动 存 储 手 机 发 送 过 来 的 字 符 串

static BleDataReceivedFn dataReceivedCallback = NULL; // 外部注册的数据接收回调

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

			// 转交给外部注册的回调处理（本层不含任何业务/显示逻辑）
			if (dataReceivedCallback != NULL) {
				dataReceivedCallback(String(receivedData.c_str()));
			}
		}
	}
};

void bleInit() {
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
}

bool bleIsConnected() {
	return isConnected;
}

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

void bleOnDataReceived(BleDataReceivedFn cb) {
	dataReceivedCallback = cb;
}
