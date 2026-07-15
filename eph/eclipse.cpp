#include "eclipse.h"

#include <algorithm>
#include <cmath>

#include "eph.h"
#include "eph0.h"
#include "eph_rsgs.h"
#include "eph_rspl.h"
#include "eph_yspl.h"
#include "../mylib/tool.h"

namespace {

const double kPi = 3.14159265358979323846;
const double kRadToDeg = 180.0 / kPi;
const double kSynodicMonth = 29.5306;

EclipseGeoPoint toGeo(mystl::array3 p)
{
    EclipseGeoPoint out;
    out.valid = std::isfinite(p[0]) && std::isfinite(p[1]) && std::fabs(p[1]) < 10.0;
    if (out.valid) {
        out.longitudeDeg = p[0] * kRadToDeg;
        out.latitudeDeg = p[1] * kRadToDeg;
    }
    return out;
}

EclipseEvent makeSolar(double probe)
{
    _ECFAST fast = ecFast(probe);
    EclipseEvent event;
    event.kind = EclipseEvent::Solar;
    if (fast.lx == "N") return event;

    RS_GS::init(fast.jdSuo, 7);
    _FEATURE f = RS_GS::feature(fast.jdSuo);
    event.type = f.lx.c_str();
    event.maximumTd = f.jd;
    event.contactsTd[0] = f.gk3[2]; // partial phase begins globally
    event.contactsTd[1] = f.jd;
    event.contactsTd[2] = f.gk4[2]; // partial phase ends globally
    event.contactsTd[3] = f.gk1[2]; // central phase begins globally
    event.contactsTd[4] = f.gk2[2]; // central phase ends globally
    event.magnitude = f.sf;
    event.durationDays = f.tt;
    event.centerLongitudeDeg = f.zxJ * kRadToDeg;
    event.centerLatitudeDeg = f.zxW * kRadToDeg;
    event.pathWidthKm = f.dw;
    event.hasCenter = f.d < 1.0 && std::fabs(f.zxW) < 10.0;
    return event;
}

void appendSolar(std::vector<EclipseEvent>& out, double startTd, int wanted)
{
    const size_t targetSize = out.size() + (size_t)wanted;
    double probe = startTd;
    double lastMaximum = -1e100;
    for (int guard = 0; guard < wanted * 20 + 120 && out.size() < targetSize; ++guard) {
        _ECFAST fast = ecFast(probe);
        if (fast.lx != "N" && fast.jdSuo >= startTd - 1.0 && fast.jdSuo > lastMaximum + 1.0) {
            EclipseEvent event = makeSolar(fast.jdSuo);
            if (!event.type.empty()) {
                out.push_back(event);
                lastMaximum = event.maximumTd;
            }
        }
        probe = fast.jdSuo + kSynodicMonth + 1.0;
    }
}

void appendLunar(std::vector<EclipseEvent>& out, double startTd, int wanted)
{
    const size_t targetSize = out.size() + (size_t)wanted;
    double probe = startTd;
    double lastMaximum = -1e100;
    for (int guard = 0; guard < wanted * 20 + 120 && out.size() < targetSize; ++guard) {
        YS_PL::lecMax(probe);
        double maximum = YS_PL::lT[1];
        bool hasPenumbral = YS_PL::lT[3] != 0.0 && YS_PL::lT[4] != 0.0;
        if (maximum >= startTd - 1.0 && maximum > lastMaximum + 1.0 &&
            (!YS_PL::LX.empty() || hasPenumbral)) {
            EclipseEvent event;
            event.kind = EclipseEvent::Lunar;
            event.type = YS_PL::LX.empty() ? "半影" : YS_PL::LX.c_str();
            event.maximumTd = maximum;
            for (int i = 0; i < 7; ++i) event.contactsTd[i] = YS_PL::lT[i];
            event.magnitude = YS_PL::sf;
            double first = event.contactsTd[3] != 0.0 ? event.contactsTd[3] : event.contactsTd[0];
            double last = event.contactsTd[4] != 0.0 ? event.contactsTd[4] : event.contactsTd[2];
            event.durationDays = last > first ? last - first : 0.0;
            out.push_back(event);
            lastMaximum = maximum;
        }
        probe += kSynodicMonth;
    }
}

} // namespace

double eclipseTdToUtcJD(double jdTd)
{
    return jdTd + J2000 - dt_T(jdTd);
}

std::vector<EclipseEvent> searchEclipses(int startYear, int startMonth,
                                         int resultCount, int filter)
{
    resultCount = std::max(1, std::min(resultCount, 100));
    startMonth = std::max(1, std::min(startMonth, 12));
    double startTd = toJD(Date{startYear, startMonth, 1, 0, 0, 0.0}) - J2000;
    startTd += dt_T(startTd);

    std::vector<EclipseEvent> events;
    if (filter != 2) appendSolar(events, startTd, filter == 1 ? resultCount : resultCount * 2);
    if (filter != 1) appendLunar(events, startTd, filter == 2 ? resultCount : resultCount * 2);
    std::sort(events.begin(), events.end(), [](const EclipseEvent& a, const EclipseEvent& b) {
        return a.maximumTd < b.maximumTd;
    });
    if ((int)events.size() > resultCount) events.resize(resultCount);
    return events;
}

void calculateLocalSolarEclipse(EclipseEvent& event, double longitudeDeg,
                                double latitudeDeg, double altitudeKm,
                                bool nasaRadius)
{
    if (event.kind != EclipseEvent::Solar) return;
    RS_PL::nasa_r = nasaRadius;
    RS_PL::secMax(event.maximumTd, longitudeDeg / kRadToDeg,
                  latitudeDeg / kRadToDeg, altitudeKm);
    event.localType = RS_PL::LX.c_str();
    for (int i = 0; i < 5; ++i) event.localContactsTd[i] = RS_PL::sT[i];
    event.localMagnitude = RS_PL::sf;
    event.localDurationDays = RS_PL::dur;
}

std::vector<EclipsePathSample> sampleSolarEclipsePath(const EclipseEvent& event,
                                                       double stepMinutes)
{
    std::vector<EclipsePathSample> result;
    if (event.kind != EclipseEvent::Solar || event.type.empty()) return result;
    RS_GS::init(event.maximumTd, 7);

    double begin = event.contactsTd[0];
    double end = event.contactsTd[2];
    if (!(end > begin)) {
        begin = event.maximumTd - 3.0 / 24.0;
        end = event.maximumTd + 3.0 / 24.0;
    }
    double step = std::max(0.25, std::min(stepMinutes, 30.0)) / 1440.0;
    for (double t = begin; t <= end + step * 0.5; t += step) {
        _ECLIPSE_SHADOW_POINT p = RS_GS::shadowPoint(t);
        EclipsePathSample sample;
        sample.jdTd = t;
        sample.center = toGeo(p.center);
        sample.penumbraNorth = toGeo(p.penumbraNorth);
        sample.penumbraSouth = toGeo(p.penumbraSouth);
        sample.umbraNorth = toGeo(p.umbraNorth);
        sample.umbraSouth = toGeo(p.umbraSouth);
        result.push_back(sample);
    }
    return result;
}

LunarShadowGeometry lunarShadowGeometry(double jdTd)
{
    RE0 re = {};
    YS_PL::lecXY(jdTd, re);
    LunarShadowGeometry out;
    out.valid = std::isfinite(re.x) && std::isfinite(re.y) && re.Er > 0.0;
    out.x = re.x;
    out.y = re.y;
    out.moonRadius = re.mr;
    out.umbraRadius = re.er;
    out.penumbraRadius = re.Er;
    return out;
}
