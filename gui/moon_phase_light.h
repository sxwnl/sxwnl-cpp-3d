// Where the Sun is, seen from the camera of the 3-D moon-phase view.
//
// Header-only and free of any GL dependency, so Renderer::renderMoonPhase and
// moonphase_test run the same arithmetic rather than a copy each.
//
// The view space is the one renderMoonPhase sets up: the camera sits on +Z
// looking at the Moon, +X is screen-right and +Y screen-up.
#ifndef SXWNL_GUI_MOON_PHASE_LIGHT_H
#define SXWNL_GUI_MOON_PHASE_LIGHT_H

#include <cmath>

namespace sx {

struct MoonPhaseLight { double x, y, z; };

// elongDeg      Moon minus Sun ecliptic longitude, 0..360 (Scene's convention:
//               0 new, 90 first quarter, 180 full, 270 last quarter).
// limbAngleDeg  direction of the lit limb on screen, degrees clockwise from up,
//               exactly what the 2-D disk in panels.cpp is drawn with.
//
// Returns a unit vector toward the Sun.
//
// Two numbers decide where the light comes from, and each must carry only its
// own half of the answer. The elongation says how far round behind the Moon
// the Sun is -- that is the phase, and it fixes the Z component. The limb
// angle says which way across the disc the lit side faces, and it alone fixes
// the direction in the screen plane. The 2-D disk works the same way: it takes
// the illumination and the angle, and has no separate waxing flip.
//
// This used to take sin(elong) as the in-plane component and then roll it to
// the limb angle. sin(elong) is negative for a waning Moon (180..360), so it
// pointed the light at the dark limb before the roll; the roll then turned it
// by the limb angle, which for a waning Moon already points the other way.
// The side was counted twice, and every waning phase came out lit on the
// opposite side to the 2-D disk -- 180 degrees out, with the terminator at
// the right slope, which is why it looked plausible -- in both the schematic
// view (limb at 270) and the real one.
inline MoonPhaseLight moonPhaseSunDir(double elongDeg, double limbAngleDeg) {
    const double kDeg  = 3.14159265358979323846 / 180.0;
    const double e     = elongDeg * kDeg;
    const double inPlane = std::fabs(std::sin(e));   // never picks the side
    const double a     = limbAngleDeg * kDeg;
    return { inPlane * std::sin(a),    // clockwise from up: +X at 90
             inPlane * std::cos(a),    // +Y at 0
             -std::cos(e) };           // Sun beyond the Moon at new moon
}

} // namespace sx

#endif // SXWNL_GUI_MOON_PHASE_LIGHT_H
