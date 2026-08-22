#ifndef BLE_SERVICE_H
#define BLE_SERVICE_H

#include <Arduino.h>

// ====================== 在 这 里 修 改 参 数 ======================
#define DEVICE_NAME "Glasses" // 设 备 名 称， 手 机 上 搜 索 到 的 名 字
#define PAIRING_PIN "" // 配 对 密 码， 设 置 为 空 字 符 串 则 不 需 要 密 码

// 通 用Nordic UART Service UUID （无 需 修 改！ 所 有APP都 支 持 这 个 标 准）
#define SERVICE_UUID "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define CHARACTERISTIC_UUID_RX "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"
#define CHARACTERISTIC_UUID_TX "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"

// 数 据 接 收 回 调 类 型： 手 机 发 来 数 据 时 通 过 它 交 给 外 部 处 理
typedef void (*BleDataReceivedFn)(const String &text);

void bleInit();                                 // 初始化BLE设备/服务并开始广播
bool bleIsConnected();                          // 当前连接状态
void sendBLEString(String str);                 // 向手机发送任意字符串
void disconnectBLE();                           // 主动断开当前的蓝牙连接
void bleOnDataReceived(BleDataReceivedFn cb);   // 注册数据接收回调（setup中调用一次）

#endif // BLE_SERVICE_H
