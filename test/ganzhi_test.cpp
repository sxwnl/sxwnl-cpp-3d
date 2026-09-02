// Dumps the engine's own 干支纪月 / 纪日 for a range of months so
// tools/check_ganzhi.js can diff them against lunar-javascript.
//
// This matters because the almanac tables are keyed by those two values: if the
// engine's 节气-based month boundary disagreed with the reference by even a day,
// every 宜忌 lookup near a 节 would silently return the wrong row.
#include <cstdio>
#include <cstdlib>

#include "../lunar/lunar.h"
#include "../lunar/lunar_ob.h"

int main(int argc, char** argv) {
    const int startYear = argc > 1 ? std::atoi(argv[1]) : 2020;
    const int months = argc > 2 ? std::atoi(argv[2]) : 144;

    init_ob();
    for (int k = 0; k < months; ++k) {
        const int y = startYear + k / 12;
        const int m = k % 12 + 1;
        OB_LUN lun = yueLiCalc(y, m);
        for (int i = 0; i < lun.dn; ++i) {
            const OB_DAY& d = lun.day[i];
            std::printf("%d-%d-%d %s %s %d\n", d.y, d.m, d.d,
                        d.Lmonth2.c_str(), d.Lday2.c_str(), d.week);
        }
    }
    return 0;
}
