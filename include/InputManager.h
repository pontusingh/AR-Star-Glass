#ifndef INPUT_MANAGER_H
#define INPUT_MANAGER_H

#include <Arduino.h>
#include "ButtonDriver.h"
#include "AstronomyEngine.h"

// 行为层：模式状态机（第 5 阶段）。
// 只负责跟踪模式、路由按键、维护光标/编辑缓冲，不负责渲染。
// 渲染（串口 + OLED）由 RenderManager 读取 inputManagerGetSnapshot() 完成。
// 数据页（校准 Top5 / 推荐名录）所需 ranker 刷新由 main.cpp 协同：
//   - inputManagerConsumeDataDirty() 返回 true 时，main 调用 rankerUpdate()；
//   - inputManagerConsumeRender() 返回 true 时，main 调用 renderManagerRender()。
// 按键 B 全局「逐级退回」，顶层为主菜单。

typedef enum {
    IM_MENU,            // 主菜单（定位/校准/观星/时空旅行）
    IM_LOCATION,        // 定位页（查看 + 重新定位）
    IM_CALIBRATE,       // 校准页（单点验证）
    IM_OBSERVE_LIVE,    // 观星-直接观星（实时标注）
    IM_OBSERVE_LIST,    // 观星-推荐名录
    IM_OBSERVE_GUIDE,   // 观星-转向指导
    IM_TT_SELECT,       // 时空旅行-字段选择
    IM_TT_EDIT,         // 时空旅行-数值编辑
} InputMode;

typedef enum { CAL_BROWSE, CAL_SELECTED, CAL_DONE } CalPhase;

typedef struct {
    InputMode mode;
    CalPhase  calPhase;
    int menuCursor;         // 主菜单 0..3
    int listCursor;         // 校准 Top5 / 推荐名录 下标
    int selectedTarget;     // 转向指导 / 校准选中目标（-1 无）
    int ttFieldCursor;      // 时空旅行字段 0..4，5=确认启动
    astro_utc_t ttBuffer;   // 时空旅行编辑缓冲
} InputSnapshot;

void inputManagerInit();                       // 上电 → 主菜单
void inputManagerSetBaseTime(astro_utc_t utc); // main 每圈同步基准时间（时空旅行缓冲初值用）
void inputManagerUpdate(ButtonId btn);         // 每圈调用：路由按键(可 BTN_NONE) + 3s 超时

InputMode inputManagerMode();
void inputManagerGetSnapshot(InputSnapshot *out);

bool inputManagerConsumeDataDirty();           // 返回并清除：进入数据页后需重算 ranker
bool inputManagerConsumeRender();              // 返回并清除：显示内容已变，需重新渲染

#endif // INPUT_MANAGER_H
