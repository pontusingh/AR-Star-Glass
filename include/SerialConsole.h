#ifndef SERIAL_CONSOLE_H
#define SERIAL_CONSOLE_H

// 行为层：串口终端（CLI）。
// 从 Serial 读取字符，累积成行后解析并执行命令（H/P/T/TT/O/I/R）。
// 命令可能修改 nowUtc（T 命令设置 UTC）和 forceRefresh（触发重算与打印）。
// 打印可见目标表：serialReportTargets()（含高度角/方位角，仅按 R 或注入变更触发，不自动连续打印）。
// 首次调用 serialConsoleInit() 时打印帮助信息。

#include <Arduino.h>
#include "AstronomyEngine.h"
#include "StarCatalog.h"
#include "VisibilityRanker.h"
#include "TimeTravelManager.h"
#include "GpsManager.h"

// 初始化：首次调用时打印帮助信息
void serialConsoleInit();

// 从 Serial 读取字符，累积到行缓冲，遇换行解析执行。
// 命令可能重锚时钟（T → gpsManagerSetUtc）和置 forceRefresh（R/P/TT）。
void serialConsoleTick(bool &forceRefresh);

// 打印可见目标表头 + Top N（纯输出，不修改状态）
void serialReportTargets(astro_time_t effectiveTime, astro_observer_t effectiveObs);

#endif // SERIAL_CONSOLE_H