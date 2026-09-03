#include "astro_events.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "eclipse.h"
#include "eph.h"
#include "eph0.h"
#include "../mylib/tool.h"

namespace {

const double kPi = 3.14159265358979323846;
const double kRadToDeg = 180.0 / kPi;
const double kCentury = 36525.0;          // days per Julian century
const double kSynodicMonth = 29.530588;   // days
const double kAnomalisticYear = 365.259636;
// Earth's perihelion nearest J2000: 2000-01-03 05:18 TD, J2000-relative.
const double kPerihelionEpoch = 2.72;

// The Sun is ~959.63" across at 1 AU, so an inferior conjunction closer to the
// ecliptic than this in geocentric latitude is a transit rather than a miss.
const double kSunSemiDiameterRad = 959.63 / 3600.0 * kPi / 180.0;

bool isFiniteNum(double v) { return std::isfinite(v); }

// Synodic period in days. Index follows the engine: 1=Mercury..8 (the table
// only reaches Neptune, so Pluto borrows Neptune's - they differ by a day).
double synodicDays(int xt)
{
    if (xt >= 1 && xt <= 8) return cs_xxHH[std::min(xt, 8) - 1];
    return 365.25;
}

// The engine's own phenomena routines index XL0 directly, which only holds
// the eight VSOP planets; Pluto comes from pluto_coord instead and has to be
// solved separately. Everything else routes through them.
bool hasSeriesPhenomena(int xt) { return xt >= 1 && xt <= 7; }

void push(std::vector<AstroEvent>& out, double tCenturies, int kind,
          const std::string& detail = std::string())
{
    if (!isFiniteNum(tCenturies)) return;
    AstroEvent e;
    e.jdTd = tCenturies * kCentury;
    e.kind = kind;
    e.detail = detail;
    out.push_back(e);
}

std::string degText(double angleRad)
{
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.1f\xc2\xb0", std::fabs(angleRad) * kRadToDeg);
    return buf;
}

// Heliocentric ecliptic longitude difference planet - Earth, wrapped to
// (-pi, pi]. Zero at opposition (or inferior conjunction), pi at conjunction.
double helioLonDiff(int xt, double t)
{
    mystl::array3 e = p_coord(0, t, -1, -1, -1);
    mystl::array3 p = p_coord(xt, t, -1, -1, -1);
    return rad2rrad(p[0] - e[0]);
}

// Newton solve for helioLonDiff == w0, used only for Pluto. The mean rate is
// a good enough starting derivative, and one numerical refinement settles it.
double solvePlutoAspect(double t, double w0)
{
    const double hh = synodicDays(8) / kCentury;
    double v = -2.0 * kPi / hh;       // outer planet: retrograde relative to Earth
    for (int i = 0; i < 6; ++i) {
        double d = rad2rrad(helioLonDiff(8, t) - w0);
        if (!isFiniteNum(d)) return t;
        t -= d / v;
    }
    const double dt = 1e-5;
    double a = rad2rrad(helioLonDiff(8, t) - w0);
    double b = rad2rrad(helioLonDiff(8, t + dt) - w0);
    double dv = (b - a) / dt;
    if (std::fabs(dv) > 1e-9) t -= a / dv;
    return t;
}

// Which way a station turns, read off the motion rather than taken from
// xingLiu()'s sn flag: sn selects which of the two stations around the
// conjunction to solve for, and which of those is the direct one depends on
// whether the planet is inside Earth's orbit. Sampling the geocentric right
// ascension either side of the solution says it outright.
// The station is a turning point, so the two samples either side of it are
// nearly equal and their difference says nothing; it is the curvature that
// carries the answer. Retrograde means right ascension running backwards, so
// the arc peaks where retrograde begins and bottoms out where it ends.
int classifyStation(int xt, double t)
{
    const double h = 3.0 / kCentury;
    double mid    = xingLiu0(xt, t, 8, 0.0)[0];
    double before = xingLiu0(xt, t - h, 8, 0.0)[0];
    double after  = xingLiu0(xt, t + h, 8, 0.0)[0];
    double bulge = rad2rrad(before - mid) + rad2rrad(after - mid);
    if (!isFiniteNum(bulge)) return AE_StationDirect;
    return bulge > 0.0 ? AE_StationDirect : AE_StationRetrograde;
}

// Earth's heliocentric radius vector (AU) at t centuries TD.
double earthRadiusAU(double t) { return XL0_calc(0, 2, t, -1); }

// Parabolic extremum of the radius vector near t, the same three-point
// refinement daJu() uses for the elongation maximum. sign +1 seeks the
// maximum (aphelion), -1 the minimum (perihelion).
double refineApsis(double t, double sign)
{
    double steps[3] = {4.0 / kCentury, 0.5 / kCentury, 0.05 / kCentury};
    for (double dt : steps) {
        double r1 = sign * earthRadiusAU(t - dt);
        double r2 = sign * earthRadiusAU(t);
        double r3 = sign * earthRadiusAU(t + dt);
        double denom = r1 + r3 - 2.0 * r2;
        if (std::fabs(denom) < 1e-14) break;
        t += (r1 - r3) / denom * dt / 2.0;
    }
    return t;
}

void addPlanetEvents(std::vector<AstroEvent>& out, int xt, double t0, double span)
{
    const double hh = synodicDays(xt) / kCentury;
    const bool inner = (xt == 1 || xt == 2);
    // One cycle of margin on each side: these routines snap to the solution
    // nearest their argument, so stepping the argument by the synodic period
    // walks the sequence, and the ends are trimmed by the caller's window.
    const int nCycles = std::max(2, (int)std::ceil(span / hh) + 1);

    for (int k = -1; k <= nCycles; ++k) {
        double t = t0 + k * hh;

        if (inner) {
            mystl::array2 e = daJu(xt, t, true);
            push(out, e[0], AE_GreatestElongEast, degText(e[1]));
            e = daJu(xt, t, false);
            push(out, e[0], AE_GreatestElongWest, degText(e[1]));
        }

        if (!hasSeriesPhenomena(xt)) {
            push(out, solvePlutoAspect(t, kPi), AE_Conjunction);
            push(out, solvePlutoAspect(t, 0.0), AE_Opposition);
            continue;
        }

        mystl::array2 sup = xingHR(xt, t, false);
        push(out, sup[0], AE_Conjunction);

        mystl::array2 inf = xingHR(xt, t, true);
        if (inner) {
            // At inferior conjunction the planet passes the Sun's disc when
            // its geocentric latitude difference stays inside the solar
            // semi-diameter; otherwise it slips above or below.
            bool transit = isFiniteNum(inf[1]) && std::fabs(inf[1]) < kSunSemiDiameterRad;
            push(out, inf[0], transit ? AE_Transit : AE_InferiorConjunction,
                 transit ? degText(inf[1]) : std::string());
        } else {
            push(out, inf[0], AE_Opposition);
        }

        for (bool sn : {true, false}) {
            double st = xingLiu(xt, t, sn);
            if (isFiniteNum(st)) push(out, st, classifyStation(xt, st));
        }
    }

    // 合月 runs on the lunar month, not the synodic period.
    if (hasSeriesPhenomena(xt)) {
        const double month = kSynodicMonth / kCentury;
        int nMonths = std::max(2, (int)std::ceil(span / month) + 1);
        for (int k = -1; k <= nMonths; ++k) {
            mystl::array4 hy = xingHY(xt, t0 + k * month);
            push(out, hy[0], AE_ConjunctionMoon, degText(hy[1]));
        }
    }
}

// The next transit of Mercury or Venus, or nothing if none turns up within
// `years`. Transits are far too rare to fall inside the ordinary search
// window - Mercury manages about thirteen a century, Venus a pair every 121
// years - so they are hunted separately and given a reserved slot.
//
// Solving every inferior conjunction exactly would be far too slow to run
// behind a card that refreshes daily, so the mean conjunction times drive a
// cheap low-precision latitude test first: only a conjunction that already
// looks near the ecliptic is worth a full solve.
bool nextTransit(int xt, double t0, double years, AstroEvent& out)
{
    const double hh = synodicDays(xt) / kCentury;
    const int nCycles = (int)std::ceil(years * 365.25 / synodicDays(xt));
    // Anchor the mean series on one real conjunction so the coarse times do
    // not drift out of the window over a century of stepping.
    mystl::array2 first = xingHR(xt, t0, true);
    if (!isFiniteNum(first[0])) return false;

    for (int k = 0; k <= nCycles; ++k) {
        double tMean = first[0] + k * hh;
        mystl::array3 e = p_coord(0, tMean, 10, 10, 10);
        mystl::array3 p = p_coord(xt, tMean, 10, 10, 10);
        p = h2g(p, e);
        if (std::fabs(p[1]) > 1.5 * kPi / 180.0) continue;  // nowhere near a node

        mystl::array2 c = xingHR(xt, tMean, true);
        if (!isFiniteNum(c[0]) || c[0] <= t0) continue;
        if (std::fabs(c[1]) >= kSunSemiDiameterRad) continue;
        out.jdTd = c[0] * kCentury;
        out.kind = AE_Transit;
        out.detail = degText(c[1]);
        return true;
    }
    return false;
}

void addLunarPhases(std::vector<AstroEvent>& out, double t0, double span)
{
    // MS_aLon_t(W) solves for the moon-sun apparent longitude difference W,
    // so whole turns are new moons and the quarters fall on the fractions.
    double n0 = std::floor(t0 * kCentury / kSynodicMonth) - 1.0;
    int nMonths = std::max(2, (int)std::ceil(span * kCentury / kSynodicMonth) + 2);
    static const struct { double frac; int kind; } kPhases[4] = {
        {0.00, AE_NewMoon}, {0.25, AE_FirstQuarter},
        {0.50, AE_FullMoon}, {0.75, AE_LastQuarter},
    };
    for (int k = 0; k <= nMonths; ++k)
        for (const auto& p : kPhases)
            push(out, MS_aLon_t((n0 + k + p.frac) * 2.0 * kPi), p.kind);
}

void addSolarTerms(std::vector<AstroEvent>& out, double t0, double span)
{
    // The Sun's apparent longitude reaches 0/90/180/270 deg at the equinoxes
    // and solstices; S_aLon_t() inverts that the same way the 24-term page does.
    double n0 = std::floor(t0 * kCentury / 365.2422) - 1.0;
    int nYears = std::max(1, (int)std::ceil(span * kCentury / 365.2422) + 1);
    static const struct { double frac; int kind; } kPoints[4] = {
        {0.00, AE_MarchEquinox},     {0.25, AE_JuneSolstice},
        {0.50, AE_SeptemberEquinox}, {0.75, AE_DecemberSolstice},
    };
    for (int k = 0; k <= nYears; ++k)
        for (const auto& p : kPoints)
            push(out, S_aLon_t((n0 + k + 1.0 + p.frac) * 2.0 * kPi), p.kind);
}

void addEarthApsides(std::vector<AstroEvent>& out, double t0, double span)
{
    double days = t0 * kCentury;
    double k0 = std::floor((days - kPerihelionEpoch) / kAnomalisticYear) - 1.0;
    int nYears = std::max(1, (int)std::ceil(span * kCentury / kAnomalisticYear) + 1);
    for (int k = 0; k <= nYears; ++k) {
        double peri = (kPerihelionEpoch + (k0 + k) * kAnomalisticYear) / kCentury;
        push(out, refineApsis(peri, -1.0), AE_Perihelion);
        push(out, refineApsis(peri + kAnomalisticYear / 2.0 / kCentury, 1.0), AE_Aphelion);
    }
}

// Eclipses come from the existing search, which is driven by a calendar month.
void addEclipses(std::vector<AstroEvent>& out, double t0, int filter, int count)
{
    Date d = setFromJD(t0 * kCentury + J2000);
    std::vector<EclipseEvent> found = searchEclipses(d.Y, d.M, count, filter);
    for (const EclipseEvent& e : found) {
        int kind = (e.kind == EclipseEvent::Solar) ? AE_SolarEclipse : AE_LunarEclipse;
        push(out, e.maximumTd / kCentury, kind, e.type);
    }
}

} // namespace

