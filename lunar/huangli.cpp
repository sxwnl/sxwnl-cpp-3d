#include "huangli.h"
#include "huangli_data.h"

#include <cstdio>
#include <cstring>
#include <string>

namespace sx {

using namespace huangli_data;

namespace {

// Index into the 60-term 甲子 cycle, or -1 if the string is not one of them.
int jiaZiIndex(const std::string& ganZhi) {
    for (int i = 0; i < kJiaZi_N; ++i)
        if (ganZhi == kJiaZi[i]) return i;
    return -1;
}

// Position of a 干支's branch within 子..亥, or -1.
int zhiIndexOf(const std::string& ganZhi) {
    for (int i = 1; i < kZhi_N; ++i) {
        const std::string z = kZhi[i];
        if (ganZhi.size() >= z.size() &&
            ganZhi.compare(ganZhi.size() - z.size(), z.size(), z) == 0)
            return i - 1;
    }
    return -1;
}

int ganIndexOf(const std::string& ganZhi) {
    for (int i = 1; i < kGan_N; ++i) {
        const std::string g = kGan[i];
        if (ganZhi.size() >= g.size() && ganZhi.compare(0, g.size(), g) == 0)
            return i - 1;
    }
    return -1;
}

// Two-digit uppercase hex, the key format used inside the packed tables.
std::string hex2(int v) {
    char buf[4];
    std::snprintf(buf, sizeof(buf), "%02X", v & 0xFF);
    return std::string(buf);
}

int parseHex2(const char* p) {
    auto digit = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        return -1;
    };
    const int hi = digit(p[0]), lo = digit(p[1]);
    return (hi < 0 || lo < 0) ? -1 : hi * 16 + lo;
}

// ---------------------------------------------------------------------------
//  宜 / 忌
// ---------------------------------------------------------------------------
// kDayYiJi is a run of records shaped
//     <日干支hex>=<月干支hex><月干支hex>...:<宜hex...>,<忌hex...>
// concatenated without separators. A day can appear more than once with
// different month sets, so scan forward until one lists this month.
void decodeDayYiJi(const std::string& monthGanZhi, const std::string& dayGanZhi,
                   std::vector<std::string>& yi, std::vector<std::string>& ji) {
    const int dayIdx = jiaZiIndex(dayGanZhi);
    const int monthIdx = jiaZiIndex(monthGanZhi);
    if (dayIdx < 0 || monthIdx < 0) return;

    const std::string dayKey = hex2(dayIdx) + "=";
    const std::string monthKey = hex2(monthIdx);
    const std::string table = kDayYiJi;

    size_t index = table.find(dayKey);
    while (index != std::string::npos) {
        // Everything after this record's "=" up to the next record's key. The
        // next key is 3 characters ("XX="), so trim those two leading hex
        // digits back off the tail.
        std::string right = table.substr(index + dayKey.size());
        std::string left = right;
        const size_t nextEq = left.find('=');
        if (nextEq != std::string::npos && nextEq >= 2)
            left = left.substr(0, nextEq - 2);

        const size_t colon = left.find(':');
        if (colon == std::string::npos) break;

        bool matched = false;
        for (size_t i = 0; i + 1 < colon; i += 2) {
            if (left.compare(i, 2, monthKey) == 0) { matched = true; break; }
        }
        if (matched) {
            const std::string body = left.substr(colon + 1);
            const size_t comma = body.find(',');
            const std::string yiPart = body.substr(0, comma);
            const std::string jiPart =
                (comma == std::string::npos) ? std::string() : body.substr(comma + 1);

            for (size_t i = 0; i + 1 < yiPart.size(); i += 2) {
                const int n = parseHex2(yiPart.c_str() + i);
                if (n >= 0 && n < kYiJi_N) yi.push_back(kYiJi[n]);
            }
            for (size_t i = 0; i + 1 < jiPart.size(); i += 2) {
                const int n = parseHex2(jiPart.c_str() + i);
                if (n >= 0 && n < kYiJi_N) ji.push_back(kYiJi[n]);
            }
            return;
        }
        const size_t next = right.find(dayKey);
        if (next == std::string::npos) break;
        index = (index + dayKey.size()) + next;
    }
}

// ---------------------------------------------------------------------------
//  吉神 / 凶煞
// ---------------------------------------------------------------------------
// kDayShenSha[monthZhi - 寅] is a ';'-separated list of
//     ;<日干支hex><神煞hex...>
// Entries below 60 are auspicious, 60 and above are inauspicious.
void decodeDayShenSha(int monthZhiIndex, const std::string& dayGanZhi,
                      std::vector<std::string>& jiShen,
                      std::vector<std::string>& xiongSha) {
    const int dayIdx = jiaZiIndex(dayGanZhi);
    if (dayIdx < 0) return;

    int m = monthZhiIndex - 2;   // the table starts at 寅月
    if (m < 0) m += 12;
    if (m < 0 || m >= kDayShenSha_N) return;

    const std::string row = kDayShenSha[m];
    const std::string key = ";" + hex2(dayIdx);

    const size_t at = row.find(key);
    if (at == std::string::npos) return;
    size_t start = at + key.size();
    size_t end = row.find(';', start);
    if (end == std::string::npos) end = row.size();

    for (size_t i = start; i + 1 < end; i += 2) {
        const int n = parseHex2(row.c_str() + i);
        if (n < 0) continue;
        const int slot = n + 1;
        if (slot >= kShenSha_N) continue;
        if (n < 60) jiShen.push_back(kShenSha[slot]);
        else        xiongSha.push_back(kShenSha[slot]);
    }
}

// ---------------------------------------------------------------------------
//  时宜 / 时忌
// ---------------------------------------------------------------------------
// kTimeYiJi records are keyed by the pair <日干支hex><时干支hex>, unlike the
// daily table which keys on the day alone and lists months after it.
void decodeTimeYiJi(const std::string& dayGanZhi, const std::string& timeGanZhi,
                    std::vector<std::string>& yi, std::vector<std::string>& ji) {
    const int dayIdx = jiaZiIndex(dayGanZhi);
    const int timeIdx = jiaZiIndex(timeGanZhi);
    if (dayIdx < 0 || timeIdx < 0) return;

    const std::string table = kTimeYiJi;
    const std::string key = hex2(dayIdx) + hex2(timeIdx) + "=";
    const size_t at = table.find(key);
    if (at == std::string::npos) return;

    std::string left = table.substr(at + key.size());
    const size_t nextEq = left.find('=');
    if (nextEq != std::string::npos && nextEq >= 4)
        left = left.substr(0, nextEq - 4);   // the next key is 4 hex digits + '='

    const size_t comma = left.find(',');
    const std::string yiPart = left.substr(0, comma);
    const std::string jiPart =
        (comma == std::string::npos) ? std::string() : left.substr(comma + 1);

    for (size_t i = 0; i + 1 < yiPart.size(); i += 2) {
        const int n = parseHex2(yiPart.c_str() + i);
        if (n >= 0 && n < kYiJi_N) yi.push_back(kYiJi[n]);
    }
    for (size_t i = 0; i + 1 < jiPart.size(); i += 2) {
        const int n = parseHex2(jiPart.c_str() + i);
        if (n >= 0 && n < kYiJi_N) ji.push_back(kYiJi[n]);
    }
}

const XiuInfo* findXiu(const std::string& name) {
    for (int i = 0; i < kXiuInfo_N; ++i)
        if (name == kXiuInfo[i].name) return &kXiuInfo[i];
    return nullptr;
}

} // namespace

