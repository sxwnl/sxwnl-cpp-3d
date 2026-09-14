// Checks the moon-phase geometry against published worked examples.
//
// The bright-limb position angle decides which way a crescent tilts, and a sign
// slip there is invisible in code review but glaring on screen. Meeus,
// Astronomical Algorithms, chapter 48, works the same numbers through by hand,
// so the formula is pinned to his results rather than to my own arithmetic.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <initializer_list>
#include "../gui/mesh_frames.h"
#include "../gui/mathx.h"

namespace {

const double kPi = 3.14159265358979323846;
const double kDeg = kPi / 180.0;

// Meeus (48.5): position angle of the midpoint of the bright limb, measured
// from the celestial north pole towards east.
double brightLimbChi(double sRa, double sDec, double mRa, double mDec) {
    const double dRa = sRa - mRa;
    double chi = std::atan2(
        std::cos(sDec) * std::sin(dRa),
        std::sin(sDec) * std::cos(mDec) -
            std::cos(sDec) * std::sin(mDec) * std::cos(dRa));
    chi /= kDeg;
    while (chi < 0.0) chi += 360.0;
    return chi;
}

// Meeus (48.1/48.3): illuminated fraction from the elongation.
double illuminatedFraction(double sRa, double sDec, double mRa, double mDec,
                           double sunDistKm, double moonDistKm) {
    const double cosPsi = std::sin(sDec) * std::sin(mDec) +
                          std::cos(sDec) * std::cos(mDec) * std::cos(sRa - mRa);
    const double psi = std::acos(cosPsi);
    const double i = std::atan2(sunDistKm * std::sin(psi),
                                moonDistKm - sunDistKm * std::cos(psi));
    return (1.0 + std::cos(i)) / 2.0;
}

int failures = 0;

void expectNear(const char* what, double got, double want, double tol) {
    const double diff = std::fabs(got - want);
    const bool ok = diff <= tol;
    if (!ok) ++failures;
    std::printf("%-28s got %10.4f  want %10.4f  (tol %.4f)  %s\n",
                what, got, want, tol, ok ? "ok" : "FAIL");
}

} // namespace

// Mirrors the two convex pieces panels.cpp paints, and returns the lit area as
// a fraction of the disc. If the terminator's sign or the dark/lit assignment
// ever flips, the fraction stops tracking the illumination and this catches it -
// which the eye cannot reliably do for a thin crescent.
double drawnLitFraction(double illum) {
    const int N = 512;
    const double r = 1.0;
    const double tx = r * (1.0 - 2.0 * illum);

    auto arcArea = [&](double semiX) {
        // Shoelace over the open arc plus its closing diameter.
        double area = 0.0;
        double px = 0.0, py = -r;
        for (int i = 1; i <= N; ++i) {
            const double t = kPi * (double)i / N;
            const double x = semiX * std::sin(t);
            const double y = -r * std::cos(t);
            area += px * y - x * py;
            px = x; py = y;
        }
        area += px * (-r) - 0.0 * py;   // close back to the first point
        return area / 2.0;
    };

    const double half = arcArea(r);              // lit hemisphere
    const double lens = arcArea(tx);             // terminator half-ellipse
    // tx > 0: the lens is painted dark and removes area; tx < 0 it is painted
    // lit and adds. arcArea carries the sign of semiX, so it is just a sum.
    return (half - lens) / (kPi * r * r);
}

