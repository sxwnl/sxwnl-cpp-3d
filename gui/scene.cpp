#include "scene.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <ctime>

#include "../eph/eph0.h"
#include "../eph/eph.h"
#include "../mylib/lat_lon_data.h"
#include "../mylib/tool.h"
#include "../mylib/mystl/static_array.h"

namespace sx {

static const double kPi = 3.14159265358979323846;
static const double kJ2000 = 2451545.0;
static const double kDeg = kPi / 180.0;

double nowJD() {
    using namespace std::chrono;
    const double unixSeconds = duration<double>(system_clock::now().time_since_epoch()).count();
    return 2440587.5 + unixSeconds / 86400.0;
}

double jdToCenturiesTD(double jd) { return (jd - kJ2000) / 36525.0; }

Date localDateFromUtcJD(double utcJD, double timezoneHours) {
    return setFromJD(utcJD + timezoneHours / 24.0);
}

double utcJDFromLocalDate(const Date& localDate, double timezoneHours) {
    return toJD(localDate) - timezoneHours / 24.0;
}

double speedToDaysPerSecond(int unit, double amount) {
    if (!std::isfinite(amount)) return 0.0;
    if (unit == 0) return amount / 86400.0;
    if (unit == 1) return amount / 24.0;
    return amount;
}

void heliocentricAU(int xt, double t, int n, double xyz[3]) {
    if (xt < 0) { xyz[0] = xyz[1] = xyz[2] = 0.0; return; }
    if (xt == 8) {
        mystl::array3 p = pluto_coord(t); // already J2000 heliocentric rectangular (AU)
        xyz[0] = p[0]; xyz[1] = p[1]; xyz[2] = p[2];
        return;
    }
    mystl::array3 llr = p_coord(xt, t, n, n, n); // heliocentric ecliptic (L,B,R)
    mystl::array3 c = llr2xyz(llr);
    xyz[0] = c[0]; xyz[1] = c[1]; xyz[2] = c[2];
}

static float rand01(unsigned int& rng) {
    rng = rng * 1664525u + 1013904223u;
    return (rng >> 8) / (float)(1 << 24);
}

static double solveKepler(double M, double e) {
    double E = M;
    for (int i = 0; i < 6; ++i) {
        double f = E - e * std::sin(E) - M;
        double fp = 1.0 - e * std::cos(E);
        E -= f / fp;
    }
    return E;
}

static gx::Vec3 asteroidWorldAU(const AsteroidInfo& a, double jd, const Scene& scene) {
    double days = jd - kJ2000;
    double meanMotionDegPerDay = 0.9856076686 / std::pow(a.aAU, 1.5);
    double M = std::fmod(a.meanAnomDeg + meanMotionDegPerDay * days, 360.0);
    if (M < 0.0) M += 360.0;

    double E = solveKepler(M * kDeg, a.e);
    double xOrb = a.aAU * (std::cos(E) - a.e);
    double yOrb = a.aAU * std::sqrt(1.0 - a.e * a.e) * std::sin(E);

    double w = a.omegaDeg * kDeg;
    double O = a.OmegaDeg * kDeg;
    double i = a.incDeg * kDeg;
    double cw = std::cos(w), sw = std::sin(w);
    double cO = std::cos(O), sO = std::sin(O);
    double ci = std::cos(i), si = std::sin(i);

    double x1 = cw * xOrb - sw * yOrb;
    double y1 = sw * xOrb + cw * yOrb;

    double xyz[3] = {
        cO * x1 - sO * y1 * ci,
        sO * x1 + cO * y1 * ci,
        y1 * si
    };
    return scene.toWorld(xyz);
}

static void buildAsteroidBelt(std::vector<AsteroidInfo>& out) {
    // NASA describes the main asteroid belt as the region between Mars and
    // Jupiter; Psyche's orbit reaches 3.3 AU, so we model a dense belt from
    // roughly 2.1 AU outward with a few sparse resonance gaps.
    const int target = 2600;
    out.clear();
    out.reserve(target);
    unsigned int rng = 0x5a17e9u;
    while ((int)out.size() < target) {
        double a = 2.06 + 1.32 * rand01(rng);
        double gap = std::min({
            std::abs(a - 2.50) / 0.035,
            std::abs(a - 2.82) / 0.040,
            std::abs(a - 2.95) / 0.035
        });
        double keep = 0.26 + 0.74 * std::min(1.0, gap);
        if (rand01(rng) > keep) continue;

        if (rand01(rng) < 0.52f) {
            double beltBias = std::pow(rand01(rng), 1.25);
            a = 2.16 + 0.82 * beltBias;
        }

        AsteroidInfo p;
        p.aAU = a;
        p.e = 0.03 + 0.22 * std::pow(rand01(rng), 1.8);
        p.incDeg = 0.4 + 18.0 * std::pow(rand01(rng), 2.4);
        if (rand01(rng) < 0.08f) p.incDeg += 8.0 * rand01(rng);
        p.omegaDeg = 360.0 * rand01(rng);
        p.OmegaDeg = 360.0 * rand01(rng);
        p.meanAnomDeg = 360.0 * rand01(rng);
        p.displaySize = 1.0f + 2.2f * std::pow(rand01(rng), 6.0f);
        p.brightness = 0.28f + 0.62f * rand01(rng);
        out.push_back(p);
    }
}

Scene::Scene() {
    // xt, name, pinyin, color(rgb), realRadiusKm, siderealYears, isSun
    // Rotational elements: IAU WGCCRE (Archinal et al.). {a0, a0/century,
    // d0, d0/century, W0, W deg/day, valid}. Small periodic terms (Mercury
    // libration, Mars/Jupiter nutation, Neptune's N term) are dropped — they
    // are well under a degree and invisible at render scale.
    // Sanity: W rates invert to the known rotation periods — Jupiter
    // 360/870.536 = 9.92 h, Saturn 10.66 h, Mars 24.62 h, Mercury 58.65 d,
    // Venus 243.0 d (retrograde), Sun 25.38 d (Carrington).
    const RotationElements kSun     {286.13,   0.0,     63.87,   0.0,      84.176,   14.1844000,   true};
    const RotationElements kMercury {281.0103,-0.0328,  61.4155,-0.0049,  329.5988,   6.1385108,   true};
    const RotationElements kVenus   {272.76,   0.0,     67.16,   0.0,     160.20,    -1.4813688,   true};
    const RotationElements kEarth   {  0.00,  -0.641,   90.00,  -0.557,   190.147,  360.9856235,   true};
    const RotationElements kMars    {317.269202,-0.10927547, 54.432516,-0.05827105, 176.049863, 350.891982443297, true};
    const RotationElements kJupiter {268.056595,-0.006499, 64.495303, 0.002413, 284.95, 870.5360000, true};
    const RotationElements kSaturn  { 40.589,  -0.036,   83.537, -0.004,    38.90,   810.7939024,  true};
    const RotationElements kUranus  {257.311,   0.0,    -15.175,  0.0,     203.81,  -501.1600928,  true};
    const RotationElements kNeptune {299.36,    0.0,     43.46,   0.0,     253.18,   536.3128492,  true};
    const RotationElements kPluto   {132.993,   0.0,     -6.163,  0.0,     302.695,   56.3625225,  true};

    info_ = {
        {-1, "太阳",   "sun",     {1.00f, 0.85f, 0.30f}, 696000.0, 0.0,      true,  kSun},
        { 1, "水星",   "mercury", {0.70f, 0.70f, 0.72f},   2440.0, 0.2408467, false, kMercury},
        { 2, "金星",   "venus",   {0.90f, 0.75f, 0.45f},   6052.0, 0.6151973, false, kVenus},
        { 0, "地球",   "earth",   {0.30f, 0.55f, 0.95f},   6371.0, 1.0000174, false, kEarth},
        { 3, "火星",   "mars",    {0.85f, 0.40f, 0.25f},   3390.0, 1.8808476, false, kMars},
        { 4, "木星",   "jupiter", {0.85f, 0.70f, 0.55f},  69911.0, 11.862615, false, kJupiter},
        { 5, "土星",   "saturn",  {0.90f, 0.82f, 0.60f},  58232.0, 29.447498, false, kSaturn},
        { 6, "天王星", "uranus",  {0.60f, 0.85f, 0.90f},  25362.0, 84.016846, false, kUranus},
        { 7, "海王星", "neptune", {0.30f, 0.45f, 0.95f},  24622.0, 164.79132, false, kNeptune},
        { 8, "冥王星", "pluto",   {0.75f, 0.65f, 0.55f},   1188.0, 247.92065, false, kPluto},
    };
    state_.resize(info_.size());
    buildAsteroidBelt(asteroidInfo_);
    asteroidState_.resize(asteroidInfo_.size());
    clock_.jd = nowJD();
    rebuildOrbits();
    update();
}

// Apply a matrix's 3x3 rotation part to a direction.
static gx::Vec3 mul3(const gx::Mat4& m, const gx::Vec3& v) {
    return { m.m[0]*v.x + m.m[4]*v.y + m.m[8]*v.z,
             m.m[1]*v.x + m.m[5]*v.y + m.m[9]*v.z,
             m.m[2]*v.x + m.m[6]*v.y + m.m[10]*v.z };
}

// Earth gets its own solution rather than the IAU elements above. The WGCCRE
// report's Earth entry is an explicitly low-precision linear fit (it carries no
// precession/nutation), and measured against the true subsolar point it drifts
// ~0.73° over 2025-2030, versus 0.02° for the sidereal-time form used here.
Scene::Orientation Scene::solveEarthOrientation(double T) const {
    Orientation s;
    const double kDeg = 3.14159265358979323846 / 180.0;
    double eps = 23.4392911 - 0.0130041667 * T
               - 1.638889e-7 * T * T + 5.036111e-7 * T * T * T;

    double du = clock_.jd - kJ2000;
    double gmst = 280.46061837 + 360.98564736629 * du
                + 0.000387933 * T * T - T * T * T / 38710000.0;
    gmst = std::fmod(gmst, 360.0);
    if (gmst < 0.0) gmst += 360.0;

    // Solving "the subsolar point must face the Sun" in this world frame gives
    // spin = GMST + 90° with the pole leaning by -eps: the north celestial pole
    // sits at ecliptic longitude 90°, latitude 90°-eps, which toWorld() places
    // at (0, cos eps, -sin eps).
    s.poleNodeDeg  = 0.0f;
    s.axialTiltDeg = (float)(-eps);
    s.spinDeg      = (float)(gmst + 90.0);
    s.poleDir      = {0.0f, (float)std::cos(eps * kDeg), (float)(-std::sin(eps * kDeg))};
    return s;
}

// Turn IAU rotational elements into the render angles. Everything is derived
// numerically from the pole and prime-meridian vectors, so there is no
// per-body hand-tuning and the awkward cases (Uranus tipped past 90°, the
// retrograde bodies) fall out on their own.
Scene::Orientation Scene::orientationFromPoleAndW(double a0, double d0, double W) {
    Orientation s;
    const double kDeg = 3.14159265358979323846 / 180.0;

    // Pole and prime-meridian directions in ICRF equatorial coordinates.
    // The prime meridian sits W east of the node of the body's equator.
    double ca = std::cos(a0), sa = std::sin(a0), cd = std::cos(d0), sd = std::sin(d0);
    double P[3] = { cd * ca, cd * sa, sd };
    double N[3] = { -sa, ca, 0.0 };                  // ascending node
    double M[3] = { -sd * ca, -sd * sa, cd };        // P x N, completes the triad
    double cw = std::cos(W), sw = std::sin(W);
    double PM[3] = { N[0]*cw + M[0]*sw, N[1]*cw + M[1]*sw, N[2]*cw + M[2]*sw };

    // Equatorial -> ecliptic (obliquity at J2000) -> world, matching toWorld().
    const double eps0 = 23.4392911 * kDeg;
    auto toWorldDir = [&](const double v[3], gx::Vec3& out) {
        double ex = v[0];
        double ey = v[1] * std::cos(eps0) + v[2] * std::sin(eps0);
        double ez = -v[1] * std::sin(eps0) + v[2] * std::cos(eps0);
        out = {(float)ex, (float)ez, (float)(-ey)};
    };
    gx::Vec3 pw, pmw;
    toWorldDir(P, pw);
    toWorldDir(PM, pmw);
    pw = gx::normalize(pw);
    pmw = gx::normalize(pmw);
    s.poleDir = pw;

    // rotateY(node) * rotateX(tilt) carries +Y onto the pole.
    double tilt = std::acos(std::max(-1.0f, std::min(1.0f, pw.y)));
    double node = std::atan2(pw.x, pw.z);
    s.axialTiltDeg = (float)(tilt / kDeg);
    s.poleNodeDeg  = (float)(node / kDeg);

    // Undo those two to read off the spin that lands +Z on the prime meridian.
    gx::Vec3 v = mul3(gx::rotateX((float)-tilt) * gx::rotateY((float)-node), pmw);
    s.spinDeg = (float)(std::atan2(v.x, v.z) / kDeg);
    return s;
}

Scene::Orientation Scene::solveOrientation(const RotationElements& r, double T) const {
    if (!r.valid) return Orientation{};
    const double kDeg = 3.14159265358979323846 / 180.0;
    double d  = clock_.jd - kJ2000;
    double a0 = (r.raDeg  + r.raTPerCentury  * T) * kDeg;
    double d0 = (r.decDeg + r.decTPerCentury * T) * kDeg;
    double W  = (r.w0Deg  + r.wRateDegPerDay * d) * kDeg;
    return orientationFromPoleAndW(a0, d0, W);
}

// IAU physical libration model for the Moon (Archinal et al. 2018, "Report
// of the IAU WGCCRE"). Unlike the planets, the Moon's pole is not a simple
// linear precession: 13 periodic terms tied to the lunar orbit are added on
// top of the linear part, and they are NOT negligible — E1 alone has a
// 3.88 deg amplitude in a0 and is what produces the Cassini-law tilt of the
// spin axis (a constant 1.5424 deg from the ecliptic pole); dropping the
// periodic terms leaves the pole sitting almost exactly on the ecliptic
// pole instead (verified: 0.02 deg instead of 1.5 deg).
// d = days from J2000.0 TDB (TD~UT at this precision).
Scene::Orientation Scene::solveMoonOrientation(double d) const {
    const double kDeg = 3.14159265358979323846 / 180.0;
    double T = d / 36525.0;

    auto E = [&](double c0, double rate) { return (c0 + rate * d) * kDeg; };
    double E1  = E(125.045, -0.0529921);
    double E2  = E(250.089, -0.1059842);
    double E3  = E(260.008, 13.0120009);
    double E4  = E(176.625, 13.3407154);
    double E5  = E(357.529,  0.9856003);
    double E6  = E(311.589, 26.4057084);
    double E7  = E(134.963, 13.0649930);
    double E8  = E(276.617,  0.3287146);
    double E9  = E( 34.226,  1.7484877);
    double E10 = E( 15.134, -0.1589763);
    double E11 = E(119.743,  0.0036096);
    double E12 = E(239.961,  0.1643573);
    double E13 = E( 25.053, 12.9590088);

    double a0 = 269.9949 + 0.0031 * T
              - 3.8787*std::sin(E1) - 0.1204*std::sin(E2) + 0.0700*std::sin(E3)
              - 0.0172*std::sin(E4) + 0.0072*std::sin(E6) - 0.0052*std::sin(E10)
              + 0.0043*std::sin(E13);
    double d0 = 66.5392 + 0.0130 * T
              + 1.5419*std::cos(E1) + 0.0239*std::cos(E2) - 0.0278*std::cos(E3)
              + 0.0068*std::cos(E4) - 0.0029*std::cos(E6) + 0.0009*std::cos(E7)
              + 0.0008*std::cos(E10) - 0.0009*std::cos(E13);
    double W  = 38.3213 + 13.17635815 * d - 1.4e-12 * d * d
              - 3.5610*std::sin(E1) - 0.1208*std::sin(E2) + 0.0768*std::sin(E3)
              - 0.0204*std::sin(E4) + 0.0021*std::sin(E5) + 0.0021*std::sin(E6)
              - 0.0072*std::sin(E7) - 0.0007*std::sin(E8) + 0.0057*std::sin(E9)
              - 0.0013*std::sin(E10) + 0.0003*std::sin(E11) + 0.0013*std::sin(E12)
              - 0.0006*std::sin(E13);

    return orientationFromPoleAndW(a0 * kDeg, d0 * kDeg, W * kDeg);
}

gx::Vec3 Scene::toWorld(const double a[3]) const {
    double r = std::sqrt(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]);
    if (r < 1e-9) return {0, 0, 0};
    double mapped = scale_.logDistance ? scale_.logK * std::log(1.0 + r)
                                       : scale_.linearAUtoWorld * r;
    double s = mapped / r;
    // ecliptic (X=equinox, Y=in-plane, Z=north) -> world (Y up, ecliptic = XZ).
    // The Z component is negated so this is a proper rotation (det=+1) rather
    // than a reflection. A plain axis swap mirrors the whole scene: angles and
    // distances survive, but every orbit runs backwards and no rotation can
    // then line the Earth's texture up with its own spin at the same time.
    return {(float)(a[0] * s), (float)(a[2] * s), (float)(-a[1] * s)};
}

