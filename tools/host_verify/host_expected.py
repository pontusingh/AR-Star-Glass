# -*- coding: utf-8 -*-
"""
第 1 阶段数值验证（官方库对照端）。

用官方 astronomy-engine Python 包（与固件内 C 版同源、共享同一测试集）
计算与 host_verify.c 完全相同的时刻/地点/天体的地平坐标，打印成可 diff 的格式。

运行：python tools/host_verify/host_expected.py
"""
import astronomy

TIME_ARGS = (2026, 8, 25, 12, 0, 0.0)
OBS = astronomy.Observer(39.9042, 116.4074, 40.0)

# 与 host_verify.c 一致的天体编号（astro_body_t 枚举值）与恒星样本
BODIES = [(astronomy.Body.Sun, 9), (astronomy.Body.Moon, 10),
          (astronomy.Body.Jupiter, 4), (astronomy.Body.Mars, 3)]
STARS = [
    ("Vega",       18.61560, 38.78300),
    ("Sirius",      6.75260, -16.71300),
    ("Polaris",     2.52973, 89.26400),
    ("Betelgeuse",  5.91953,   7.40700),
]

t = astronomy.Time.Make(*TIME_ARGS)
print("TIME 2026-08-25T12:00:00Z  OBS lat=%.4f lon=%.4f h=%.0f" % (OBS.latitude, OBS.longitude, OBS.height))

for body, body_id in BODIES:
    # 注意：官方 Python 版参数顺序是 (body, time, observer)，与 C 版不同
    equ = astronomy.Equator(body, t, OBS, True, True)   # ofdate + aberration
    hor = astronomy.Horizon(t, OBS, equ.ra, equ.dec, astronomy.Refraction.Normal)
    print("BODY %d az=%.6f alt=%.6f" % (body_id, hor.azimuth, hor.altitude))

# 恒星：官方路径是 DefineStar 后走 Equator/Horizon，用于对照固件的矩阵批量路径
star_bodies = [astronomy.Body.Star1, astronomy.Body.Star2,
               astronomy.Body.Star3, astronomy.Body.Star4]
for i, (name, ra_h, dec_d) in enumerate(STARS):
    astronomy.DefineStar(star_bodies[i], ra_h, dec_d, 50.0)
    equ = astronomy.Equator(star_bodies[i], t, OBS, True, False)  # 恒星无光行差项差异
    hor = astronomy.Horizon(t, OBS, equ.ra, equ.dec, astronomy.Refraction.Normal)
    print("STAR %s az=%.6f alt=%.6f" % (name, hor.azimuth, hor.altitude))