// Mirrors the camera and light that Renderer::renderMoonPhase sets up, and
// returns the fraction of the visible disc the Sun lights, sampled on a grid.
//
// The 3-D view sat on a 42-degree perspective camera 3.05 radii out for a long
// time. A sphere seen that close hides a band of its own limb and squeezes what
// is left of it towards the edge of the disc, and a crescent is nothing but
// limb: at 11.9% illumination the render put 4.0% of the disc in sunlight and
// drew it less than half as wide as the 2-D disc beside it. The Moon is 0.52
// degrees across from Earth, so the honest camera is an orthographic one --
// with it, the lit fraction lands on Meeus's k at every phase, which is what
// this pins down.
double renderedLitFraction(double elongDeg, bool orthographic) {
    const double elong = elongDeg * kDeg;
    // Sun direction, in the plane the elongation is measured in. The camera
    // stands at +Z, so the Sun is at -Z at new moon and behind the eye at full.
    const double lx =  std::sin(elong), ly = 0.0, lz = -std::cos(elong);

    const double R = 0.96;                 // mesh is normalised to unit radius
    const double d      = orthographic ? 120.0 : 3.05;
    const double frame  = R * 1.11;        // ortho half-extent
    const double tanH   = std::tan(21.0 * kDeg);  // half of the old 42-deg fov

    const int N = 900;
    double lit = 0.0, disc = 0.0;
    for (int j = 0; j < N; ++j) {
        for (int i = 0; i < N; ++i) {
            const double nx = ((i + 0.5) / N) * 2.0 - 1.0;
            const double ny = ((j + 0.5) / N) * 2.0 - 1.0;
            double ox, oy, oz, dx, dy, dz;
            if (orthographic) {
                ox = nx * frame; oy = ny * frame; oz = d;
                dx = 0.0; dy = 0.0; dz = -1.0;
            } else {
                ox = 0.0; oy = 0.0; oz = d;
                const double n = std::sqrt(nx*nx*tanH*tanH + ny*ny*tanH*tanH + 1.0);
                dx = nx * tanH / n; dy = ny * tanH / n; dz = -1.0 / n;
            }
            // Nearest intersection with the sphere at the origin.
            const double b = ox*dx + oy*dy + oz*dz;
            const double c = ox*ox + oy*oy + oz*oz - R*R;
            const double h = b*b - c;
            if (h <= 0.0) continue;
            const double t = -b - std::sqrt(h);
            if (t <= 0.0) continue;
            const double px = ox + dx*t, py = oy + dy*t, pz = oz + dz*t;
            disc += 1.0;
            if ((px*lx + py*ly + pz*lz) / R > 0.0) lit += 1.0;  // Sun above the
        }                                                      // local horizon
    }
    return disc > 0.0 ? lit / disc : 0.0;
}

// Where meshAxisFixFor("moon") sends a direction in the mesh's own space, read
// back as selenographic latitude/longitude.
void moonMeshDirToLatLon(double x, double y, double z, double& lat, double& lon) {
    const gx::Mat4 m = sx::meshAxisFixFor("moon");
    // column-major, so column j is m[j*4 + i]
    const double bx = m.m[0]*x + m.m[4]*y + m.m[8]*z;
    const double by = m.m[1]*x + m.m[5]*y + m.m[9]*z;
    const double bz = m.m[2]*x + m.m[6]*y + m.m[10]*z;
    lat = std::asin(std::max(-1.0, std::min(1.0, by))) / kDeg;
    lon = std::atan2(bx, bz) / kDeg;          // +Z prime meridian, +X 90 east
}

// The reflectance kFS_lit computes for the Moon: the lunar-Lambert law of
// McEwen (1991), Lommel-Seeliger mixed against Lambert by weight L.
//
// Mirrors the shader rather than linking it -- the real one is GLSL. It is
// three lines, and the whole point is that they are the right three: the
// version this replaced carried an extra mu0 on the Lommel-Seeliger term,
// which is invisible in the formula and ruinous on screen.
double lunarLambert(double mu0, double muv, double L) {
    mu0 = std::max(mu0, 0.0);
    muv = std::max(muv, 0.0);
    const double ls = 2.0 * mu0 / std::max(mu0 + muv, 1e-4);
    return mu0 + (ls - mu0) * L;
}

// Libration and the position angle of the Moon's axis (Meeus ch. 53).
// Mirrors scene.cpp's moonPhysical(); the series are the point of the check,
// and Scene cannot be linked without a GL build.
struct Libration { double l, b, P; };

