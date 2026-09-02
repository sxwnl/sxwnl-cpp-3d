// Diffs the engine's 干支纪月 / 纪日 against lunar-javascript.
//
//   node tools/check_ganzhi.js build/ganzhi_test
//
// The almanac tables are keyed by these two values, so a disagreement near a
// 节 boundary would quietly select the wrong 宜忌 row.
'use strict';

const path = require('path');
const { execFileSync } = require('child_process');

const binary = process.argv[2] || path.join('build', 'ganzhi_test');

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

const out = execFileSync(binary, { encoding: 'utf8', maxBuffer: 256 * 1024 * 1024 });
const lines = out.split('\n').map((l) => l.replace(/\r$/, '')).filter((l) => l.length > 0);

let checked = 0;
let badMonth = 0;
let badDay = 0;
let badWeek = 0;
const samples = [];

for (const line of lines) {
    const [ymd, month, day, week] = line.split(' ');
    const [y, m, d] = ymd.split('-').map(Number);
    const solar = Solar.fromYmd(y, m, d);
    const lunar = solar.getLunar();
    const refMonth = lunar.getMonthInGanZhi();
    const refDay = lunar.getDayInGanZhi();
    const refWeek = solar.getWeek();
    checked++;
    if (month !== refMonth) {
        badMonth++;
        if (samples.length < 8) samples.push(`${ymd} month: cpp=${month} ref=${refMonth}`);
    }
    if (day !== refDay) {
        badDay++;
        if (samples.length < 8) samples.push(`${ymd} day: cpp=${day} ref=${refDay}`);
    }
    if (Number(week) !== refWeek) {
        badWeek++;
        if (samples.length < 8) samples.push(`${ymd} week: cpp=${week} ref=${refWeek}`);
    }
}

console.log(`checked ${checked} days`);
console.log(`month mismatches: ${badMonth}`);
console.log(`day   mismatches: ${badDay}`);
console.log(`week  mismatches: ${badWeek}`);
for (const s of samples) console.log('  ' + s);
process.exit(badMonth + badDay + badWeek === 0 ? 0 : 1);
