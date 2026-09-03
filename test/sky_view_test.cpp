// The ground view's geometry, checked against the eclipse engine.
//
// computeSkyView() reaches the same eclipse by a different road: it asks MSC
// for two topocentric places and measures the discs against each other, while
// searchEclipses()/calculateLocalSolarEclipse() solve the Besselian elements.
// Where the two must agree - the magnitude at maximum, the tangency of the
// discs at first and last contact, the Moon's depth in Earth's shadow - they
// are made to say so here.
#include <cmath>
#include <iostream>
#include <vector>

#include "../eph/eclipse.h"
#include "../eph/sky_view.h"

static bool require(bool condition, const char* message)
{
    if (condition) return true;
    std::cerr << "FAILED: " << message << "\n";
    return false;
}

int main()
{
    bool ok = true;

    // ---- A total eclipse, watched from its own centre line ----------------
    {
        std::vector<EclipseEvent> events = searchEclipses(2024, 4, 1, 1);
        ok &= require(!events.empty(), "solar search returns the 2024-04-08 eclipse");
        if (!events.empty()) {
            EclipseEvent e = events[0];
            const double lon = e.centerLongitudeDeg, lat = e.centerLatitudeDeg;
            calculateLocalSolarEclipse(e, lon, lat, 0.0, false);

            SkyView v = computeSkyView(e.localContactsTd[1], lon, lat, 0.0);
            ok &= require(v.valid, "sky view is valid");
            ok &= require(v.total, "centre line sees totality at maximum");
            ok &= require(std::fabs(v.obscuration - 1.0) < 1e-9, "totality hides the whole disc");
            ok &= require(std::fabs(v.magnitude - e.localMagnitude) < 2e-3,
                          "magnitude agrees with the local circumstances");
            ok &= require(v.sun.altitudeDeg > 0.0, "the Sun is up where the shadow lands");
            ok &= require(v.sun.radiusDeg > 0.25 && v.sun.radiusDeg < 0.30,
                          "solar apparent radius is about a quarter of a degree");
            ok &= require(v.moon.radiusDeg > 0.22 && v.moon.radiusDeg < 0.32,
                          "lunar apparent radius is about a quarter of a degree");
            ok &= require(v.daylight < 0.02, "totality takes the daylight");

            // At first and last contact the two discs are exactly tangent, so
            // the magnitude passes through zero. The contact times come from
            // the Besselian solution and the separation from the ephemeris:
            // nothing but real agreement puts this within a thousandth.
            for (int i : {0, 2}) {
                SkyView c = computeSkyView(e.localContactsTd[i], lon, lat, 0.0);
                ok &= require(std::fabs(c.magnitude) < 3e-3, "discs are tangent at contact");
                ok &= require(std::fabs(c.separationDeg -
                                        (c.sun.radiusDeg + c.moon.radiusDeg)) < 2e-3,
                              "separation equals the sum of the radii at contact");
            }

            // Half a world away the eclipse is not merely unseen - the Sun is
            // not even up. That exercises the horizon transform, which nothing
            // above touches.
            double antiLon = lon > 0.0 ? lon - 180.0 : lon + 180.0;
            SkyView far = computeSkyView(e.localContactsTd[1], antiLon, -lat, 0.0);
            ok &= require(far.sun.altitudeDeg < 0.0, "the Sun is down at the antipode");
        }
    }

    // ---- An annular eclipse -----------------------------------------------
    {
        std::vector<EclipseEvent> events = searchEclipses(2024, 10, 1, 1);
        ok &= require(!events.empty(), "solar search returns the 2024-10-02 eclipse");
        if (!events.empty()) {
            const EclipseEvent& e = events[0];
            SkyView v = computeSkyView(e.maximumTd, e.centerLongitudeDeg,
                                       e.centerLatitudeDeg, 0.0);
            ok &= require(v.annular, "centre line sees the ring");
            ok &= require(!v.total, "an annular eclipse is not total");
            ok &= require(v.obscuration < 1.0, "the ring is still showing");
            ok &= require(std::fabs(v.magnitude - e.magnitude) < 0.02,
                          "annular magnitude is the ratio of the diameters");
            ok &= require(v.daylight > 0.3, "an annular eclipse does not bring on night");
        }
    }

    // ---- Lunar eclipses: the Moon's depth in Earth's shadow ---------------
    {
        std::vector<EclipseEvent> events = searchEclipses(2025, 1, 6, 2);
        ok &= require(!events.empty(), "lunar search returns events");
        for (const EclipseEvent& e : events) {
            // A lunar eclipse looks the same from anywhere the Moon is up, so
            // the observer is arbitrary.
            SkyView v = computeSkyView(e.maximumTd, 0.0, 0.0, 0.0);
            ok &= require(std::fabs(v.umbralMagnitude - e.magnitude) < 2e-3 ||
                          (v.umbralMagnitude == 0.0 &&
                           std::fabs(v.penumbralMagnitude - e.magnitude) < 2e-3),
                          "umbral (or penumbral) magnitude matches the search");
            ok &= require(v.penumbralMagnitude > v.umbralMagnitude,
                          "the penumbra reaches further than the umbra");
            ok &= require(v.penumbraRadiusDeg > v.umbraRadiusDeg,
                          "the penumbra is the wider shadow");
            ok &= require(v.umbraRadiusDeg > v.moon.radiusDeg,
                          "the umbra is wider than the Moon at that distance");
            ok &= require(v.moonIllum > 0.97, "an eclipsed Moon is a full Moon");
        }
    }

    if (ok) std::cout << "sky view checks passed\n";
    return ok ? 0 : 1;
}