const char* astroEventName(int kind, bool chinese)
{
    struct Row { const char* zh; const char* en; };
    static const Row kNames[AE_KindCount] = {
        {"东大距",       "Greatest elong. E"},
        {"西大距",       "Greatest elong. W"},
        {"合",                   "Conjunction"},
        {"下合",             "Inferior conj."},
        {"冲",                   "Opposition"},
        {"凌日",             "Transit"},
        {"顺留",             "Direct station"},
        {"逆留",             "Retrograde station"},
        {"合月",             "Conj. with Moon"},
        {"朔",                   "New moon"},
        {"上弦",             "First quarter"},
        {"望",                   "Full moon"},
        {"下弦",             "Last quarter"},
        {"日食",             "Solar eclipse"},
        {"月食",             "Lunar eclipse"},
        {"过近日点", "Perihelion"},
        {"过远日点", "Aphelion"},
        {"春分",             "March equinox"},
        {"夏至",             "June solstice"},
        {"秋分",             "September equinox"},
        {"冬至",             "December solstice"},
    };
    if (kind < 0 || kind >= AE_KindCount) return "";
    return chinese ? kNames[kind].zh : kNames[kind].en;
}

std::vector<AstroEvent> upcomingAstroEvents(int xt, double jdTdNow, int maxCount)
{
    std::vector<AstroEvent> all;
    if (maxCount <= 0) return all;

    const double t0 = jdTdNow / kCentury;
    // How far ahead to reach for. A slow outer planet only has a handful of
    // events a year, so the window has to grow with how many were asked for.
    double span = 0.0;

    switch (xt) {
    case -1: // Sun: what the Sun itself does in our sky, plus solar eclipses.
        span = maxCount * 0.5 / 4.0 * 365.2422 / kCentury;
        addSolarTerms(all, t0, span);
        addEclipses(all, t0, 1, maxCount);
        break;
    case 0:  // Earth: eclipses of both kinds, and the shape of its own orbit.
        span = maxCount * 0.5 * 365.2422 / 4.0 / kCentury;
        addEarthApsides(all, t0, span);
        addEclipses(all, t0, 0, maxCount);
        break;
    case 10: // Moon
        span = maxCount * 0.5 * kSynodicMonth / 4.0 / kCentury;
        addLunarPhases(all, t0, span);
        addEclipses(all, t0, 0, maxCount);
        break;
    default:
        if (xt < 1 || xt > 8) return all;
        span = std::max(2.0, maxCount * 0.35) * synodicDays(xt) / kCentury;
        addPlanetEvents(all, xt, t0, span);
        break;
    }

    // Rare events would otherwise never make the list: a body's routine
    // business - conjunctions with the Moon, quarter phases - fills the window
    // several times over before the next eclipse or transit comes round.
    // They get a reserved slot instead, so what the card shows is "the next
    // few things, and the next rare thing".
    std::vector<int> reserved;
    if (xt == 0 || xt == 10) { reserved.push_back(AE_SolarEclipse); reserved.push_back(AE_LunarEclipse); }
    if (xt == -1)            { reserved.push_back(AE_SolarEclipse); }
    if (xt == 1 || xt == 2) {
        AstroEvent tr;
        if (nextTransit(xt, t0, 130.0, tr)) { all.push_back(tr); reserved.push_back(AE_Transit); }
    }

    // Keep what is still ahead, in order, and drop the duplicates the
    // overlapping search windows inevitably produce.
    all.erase(std::remove_if(all.begin(), all.end(), [&](const AstroEvent& e) {
                  return !isFiniteNum(e.jdTd) || e.jdTd <= jdTdNow;
              }), all.end());
    std::sort(all.begin(), all.end(), [](const AstroEvent& a, const AstroEvent& b) {
        if (a.jdTd != b.jdTd) return a.jdTd < b.jdTd;
        return a.kind < b.kind;
    });
    all.erase(std::unique(all.begin(), all.end(),
                          [](const AstroEvent& a, const AstroEvent& b) {
                              return a.kind == b.kind &&
                                     std::fabs(a.jdTd - b.jdTd) < 0.02;
                          }), all.end());
    if ((int)all.size() <= maxCount) return all;

    std::vector<AstroEvent> kept(all.begin(), all.begin() + maxCount);
    for (int kind : reserved) {
        auto isKind = [kind](const AstroEvent& e) { return e.kind == kind; };
        if (std::any_of(kept.begin(), kept.end(), isKind)) continue;
        auto src = std::find_if(all.begin(), all.end(), isKind);
        if (src == all.end()) continue;
        // Give up the last ordinary entry rather than another reserved one,
        // so two reservations cannot evict each other.
        auto victim = std::find_if(kept.rbegin(), kept.rend(), [&](const AstroEvent& e) {
            return std::find(reserved.begin(), reserved.end(), e.kind) == reserved.end();
        });
        if (victim == kept.rend()) break;
        *victim = *src;
    }
    std::sort(kept.begin(), kept.end(), [](const AstroEvent& a, const AstroEvent& b) {
        return a.jdTd < b.jdTd;
    });
    return kept;
}
