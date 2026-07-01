#include "scene.h"

#include <algorithm>
#include <cmath>
#include <ctime>

#include "../eph/eph0.h"
#include "../eph/eph.h"
#include "../mylib/tool.h"
#include "../mylib/mystl/static_array.h"

namespace sx {

static const double kPi = 3.14159265358979323846;
static const double kJ2000 = 2451545.0;
static const double kDeg = kPi / 180.0;

double nowJD() {
    time_t tt = time(nullptr);
    struct tm* lt = localtime(&tt);
    Date d{lt->tm_year + 1900, lt->tm_mon + 1, lt->tm_mday,
           lt->tm_hour, lt->tm_min, (double)lt->tm_sec};
    return toJD(d);
}

double jdToCenturiesTD(double jd) { return (jd - kJ2000) / 36525.0; }

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
    info_ = {
        {-1, "太阳",   "sun",     {1.00f, 0.85f, 0.30f}, 696000.0, 0.0,      true},
        { 1, "水星",   "mercury", {0.70f, 0.70f, 0.72f},   2440.0, 0.2408467, false},
        { 2, "金星",   "venus",   {0.90f, 0.75f, 0.45f},   6052.0, 0.6151973, false},
        { 0, "地球",   "earth",   {0.30f, 0.55f, 0.95f},   6371.0, 1.0000174, false},
        { 3, "火星",   "mars",    {0.85f, 0.40f, 0.25f},   3390.0, 1.8808476, false},
        { 4, "木星",   "jupiter", {0.85f, 0.70f, 0.55f},  69911.0, 11.862615, false},
        { 5, "土星",   "saturn",  {0.90f, 0.82f, 0.60f},  58232.0, 29.447498, false},
        { 6, "天王星", "uranus",  {0.60f, 0.85f, 0.90f},  25362.0, 84.016846, false},
        { 7, "海王星", "neptune", {0.30f, 0.45f, 0.95f},  24622.0, 164.79132, false},
        { 8, "冥王星", "pluto",   {0.75f, 0.65f, 0.55f},   1188.0, 247.92065, false},
    };
    state_.resize(info_.size());
    buildAsteroidBelt(asteroidInfo_);
    asteroidState_.resize(asteroidInfo_.size());
    clock_.jd = nowJD();
    rebuildOrbits();
    update();
}

gx::Vec3 Scene::toWorld(const double a[3]) const {
    double r = std::sqrt(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]);
    if (r < 1e-9) return {0, 0, 0};
    double mapped = scale_.logDistance ? scale_.logK * std::log(1.0 + r)
                                       : scale_.linearAUtoWorld * r;
    double s = mapped / r;
    // ecliptic (X=equinox, Y=in-plane, Z=north) -> world (Y up, ecliptic = XZ)
    return {(float)(a[0] * s), (float)(a[2] * s), (float)(a[1] * s)};
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

        // 3D: place Moon near Earth (ecliptic→world coord swap: X→X, Y→Z, Z→Y)
        gx::Vec3 earthWorld = toWorld(earth);
        float off = scale_.sizeScale * 1.55f;
        moon_.worldPos = {
            earthWorld.x + (float)mDir[0]*off,
            earthWorld.y + (float)mDir[2]*off,
            earthWorld.z + (float)mDir[1]*off
        };
        moon_.displayRadius = scale_.sizeScale * 0.075f;
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

        // ---- Self-rotation (Earth only for now) ----------------------------
        // Earth spins prograde (eastward) about a polar axis tilted by the
        // mean obliquity ε from the ecliptic normal. In our world frame the
        // ecliptic normal is +Y and the equinox is +X, so a rotateX(ε) tilts
        // the pole toward +Z (ecliptic longitude 90°) — the true direction of
        // the north celestial pole. The spin angle is tied to Greenwich Mean
        // Sidereal Time so the texture's meridian tracks a real sidereal rate
        // (~360.9856°/day) rather than an arbitrary one.
        if (info_[i].xt == 0) {
            // Mean obliquity of the ecliptic at the current epoch (deg).
            double T  = t; // Julian centuries from J2000 (TD ≈ UT at this scale)
            double eps = 23.4392911 - 0.0130041667 * T
                       - 1.638889e-7 * T * T + 5.036111e-7 * T * T * T;

            // Greenwich Mean Sidereal Time (deg), reduced to [0,360).
            double du = clock_.jd - kJ2000;
            double gmst = 280.46061837 + 360.98564736629 * du
                        + 0.000387933 * T * T - T * T * T / 38710000.0;
            gmst = std::fmod(gmst, 360.0);
            if (gmst < 0.0) gmst += 360.0;

            // kEarthTexLon0 offsets the texture so its Greenwich meridian lines
            // up with the daymap's prime meridian (texture-convention constant).
            // Standard equirectangular daymaps have u=0 at -180° longitude,
            // so u=0.5 = 0° (Greenwich). The spin rotates the texture eastward;
            // GMST counts eastward from the vernal equinox to Greenwich, so we
            // need Greenwich to face the sun-direction at solar noon.
            // rotateY sign convention: our rotateY(+a) rotates +X toward -Z,
            // but Earth's prograde spin rotates +X toward +Z (east), so we negate.
            // The +180 aligns the u=0 seam (at ±180° lon) with the starting frame.
            const double kEarthTexLon0 = 180.0;
            s.spinDeg      = (float)(-(gmst) + kEarthTexLon0);
            s.axialTiltDeg = (float)eps;
        } else {
            s.spinDeg      = 0.0f;
            s.axialTiltDeg = 0.0f;
        }
    }

    for (size_t i = 0; i < asteroidInfo_.size(); ++i) {
        asteroidState_[i].world = asteroidWorldAU(asteroidInfo_[i], clock_.jd, *this);
        asteroidState_[i].displaySize = asteroidInfo_[i].displaySize;
        asteroidState_[i].brightness = asteroidInfo_[i].brightness;
    }
}

} // namespace sx
