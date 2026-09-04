#ifndef RENDER_MANAGER_H
#define RENDER_MANAGER_H

#include <Arduino.h>

// 行为层：统一渲染（第 3 阶段页面渲染）。
// 读取 InputManager 快照 + ranker/gps/朝向/校准数据，产出：
//   - 串口页面（全量：标题 + 全部条目 + 附加行 + 提示）
//   - OLED 页面（精简 + 4 行窗口滚动，标题/提示不上屏）
// 串口与 OLED 共用同一套「页面内容」逻辑（条目、光标、窗口计算），
// 仅最终排版不同；OLED 的精简规则集中在各 oled*() 函数，后续可独立调整。

void renderManagerRender();

#endif // RENDER_MANAGER_H