void Scene::rebuildOrbits() {
    const int N = 256;
    double t0 = jdToCenturiesTD(clock_.jd);
    orbits_.assign(info_.size(), {});
    for (size_t i = 0; i < info_.size(); ++i) {
        if (info_[i].isSun || info_[i].siderealYears <= 0) continue;
        double Pcent = info_[i].siderealYears / 100.0;
        std::vector<gx::Vec3>& line = orbits_[i];
        line.reserve(N);
        for (int k = 0; k < N; ++k) {
            double tt = t0 + (double)k / N * Pcent;
            double xyz[3];
            heliocentricAU(info_[i].xt, tt, 10, xyz);
            line.push_back(toWorld(xyz));
        }
    }
    lastLog_ = scale_.logDistance;
    lastLinear_ = scale_.linearAUtoWorld;
    lastLogK_ = scale_.logK;
}

// Position angle of the Moon's bright limb, converted to something a flat disk
// can be drawn with.
//
// chi is the textbook quantity (Meeus, Astronomical Algorithms, ch. 48): the
// position angle of the midpoint of the bright limb, measured at the Moon from
// the celestial north pole towards east. Subtracting the parallactic angle q
// re-references it from the pole to the observer's zenith, which is the
// orientation someone standing outside actually sees.
//
// Returned in degrees clockwise from screen-up: on screen y grows downward and,
// looking up at the sky with the zenith up, east lies to the left, so the two
// sign flips cancel into a single negation.
static double brightLimbAngle(double t, double moonLon, double moonLat,
                              double sunLon) {
    const double eps = hcjj(t);

    auto toEquatorial = [eps](double lam, double bet, double& ra, double& dec) {
        const double cb = std::cos(bet), sb = std::sin(bet);
        const double x = cb * std::cos(lam);
        const double y = cb * std::sin(lam) * std::cos(eps) - sb * std::sin(eps);
        const double z = cb * std::sin(lam) * std::sin(eps) + sb * std::cos(eps);
        ra = std::atan2(y, x);
        dec = std::asin(std::max(-1.0, std::min(1.0, z)));
    };

    double sRa, sDec, mRa, mDec;
    toEquatorial(sunLon, 0.0, sRa, sDec);
    toEquatorial(moonLon, moonLat, mRa, mDec);

    const double dRa = sRa - mRa;
    const double chi = std::atan2(
        std::cos(sDec) * std::sin(dRa),
        std::sin(sDec) * std::cos(mDec) -
            std::cos(sDec) * std::sin(mDec) * std::cos(dRa));

    // Parallactic angle at the configured observing site.
    const double phi = jw.W * kDeg;
    const double lst = pGST2(t * 36525.0) + jw.J * kDeg;
    const double H = lst - mRa;
    const double q = std::atan2(
        std::sin(H),
        std::tan(phi) * std::cos(mDec) - std::sin(mDec) * std::cos(H));

    double deg = -(chi - q) / kDeg;
    while (deg < 0.0)    deg += 360.0;
    while (deg >= 360.0) deg -= 360.0;
    return deg;
}

