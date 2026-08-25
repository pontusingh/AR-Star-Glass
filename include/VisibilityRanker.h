#ifndef VISIBILITY_RANKER_H
#define VISIBILITY_RANKER_H

// 行为层：可见性排序器。
// 给定「有效时刻 + 观察点」，遍历恒星目录与太阳系天体目录：
//   1) 用天文引擎算出每个天体的地平坐标（方位角/高度角）；
//   2) 过滤掉地平线以下（低于阈值）的天体；
//   3) 按「亮度优先、高度角加权」的评分降序排列，缓存结果。
// 结果供本阶段串口验证台打印，后续阶段由 RenderManager / 推荐名录消费。

#include <Arduino.h>
#include "AstronomyEngine.h"

// 结果缓存容量：60 颗亮星 + 10 个太阳系天体，留少量余量
#define RANKER_MAX_TARGETS 72

// 目标类别（决定 catalogIndex 的含义）
typedef enum {
    TARGET_STAR, // 目录恒星：catalogIndex 为 STAR_CATALOG 下标
    TARGET_BODY  // 太阳系天体：catalogIndex 为 BODY_CATALOG 下标
} TargetKind;

// 一条排序后的目标记录
typedef struct {
    TargetKind kind;
    int catalogIndex;  // 在对应目录中的下标，用于取回名字等元数据
    double magnitude;  // 视星等（月亮/行星取参考值，见 rankerUpdate 实现）
    double altitude;   // 高度角（度，已含折射修正）
    double azimuth;    // 方位角（度，顺时针从正北）
} RankedTarget;

// 设置/读取地平线过滤阈值（度）。只有高度角 > 阈值的目标才进入结果表。
void rankerSetHorizonMin(double degrees);
double rankerHorizonMin();

// 重算一次可见目标表。计算量与星表规模成正比但每帧只做一次旋转矩阵，
// 建议以约 1 秒一次的低节奏调用（在 loop() 里用 millis() 节流），不要每圈都调。
void rankerUpdate(astro_time_t time, astro_observer_t observer);

// 访问上一次 rankerUpdate 的结果；rankerAt 越界返回 NULL。
int rankerCount();
const RankedTarget *rankerAt(int index);

#endif // VISIBILITY_RANKER_H
