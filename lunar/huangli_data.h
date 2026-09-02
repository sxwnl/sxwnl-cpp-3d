// Tables backing the almanac (黄历) lookups. The definitions live in
// huangli_data.cpp, which is generated - see tools/gen_huangli.js.
#ifndef SXWNL_LUNAR_HUANGLI_DATA_H
#define SXWNL_LUNAR_HUANGLI_DATA_H

namespace sx {
namespace huangli_data {

struct XiuInfo {
    const char* name;    // 宿名, e.g. 角
    const char* luck;    // 吉 / 凶
    const char* zheng;   // 七政: 木火土金水日月
    const char* animal;  // 二十八禽
    const char* shou;    // 四兽: 青龙/玄武/白虎/朱雀
};

struct TianShenInfo {
    const char* name;    // 值神, e.g. 青龙
    const char* type;    // 黄道 / 黑道
    const char* luck;    // 吉 / 凶
};

// Index 0 of the ganzhi tables is an empty placeholder, matching the source
// library, so 甲 is kGan[1] and 子 is kZhi[1].
extern const char* const kGan[];          extern const int kGan_N;
extern const char* const kZhi[];          extern const int kZhi_N;
extern const char* const kJiaZi[];        extern const int kJiaZi_N;
extern const char* const kZhiXing[];      extern const int kZhiXing_N;
extern const char* const kPengZuGan[];    extern const int kPengZuGan_N;
extern const char* const kPengZuZhi[];    extern const int kPengZuZhi_N;
extern const char* const kChong[];        extern const int kChong_N;
extern const char* const kShengXiao[];    extern const int kShengXiao_N;
extern const char* const kYiJi[];         extern const int kYiJi_N;
extern const char* const kShenSha[];      extern const int kShenSha_N;
extern const char* const kTianShen[];     extern const int kTianShen_N;
extern const char* const kSha[];          extern const int kSha_N;

extern const TianShenInfo kTianShenInfo[]; extern const int kTianShenInfo_N;
extern const int kZhiTianShenOffset[12];

extern const char* const kXiu[12][7];      // [日支 index 0..11][星期 0..6]
extern const XiuInfo kXiuInfo[];           extern const int kXiuInfo_N;

// Packed lookup tables, decoded at runtime by huangli.cpp.
extern const char* const kDayYiJi;
extern const char* const kDayShenSha[];    extern const int kDayShenSha_N;

} // namespace huangli_data
} // namespace sx
#endif