Libration moonLibration(double T, double lamDeg, double betDeg, double dpsiDeg,
                        double epsRad, double alphaDeg) {
    auto sd = [](double d) { return std::sin(d * kDeg); };
    auto cd = [](double d) { return std::cos(d * kDeg); };
    const double T2 = T*T, T3 = T2*T, T4 = T3*T;
    const double D  = 297.8501921 + 445267.1114034*T - 0.0018819*T2 + T3/545868.0 - T4/113065000.0;
    const double M  = 357.5291092 + 35999.0502909*T - 0.0001536*T2 + T3/24490000.0;
    const double Mp = 134.9633964 + 477198.8675055*T + 0.0087414*T2 + T3/69699.0 - T4/14712000.0;
    const double F  = 93.2720950 + 483202.0175233*T - 0.0036539*T2 - T3/3526000.0 + T4/863310000.0;
    const double Om = 125.0445479 - 1934.1362891*T + 0.0020754*T2 + T3/467441.0 - T4/60616000.0;
    const double Ecc = 1.0 - 0.002516*T - 0.0000074*T2;
    const double K1 = 119.75 + 131.849*T, K2 = 72.56 + 20.186*T;

    const double I = 1.54242;
    const double W = lamDeg - dpsiDeg - Om;
    const double cb = cd(betDeg), sb = sd(betDeg);
    const double A  = std::atan2(sd(W)*cb*cd(I) - sb*sd(I), cd(W)*cb) / kDeg;
    double lp = A - F;
    while (lp < -180.0) lp += 360.0;
    while (lp >= 180.0) lp -= 360.0;
    const double bp = std::asin(std::max(-1.0, std::min(1.0,
                          -sd(W)*cb*sd(I) - sb*cd(I)))) / kDeg;

    const double rho = -0.02752*cd(Mp) -0.02245*sd(F) +0.00684*cd(Mp-2*F)
                     -0.00293*cd(2*F) -0.00085*cd(2*F-2*D) -0.00054*cd(Mp-2*D)
                     -0.00020*sd(Mp+F) -0.00020*cd(Mp+2*F) -0.00020*cd(Mp-F)
                     +0.00014*cd(Mp+2*F-2*D);
    const double sig = -0.02816*sd(Mp) +0.02244*cd(F) -0.00682*sd(Mp-2*F)
                     -0.00279*sd(2*F) -0.00083*sd(2*F-2*D) +0.00069*sd(Mp-2*D)
                     +0.00040*cd(Mp+F) -0.00025*sd(2*Mp) -0.00023*sd(Mp+2*F)
                     +0.00020*cd(Mp-F) +0.00019*sd(Mp-F) +0.00013*sd(Mp+2*F-2*D)
                     -0.00010*cd(Mp-3*F);
    const double tau = +0.02520*Ecc*sd(M) +0.00473*sd(2*Mp-2*F) -0.00467*sd(Mp)
                     +0.00396*sd(K1) +0.00276*sd(2*Mp-2*D) +0.00196*sd(Om)
                     -0.00183*cd(Mp-F) +0.00115*sd(Mp-2*D) -0.00096*sd(Mp-D)
                     +0.00046*sd(2*F-2*D) -0.00039*sd(Mp-F) -0.00032*sd(Mp-M-D)
                     +0.00027*sd(2*Mp-M-2*D) +0.00023*sd(K2) -0.00014*sd(2*D)
                     +0.00014*cd(2*Mp-2*F) -0.00012*sd(Mp-2*F) -0.00012*sd(2*Mp)
                     +0.00011*sd(2*Mp-2*M-2*D);

    Libration out;
    out.l = lp + (-tau + (rho*cd(A) + sig*sd(A)) * std::tan(bp * kDeg));
    out.b = bp + (sig*cd(A) - rho*sd(A));

    const double V  = Om + dpsiDeg + sig/sd(I);
    const double Ir = (I + rho) * kDeg;
    const double X  = std::sin(Ir) * sd(V);
    const double Y  = std::sin(Ir) * cd(V) * std::cos(epsRad)
                    - std::cos(Ir) * std::sin(epsRad);
    const double om = std::atan2(X, Y);
    out.P = std::asin(std::max(-1.0, std::min(1.0,
                std::sqrt(X*X + Y*Y) * std::cos(alphaDeg*kDeg - om)
                / std::cos(out.b * kDeg)))) / kDeg;
    return out;
}

