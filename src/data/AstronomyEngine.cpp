// 数据层实现：Astronomy Engine C 源码的薄封装。
// 两条计算路径：
//   1) 太阳系天体（日/月/行星）：引擎原生 Equator() + Horizon()，逐天体调用；
//   2) 恒星批量：每帧一次 EQJ→HOR 旋转矩阵，之后每颗恒星只做一次 3x3 矩阵乘，
//      绕开引擎仅支持 8 颗自定义恒星的限制。
// 坐标系约定均以 lib/AstronomyEngine/src/astronomy.c 源码为准：
//   - 地平系笛卡尔向量 x=北、y=西、z=天顶；
//   - 方位角从「逆时针从北」（atan2(y,x)）经 ToggleAzimuthDirection 转成顺时针从北；
//   - 旋转矩阵按 RotateVector() 的列主序方式作用于向量。
#include "AstronomyEngine.h"

#include <math.h>

astro_time_t astroMakeTime(int year, int month, int day, int hour, int minute, double second)
{
    // 直接透传引擎构造函数；引擎内部会同时算好 UT 和 TT 两套时间刻度。
    return Astronomy_MakeTime(year, month, day, hour, minute, second);
}

bool astroBodyHorizon(astro_body_t body, astro_time_t time, astro_observer_t observer, AstroAltAz *out)
{
    if (out == NULL || body == BODY_EARTH || body == BODY_INVALID)
        return false;

    // 第一步：地心 → 站心赤道坐标（当日春分点），带光行差修正。
    // EQUATOR_OF_DATE 保证与后续地平变换用同一个「当日」赤道基准。
    astro_equatorial_t equ = Astronomy_Equator(body, &time, observer, EQUATOR_OF_DATE, ABERRATION);
    if (equ.status != ASTRO_SUCCESS)
        return false;

    // 第二步：站心赤道坐标 → 地平坐标。REFRACTION_NORMAL 为引擎推荐的大气折射模型。
    astro_horizon_t hor = Astronomy_Horizon(&time, observer, equ.ra, equ.dec, REFRACTION_NORMAL);

    // astro_horizon_t 没有 status 字段，出错时字段为 NAN，这里统一转成 valid 标志。
    out->azimuth = hor.azimuth;
    out->altitude = hor.altitude;
    out->valid = !isnan(out->azimuth) && !isnan(out->altitude);
    return out->valid;
}

astro_rotation_t astroEqjToHor(astro_time_t time, astro_observer_t observer)
{
    // 引擎直接提供 J2000 赤道系 → 当日地平系 的复合旋转
    // （内部已包含岁差、章动、地球自转、极移），无需自己分步组合。
    return Astronomy_Rotation_EQJ_HOR(&time, observer);
}

bool astroStarHorizon(const astro_rotation_t *rotEqjToHor, double raHours, double decDeg, AstroAltAz *out)
{
    if (rotEqjToHor == NULL || out == NULL || rotEqjToHor->status != ASTRO_SUCCESS)
        return false;

    // 由 J2000 赤经（小时）/赤纬（度）构造单位方向向量：
    // x 指向春分点，y 指向夏至点方向，z 指向北天极。
    double raRad = raHours * HOUR2RAD;
    double decRad = decDeg * DEG2RAD;
    double cosDec = cos(decRad);
    double vx = cosDec * cos(raRad);
    double vy = cosDec * sin(raRad);
    double vz = sin(decRad);

    // 应用旋转矩阵得到地平系向量。乘法顺序严格对齐引擎 RotateVector()
    // 的列主序约定（target_i = Σ_k rot[k][i] * v_k），不要改成普通行主序。
    const double (*r)[3] = rotEqjToHor->rot;
    double hx = r[0][0] * vx + r[1][0] * vy + r[2][0] * vz;
    double hy = r[0][1] * vx + r[1][1] * vy + r[2][1] * vz;
    double hz = r[0][2] * vx + r[1][2] * vy + r[2][2] * vz;

    // z 分量即高度角的正弦；atan2(y,x) 给出「逆时针从北」的方位角。
    double alt = asin(hz) * RAD2DEG;
    double azCcw = atan2(hy, hx) * RAD2DEG;

    // 与引擎 Horizon()/ToggleAzimuthDirection() 相同的口径转换：
    // 逆时针从北 → 顺时针从北，并归一化到 [0,360)。
    double az = 360.0 - azCcw;
    if (az >= 360.0)
        az -= 360.0;
    else if (az < 0.0)
        az += 360.0;

    // 补上大气折射抬升量，使恒星路径与行星路径（Horizon 内部 REFRACTION_NORMAL）
    // 的口径一致；高高度角时该值趋近于 0，不影响验证精度。
    out->azimuth = az;
    out->altitude = alt + Astronomy_Refraction(REFRACTION_NORMAL, alt);
    out->valid = true;
    return true;
}
