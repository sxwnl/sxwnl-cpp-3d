// Generates lunar/huangli_data.cpp from the lunar-javascript tables.
//
// The almanac (黄历) content - 宜/忌, 吉神/凶煞, 值神, 建除, 星宿, 彭祖百忌 -
// is data, not algorithm: hundreds of packed table entries that cannot be
// derived from an ephemeris. Rather than transcribe them by hand and get them
// subtly wrong, this pulls them straight out of the reference implementation
// and emits a C++ translation unit.
//
//   node tools/gen_huangli.js > lunar/huangli_data.cpp
//
// Expects lunar.js beside this script, or LUNAR_JS pointing at it, or the
// lunar-javascript package installed.
// Source: https://github.com/6tail/lunar-javascript  (MIT)
'use strict';

const path = require('path');

function loadLunar() {
    const candidates = [
        process.env.LUNAR_JS,
        path.join(__dirname, 'lunar.js'),
        'lunar-javascript',
    ].filter(Boolean);
    for (const c of candidates) {
        try { return require(c); } catch (e) { /* try the next */ }
    }
    throw new Error('cannot locate lunar.js; set LUNAR_JS=/path/to/lunar.js');
}

const { LunarUtil, I18n } = loadLunar();
I18n.setLanguage('chs');

// Tables hold i18n keys like "{yj.jiSi}". Resolve them to Chinese once, here,
// so the C++ side carries plain strings and needs no lookup machinery. Applying
// this to an already-resolved string is a no-op.
const tr = (s) => (typeof s === 'string'
    ? s.replace(/\{(.[^}]*)\}/g, (_m, k) => I18n.getMessage(k))
    : s);

// Some builds resolve the tables in place when a language is set, so a lookup
// key may be either form. Try both.
function pick(obj, key) {
    if (obj[key] !== undefined) return obj[key];
    const t = tr(key);
    return obj[t] !== undefined ? obj[t] : '';
}