int main() {
    // Meeus example 48.1, 1992 April 12.0 TD.
    const double sRa  =  20.6579 * kDeg;
    const double sDec =   8.6964 * kDeg;
    const double mRa  = 134.6885 * kDeg;
    const double mDec =  13.7684 * kDeg;

    expectNear("chi (example 48.1)",
               brightLimbChi(sRa, sDec, mRa, mDec), 285.0, 0.1);

    expectNear("illuminated fraction",
               illuminatedFraction(sRa, sDec, mRa, mDec, 149971520.0, 368410.0),
               0.6786, 0.001);

    // A waxing crescent must light the side towards the Sun, and a waning one
    // the other side. Check the two hemispheres come out opposite.
    const double chiWaxing = brightLimbChi(0.0, 0.0, 45.0 * kDeg, 0.0);
    const double chiWaning = brightLimbChi(0.0, 0.0, -45.0 * kDeg, 0.0);
    expectNear("chi, moon east of sun", chiWaxing, 270.0, 0.5);
    expectNear("chi, moon west of sun", chiWaning,  90.0, 0.5);

    // The painted crescent must actually cover the illuminated fraction.
    for (double f : {0.02, 0.1, 0.238, 0.4, 0.5, 0.6, 0.75, 0.95}) {
        char label[48];
        std::snprintf(label, sizeof(label), "lit area, illum=%.3f", f);
        expectNear(label, drawnLitFraction(f), f, 0.001);
    }

    // Same check for the 3-D render: the sunlit part of the sphere the camera
    // can see has to be the illuminated fraction, at every phase.
    for (double elong : {40.4, 90.0, 140.0, 180.0, 280.0}) {
        const double k = (1.0 - std::cos(elong * kDeg)) / 2.0;
        char label[48];
        std::snprintf(label, sizeof(label), "3D lit area, elong=%.1f", elong);
        expectNear(label, renderedLitFraction(elong, true), k, 0.003);
    }

    // And the reason it is orthographic: the old perspective camera lost two
    // thirds of a thin crescent. Kept as a check on the check -- if this ever
    // stops being wrong, the measurement above has gone blind.
    {
        const double k = (1.0 - std::cos(40.4 * kDeg)) / 2.0;
        const double persp = renderedLitFraction(40.4, false);
        const bool ok = persp < k * 0.5;
        if (!ok) ++failures;
        std::printf("%-28s got %10.4f  want %10s  %s\n",
                    "perspective loses crescent", persp, "< 0.0595",
                    ok ? "ok" : "FAIL");
    }

    // The Moon mesh has to sit in the frame the rest of the code assumes:
    // +Y its north pole, +Z the centre of the near side. Nothing in the mesh
    // says so -- the fix is a measured constant -- so check it the only way
    // that is really a check, by pointing it at maria whose coordinates are
    // published and seeing where they land.
    //
    // The three directions below are in the mesh's own space, measured by
    // sampling resources/moon/Textures/Diffuse_2K.png through the mesh's own
    // UVs and clustering the mare-dark points; each is the centroid of an
    // isolated cluster. They are inputs here, independent of the matrix. A
    // 6-degree tolerance is what that measurement is worth: the atlas is a
    // cube net, the mesh carries 512 triangles, and a mare is a soft-edged
    // patch, not a point. It is still ten times tighter than the 48-degree
    // meridian error and the 79-degree pole error it would have caught.
    {
        struct MareCheck {
            const char* name;
            double mx, my, mz;      // direction in the mesh's own space
            double lat, lon;        // published selenographic coordinates
        };
        // Coordinates: IAU/USGS Gazetteer of Planetary Nomenclature.
        const MareCheck maria[] = {
            {"Mare Crisium",     +0.040909, +0.997206, -0.062506, +17.0,  +59.1},
            {"Mare Smythii",     +0.303477, +0.907252, +0.291197,  +1.3,  +87.5},
            {"Mare Moscoviense", +0.990884, +0.095332, +0.095191, +27.3, +147.9},
        };
        for (const MareCheck& m : maria) {
            double lat = 0.0, lon = 0.0;
            moonMeshDirToLatLon(m.mx, m.my, m.mz, lat, lon);
            char l1[56], l2[56];
            std::snprintf(l1, sizeof(l1), "%s lat", m.name);
            std::snprintf(l2, sizeof(l2), "%s lon", m.name);
            expectNear(l1, lat, m.lat, 6.0);
            double dlon = std::fmod(lon - m.lon + 540.0, 360.0) - 180.0;
            expectNear(l2, m.lon + dlon, m.lon, 6.0);
        }
    }

    // And it has to be a rotation: the renderer multiplies it into a model
    // matrix, so a scale or a reflection hiding in here would quietly resize
    // the Moon or mirror its map.
    {
        const gx::Mat4 m = sx::meshAxisFixFor("moon");
        auto col = [&](int j, int i) { return (double)m.m[j*4 + i]; };
        double worstDot = 0.0, worstLen = 0.0;
        for (int a = 0; a < 3; ++a) {
            double la = 0.0;
            for (int i = 0; i < 3; ++i) la += col(a,i) * col(a,i);
            worstLen = std::max(worstLen, std::fabs(std::sqrt(la) - 1.0));
            for (int b = a + 1; b < 3; ++b) {
                double d = 0.0;
                for (int i = 0; i < 3; ++i) d += col(a,i) * col(b,i);
                worstDot = std::max(worstDot, std::fabs(d));
            }
        }
        // det = c0 . (c1 x c2); +1 for a rotation, -1 if the map is mirrored.
        const double det =
            col(0,0)*(col(1,1)*col(2,2) - col(1,2)*col(2,1)) -
            col(1,0)*(col(0,1)*col(2,2) - col(0,2)*col(2,1)) +
            col(2,0)*(col(0,1)*col(1,2) - col(0,2)*col(1,1));
        expectNear("moon frame column norms", worstLen, 0.0, 1e-4);
        expectNear("moon frame orthogonality", worstDot, 0.0, 1e-4);
        expectNear("moon frame determinant", det, 1.0, 1e-4);
    }

    // A full moon is a flat disc, not a shaded ball: at zero phase angle the
    // Sun and the viewer are in the same place, so every point on the disc has
    // mu0 == mu, and Lommel-Seeliger returns 1 for all of them. This is the
    // check that catches a stray mu0 on that term -- with one, the law folds
    // back into Lambert for exactly this case and the limb goes to a seventh
    // of the centre, which is the one thing anyone who has looked at the Moon
    // would notice. Lambert is kept alongside as the contrast.
    {
        const double L = 0.92;   // kMoonLunarLambert in renderer.cpp
        double centre = lunarLambert(1.0, 1.0, L);
        expectNear("full moon, disc centre", centre, 1.0, 1e-6);
        for (double rr : {0.30, 0.60, 0.85, 0.95}) {
            const double mu = std::sqrt(1.0 - rr * rr);   // mu0 == muv here
            char label[48];
            std::snprintf(label, sizeof(label), "full moon, r/R=%.2f", rr);
            // Within 10% of disc centre all the way out to 0.95 of the radius.
            expectNear(label, lunarLambert(mu, mu, L) / centre, 1.0, 0.10);
        }
        // Lambert over the same span would be down to 0.31: that is the size of
        // the error, and the reason the law is worth carrying at all.
        expectNear("Lambert at r/R=0.95 (for contrast)",
                   lunarLambert(std::sqrt(1.0 - 0.95 * 0.95),
                                std::sqrt(1.0 - 0.95 * 0.95), 0.0),
                   0.312, 0.01);

        // And the crescent limb, the other end of the same law: the Sun well
        // round the side, the viewer looking along the surface. There mu goes
        // to 0 and Lommel-Seeliger goes to 2 whatever the incidence, so the
        // bright edge of a crescent is held up instead of fading -- here 2.9
        // times what Lambert would leave. This is the arc in a photograph.
        const double mu0 = std::cos((139.6 - 90.0) * kDeg);   // elongation 40.4
        expectNear("crescent limb, Lommel-Seeliger term",
                   2.0 * mu0 / mu0, 2.0, 1e-9);
        expectNear("crescent limb vs Lambert",
                   lunarLambert(mu0, 0.0, L) / mu0, 2.92, 0.05);
    }

    // Meeus example 53.a, 1992 April 12.0 TD -- the same instant as 48.1 above,
    // so alpha is the 134.6885 already checked there. The published answers are
    // l = -1.23, b = +4.20, P = 15.08. Tolerance is 0.02 deg: that is the
    // spread of the physical libration model itself, and 0.02 deg of an 8 deg
    // libration moves the rendered map by a quarter of a pixel at 512.
    {
        const Libration lib = moonLibration(-0.077221081451,
                                            133.162655, -3.229126, 0.004610,
                                            23.440636 * kDeg, 134.688470);
        expectNear("libration l (Meeus 53.a)", lib.l, -1.23, 0.02);
        expectNear("libration b (Meeus 53.a)", lib.b, +4.20, 0.02);
        expectNear("axis angle P (Meeus 53.a)", lib.P, 15.08, 0.02);
    }

    // Libration has to stay inside its physical envelope whenever it is asked
    // for, not just on the one day the book works through. Sampled every 6 h
    // for a year, the sub-Earth point must wander -- otherwise the model has
    // gone constant and the 3-D view is back to a dead-centre map -- and must
    // stay within the amplitudes the orbit allows.
    {
        double lMin = 999, lMax = -999, bMin = 999, bMax = -999;
        for (int i = 0; i < 1460; ++i) {
            const double T = (i * 0.25) / 36525.0;   // from J2000, quarter-days
            // A crude Moon is enough here, but it has to be an elliptical one:
            // libration in longitude is precisely the gap between where the
            // Moon actually is on its eccentric orbit and where its uniform
            // spin has got to, so a circular toy Moon would report none and the
            // envelope would pass while measuring nothing. The 6.289 deg
            // equation of the centre is the term that opens it.
            const double days = i * 0.25;
            const double Lp   = 218.3165 + 13.176396 * days;
            const double Mpr  = 134.9634 + 13.064993 * days;
            const double lam  = std::fmod(Lp + 6.289 * std::sin(Mpr * kDeg), 360.0);
            const double bet  = 5.13 * std::sin((93.272 + 13.229350 * days) * kDeg);
            const Libration lb = moonLibration(T, lam, bet, 0.0,
                                               23.4393 * kDeg, lam);
            lMin = std::min(lMin, lb.l); lMax = std::max(lMax, lb.l);
            bMin = std::min(bMin, lb.b); bMax = std::max(bMax, lb.b);
        }
        // Real libration reaches about +-8 in longitude and +-7 in latitude;
        // this toy orbit carries only the leading term of each, so ask for a
        // swing of 6 rather than the full amplitude.
        const bool moves = (lMax - lMin) > 6.0 && (bMax - bMin) > 6.0;
        const bool bounded = lMax < 12.0 && lMin > -12.0 && bMax < 12.0 && bMin > -12.0;
        if (!moves)   ++failures;
        if (!bounded) ++failures;
        std::printf("%-28s lon %+6.2f..%+6.2f  lat %+6.2f..%+6.2f  %s\n",
                    "libration envelope, 1 yr", lMin, lMax, bMin, bMax,
                    (moves && bounded) ? "ok" : "FAIL");
    }

    // The three turns renderMoonPhase composes for the real orientation have to
    // mean what they say, whatever the sign conventions of rotateX/Y/Z happen
    // to be. Two things define it: the sub-Earth point ends up dead centre
    // facing the camera, and the north pole projects at the screen angle Scene
    // measured. Check both by pushing selenographic directions through the same
    // product the renderer builds.
    {
        auto orientationFor = [](double libLon, double libLat, double axis) {
            const float r = (float)kDeg;
            return gx::rotateZ(-(float)axis * r)
                 * gx::rotateX((float)libLat * r)
                 * gx::rotateY(-(float)libLon * r);
        };
        // A selenographic (lon, lat) as a direction in the body frame that
        // mesh_frames.h defines: +Y north pole, +Z the centre of the near side.
        auto seleno = [](double lon, double lat) {
            return gx::Vec3{(float)(std::cos(lat*kDeg) * std::sin(lon*kDeg)),
                            (float)std::sin(lat*kDeg),
                            (float)(std::cos(lat*kDeg) * std::cos(lon*kDeg))};
        };
        for (double lon : {0.0, -7.5, 6.0}) {
            for (double lat : {0.0, 5.5, -6.5}) {
                for (double axis : {0.0, 24.0, -18.0, 340.0}) {
                    const gx::Mat4 R = orientationFor(lon, lat, axis);
                    // Sub-Earth point to the camera: +Z, so x and y vanish.
                    const gx::Vec3 c = gx::transformDir(R, seleno(lon, lat));
                    // Pole on screen at `axis` degrees clockwise from up.
                    const gx::Vec3 p = gx::transformDir(R, gx::Vec3{0, 1, 0});
                    double want = std::fmod(axis + 360.0, 360.0);
                    double got  = std::atan2(p.x, p.y) / kDeg;
                    got = std::fmod(got + 360.0, 360.0);
                    double dd = std::fmod(got - want + 540.0, 360.0) - 180.0;
                    const bool ok = std::fabs(c.x) < 1e-5 && std::fabs(c.y) < 1e-5 &&
                                    c.z > 0.999f && std::fabs(dd) < 1e-3;
                    if (!ok) {
                        ++failures;
                        std::printf("%-28s lon %+.1f lat %+.1f axis %+.1f -> "
                                    "centre (%+.4f,%+.4f,%+.4f) pole %.2f  FAIL\n",
                                    "orientation compose", lon, lat, axis,
                                    c.x, c.y, c.z, got);
                    }
                }
            }
        }
        std::printf("%-28s %-42s %s\n", "orientation compose",
                    "36 combinations of libration and axis angle",
                    failures == 0 ? "ok" : "see above");
    }

    std::printf(failures == 0 ? "\nALL OK\n" : "\n%d FAILURE(S)\n", failures);
    return failures == 0 ? 0 : 1;
}
