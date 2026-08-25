#ifndef ASTRONOMY_ENGINE_H
#define ASTRONOMY_ENGINE_H

// 数据层：Astronomy Engine C 源码（lib/AstronomyEngine/）的薄封装。
// 职责：把「时间 + 观察点 + 天体」翻译成地平坐标（方位角/高度角），
// 屏蔽引擎的 C 结构体和坐标系约定，向上层只暴露少量自由函数。
//
// 注意：移植版裁剪了 Astronomy_CurrentTime()（见 library.json 构建标志），
// 所有时间必须显式构造 —— 通过 astroMakeTime() 从年月日时分秒生成。

#include <stdbool.h>   // C++ 编译时由编译器内建支持；保持头文件不依赖 Arduino 框架，
#include "astronomy.h" // 以便 tools/host_verify 可在 PC 端直接编译同一份代码做数值对照

#ifdef __cplusplus
extern "C" {
#endif

// 地平坐标结果：描述一个天体「在天空的哪个方向」
typedef struct {
    double azimuth;   // 方位角：顺时针从正北量起，0=北 90=东 180=南 270=西
    double altitude;  // 高度角：相对理想地平线，已加大气折射修正，单位为度
    bool   valid;     // false = 引擎计算失败，此时 az/alt 无意义，调用方必须检查
} AstroAltAz;

// 把 UTC 年月日时分秒构造成引擎内部时间。
// 本固件唯一的时间入口：GPS / 串口设置 / 时空旅行覆盖，最终都先拆成
// 年月日时分秒再调这里（引擎移植版没有 CurrentTime()，无法读系统时钟）。
astro_time_t astroMakeTime(int year, int month, int day, int hour, int minute, double second);

// 太阳系天体路径（太阳 / 月亮 / 八大行星）：引擎原生支持，
// 内部一步完成光行差 + 岁差 + 周日自转，直接返回地平坐标。
bool astroBodyHorizon(astro_body_t body, astro_time_t time, astro_observer_t observer, AstroAltAz *out);

// 恒星批量路径第 1 步：计算 J2000 赤道坐标系 → 当前时刻地平坐标系 的旋转矩阵。
// 每帧只需调用一次；之后对星表里每颗恒星调 astroStarHorizon() 即可。
// 这样绕开了引擎原生仅支持 8 颗自定义恒星（BODY_STAR1..STAR8）的限制，
// 星表规模不再影响计算成本（每颗星只是一次 3x3 矩阵乘 + 少量三角函数）。
astro_rotation_t astroEqjToHor(astro_time_t time, astro_observer_t observer);

// 恒星批量路径第 2 步：用预计算的旋转矩阵，把一颗恒星的 J2000 坐标
// （ra 单位为小时，dec 单位为度）变换到地平坐标。
// rotEqjToHor 必须来自同一时刻、同一观察点的 astroEqjToHor()，否则结果错误。
bool astroStarHorizon(const astro_rotation_t *rotEqjToHor, double raHours, double decDeg, AstroAltAz *out);

#ifdef __cplusplus
}
#endif

#endif // ASTRONOMY_ENGINE_H