function cstr(s) {
    return '"' + String(s).replace(/\\/g, '\\\\').replace(/"/g, '\\"') + '"';
}

function emitArray(name, arr, perLine) {
    const items = arr.map((v) => cstr(tr(v)));
    let out = `const char* const ${name}[] = {\n`;
    for (let i = 0; i < items.length; i += perLine) {
        out += '    ' + items.slice(i, i + perLine).join(', ') + ',\n';
    }
    out += '};\n';
    out += `const int ${name}_N = ${items.length};\n\n`;
    return out;
}

// A packed table has to be split across literals: MSVC caps a single string
// literal at 16380 bytes and these run far past that.
function emitLongString(name, s, chunk) {
    let out = `const char* const ${name} =\n`;
    for (let i = 0; i < s.length; i += chunk) {
        out += '    ' + cstr(s.substring(i, i + chunk)) + '\n';
    }
    out += ';\n\n';
    return out;
}

function emitStringArray(name, arr, chunk) {
    let out = `const char* const ${name}[] = {\n`;
    for (const s of arr) {
        out += '    ';
        for (let i = 0; i < s.length; i += chunk) {
            out += cstr(s.substring(i, i + chunk));
            if (i + chunk < s.length) out += '\n    ';
        }
        out += ',\n';
    }
    out += '};\n';
    out += `const int ${name}_N = ${arr.length};\n\n`;
    return out;
}

// XIU is keyed by "<日支><星期>", e.g. "申1". Flatten to 12x7 so the C++ side
// indexes instead of hashing strings.
function emitXiu() {
    const zhi = LunarUtil.ZHI; // 1..12
    const rows = [];
    for (let z = 1; z <= 12; z++) {
        const row = [];
        for (let w = 0; w < 7; w++) row.push(tr(pick(LunarUtil.XIU, zhi[z] + w)));
        rows.push(row);
    }
    let out = 'const char* const kXiu[12][7] = {\n';
    for (const row of rows) out += '    {' + row.map(cstr).join(', ') + '},\n';
    out += '};\n\n';

    // Luck / seven-luminaries / animal / beast hang off the 宿 name.
    const names = [];
    for (const row of rows) for (const n of row) if (n && !names.includes(n)) names.push(n);
    out += 'const XiuInfo kXiuInfo[] = {\n';
    for (const n of names) {
        const raw = Object.keys(LunarUtil.XIU_LUCK).find((k) => tr(k) === n) || n;
        out += '    {' + [
            cstr(n),
            cstr(tr(pick(LunarUtil.XIU_LUCK, raw))),
            cstr(tr(pick(LunarUtil.ZHENG, raw))),
            cstr(tr(pick(LunarUtil.ANIMAL, raw))),
            cstr(tr(pick(LunarUtil.SHOU, raw))),
        ].join(', ') + '},\n';
    }
    out += '};\n';
    out += `const int kXiuInfo_N = ${names.length};\n\n`;
    return out;
}

// TIAN_SHEN_TYPE maps 值神 -> 黄道/黑道; TIAN_SHEN_TYPE_LUCK maps that -> 吉/凶.
function emitTianShen() {
    let out = emitArray('kTianShen', LunarUtil.TIAN_SHEN, 4);
    out += 'const TianShenInfo kTianShenInfo[] = {\n';
    for (let i = 1; i < LunarUtil.TIAN_SHEN.length; i++) {
        const raw = LunarUtil.TIAN_SHEN[i];
        const type = pick(LunarUtil.TIAN_SHEN_TYPE, raw);
        out += '    {' + [
            cstr(tr(raw)),
            cstr(tr(type)),
            cstr(tr(pick(LunarUtil.TIAN_SHEN_TYPE_LUCK, type))),
        ].join(', ') + '},\n';
    }
    out += '};\n';
    out += `const int kTianShenInfo_N = ${LunarUtil.TIAN_SHEN.length - 1};\n\n`;

    const zhi = LunarUtil.ZHI;
    const offs = [];
    for (let z = 1; z <= 12; z++) offs.push(pick(LunarUtil.ZHI_TIAN_SHEN_OFFSET, zhi[z]));
    out += `const int kZhiTianShenOffset[12] = {${offs.join(', ')}};\n\n`;
    return out;
}

let out = '';
out += '// GENERATED FILE - DO NOT EDIT BY HAND.\n';
out += '// Produced by tools/gen_huangli.js from lunar-javascript (MIT).\n';
out += '// Regenerate with:  node tools/gen_huangli.js > lunar/huangli_data.cpp\n';
out += '#include "huangli_data.h"\n\n';
out += 'namespace sx {\nnamespace huangli_data {\n\n';

out += emitArray('kGan', LunarUtil.GAN, 6);
out += emitArray('kZhi', LunarUtil.ZHI, 7);
out += emitArray('kJiaZi', LunarUtil.JIA_ZI, 6);
out += emitArray('kZhiXing', LunarUtil.ZHI_XING, 7);
out += emitArray('kPengZuGan', LunarUtil.PENGZU_GAN, 2);
out += emitArray('kPengZuZhi', LunarUtil.PENGZU_ZHI, 2);
out += emitArray('kChong', LunarUtil.CHONG, 6);
out += emitArray('kChongGan', LunarUtil.CHONG_GAN, 5);
out += emitArray('kShengXiao', LunarUtil.SHENGXIAO, 6);
out += emitArray('kYiJi', LunarUtil.YI_JI, 4);
out += emitArray('kShenSha', LunarUtil.SHEN_SHA, 4);
out += emitTianShen();
out += emitXiu();

{   // SHA is keyed by 日支 -> direction.
    const zhi = LunarUtil.ZHI;
    const vals = [];
    for (let z = 1; z <= 12; z++) vals.push(pick(LunarUtil.SHA, zhi[z]));
    out += emitArray('kSha', vals, 6);
}

out += emitLongString('kDayYiJi', LunarUtil.DAY_YI_JI, 2000);
out += emitStringArray('kDayShenSha', LunarUtil.DAY_SHEN_SHA, 2000);
out += emitLongString('kTimeYiJi', LunarUtil.TIME_YI_JI, 2000);

out += '} // namespace huangli_data\n} // namespace sx\n';

process.stdout.write(out);
