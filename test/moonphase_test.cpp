// Checks the moon-phase geometry against published worked examples.
//
// The bright-limb position angle decides which way a crescent tilts, and a sign
// slip there is invisible in code review but glaring on screen. Meeus,
// Astronomical Algorithms, chapter 48, works the same numbers through by hand,
// so the formula is pinned to his results rather than to my own arithmetic.
#include <cmath>
#include <cstdio>
#include <initializer_list>

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

    std::printf(failures == 0 ? "\nALL OK\n" : "\n%d FAILURE(S)\n", failures);
    return failures == 0 ? 0 : 1;
}