std::vector<ShiChen> computeShiChen(const std::string& dayGanZhi) {
    std::vector<ShiChen> out;
    const int dayGan = ganIndexOf(dayGanZhi);
    const int dayZhi = zhiIndexOf(dayGanZhi);
    if (dayGan < 0 || dayZhi < 0) return out;

    out.reserve(12);
    for (int h = 0; h < 12; ++h) {
        ShiChen sc;
        // 五鼠遁: the hour stem follows the day stem, repeating every five days.
        const int timeGan = (dayGan % 5 * 2 + h) % 10;
        sc.zhi = kZhi[h + 1];
        sc.ganZhi = std::string(kGan[timeGan + 1]) + kZhi[h + 1];

        // 子时 straddles midnight; the rest are plain two-hour blocks.
        char buf[32];
        const int startHour = (h * 2 + 23) % 24;
        const int endHour = (startHour + 1) % 24;
        std::snprintf(buf, sizeof(buf), "%02d:00-%02d:59", startHour, endHour);
        sc.range = buf;

        const int tsIndex = (h + kZhiTianShenOffset[dayZhi]) % 12;
        if (tsIndex < kTianShenInfo_N) {
            sc.tianShen = kTianShenInfo[tsIndex].name;
            sc.luck = kTianShenInfo[tsIndex].luck;
        }

        // 正冲: the opposing branch, with the stem that clashes with this
        // hour's stem. The stem pairing is its own table, not a rotation by six.
        const int chongZhi = (h + 6) % 12;
        sc.chong = (timeGan < kChongGan_N ? std::string(kChongGan[timeGan])
                                          : std::string()) +
                   kZhi[chongZhi + 1];
        if (chongZhi + 1 < kShengXiao_N) sc.shengXiao = kShengXiao[chongZhi + 1];

        decodeTimeYiJi(dayGanZhi, sc.ganZhi, sc.yi, sc.ji);
        if (sc.yi.empty() && kShenSha_N > 0) sc.yi.push_back(kShenSha[0]);
        if (sc.ji.empty() && kShenSha_N > 0) sc.ji.push_back(kShenSha[0]);
        out.push_back(sc);
    }
    return out;
}

