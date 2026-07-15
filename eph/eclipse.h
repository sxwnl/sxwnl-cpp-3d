#ifndef EPH_ECLIPSE_H
#define EPH_ECLIPSE_H

#include <string>
#include <vector>

struct EclipseGeoPoint
{
    double longitudeDeg = 0.0;
    double latitudeDeg = 0.0;
    bool valid = false;
};

struct EclipsePathSample
{
    double jdTd = 0.0; // J2000-relative terrestrial time
    EclipseGeoPoint center;
    EclipseGeoPoint penumbraNorth;
    EclipseGeoPoint penumbraSouth;
    EclipseGeoPoint umbraNorth;
    EclipseGeoPoint umbraSouth;
};

struct EclipseEvent
{
    enum Kind { Solar = 0, Lunar = 1 };

    Kind kind = Solar;
    std::string type;
    double maximumTd = 0.0; // J2000-relative terrestrial time
    double contactsTd[7] = {}; // solar: C1,max,C4,C2,C3; lunar: U1,max,U4,P1,P4,U2,U3
    double magnitude = 0.0;
    double durationDays = 0.0;
    double centerLongitudeDeg = 0.0;
    double centerLatitudeDeg = 0.0;
    double pathWidthKm = 0.0;
    bool hasCenter = false;

    std::string localType;
    double localContactsTd[5] = {};
    double localMagnitude = 0.0;
    double localDurationDays = 0.0;
};

struct LunarShadowGeometry
{
    bool valid = false;
    double x = 0.0;
    double y = 0.0;
    double moonRadius = 0.0;
    double umbraRadius = 0.0;
    double penumbraRadius = 0.0;
};

// filter: 0=all, 1=solar only, 2=lunar only.
std::vector<EclipseEvent> searchEclipses(int startYear, int startMonth,
                                         int resultCount, int filter = 0);
void calculateLocalSolarEclipse(EclipseEvent& event, double longitudeDeg,
                                double latitudeDeg, double altitudeKm,
                                bool nasaRadius = false);
std::vector<EclipsePathSample> sampleSolarEclipsePath(const EclipseEvent& event,
                                                       double stepMinutes = 2.0);
LunarShadowGeometry lunarShadowGeometry(double jdTd);
double eclipseTdToUtcJD(double jdTd);

#endif
