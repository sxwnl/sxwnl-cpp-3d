#include <cmath>
#include <iostream>

#include "../gui/scene.h"

static bool near(double a, double b, double epsilon)
{
    return std::fabs(a - b) <= epsilon;
}

static bool require(bool condition, const char* message)
{
    if (condition) return true;
    std::cerr << "FAILED: " << message << "\n";
    return false;
}

int main()
{
    bool ok = true;
    Date local{2026, 7, 16, 20, 30, 15.0};
    double utc = sx::utcJDFromLocalDate(local, 8.0);
    Date roundTrip = sx::localDateFromUtcJD(utc, 8.0);
    ok &= require(roundTrip.Y == local.Y && roundTrip.M == local.M && roundTrip.D == local.D,
                  "UTC+8 date round trip");
    ok &= require(roundTrip.h == local.h && roundTrip.m == local.m,
                  "UTC+8 time round trip");
    Date greenwich = sx::localDateFromUtcJD(utc, 0.0);
    ok &= require(greenwich.h == 12 && greenwich.m == 30,
                  "same instant differs by eight hours");

    ok &= require(near(sx::speedToDaysPerSecond(0, 1.0), 1.0 / 86400.0, 1e-14),
                  "one second preset is realtime");
    ok &= require(near(sx::speedToDaysPerSecond(1, 24.0), 1.0, 1e-14),
                  "24 hours per second equals one day per second");
    ok &= require(near(sx::speedToDaysPerSecond(2, 2.0), 2.0, 1e-14),
                  "two days preset");
    ok &= require(near(sx::speedToDaysPerSecond(1, -12.0), -0.5, 1e-14),
                  "negative speed runs backward");

    sx::SimClock clock{utc, false, 2.0f};
    clock.advance(3.0);
    ok &= require(near(clock.jd, utc, 1e-14), "paused clock does not advance");
    clock.playing = true;
    clock.advance(3.0);
    ok &= require(near(clock.jd, utc + 6.0, 1e-10), "playing clock advances by preset speed");

    if (!ok) return 1;
    std::cout << "time control tests passed\n";
    return 0;
}
