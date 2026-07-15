// Solar-system model: bodies, simulation clock, and per-frame state computed
// from the sxwnl astronomical engine (heliocentric J2000 ecliptic coords).
#ifndef SXWNL_GUI_SCENE_H
#define SXWNL_GUI_SCENE_H

#include <string>
#include <vector>
#include "mathx.h"
#include "../mylib/tool.h"

namespace sx {

// Static metadata for each rendered body.
struct BodyInfo {
    int xt;                 // engine planet index (0=Earth..7=Neptune, 8=Pluto); -1 = Sun
    std::string name;       // UTF-8 Chinese name
    std::string pinyin;     // ASCII id (fallback label / table key)
    float color[3];
    double realRadiusKm;
    double siderealYears;   // orbital period, for orbit sampling (0 for Sun)
    bool isSun;
};

// Per-frame computed state.
struct BodyState {
    gx::Vec3 world;          // scaled world-space position for rendering
    float displayRadius;     // scaled sphere radius for rendering
    double helioXYZ[3];      // raw heliocentric ecliptic xyz (AU)
    double L, B, R;          // heliocentric ecliptic lon(deg), lat(deg), radius(AU)
    double geoDistAU;        // geocentric distance (AU)
    double speedDegPerDay;   // heliocentric longitude angular speed

    // Self-rotation (currently driven only for Earth; 0 leaves a body upright
    // and unspun so non-rotating bodies render exactly as before).
    float spinDeg      = 0.0f; // rotation about the body's own polar axis
    float axialTiltDeg = 0.0f; // obliquity of the polar axis vs. the ecliptic
};

struct AsteroidInfo {
    double aAU = 0.0;        // semi-major axis (AU)
    double e = 0.0;          // eccentricity
    double incDeg = 0.0;     // inclination relative to the ecliptic
    double omegaDeg = 0.0;   // argument of perihelion
    double OmegaDeg = 0.0;   // longitude of ascending node
    double meanAnomDeg = 0.0;// mean anomaly at J2000
    float  displaySize = 1.0f;
    float  brightness = 0.6f;
};

struct AsteroidState {
    gx::Vec3 world;
    float displaySize = 1.0f;
    float brightness = 0.6f;
};


// Moon phase data (computed every frame from the engine).
struct MoonData {
    double illumination  = 0.0;   // [0,1]: 0=new moon, 1=full moon
    bool   waxing        = true;  // true if new→full (elongation 0..180°)
    double elongationDeg = 0.0;   // moon − sun ecliptic longitude (0..360°)
    double ageDays       = 0.0;   // days since last new moon
    std::string phaseName;        // 朔/蛾眉月/上弦月/盈凸月/望/亏凸月/下弦月/残月
    gx::Vec3 worldPos;            // exaggerated 3D position near Earth
    float displayRadius  = 0.2f;
    bool  valid          = false;
};

struct SimClock {
    double jd;               // current UTC Julian Day (double for precision)
    bool playing = false;
    float speedDaysPerSec = 5.0f;

    void advance(double realDtSec) {
        if (playing) jd += (double)speedDaysPerSec * realDtSec;
    }
};

// Visual scaling controls (driven by the UI).
struct ScaleParams {
    bool logDistance = true;
    float linearAUtoWorld = 8.0f; // world units per AU (linear mode)
    float logK = 12.0f;           // world = logK * ln(1 + auR) (log mode)
    float sizeScale = 0.9f;       // visual body scale, intentionally compressed
};

double nowJD();                   // system clock -> Julian Day
double jdToCenturiesTD(double jd);// (jd - J2000)/36525
Date localDateFromUtcJD(double utcJD, double timezoneHours);
double utcJDFromLocalDate(const Date& localDate, double timezoneHours);
double speedToDaysPerSecond(int unit, double amount);

class Scene {
public:
    Scene();

    const std::vector<BodyInfo>& bodies() const { return info_; }
    const std::vector<BodyState>& states() const { return state_; }
    const std::vector<AsteroidState>& asteroids() const { return asteroidState_; }
    const std::vector<std::vector<gx::Vec3>>& orbits() const { return orbits_; }
    const MoonData& moon() const { return moon_; }
    const SimClock& clock() const { return clock_; }

    SimClock& clock() { return clock_; }
    ScaleParams& scale() { return scale_; }

    // Recompute body states for the current clock time.
    void update();
    // Recompute cached orbit polylines (call when scale params change).
    void rebuildOrbits();

    // Map a raw heliocentric AU vector to world space using current scaling.
    gx::Vec3 toWorld(const double xyzAU[3]) const;

private:
    std::vector<BodyInfo> info_;
    std::vector<BodyState> state_;
    std::vector<AsteroidInfo> asteroidInfo_;
    std::vector<AsteroidState> asteroidState_;
    std::vector<std::vector<gx::Vec3>> orbits_;
    MoonData moon_;
    SimClock clock_;
    ScaleParams scale_;
    bool lastLog_ = true;
    float lastLinear_ = 8.0f, lastLogK_ = 12.0f;
};

// Fill xyz (AU) with the heliocentric J2000 ecliptic position of a body.
// xt: 0..7 planets via p_coord, 8 = Pluto via pluto_coord, -1 = Sun (origin).
void heliocentricAU(int xt, double tCenturies, int nTerms, double xyz[3]);

} // namespace sx

#endif
