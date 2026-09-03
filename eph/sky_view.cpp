#include "sky_view.h"

#include <cmath>

#include "eph0.h"
#include "eph_msc.h"

namespace
{

// Local horizon frame: x north, y east, z zenith. Everything below works in
// direction vectors rather than angles, so the geometry stays well behaved
// near the zenith and across the north point.
// A plain aggregate: the engine builds as C++11, where a member initializer
// would cost this type its brace initialization.
struct Vec3
{
    double x, y, z;
};

Vec3 fromAzAlt(double az, double alt)
{
    double c = std::cos(alt);
    return {c * std::cos(az), c * std::sin(az), std::sin(alt)};
}

double dot(const Vec3 &a, const Vec3 &b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

Vec3 cross(const Vec3 &a, const Vec3 &b)
{
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

Vec3 normalize(const Vec3 &a)
{
    double l = std::sqrt(dot(a, a));
    if (l < 1e-12) return {0.0, 0.0, 1.0};
    return {a.x / l, a.y / l, a.z / l};
}

// Equatorial to horizon, for places that have no parallax correction of their
// own - the Moon's geocentric position and the antisolar point. `gst` is the
// true sidereal time MSC has already computed; H is the hour angle.
Vec3 equatorialToHorizon(double ra, double dec, double gst, double lon, double lat)
{
    double H = gst + lon - ra;
    double cd = std::cos(dec), sd = std::sin(dec);
    double ch = std::cos(H), sh = std::sin(H);
    double cl = std::cos(lat), sl = std::sin(lat);
    Vec3 v;
    v.x = -ch * cd * sl + sd * cl; // north
    v.y = -sh * cd;                // east
    v.z = ch * cd * cl + sd * sl;  // up
    return v;
}

// Gnomonic offsets of `v` around `centre`, in degrees: +x to the observer's
// right when facing `centre`, +y towards the zenith. Over the degree or two an
// eclipse spans this is a faithful flat picture of the sky.
void tangentOffset(const Vec3 &centre, const Vec3 &v, double &dxDeg, double &dyDeg)
{
    Vec3 u = normalize(centre);
    Vec3 zenith{0.0, 0.0, 1.0};
    Vec3 right = cross(zenith, u);
    if (dot(right, right) < 1e-12) right = {0.0, 1.0, 0.0}; // looking straight up
    right = normalize(right);
    Vec3 up = cross(u, right);
    double along = dot(v, u);
    if (along < 1e-6) { dxDeg = dyDeg = 0.0; return; } // behind the observer
    dxDeg = std::atan(dot(v, right) / along) * radd;
    dyDeg = std::atan(dot(v, up) / along) * radd;
}

double angleBetween(const Vec3 &a, const Vec3 &b)
{
    double c = dot(normalize(a), normalize(b));
    if (c > 1.0) c = 1.0;
    if (c < -1.0) c = -1.0;
    return std::acos(c);
}

// Area two overlapping discs share, in units of r1's area. r1 is the disc being
// covered; the answer is the covered fraction of it.
double coveredFraction(double r1, double r2, double d)
{
    if (r1 <= 0.0) return 0.0;
    if (d >= r1 + r2) return 0.0;
    if (d <= r2 - r1) return 1.0;                 // wholly covered
    if (d <= r1 - r2) return (r2 * r2) / (r1 * r1); // annular: the ring is left
    double a1 = (d * d + r1 * r1 - r2 * r2) / (2.0 * d * r1);
    double a2 = (d * d + r2 * r2 - r1 * r1) / (2.0 * d * r2);
    if (a1 > 1.0) a1 = 1.0; if (a1 < -1.0) a1 = -1.0;
    if (a2 > 1.0) a2 = 1.0; if (a2 < -1.0) a2 = -1.0;
    double t1 = std::acos(a1), t2 = std::acos(a2);
    double area = r1 * r1 * (t1 - std::sin(2.0 * t1) / 2.0) +
                  r2 * r2 * (t2 - std::sin(2.0 * t2) / 2.0);
    double frac = area / (_pi * r1 * r1);
    if (frac < 0.0) frac = 0.0;
    if (frac > 1.0) frac = 1.0;
    return frac;
}

double clamp01(double v) { return v < 0.0 ? 0.0 : (v > 1.0 ? 1.0 : v); }

} // namespace

SkyView computeSkyView(double jdTd, double lonDeg, double latDeg, double altitudeKm)
{
    SkyView out;
    out.jdTd = jdTd;

    const double lon = lonDeg / radd;
    const double lat = latDeg / radd;
    MSC::calc(jdTd, lon, lat, altitudeKm);
    out.valid = true;

    out.sun.valid = true;
    out.sun.azimuthDeg = MSC::sPJ * radd;
    out.sun.altitudeDeg = MSC::sPW * radd;
    out.sun.radiusDeg = MSC::sRad / 3600.0;

    out.moon.valid = true;
    out.moon.azimuthDeg = MSC::mPJ * radd;
    out.moon.altitudeDeg = MSC::mPW * radd;
    out.moon.radiusDeg = MSC::mRad / 3600.0;
    out.moonIllum = MSC::mIll;

    // ---- Solar geometry, from the unrefracted topocentric places ----------
    Vec3 sunDir = fromAzAlt(MSC::sDJ, MSC::sDW);
    Vec3 moonDir = fromAzAlt(MSC::mDJ, MSC::mDW);
    tangentOffset(sunDir, moonDir, out.moonDx, out.moonDy);
    out.separationDeg = angleBetween(sunDir, moonDir) * radd;

    const double rs = out.sun.radiusDeg, rm = out.moon.radiusDeg;
    const double d = out.separationDeg;
    if (rs > 0.0)
    {
        // Not clamped at the top: a magnitude over 1 is the useful number for
        // a total eclipse - it says by how much the Moon's disc oversails the
        // Sun's, and so how long totality lasts.
        double mag = (rs + rm - d) / (2.0 * rs);
        out.magnitude = mag > 0.0 ? mag : 0.0;
        out.obscuration = coveredFraction(rs, rm, d);
        out.total = (d <= rm - rs);
        out.annular = (d <= rs - rm);
        // Once the Moon is wholly inside the Sun's disc, the covered fraction of
        // a diameter stops meaning anything - it only grows as the ring gets
        // more even. The magnitude quoted for an annular eclipse is the ratio of
        // the two apparent diameters instead, which is what the eclipse search
        // reports and what an observer would compare the ring against.
        if (out.annular && rs > 0.0) out.magnitude = rm / rs;
    }

    // ---- Lunar geometry: Earth's shadow at the Moon's distance ------------
    // The shadow's axis runs from the Sun through the Earth, so its centre in
    // the sky is the antisolar point. Moon and shadow are both taken
    // geocentrically: a lunar eclipse looks the same from everywhere the Moon
    // is up, and mixing a topocentric Moon with a geocentric shadow would slide
    // one against the other by up to a degree.
    Vec3 moonGeo = equatorialToHorizon(MSC::mCJ, MSC::mCW, MSC::gst, lon, lat);
    Vec3 shadowGeo = equatorialToHorizon(MSC::sCJ + _pi, -MSC::sCW, MSC::gst, lon, lat);
    tangentOffset(moonGeo, shadowGeo, out.shadowDx, out.shadowDy);
    out.umbraRadiusDeg = MSC::eShadow / 3600.0;
    out.penumbraRadiusDeg = MSC::eShadow2 / 3600.0;
    {
        double sep = angleBetween(moonGeo, shadowGeo) * radd;
        double rMoonGeo = MSC::e_mRad / 3600.0;
        if (rMoonGeo > 0.0)
        {
            double um = (out.umbraRadiusDeg + rMoonGeo - sep) / (2.0 * rMoonGeo);
            double pm = (out.penumbraRadiusDeg + rMoonGeo - sep) / (2.0 * rMoonGeo);
            out.umbralMagnitude = um > 0.0 ? um : 0.0;
            out.penumbralMagnitude = pm > 0.0 ? pm : 0.0;
        }
    }

    // ---- Sky brightness ----------------------------------------------------
    // Photometrically the daylight left is simply the uncovered fraction, but
    // that is not what anyone sees: the eye keeps up until the Sun is nearly
    // gone and the last percent is where the light collapses. The exponent
    // bends the curve to match, and the last stretch is forced to the floor so
    // that totality reads as night rather than as a dim afternoon.
    double open = 1.0 - out.obscuration;
    out.daylight = std::pow(clamp01(open), 0.30);
    if (out.obscuration > 0.985)
    {
        double t = clamp01((out.obscuration - 0.985) / 0.015);
        out.daylight *= (1.0 - t);
    }

    return out;
}

SubPoint subLunarPoint(double jdTd)
{
    SubPoint out;
    // The observer does not matter here - only the sidereal time does - so the
    // calculation is run from the prime meridian and the Moon's geocentric
    // right ascension is turned straight into a longitude.
    MSC::calc(jdTd, 0.0, 0.0, 0.0);
    double lon = MSC::mCJ - MSC::gst;
    lon = std::fmod(lon, pi2);
    if (lon > _pi) lon -= pi2;
    if (lon < -_pi) lon += pi2;
    out.lonDeg = lon * radd;
    out.latDeg = MSC::mCW * radd;
    out.valid = true;
    return out;
}
