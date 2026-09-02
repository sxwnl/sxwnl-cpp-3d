// Almanac (黄历) for a single day: 宜/忌, 吉神/凶煞, and the classical
// day markers (值神, 建除十二神, 二十八宿, 彭祖百忌, 冲煞).
//
// This is a lookup layer, not an astronomy one. Everything here keys off the
// day's and month's 干支, which the engine already computes in OB_DAY (Lday2 /
// Lmonth2) from its own 节气 boundaries - so the almanac stays consistent with
// the rest of the calendar rather than re-deriving the calendar itself.
//
// Tables come from lunar-javascript (MIT) via tools/gen_huangli.js.
#ifndef SXWNL_LUNAR_HUANGLI_H
#define SXWNL_LUNAR_HUANGLI_H

#include <string>
#include <vector>

namespace sx {

struct HuangLi {
    bool valid = false;

    std::string monthGanZhi;   // 月干支
    std::string dayGanZhi;     // 日干支

    std::string zhiXing;       // 建除十二神: 建除满平定执破危成收开闭
    std::string tianShen;      // 值神: 青龙, 明堂, ...
    std::string tianShenType;  // 黄道 / 黑道
    std::string tianShenLuck;  // 吉 / 凶

    std::string xiu;           // 二十八宿
    std::string xiuLuck;       // 吉 / 凶
    std::string xiuZheng;      // 七政
    std::string xiuAnimal;     // 二十八禽

    std::string pengZuGan;     // 彭祖百忌 (天干句)
    std::string pengZuZhi;     // 彭祖百忌 (地支句)
    std::string chongShengXiao;// 所冲生肖
    std::string chongZhi;      // 所冲地支
    std::string sha;           // 煞方位

    std::vector<std::string> yi;        // 宜
    std::vector<std::string> ji;        // 忌
    std::vector<std::string> jiShen;    // 吉神宜趋
    std::vector<std::string> xiongSha;  // 凶神宜忌
};

// monthGanZhi / dayGanZhi are two-character strings such as "丙申"; week is
// 0..6 with Sunday = 0. Returns valid == false if either 干支 is unrecognised.
HuangLi computeHuangLi(const std::string& monthGanZhi,
                       const std::string& dayGanZhi,
                       int week);

} // namespace sx
#endif
