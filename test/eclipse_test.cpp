#include <cmath>
#include <iostream>

#include "../eph/eclipse.h"
#include "../mylib/tool.h"

static bool require(bool condition, const char* message)
{
    if (condition) return true;
    std::cerr << "FAILED: " << message << "\n";
    return false;
}

int main()
{
    bool ok = true;
    std::vector<EclipseEvent> events = searchEclipses(2027, 1, 12, 0);
    ok &= require(!events.empty(), "combined search returns events");

    bool foundSolar = false;
    bool foundLunar = false;
    for (const EclipseEvent& event : events) {
        ok &= require(std::isfinite(event.maximumTd), "maximum is finite");
        ok &= require(event.maximumTd != 0.0, "maximum is nonzero");
        ok &= require(event.magnitude >= 0.0, "magnitude is nonnegative");
        if (event.kind == EclipseEvent::Solar) {
            foundSolar = true;
            ok &= require(!event.type.empty(), "solar type is present");
            ok &= require(event.contactsTd[0] < event.maximumTd, "solar C1 before maximum");
            ok &= require(event.maximumTd < event.contactsTd[2], "solar maximum before C4");
            std::vector<EclipsePathSample> path = sampleSolarEclipsePath(event, 5.0);
            ok &= require(!path.empty(), "solar path is sampled");
            for (const EclipsePathSample& sample : path) {
                ok &= require(std::isfinite(sample.jdTd), "path time is finite");
                if (sample.center.valid) {
                    ok &= require(sample.center.longitudeDeg >= -180.001, "longitude lower bound");
                    ok &= require(sample.center.longitudeDeg <= 180.001, "longitude upper bound");
                    ok &= require(sample.center.latitudeDeg >= -90.001, "latitude lower bound");
                    ok &= require(sample.center.latitudeDeg <= 90.001, "latitude upper bound");
                }
            }
        } else {
            foundLunar = true;
            ok &= require(event.contactsTd[1] == event.maximumTd, "lunar maximum mapping");
            ok &= require(event.contactsTd[3] < event.maximumTd, "lunar P1 before maximum");
            ok &= require(event.maximumTd < event.contactsTd[4], "lunar maximum before P4");
            LunarShadowGeometry geometry = lunarShadowGeometry(event.maximumTd);
            ok &= require(geometry.valid, "lunar shadow is valid");
            ok &= require(geometry.penumbraRadius > geometry.umbraRadius, "penumbra exceeds umbra");
            ok &= require(geometry.umbraRadius > 0.0, "umbra radius is positive");
            ok &= require(geometry.moonRadius > 0.0, "moon radius is positive");
        }
    }
    ok &= require(foundSolar, "combined search contains solar eclipse");
    ok &= require(foundLunar, "combined search contains lunar eclipse");

    std::vector<EclipseEvent> known = searchEclipses(2027, 2, 2, 1);
    ok &= require(!known.empty(), "known solar search returns an event");
    Date local = setFromJD(eclipseTdToUtcJD(known.front().maximumTd) + 8.0 / 24.0);
    ok &= require(local.Y == 2027, "known event year");
    ok &= require(local.M == 2, "known event month");
    ok &= require(local.D == 6, "known event day");

    if (!ok) return 1;
    std::cout << "eclipse tests passed\n";
    return 0;
}
