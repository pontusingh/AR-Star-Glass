#ifndef STAR_CATALOG_H
#define STAR_CATALOG_H

// 数据层：闪存驻留的天体目录（亮星 + 太阳系天体名称表）与访问接口。
// 星表本体在 StarCatalogData.cpp（由 tools/gen_star_catalog.py 生成），
// const 数组直接落在 flash 映射区，运行期不占用 RAM、也无需拷贝。

#include <Arduino.h>
#include "AstronomyEngine.h" // 复用引擎的 astro_body_t 枚举

// 一颗目录恒星（全部使用 J2000 历元坐标；赤经单位为小时，其余角度为度）
typedef struct {
    uint32_t hip;        // Hipparcos 星表编号（唯一标识，校准选星时用；编号可超 65535）
    float raHours;       // J2000 赤经，小时 [0,24)
    float decDeg;        // J2000 赤纬，度 [-90,+90]
    float mag;           // 视星等（越小越亮）
    char nameAscii[16];  // ASCII 名（串口日志 / 内部匹配用）
    char nameCn[16];     // 中文名，UTF-8（OLED 显示阶段直接可用）
} CatalogStar;

// 一个太阳系天体条目（位置随时间变化，只存名字映射，坐标由天文引擎实时算）
typedef struct {
    astro_body_t body;   // 引擎天体枚举值
    char nameAscii[12];  // ASCII 名
    char nameCn[12];     // 中文名，UTF-8
} CatalogBody;

// 恒星目录（按亮度降序排列），定义在 StarCatalogData.cpp（自动生成）
extern const CatalogStar STAR_CATALOG[];
extern const int STAR_CATALOG_COUNT;

// 太阳系天体名称表（太阳/月亮/八大行星/冥王星），定义在 StarCatalog.cpp
extern const CatalogBody BODY_CATALOG[];
extern const int BODY_CATALOG_COUNT;

#endif // STAR_CATALOG_H
