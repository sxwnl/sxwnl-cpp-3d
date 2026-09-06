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
// IAU WGCCRE rotational elements. The bundled sxwnl ephemeris is positional
// only (heliocentric series + lunar theory), so it carries nothing about how a
// body spins; these standard constants supply that. Angles in degrees.
//   pole right ascension  a0 = raDeg  + raTPerCentury  * T
//   pole declination      d0 = decDeg + decTPerCentury * T
//   prime meridian        W  = w0Deg  + wRateDegPerDay * d
// T = Julian centuries from J2000, d = days from J2000. A negative wRate marks
// retrograde rotation (Venus, Uranus, Pluto).
struct RotationElements {
    double raDeg = 0.0,  raTPerCentury  = 0.0;
    double decDeg = 90.0, decTPerCentury = 0.0;
    double w0Deg = 0.0,  wRateDegPerDay = 0.0;
    bool valid = false;   // false leaves the body unspun and upright
};

struct BodyInfo {
    int xt;                 // engine planet index (0=Earth..7=Neptune, 8=Pluto); -1 = Sun
    std::string name;       // UTF-8 Chinese name
    std::string pinyin;     // ASCII id (fallback label / table key)
    float color[3];
    double realRadiusKm;
    double siderealYears;   // orbital period, for orbit sampling (0 for Sun)
    bool isSun;
    RotationElements rot;
};

// Per-frame computed state.
struct BodyState {
    gx::Vec3 world;          // scaled world-space position for rendering
    float displayRadius;     // scaled sphere radius for rendering
    double helioXYZ[3];      // raw heliocentric ecliptic xyz (AU)
    double L, B, R;          // heliocentric ecliptic lon(deg), lat(deg), radius(AU)
    double geoDistAU;        // geocentric distance (AU)
    double speedDegPerDay;   // heliocentric longitude angular speed

    // Orientation. The render transform is
    //   rotateY(poleNodeDeg) * rotateX(axialTiltDeg) * rotateY(spinDeg)
    // which points the body's polar axis anywhere on the sphere and then
    // spins about it. All three are 0 for a body with no rotation elements,
    // collapsing to identity so it renders upright and unspun as before.
    float spinDeg      = 0.0f; // rotation about the body's own polar axis
    float axialTiltDeg = 0.0f; // tilt of the polar axis away from ecliptic north
    float poleNodeDeg  = 0.0f; // direction the polar axis leans toward
    gx::Vec3 poleDir{0, 1, 0}; // unit polar axis in world space (for axis line)
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
    // Where the lit limb actually points, in degrees clockwise from screen-up,
    // for an observer at jw with the zenith up. The terminator is only vertical
    // when the geometry happens to line up that way; the tilt is most of what
    // makes a real crescent recognisable.
    double brightLimbAngleDeg = 90.0;
    gx::Vec3 worldPos;            // exaggerated 3D position near Earth
    float displayRadius  = 0.2f;
    bool  valid          = false;

    // True geocentric geometry, untouched by any display scaling. The eclipse
    // view needs these to rebuild the shadow axis honestly: worldPos above is
    // an artistic offset, and nothing about a shadow can be read off it.
    gx::Vec3 geoDir{0, 0, 1};     // unit vector Earth -> Moon, world frame
    double   geoDistKm = 384400.0;

    // Orientation, same convention as BodyState. The Moon is in synchronous
    // rotation, so its spin period equals its orbital period.
    float spinDeg      = 0.0f;
    float axialTiltDeg = 0.0f;
    float poleNodeDeg  = 0.0f;
    gx::Vec3 poleDir{0, 1, 0};
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

    // The Moon's orbit as unit geocentric directions over one sidereal month,
    // world frame. Unit vectors rather than positions because every view that
    // wants this draws the orbit at its own radius - the true one is 60 Earth
    // radii and fits in no frame that also shows an Earth. Rebuilt once a
    // simulated day; the shape barely moves inside one.
    const std::vector<gx::Vec3>& moonOrbitRing() const;

    // Eclipse study view. While it is on, the Moon is placed on the true
    // Sun-Moon-Earth axis at moonRadii Earth radii instead of at the artistic
    // offset the ordinary view uses, and takes its true size against the Earth.
    // The axis and the point where it crosses the Earth stay exact; only the
    // distance along it is compressed, because the real 60 Earth radii will not
    // share a frame with a recognisable Earth. Setting it here rather than in
    // the renderer keeps labels and picking on the Moon where it is drawn.
    struct EclipseFocus {
        bool  on = false;
        float moonRadii = 9.0f;
    };
    void setEclipseFocus(const EclipseFocus& f) { focus_ = f; }
    const EclipseFocus& eclipseFocus() const { return focus_; }
    // Unit vector along the true shadow axis, Sun -> Moon -> Earth, world frame.
    const gx::Vec3& shadowAxis() const { return shadowAxis_; }
    const MoonData& moon() const { return moon_; }
    const SimClock& clock() const { return clock_; }

    SimClock& clock() { return clock_; }
    ScaleParams& scale() { return scale_; }

    // Recompute body states for the current clock time.
    void update();
    // Recompute cached orbit polylines (call when scale params change).
    void rebuildOrbits();
    void rebuildMoonOrbitRing() const;

    // Map a raw heliocentric AU vector to world space using current scaling.
    gx::Vec3 toWorld(const double xyzAU[3]) const;

private:
    struct Orientation {
        float spinDeg = 0, axialTiltDeg = 0, poleNodeDeg = 0;
        gx::Vec3 poleDir{0, 1, 0};
    };
    // Solve a body's orientation from its IAU rotational elements.
    // T = Julian centuries from J2000.
    Orientation solveOrientation(const RotationElements& r, double T) const;
    // Earth uses a sidereal-time solution instead; see scene.cpp.
    Orientation solveEarthOrientation(double T) const;
    // The Moon's pole and prime meridian are not a simple linear precession:
    // the IAU model adds 13 periodic terms (physical libration) on top of the
    // linear part; see scene.cpp.
    Orientation solveMoonOrientation(double d) const;
    // Shared by solveOrientation()/solveMoonOrientation(): turn a resolved
    // pole (a0,d0) and prime-meridian angle W (radians, ICRF equatorial
    // J2000) into the render angles.
    static Orientation orientationFromPoleAndW(double a0, double d0, double W);

public:

private:
    std::vector<BodyInfo> info_;
    std::vector<BodyState> state_;
    std::vector<AsteroidInfo> asteroidInfo_;
    std::vector<AsteroidState> asteroidState_;
    std::vector<std::vector<gx::Vec3>> orbits_;
    mutable std::vector<gx::Vec3> moonRing_;
    mutable double moonRingDay_ = 1e18;   // floor(jd) the ring was built for
    EclipseFocus focus_;
    gx::Vec3 shadowAxis_{0, 0, 1};
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