void Scene::update() {
    if (scale_.logDistance != lastLog_ || scale_.linearAUtoWorld != lastLinear_ ||
        scale_.logK != lastLogK_) {
        rebuildOrbits();
    }
    double t = jdToCenturiesTD(clock_.jd);
    const double dtDay = 1.0 / 36525.0; // one day in centuries

    double earth[3];
    heliocentricAU(0, t, -1, earth);

    // ---- Moon phase & 3D position ------------------------------------------
    {
        // m_coord → geocentric ecliptic (L_rad, B_rad, R_km)
        mystl::array3 mllr = m_coord(t, -1, -1, -1);
        double mL = mllr[0], mB = mllr[1];

        // Geocentric unit vector toward Moon (ecliptic)
        double cB = std::cos(mB);
        double mDir[3] = { cB*std::cos(mL), cB*std::sin(mL), std::sin(mB) };

        // Sun geocentric ecliptic longitude = earth heliocentric lon + π
        double sunLon = std::atan2(earth[1], earth[0]) + kPi;

        // Elongation in [0, 2π)
        double elong = mL - sunLon;
        while (elong <  0.0)      elong += 2.0*kPi;
        while (elong >= 2.0*kPi)  elong -= 2.0*kPi;

        moon_.elongationDeg = elong * 180.0 / kPi;
        moon_.ageDays       = elong / (2.0*kPi) * 29.53059;
        moon_.waxing        = (elong < kPi);
        moon_.illumination  = (1.0 - std::cos(elong)) * 0.5;

        const double deg = moon_.elongationDeg;
        if      (deg <   8.0) moon_.phaseName = "朔(新月)";
        else if (deg <  82.0) moon_.phaseName = "蛾眉月";
        else if (deg <  98.0) moon_.phaseName = "上弦月";
        else if (deg < 172.0) moon_.phaseName = "盈凸月";
        else if (deg < 188.0) moon_.phaseName = "望(满月)";
        else if (deg < 262.0) moon_.phaseName = "亏凸月";
        else if (deg < 278.0) moon_.phaseName = "下弦月";
        else if (deg < 352.0) moon_.phaseName = "残月";
        else                  moon_.phaseName = "朔(新月)";

        moon_.brightLimbAngleDeg = brightLimbAngle(t, mL, mB, sunLon);

        // 3D: place Moon near Earth. Same ecliptic→world mapping as toWorld():
        // X→X, Z→Y, Y→−Z (negated so the mapping stays a proper rotation).
        gx::Vec3 earthWorld = toWorld(earth);
        float off = scale_.sizeScale * 1.55f;
        moon_.worldPos = {
            earthWorld.x + (float)mDir[0]*off,
            earthWorld.y + (float)mDir[2]*off,
            earthWorld.z - (float)mDir[1]*off
        };
        moon_.displayRadius = scale_.sizeScale * 0.075f;

        // Synchronous rotation: IAU W rate 13.17635815 deg/day inverts to the
        // 27.32-day sidereal month, so the same face keeps pointing at Earth.
        // Full physical libration model (see solveMoonOrientation) — the
        // periodic terms are what give the pole its real 1.5 deg Cassini
        // tilt, not decoration on top of an already-correct mean pole. Note
        // the Moon is drawn at an exaggerated offset from Earth, so the tidal
        // lock is only correct in absolute orientation, not aimed at the
        // Earth sphere's drawn position.
        Orientation mo = solveMoonOrientation(clock_.jd - kJ2000);
        moon_.spinDeg      = mo.spinDeg;
        moon_.axialTiltDeg = mo.axialTiltDeg;
        moon_.poleNodeDeg  = mo.poleNodeDeg;
        moon_.poleDir      = mo.poleDir;
        moon_.valid = true;
    }

    for (size_t i = 0; i < info_.size(); ++i) {
        BodyState& s = state_[i];
        double xyz[3];
        heliocentricAU(info_[i].xt, t, -1, xyz);
        s.helioXYZ[0] = xyz[0]; s.helioXYZ[1] = xyz[1]; s.helioXYZ[2] = xyz[2];
        s.world = toWorld(xyz);

        double r = std::sqrt(xyz[0] * xyz[0] + xyz[1] * xyz[1] + xyz[2] * xyz[2]);
        s.R = r;
        double L = std::atan2(xyz[1], xyz[0]);
        if (L < 0) L += 2 * kPi;
        s.L = L * 180.0 / kPi;
        s.B = (r > 1e-9 ? std::asin(xyz[2] / r) : 0.0) * 180.0 / kPi;

        double dx = xyz[0] - earth[0], dy = xyz[1] - earth[1], dz = xyz[2] - earth[2];
        s.geoDistAU = std::sqrt(dx * dx + dy * dy + dz * dz);

        if (info_[i].isSun) {
            s.speedDegPerDay = 0.0;
        } else {
            double xyz2[3];
            heliocentricAU(info_[i].xt, t + dtDay, 10, xyz2);
            double L2 = std::atan2(xyz2[1], xyz2[0]) * 180.0 / kPi;
            double dL = L2 - L * 180.0 / kPi;
            while (dL > 180) dL -= 360;
            while (dL < -180) dL += 360;
            s.speedDegPerDay = dL;
        }

        s.displayRadius = scale_.sizeScale * 0.48f *
            (float)std::pow(info_[i].realRadiusKm / 6371.0, 0.30);
        if (info_[i].isSun)
            s.displayRadius = scale_.sizeScale * 2.20f;

        // ---- Self-rotation, from IAU rotational elements --------------------
        // The renderer first maps a body's mesh into a canonical geographic
        // frame (+Y = north pole, +Z = prime meridian, +X = 90° east), so what
        // is solved here is pure astronomy, independent of how any mesh happens
        // to be modelled.
        //
        // Pole (a0,d0) and prime meridian W come in ICRF equatorial J2000
        // coordinates, so both are rotated into ecliptic and then into world
        // space, and the render angles are read back off the result. Verified
        // for Earth against the true subsolar point over 2025-2030 (max 0.02°).
        Orientation o = (info_[i].xt == 0) ? solveEarthOrientation(t)
                                           : solveOrientation(info_[i].rot, t);
        s.spinDeg = o.spinDeg;
        s.axialTiltDeg = o.axialTiltDeg;
        s.poleNodeDeg = o.poleNodeDeg;
        s.poleDir = o.poleDir;
    }

    for (size_t i = 0; i < asteroidInfo_.size(); ++i) {
        asteroidState_[i].world = asteroidWorldAU(asteroidInfo_[i], clock_.jd, *this);
        asteroidState_[i].displaySize = asteroidInfo_[i].displaySize;
        asteroidState_[i].brightness = asteroidInfo_[i].brightness;
    }
}

} // namespace sx