HuangLi computeHuangLi(const std::string& monthGanZhi,
                       const std::string& dayGanZhi,
                       int week) {
    HuangLi h;
    const int dayZhi = zhiIndexOf(dayGanZhi);
    const int dayGan = ganIndexOf(dayGanZhi);
    const int monthZhi = zhiIndexOf(monthGanZhi);
    if (dayZhi < 0 || dayGan < 0 || monthZhi < 0) return h;

    h.valid = true;
    h.monthGanZhi = monthGanZhi;
    h.dayGanZhi = dayGanZhi;

    // 建除十二神: how far the day's branch sits ahead of the month's.
    int offset = dayZhi - monthZhi;
    if (offset < 0) offset += 12;
    if (offset + 1 < kZhiXing_N) h.zhiXing = kZhiXing[offset + 1];

    // 值神: the twelve day-officers, phased by the month branch.
    const int tsOffset = kZhiTianShenOffset[monthZhi];
    const int tsIndex = (dayZhi + tsOffset) % 12;
    if (tsIndex < kTianShenInfo_N) {
        h.tianShen = kTianShenInfo[tsIndex].name;
        h.tianShenType = kTianShenInfo[tsIndex].type;
        h.tianShenLuck = kTianShenInfo[tsIndex].luck;
    }

    // 二十八宿: indexed by the day branch and the weekday.
    if (week >= 0 && week < 7) {
        h.xiu = kXiu[dayZhi][week];
        if (const XiuInfo* xi = findXiu(h.xiu)) {
            h.xiuLuck = xi->luck;
            h.xiuZheng = xi->zheng;
            h.xiuAnimal = xi->animal;
        }
    }

    if (dayGan + 1 < kPengZuGan_N) h.pengZuGan = kPengZuGan[dayGan + 1];
    if (dayZhi + 1 < kPengZuZhi_N) h.pengZuZhi = kPengZuZhi[dayZhi + 1];

    if (dayZhi < kChong_N) h.chongZhi = kChong[dayZhi];
    {   // 冲 names a branch; report the zodiac animal alongside it.
        for (int i = 1; i < kZhi_N; ++i) {
            if (h.chongZhi == kZhi[i]) {
                if (i < kShengXiao_N) h.chongShengXiao = kShengXiao[i];
                break;
            }
        }
    }
    if (dayZhi < kSha_N) h.sha = kSha[dayZhi];

    decodeDayYiJi(monthGanZhi, dayGanZhi, h.yi, h.ji);
    decodeDayShenSha(monthZhi, dayGanZhi, h.jiShen, h.xiongSha);

    // The source tables use a "none" placeholder rather than an empty list.
    if (h.yi.empty() && kShenSha_N > 0)       h.yi.push_back(kShenSha[0]);
    if (h.ji.empty() && kShenSha_N > 0)       h.ji.push_back(kShenSha[0]);
    if (h.jiShen.empty() && kShenSha_N > 0)   h.jiShen.push_back(kShenSha[0]);
    if (h.xiongSha.empty() && kShenSha_N > 0) h.xiongSha.push_back(kShenSha[0]);
    return h;
}

} // namespace sx
