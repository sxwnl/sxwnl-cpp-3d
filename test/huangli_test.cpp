// Prints the almanac for each (monthGanZhi, dayGanZhi, week) triple on stdin,
// one "date|field=value;..." record per line. tools/check_huangli.js feeds it
// the same triples, computes the reference with lunar-javascript, and diffs.
#include <iostream>
#include <sstream>
#include <string>

#include "../lunar/huangli.h"

static std::string join(const std::vector<std::string>& v) {
    std::string s;
    for (size_t i = 0; i < v.size(); ++i) {
        if (i) s += ",";
        s += v[i];
    }
    return s;
}

int main() {
    std::string line;
    while (std::getline(std::cin, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        std::istringstream in(line);
        std::string tag, month, day;
        int week = 0;
        if (!(in >> tag >> month >> day >> week)) continue;

        const sx::HuangLi h = sx::computeHuangLi(month, day, week);
        std::cout << tag
                  << "|zhiXing=" << h.zhiXing
                  << "|tianShen=" << h.tianShen
                  << "|tianShenType=" << h.tianShenType
                  << "|tianShenLuck=" << h.tianShenLuck
                  << "|xiu=" << h.xiu
                  << "|xiuLuck=" << h.xiuLuck
                  << "|xiuZheng=" << h.xiuZheng
                  << "|xiuAnimal=" << h.xiuAnimal
                  << "|pengZuGan=" << h.pengZuGan
                  << "|pengZuZhi=" << h.pengZuZhi
                  << "|chongZhi=" << h.chongZhi
                  << "|chongShengXiao=" << h.chongShengXiao
                  << "|sha=" << h.sha
                  << "|yi=" << join(h.yi)
                  << "|ji=" << join(h.ji)
                  << "|jiShen=" << join(h.jiShen)
                  << "|xiongSha=" << join(h.xiongSha);

        // The twelve 时辰, flattened onto the same line.
        const std::vector<sx::ShiChen> hours = sx::computeShiChen(day);
        for (size_t i = 0; i < hours.size(); ++i) {
            const sx::ShiChen& sc = hours[i];
            std::cout << "|h" << i << "=" << sc.ganZhi << "/" << sc.tianShen
                      << "/" << sc.luck << "/" << sc.chong << "/" << sc.shengXiao
                      << "/" << join(sc.yi) << "/" << join(sc.ji);
        }
        std::cout << "\n";
    }
    return 0;
}
