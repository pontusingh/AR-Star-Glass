// 数据层实现：太阳系天体名称表。
// 恒星目录本体在 StarCatalogData.cpp（脚本自动生成），本文件只维护
// 引擎天体枚举值到中/英文名的映射，供排序结果与后续渲染使用。
#include "StarCatalog.h"

// 顺序刻意与引擎 astro_body_t 枚举一致（水星→海王星，太阳、月亮靠后），
// 遍历顺序稳定，便于对照验证。
const CatalogBody BODY_CATALOG[] = {
    {BODY_MERCURY, "Mercury", "水星"},
    {BODY_VENUS,   "Venus",   "金星"},
    {BODY_MARS,    "Mars",    "火星"},
    {BODY_JUPITER, "Jupiter", "木星"},
    {BODY_SATURN,  "Saturn",  "土星"},
    {BODY_URANUS,  "Uranus",  "天王星"},
    {BODY_NEPTUNE, "Neptune", "海王星"},
    // 冥王星暂缓收录（保留条目便于日后恢复）：
    // 1) 肉眼不可见，对观星推荐无价值；
    // 2) 引擎 CalcPluto→GetSegment()（astronomy.c:4574）在栈上放 body_segment_t
    //    反向重放缓存，单帧实测 16512 字节，会撑爆任何常规任务栈导致溢出重启。
    // 恢复收录前，需先把该函数的栈上大对象改为堆分配并登记补丁说明。
    // {BODY_PLUTO,   "Pluto",   "冥王星"},
    {BODY_SUN,     "Sun",     "太阳"},
    {BODY_MOON,    "Moon",    "月亮"},
};

const int BODY_CATALOG_COUNT = sizeof(BODY_CATALOG) / sizeof(BODY_CATALOG[0]);
