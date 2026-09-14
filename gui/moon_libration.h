// Lunar libration and the position angle of the Moon's axis.
//
// Meeus, Astronomical Algorithms, chapter 53 (the physical-libration series are
// Eckhardt 1981). Header-only and free of any GL dependency, so Scene and
// moonphase_test run the same code rather than a copy each: a regression test
// against a second implementation of the same series can only catch a typo in
// the copy it is holding.
//
// Chapter 53 rather than the IAU rotation model in Scene::solveMoonOrientation,
// for a frame reason. The IAU elements are ICRF, while m_coord returns the mean
// ecliptic and equinox of date and hcjj measures the obliquity of the same;
// bridging those needs precession, and over the millennia this ephemeris spans
// that is not a small correction. Chapter 53 is written entirely in of-date
// terms and drops straight into the frame the rest of this code already uses.
#ifndef SXWNL_GUI_MOON_LIBRATION_H
#define SXWNL_GUI_MOON_LIBRATION_H

#include <algorithm>
#include <cmath>

namespace sx {

struct MoonLibration {
    double lonDeg    = 0.0;   // selenographic longitude of the sub-Earth point
    double latDeg    = 0.0;   // and its latitude
    double axisPADeg = 0.0;   // P, from celestial north towards east
};

// T              Julian centuries from J2000.0 (TD)
// lambdaDeg      geocentric ecliptic longitude of the Moon
// betaDeg        geocentric ecliptic latitude
// nutationLonDeg nutation in longitude
// epsRad         obliquity of the ecliptic
// alphaDeg       right ascension of the Moon
//
// lambda and alpha have to be in one frame and nutationLonDeg has to name it.
// Meeus works in apparent place and passes the real nutation; Scene passes the
// mean place m_coord returns, and 0 with it. Nutation is an argument rather
// than a constant precisely so that neither caller can drift into using the
// other's convention without saying so.
inline MoonLibration moonLibration(double T, double lambdaDeg, double betaDeg,
                                   double nutationLonDeg, double epsRad,
                                   double alphaDeg) {
    const double kDeg = 3.14159265358979323846 / 180.0;
    auto sd = [kDeg](double deg) { return std::sin(deg * kDeg); };
    auto cd = [kDeg](double deg) { return std::cos(deg * kDeg); };

    // Fundamental arguments of the lunar theory (ch. 47), degrees.
    const double T2 = T * T, T3 = T2 * T, T4 = T3 * T;
    const double D  = 297.8501921 + 445267.1114034 * T - 0.0018819 * T2
                    + T3 / 545868.0 - T4 / 113065000.0;
    const double M  = 357.5291092 + 35999.0502909 * T - 0.0001536 * T2
                    + T3 / 24490000.0;
    const double Mp = 134.9633964 + 477198.8675055 * T + 0.0087414 * T2
                    + T3 / 69699.0 - T4 / 14712000.0;
    const double F  = 93.2720950 + 483202.0175233 * T - 0.0036539 * T2
                    - T3 / 3526000.0 + T4 / 863310000.0;
    const double Om = 125.0445479 - 1934.1362891 * T + 0.0020754 * T2
                    + T3 / 467441.0 - T4 / 60616000.0;
    const double Ecc = 1.0 - 0.002516 * T - 0.0000074 * T2;   // ch. 47 E
    const double K1 = 119.75 + 131.849 * T;
    const double K2 =  72.56 +  20.186 * T;

    // Optical libration: where the Earth sits over a Moon whose equator is
    // tilted I to the ecliptic. This is the large part, +-8 deg in longitude
    // and +-7 in latitude, and it is why we see 59% of the surface over a month
    // rather than exactly half: the orbit is eccentric while the spin is
    // uniform, so the Moon runs ahead of and behind its own mean rotation.
    const double I  = 1.54242;
    const double W  = lambdaDeg - nutationLonDeg - Om;
    const double cb = cd(betaDeg), sb = sd(betaDeg);
    const double A  = std::atan2(sd(W) * cb * cd(I) - sb * sd(I), cd(W) * cb) / kDeg;
    double lp = A - F;
    while (lp < -180.0) lp += 360.0;
    while (lp >= 180.0) lp -= 360.0;
    const double bp = std::asin(std::max(-1.0, std::min(1.0,
                          -sd(W) * cb * sd(I) - sb * cd(I)))) / kDeg;

    // Physical libration: the small forced wobble of the Moon about its mean
    // rotation, a few hundredths of a degree. Mixed sine and cosine by term --
    // sigma ends on a cosine and tau carries two of them, which reads like a
    // slip and is not one.
    const double rho =
        - 0.02752 * cd(Mp)
        - 0.02245 * sd(F)
        + 0.00684 * cd(Mp - 2.0*F)
        - 0.00293 * cd(2.0*F)
        - 0.00085 * cd(2.0*F - 2.0*D)
        - 0.00054 * cd(Mp - 2.0*D)
        - 0.00020 * sd(Mp + F)
        - 0.00020 * cd(Mp + 2.0*F)
        - 0.00020 * cd(Mp - F)
        + 0.00014 * cd(Mp + 2.0*F - 2.0*D);
    const double sigma =
        - 0.02816 * sd(Mp)
        + 0.02244 * cd(F)
        - 0.00682 * sd(Mp - 2.0*F)
        - 0.00279 * sd(2.0*F)
        - 0.00083 * sd(2.0*F - 2.0*D)
        + 0.00069 * sd(Mp - 2.0*D)
        + 0.00040 * cd(Mp + F)
        - 0.00025 * sd(2.0*Mp)
        - 0.00023 * sd(Mp + 2.0*F)
        + 0.00020 * cd(Mp - F)
        + 0.00019 * sd(Mp - F)
        + 0.00013 * sd(Mp + 2.0*F - 2.0*D)
        - 0.00010 * cd(Mp - 3.0*F);
    const double tau =
        + 0.02520 * Ecc * sd(M)
        + 0.00473 * sd(2.0*Mp - 2.0*F)
        - 0.00467 * sd(Mp)
        + 0.00396 * sd(K1)
        + 0.00276 * sd(2.0*Mp - 2.0*D)
        + 0.00196 * sd(Om)
        - 0.00183 * cd(Mp - F)
        + 0.00115 * sd(Mp - 2.0*D)
        - 0.00096 * sd(Mp - D)
        + 0.00046 * sd(2.0*F - 2.0*D)
        - 0.00039 * sd(Mp - F)
        - 0.00032 * sd(Mp - M - D)
        + 0.00027 * sd(2.0*Mp - M - 2.0*D)
        + 0.00023 * sd(K2)
        - 0.00014 * sd(2.0*D)
        + 0.00014 * cd(2.0*Mp - 2.0*F)
        - 0.00012 * sd(Mp - 2.0*F)
        - 0.00012 * sd(2.0*Mp)
        + 0.00011 * sd(2.0*Mp - 2.0*M - 2.0*D);

    MoonLibration out;
    double lib = lp + (-tau + (rho * cd(A) + sigma * sd(A)) * std::tan(bp * kDeg));
    while (lib < -180.0) lib += 360.0;
    while (lib >= 180.0) lib -= 360.0;
    out.lonDeg = lib;
    out.latDeg = bp + (sigma * cd(A) - rho * sd(A));

    // Position angle of the axis: project the Moon's pole onto the sky.
    const double V  = Om + nutationLonDeg + sigma / sd(I);
    const double Ir = (I + rho) * kDeg;
    const double X  = std::sin(Ir) * sd(V);
    const double Y  = std::sin(Ir) * cd(V) * std::cos(epsRad)
                    - std::cos(Ir) * std::sin(epsRad);
    const double omega = std::atan2(X, Y);
    out.axisPADeg = std::asin(std::max(-1.0, std::min(1.0,
                        std::sqrt(X*X + Y*Y) * std::cos(alphaDeg * kDeg - omega)
                        / std::cos(out.latDeg * kDeg)))) / kDeg;
    return out;
}

}  // namespace sx

#endif  // SXWNL_GUI_MOON_LIBRATION_H
