// Cross-checks lunar/huangli.cpp against the reference implementation.
//
//   node tools/gen_huangli.js > lunar/huangli_data.cpp
//   g++ -std=c++17 -I lunar test/huangli_test.cpp lunar/huangli.cpp \
//       lunar/huangli_data.cpp -o build/huangli_test
//   node tools/check_huangli.js build/huangli_test
//
// Walks a long run of dates, feeds the C++ binary the same 干支 the reference
// derives, and diffs every field. Any mismatch is printed and the exit code is
// non-zero.
'use strict';

const path = require('path');
const { execFileSync } = require('child_process');

const binary = process.argv[2] || path.join('build', 'huangli_test');
const YEARS = Number(process.env.YEARS || 12);

function loadLunar() {
    const candidates = [process.env.LUNAR_JS, path.join(__dirname, 'lunar.js'),
                        'lunar-javascript'].filter(Boolean);
    for (const c of candidates) {
        try { return require(c); } catch (e) { /* next */ }
    }
    throw new Error('cannot locate lunar.js');
}

const { Solar, I18n } = loadLunar();
I18n.setLanguage('chs');

const cases = [];
const expected = [];
const start = new Date(Date.UTC(2020, 0, 1));
const days = YEARS * 365;
for (let i = 0; i < days; i++) {
    const dt = new Date(start.getTime() + i * 86400000);
    const solar = Solar.fromYmd(dt.getUTCFullYear(), dt.getUTCMonth() + 1, dt.getUTCDate());
    const lunar = solar.getLunar();
    const tag = `${dt.getUTCFullYear()}-${dt.getUTCMonth() + 1}-${dt.getUTCDate()}`;
    const month = lunar.getMonthInGanZhi();
    const day = lunar.getDayInGanZhi();
    const week = solar.getWeek();
    cases.push(`${tag} ${month} ${day} ${week}`);
    const row = {
        tag,
        zhiXing: lunar.getZhiXing(),
        tianShen: lunar.getDayTianShen(),
        tianShenType: lunar.getDayTianShenType(),
        tianShenLuck: lunar.getDayTianShenLuck(),
        xiu: lunar.getXiu(),
        xiuLuck: lunar.getXiuLuck(),
        xiuZheng: lunar.getZheng(),
        xiuAnimal: lunar.getAnimal(),
        pengZuGan: lunar.getPengZuGan(),
        pengZuZhi: lunar.getPengZuZhi(),
        chongZhi: lunar.getDayChong(),
        chongShengXiao: lunar.getDayChongShengXiao(),
        sha: lunar.getDaySha(),
        yi: lunar.getDayYi().join(','),
        ji: lunar.getDayJi().join(','),
        jiShen: lunar.getDayJiShen().join(','),
        xiongSha: lunar.getDayXiongSha().join(','),
    };
    // The twelve 时辰. The reference exposes these per instant, so sample the
    // middle of each double-hour. 子时 spans 23:00-00:59, and its stem follows
    // the day that 23:00 already belongs to - so sample it on the evening
    // before, which is the row a printed almanac shows at the top of the day.
    for (let hi = 0; hi < 12; hi++) {
        const at = hi === 0
            ? new Date(dt.getTime() - 86400000)
            : dt;
        const hour = hi === 0 ? 23 : hi * 2 - 1;
        const l2 = Solar.fromYmdHms(at.getUTCFullYear(), at.getUTCMonth() + 1,
                                    at.getUTCDate(), hour, 30, 0).getLunar();
        row['h' + hi] = [
            l2.getTimeInGanZhi(),
            l2.getTimeTianShen(),
            l2.getTimeTianShenLuck(),
            l2.getTimeChongGan() + l2.getTimeChong(),
            l2.getTimeChongShengXiao(),
            l2.getTimeYi().join(','),
            l2.getTimeJi().join(','),
        ].join('/');
    }
    expected.push(row);
}

const out = execFileSync(binary, {
    input: cases.join('\n') + '\n',
    encoding: 'utf8',
    maxBuffer: 256 * 1024 * 1024,
});
// The binary runs in text mode on Windows, so strip the CR the shell adds.
const lines = out.split('\n').map((l) => l.replace(/\r$/, '')).filter((l) => l.length > 0);

if (lines.length !== expected.length) {
    console.error(`line count mismatch: got ${lines.length}, want ${expected.length}`);
    process.exit(1);
}

let bad = 0;
const fieldFails = {};
for (let i = 0; i < lines.length; i++) {
    const parts = lines[i].split('|');
    const got = { tag: parts[0] };
    for (let j = 1; j < parts.length; j++) {
        const eq = parts[j].indexOf('=');
        got[parts[j].substring(0, eq)] = parts[j].substring(eq + 1);
    }
    const want = expected[i];
    for (const k of Object.keys(want)) {
        if (String(got[k]) !== String(want[k])) {
            fieldFails[k] = (fieldFails[k] || 0) + 1;
            if (bad < 10) {
                console.error(`${want.tag} ${k}:\n  cpp = ${got[k]}\n  ref = ${want[k]}`);
            }
            bad++;
        }
    }
}

console.log(`checked ${lines.length} days x ${Object.keys(expected[0]).length - 1} fields`);
if (bad === 0) {
    console.log('ALL MATCH');
} else {
    console.error('mismatched fields:', JSON.stringify(fieldFails));
    console.error(`${bad} mismatches`);
    process.exit(1);
}
