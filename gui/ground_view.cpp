#include "ground_view.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

#include "imgui.h"

#include "../eph/eph0.h"
#include "../eph/sky_view.h"
#include "../mylib/tool.h"

namespace sx {

namespace {

const double kPI  = 3.14159265358979323846;
const double kJ2K = 2451545.0;

double deg2rad(double d) { return d * kPI / 180.0; }
float  clamp01f(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

// ---------------------------------------------------------------------------
//  Projection
// ---------------------------------------------------------------------------
// Gnomonic, the way a camera sees: straight lines in the sky stay straight on
// screen. Two things fall out of that and are worth the choice on their own -
// the horizon is a great circle, so it is one horizontal line however high the
// Sun is, and the zenith is always straight up, which is how a person standing
// there holds their head.
struct SkyProjection {
    ImVec2 centre{};
    float  k = 100.0f;      // pixels per unit of tangent
    double viewAz = 0.0;    // degrees
    double viewAlt = 0.0;

    // Pixels per degree near the middle of the field. Discs and separations are
    // fractions of a degree, so this linear scale is exact enough for them
    // while the projection above carries the wide geometry.
    float pxPerDeg() const { return (float)(k * kPI / 180.0); }

    ImVec2 project(double azDeg, double altDeg) const {
        double a = deg2rad(altDeg), z = deg2rad(azDeg);
        double vx = std::cos(a) * std::cos(z), vy = std::cos(a) * std::sin(z), vz = std::sin(a);
        double va = deg2rad(viewAlt), vaz = deg2rad(viewAz);
        double ux = std::cos(va) * std::cos(vaz), uy = std::cos(va) * std::sin(vaz), uz = std::sin(va);
        double rx = -std::sin(vaz), ry = std::cos(vaz);            // right, always horizontal
        double px = -std::sin(va) * std::cos(vaz);                 // up
        double py = -std::sin(va) * std::sin(vaz), pz = std::cos(va);
        double along = vx * ux + vy * uy + vz * uz;
        if (along < 1e-4) along = 1e-4;                            // behind the observer
        double X = (vx * rx + vy * ry) / along;
        double Y = (vx * px + vy * py + vz * pz) / along;
        return ImVec2(centre.x + (float)(X * k), centre.y - (float)(Y * k));
    }

    // The horizon is a great circle and `right` is horizontal, so its image is
    // the single screen row below.
    float horizonY() const { return centre.y + (float)(std::tan(deg2rad(viewAlt)) * k); }

    // Where a compass bearing meets that row.
    float azimuthX(double azDeg) const {
        double d = deg2rad(azDeg - viewAz);
        double ca = std::cos(deg2rad(viewAlt));
        if (std::fabs(std::cos(d)) < 1e-4 || ca < 1e-6) return centre.x + (d > 0 ? 1e5f : -1e5f);
        return centre.x + (float)(std::tan(d) / ca * k);
    }

    // Altitude of the sky at a screen row, along the centre column.
    double altitudeAtRow(float py) const {
        return viewAlt + std::atan((centre.y - py) / k) * 180.0 / kPI;
    }
};

// ---------------------------------------------------------------------------
//  Colour
// ---------------------------------------------------------------------------
struct RGB { float r = 0, g = 0, b = 0; };

RGB mix(const RGB& a, const RGB& b, float t) {
    return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t};
}
ImU32 toCol(const RGB& c, float alpha = 1.0f) {
    auto q = [](float v) { return (int)std::lround(clamp01f(v) * 255.0f); };
    return IM_COL32(q(c.r), q(c.g), q(c.b), q(alpha));
}

// The sky at one altitude. `light` is how much of the Sun is left (the eclipse
// term) and `sunAlt` is where the Sun is (the ordinary day/night term); an
// eclipsed noon and a sunset are not the same sky, so both are kept.
RGB skyColour(double altDeg, double sunAltDeg, float light) {
    float t = clamp01f((float)(altDeg / 55.0));
    RGB day   = mix(RGB{0.66f, 0.77f, 0.90f}, RGB{0.16f, 0.33f, 0.66f}, t);
    RGB night = mix(RGB{0.055f, 0.070f, 0.115f}, RGB{0.012f, 0.018f, 0.045f}, t);
    float sunLight = clamp01f((float)((sunAltDeg + 6.0) / 10.0));
    float L = clamp01f(light * sunLight);
    RGB c = mix(night, day, L);

    // The ring of dusk round the horizon: in totality the sky overhead goes
    // dark while the un-eclipsed air far away still lights the rim, which is
    // the part of the experience no diagram ever shows.
    float rim = std::exp(-(float)std::max(altDeg, 0.0) / 8.0f) * (1.0f - L) * sunLight;
    c.r += 0.46f * rim; c.g += 0.22f * rim; c.b += 0.10f * rim;
    return c;
}

// ---------------------------------------------------------------------------
//  Two discs, and the piece they share
// ---------------------------------------------------------------------------
// The intersection of two discs is convex, so it fills as a convex polygon.
// It is what the eclipsed part of a disc actually is - used for the Moon in
// Earth's shadow, where the shadow has to be cut to the Moon's edge rather
// than painted over the sky beside it.
void addDiscIntersection(ImDrawList* dl, ImVec2 c1, float r1, ImVec2 c2, float r2,
                         ImU32 col, int seg = 64) {
    float dx = c2.x - c1.x, dy = c2.y - c1.y;
    float d = std::sqrt(dx * dx + dy * dy);
    if (r1 <= 0.0f || r2 <= 0.0f) return;
    if (d >= r1 + r2) return;
    if (d <= r2 - r1) { dl->AddCircleFilled(c1, r1, col, seg); return; }
    if (d <= r1 - r2) { dl->AddCircleFilled(c2, r2, col, seg); return; }

    float a = (d * d + r1 * r1 - r2 * r2) / (2.0f * d);
    float h2 = r1 * r1 - a * a;
    float h = h2 > 0.0f ? std::sqrt(h2) : 0.0f;
    ImVec2 base{c1.x + a * dx / d, c1.y + a * dy / d};
    ImVec2 perp{-dy / d, dx / d};
    ImVec2 p1{base.x + perp.x * h, base.y + perp.y * h};
    ImVec2 p2{base.x - perp.x * h, base.y - perp.y * h};

    auto ang = [](ImVec2 c, ImVec2 p) { return std::atan2(p.y - c.y, p.x - c.x); };
    const float TAU = 6.28318530718f;
    dl->PathClear();
    // Arc of the first circle that lies inside the second: the one that passes
    // the point of circle 1 nearest circle 2's centre.
    float a1 = ang(c1, p1), a2 = ang(c1, p2), amid = std::atan2(dy, dx);
    float s1 = a1, e1 = a2;
    auto between = [&](float s, float e, float m) {
        float span = e - s; while (span < 0) span += TAU; while (span > TAU) span -= TAU;
        float rel  = m - s; while (rel  < 0) rel  += TAU; while (rel  > TAU) rel  -= TAU;
        return rel <= span;
    };
    if (!between(s1, e1, amid)) std::swap(s1, e1);
    if (e1 < s1) e1 += TAU;
    dl->PathArcTo(c1, r1, s1, e1, seg / 2);
    float b1 = ang(c2, p2), b2 = ang(c2, p1), bmid = std::atan2(-dy, -dx);
    float s2 = b1, e2 = b2;
    if (!between(s2, e2, bmid)) std::swap(s2, e2);
    if (e2 < s2) e2 += TAU;
    dl->PathArcTo(c2, r2, s2, e2, seg / 2);
    dl->PathFillConvex(col);
}

// A radial gradient, written straight into the vertex buffer. ImDrawList has no
// gradient fill of its own, and stacking translucent circles to fake one leaves
// exactly the concentric rings it was meant to hide - which around a light
// source is the one artefact the eye picks out immediately.
void addRadialGlow(ImDrawList* dl, ImVec2 c, float rInner, float rOuter,
                   ImU32 inner, ImU32 outer, int seg = 72) {
    if (rOuter <= rInner || rOuter <= 0.0f) return;
    const ImVec2 uv = ImGui::GetFontTexUvWhitePixel();
    dl->PrimReserve(seg * 6, (seg + 1) * 2);
    unsigned int base = dl->_VtxCurrentIdx;
    const float TAU = 6.28318530718f;
    for (int i = 0; i <= seg; ++i) {
        float a = TAU * i / seg;
        ImVec2 d{std::cos(a), std::sin(a)};
        dl->PrimWriteVtx(ImVec2(c.x + d.x * rInner, c.y + d.y * rInner), uv, inner);
        dl->PrimWriteVtx(ImVec2(c.x + d.x * rOuter, c.y + d.y * rOuter), uv, outer);
    }
    for (int i = 0; i < seg; ++i) {
        ImDrawIdx i0 = (ImDrawIdx)(base + i * 2);
        dl->PrimWriteIdx(i0);
        dl->PrimWriteIdx((ImDrawIdx)(i0 + 1));
        dl->PrimWriteIdx((ImDrawIdx)(i0 + 3));
        dl->PrimWriteIdx(i0);
        dl->PrimWriteIdx((ImDrawIdx)(i0 + 3));
        dl->PrimWriteIdx((ImDrawIdx)(i0 + 2));
    }
}

// Three ramps end to end, so the falloff steepens near the source the way real
// glare does rather than fading off in a straight line.
void addGlare(ImDrawList* dl, ImVec2 c, float r, int cr, int cg, int cb,
              float strength, float reach) {
    if (strength <= 0.004f || r <= 0.0f) return;
    auto col = [&](float a) { return IM_COL32(cr, cg, cb, (int)std::lround(clamp01f(a * strength) * 255.0f)); };
    addRadialGlow(dl, c, r,              r * (1.0f + 0.6f * reach),  col(0.62f), col(0.26f));
    addRadialGlow(dl, c, r * (1.0f + 0.6f * reach), r * (1.0f + 2.0f * reach), col(0.26f), col(0.075f));
    addRadialGlow(dl, c, r * (1.0f + 2.0f * reach), r * (1.0f + 5.0f * reach), col(0.075f), col(0.0f));
}

// ---------------------------------------------------------------------------
//  Sky furniture
// ---------------------------------------------------------------------------
// A deterministic hash, so the stars and the corona's rays stay where they were
// last frame instead of boiling.
float hash01(int i) {
    unsigned int x = (unsigned int)i * 2654435761u;
    x ^= x >> 15; x *= 2246822519u; x ^= x >> 13;
    return (float)(x & 0xFFFFFF) / (float)0xFFFFFF;
}

void drawStars(ImDrawList* dl, const SkyProjection& proj, ImVec2 p0, ImVec2 p1,
               float visibility) {
    if (visibility <= 0.02f) return;
    for (int i = 0; i < 220; ++i) {
        double az = hash01(i * 3 + 1) * 360.0;
        double alt = std::asin(hash01(i * 3 + 2)) * 180.0 / kPI; // even over the dome
        ImVec2 p = proj.project(az, alt);
        if (p.x < p0.x || p.x > p1.x || p.y < p0.y || p.y > p1.y) continue;
        float m = hash01(i * 3 + 3);
        float r = 0.6f + m * 1.5f;
        float a = visibility * (0.25f + 0.75f * m * m);
        dl->AddCircleFilled(p, r, IM_COL32(226, 234, 255, (int)(a * 255.0f)), 6);
    }
}

// Ground, horizon and compass. The horizon is what makes the view a place
// rather than a diagram: it says how high the Sun is and which way you face.
void drawGround(ImDrawList* dl, const SkyProjection& proj, const PanelState& ps,
                ImVec2 p0, ImVec2 p1, float light) {
    float hy = proj.horizonY();
    if (hy < p1.y) {
        float top = std::max(hy, p0.y);
        RGB near_{0.085f, 0.080f, 0.075f}, far_{0.030f, 0.032f, 0.040f};
        near_ = mix(RGB{0.020f, 0.022f, 0.030f}, near_, clamp01f(light + 0.15f));
        far_  = mix(RGB{0.012f, 0.014f, 0.022f}, far_,  clamp01f(light + 0.15f));
        dl->AddRectFilledMultiColor(ImVec2(p0.x, top), ImVec2(p1.x, p1.y),
                                    toCol(far_), toCol(far_), toCol(near_), toCol(near_));
    }
    if (hy < p0.y || hy > p1.y) return;

    dl->AddLine(ImVec2(p0.x, hy), ImVec2(p1.x, hy), IM_COL32(140, 150, 170, 90), 1.0f);
    // Compass ticks along the horizon.
    for (int a = 0; a < 360; a += 15) {
        float x = proj.azimuthX(a);
        if (x < p0.x || x > p1.x) continue;
        // Only bearings in front of the observer; the tangent maps the ones
        // behind onto the same screen row.
        double d = a - proj.viewAz;
        while (d < -180.0) d += 360.0; while (d > 180.0) d -= 360.0;
        if (std::fabs(d) > 88.0) continue;
        bool cardinal = (a % 90) == 0;
        float len = cardinal ? UiS(9.0f) : UiS(4.5f);
        dl->AddLine(ImVec2(x, hy), ImVec2(x, hy - len), IM_COL32(170, 190, 215, cardinal ? 165 : 90), 1.0f);
        if (cardinal) {
            const char* zh[] = {"北", "东", "南", "西"}; // 北 东 南 西
            const char* en[] = {"N", "E", "S", "W"};
            const char* s = ps.useChinese ? zh[a / 90] : en[a / 90];
            ImVec2 ts = ImGui::CalcTextSize(s);
            dl->AddText(ImVec2(x - ts.x * 0.5f, hy + UiS(3.0f)), IM_COL32(186, 206, 232, 190), s);
        }
    }
}

// ---------------------------------------------------------------------------
//  The Sun, eclipsed
// ---------------------------------------------------------------------------
void drawCorona(ImDrawList* dl, ImVec2 c, float rm, float strength) {
    if (strength <= 0.01f) return;
    // A soft halo first, then streamers over it, then the bright inner ring.
    // The corona is a glow with structure in it, not a starburst: the light
    // falls off steeply from the limb, so most of the brightness belongs to the
    // first half-radius and the rays only show against what is left.
    addGlare(dl, c, rm, 214, 226, 248, 0.72f * strength, 0.85f);
    for (int i = 0; i < 150; ++i) {
        float a = (float)(i / 150.0 * 6.28318530718);
        float len = 0.30f + 1.5f * std::pow(hash01(i * 7 + 5), 2.6f);
        // Streamers reach further round the equator than over the poles, as
        // they do at solar minimum; the axis here is arbitrary, the shape is not.
        len *= 0.45f + 0.85f * std::fabs(std::cos(a));
        // Fade each one along its length instead of ruling a hard line to its
        // tip, which is what made them read as spokes.
        const int steps = 5;
        for (int k = 0; k < steps; ++k) {
            float t0 = (float)k / steps, t1 = (float)(k + 1) / steps;
            float r0 = rm * (1.0f + len * t0), r1 = rm * (1.0f + len * t1);
            int alpha = (int)(26.0f * strength * (1.0f - t1) * (1.0f - t1) *
                              (0.5f + 0.9f * hash01(i * 7 + 6)));
            if (alpha <= 1) break;
            ImVec2 d{std::cos(a), std::sin(a)};
            dl->AddLine(ImVec2(c.x + d.x * r0, c.y + d.y * r0),
                        ImVec2(c.x + d.x * r1, c.y + d.y * r1),
                        IM_COL32(224, 232, 248, alpha), std::max(1.0f, rm * 0.045f));
        }
    }
    addRadialGlow(dl, c, rm * 0.99f, rm * 1.30f,
                  IM_COL32(244, 248, 254, (int)(190 * strength)),
                  IM_COL32(226, 236, 252, (int)(40 * strength)));
}

void drawSolarSky(ImDrawList* dl, const SkyView& sky, const SkyProjection& proj,
                  ImVec2 sunP) {
    const float ppd = proj.pxPerDeg();
    float rs = std::max((float)sky.sun.radiusDeg * ppd, 1.0f);
    float rm = std::max((float)sky.moon.radiusDeg * ppd, 1.0f);
    ImVec2 moonP{sunP.x + (float)sky.moonDx * ppd, sunP.y - (float)sky.moonDy * ppd};
    const float open = clamp01f((float)(1.0 - sky.obscuration));

    // Direction from the Sun's centre to the last of it still showing - the
    // side away from the Moon. Where the bead is, and which limb the
    // chromosphere is on.
    double ux = sky.moonDx, uy = sky.moonDy;
    double ul = std::sqrt(ux * ux + uy * uy);
    if (ul > 1e-9) { ux /= ul; uy /= ul; } else { ux = 1.0; uy = 0.0; }
    const float exposedAngle = std::atan2(-(float)uy, -(float)ux);

    // How much corona to show. It is only ever visible with the photosphere
    // gone, but the last seconds before that are the diamond ring, when the
    // inner corona is already out - so it fades in rather than switching on.
    float corona = 0.0f;
    if (sky.total) corona = 1.0f;
    else if (sky.magnitude > 0.985 && sky.moon.radiusDeg > sky.sun.radiusDeg)
        corona = clamp01f((float)((sky.magnitude - 0.985) / 0.015)) * 0.7f;

    // Order matters, and it is the order the light itself is in: the Sun, then
    // its corona, then the Moon in front of both, then what escapes past the
    // Moon's edge.
    if (!sky.total) {
        addGlare(dl, sunP, rs * (0.55f + 0.45f * open), 255, 243, 205,
                 0.35f + 0.65f * open, 1.15f);
        dl->AddCircleFilled(sunP, rs, IM_COL32(255, 250, 232, 255), 72);
    }
    if (corona > 0.0f) drawCorona(dl, moonP, rm, corona);

    // The Moon itself: a hole in the sky, not a grey disc. Darker than the
    // darkest sky, so its edge stays readable at every phase of the eclipse.
    dl->AddCircleFilled(moonP, rm, IM_COL32(6, 7, 12, 255), 72);

    if (sky.total) {
        // How near the edge of totality: 1 at second and third contact, 0 at
        // mid-eclipse. The chromosphere is the red rim that shows for a few
        // seconds either side, on the limb the Moon is about to leave.
        double room = sky.moon.radiusDeg - sky.sun.radiusDeg;
        float edge = room > 1e-6 ? clamp01f((float)(sky.separationDeg / room)) : 1.0f;
        if (edge > 0.55f) {
            float f = (edge - 0.55f) / 0.45f;
            dl->PathArcTo(moonP, rm * 1.02f, exposedAngle - 0.55f, exposedAngle + 0.55f, 24);
            dl->PathStroke(IM_COL32(255, 96, 84, (int)(200 * f)), 0, std::max(1.5f, rm * 0.05f));
            dl->AddCircleFilled(ImVec2(moonP.x + std::cos(exposedAngle) * rm,
                                       moonP.y + std::sin(exposedAngle) * rm),
                                rm * 0.10f * f + 1.0f, IM_COL32(255, 240, 220, (int)(230 * f)), 16);
        }
    } else if (sky.magnitude > 0.94 && sky.moon.radiusDeg > sky.sun.radiusDeg) {
        // The diamond ring: the last bead of photosphere before totality closes
        // over, and the first one out the other side.
        float f = clamp01f((float)((sky.magnitude - 0.94) / 0.06));
        ImVec2 bead{sunP.x + std::cos(exposedAngle) * rs * 0.97f,
                    sunP.y + std::sin(exposedAngle) * rs * 0.97f};
        addGlare(dl, bead, std::max(rs * 0.14f, 1.5f), 255, 248, 226, 0.5f * f, 1.5f);
        dl->AddCircleFilled(bead, std::max(1.5f, rs * 0.13f), IM_COL32(255, 253, 244, 255), 16);
    }
}

// ---------------------------------------------------------------------------
//  The Moon, in Earth's shadow
// ---------------------------------------------------------------------------
void drawLunarSky(ImDrawList* dl, const SkyView& sky, const SkyProjection& proj,
                  ImVec2 moonP) {
    const float ppd = proj.pxPerDeg();
    float rmoon = std::max((float)sky.moon.radiusDeg * ppd, 2.0f);
    ImVec2 shadowP{moonP.x + (float)sky.shadowDx * ppd, moonP.y - (float)sky.shadowDy * ppd};
    float rUmb = (float)sky.umbraRadiusDeg * ppd;
    float rPen = (float)sky.penumbraRadiusDeg * ppd;

    // Glow around a full Moon, dimming as the shadow takes it.
    float bright = clamp01f(1.0f - (float)sky.umbralMagnitude * 0.85f);
    addGlare(dl, moonP, rmoon, 198, 210, 236, 0.20f * bright, 1.1f);

    dl->AddCircleFilled(moonP, rmoon, IM_COL32(236, 232, 219, 255), 96);
    // Maria, so the disc reads as the Moon and not as a white counter. The dark
    // plains of the near side - the only side there is to see - laid out to
    // look like the Moon rather than to map it.
    const float maria[][3] = {
        {-0.30f, -0.42f, 0.21f}, {-0.11f, -0.31f, 0.18f},  // Imbrium
        { 0.12f, -0.36f, 0.15f}, { 0.24f, -0.17f, 0.13f},  // Serenitatis
        { 0.19f,  0.05f, 0.18f},                           // Tranquillitatis
        { 0.47f, -0.29f, 0.08f},                           // Crisium
        {-0.47f,  0.01f, 0.16f}, {-0.41f,  0.27f, 0.15f},  // Procellarum
        { 0.03f,  0.33f, 0.13f},                           // Nubium
    };
    for (const auto& m : maria) {
        ImVec2 c{moonP.x + m[0] * rmoon, moonP.y + m[1] * rmoon};
        dl->AddCircleFilled(c, m[2] * rmoon, IM_COL32(186, 183, 176, 130), 24);
        dl->AddCircleFilled(c, m[2] * rmoon * 0.72f, IM_COL32(176, 174, 168, 110), 24);
    }
    // A touch of shading at the very edge. A full Moon has almost no limb
    // darkening - that is why it looks flat - but a little of it is what keeps
    // the disc from reading as a paper cut-out.
    addRadialGlow(dl, moonP, rmoon * 0.86f, rmoon,
                  IM_COL32(120, 116, 108, 0), IM_COL32(120, 116, 108, 70));

    // Penumbra first: a wash rather than an edge, which is exactly how it looks
    // - most of it is invisible and only the inner part shades the disc.
    if (rPen > 0.0f)
        addDiscIntersection(dl, moonP, rmoon, shadowP, rPen, IM_COL32(24, 26, 40, 60), 96);
    // Umbra: not black. Earth's atmosphere bends red sunlight into its own
    // shadow, which is why a totally eclipsed Moon is copper and not missing.
    if (rUmb > 0.0f) {
        // The umbra is neither black nor even. Earth's atmosphere bends red
        // sunlight into its own shadow - which is why a totally eclipsed Moon
        // is copper and not missing - and the light that reaches the middle has
        // passed deepest through that atmosphere, so the shadow darkens inward.
        // None of the layers is opaque: the maria stay visible through the
        // colour, as they do to anyone actually looking.
        const struct { float r; int a; } layers[] =
            {{1.08f, 90}, {1.00f, 150}, {0.78f, 70}, {0.50f, 55}};
        for (const auto& L : layers)
            addDiscIntersection(dl, moonP, rmoon, shadowP, rUmb * L.r,
                                IM_COL32(96, 34, 22, L.a), 96);
        // The shadow's own outline, out where it falls on nothing: the circle
        // whose size is the whole reason a lunar eclipse takes hours.
        dl->AddCircle(shadowP, rUmb, IM_COL32(150, 90, 80, 60), 96, 1.0f);
    }
}

// ---------------------------------------------------------------------------
//  Where the observer stands
// ---------------------------------------------------------------------------
// Where to stand when the observer is left to the centre line: the point that
// sees greatest eclipse, held there for the whole run.
//
// Held, not slid along with the shadow. An observer who moved with it would be
// under totality at every instant and would never see the eclipse happen - the
// hour of partial phases, the light going, the moment it comes back - which is
// the entire thing the ground view is for. A fixed home town has the opposite
// problem: it sees nothing at all for all but a few eclipses a century.
bool centreLineObserver(const PanelState& ps, const EclipseEvent& e,
                        double& lon, double& lat) {
    if (e.hasCenter) {
        lon = e.centerLongitudeDeg;
        lat = e.centerLatitudeDeg;
        return true;
    }
    // A partial eclipse has no central line; the best there is, is wherever the
    // penumbra is deepest at maximum.
    const EclipsePathSample* best = nullptr;
    for (const EclipsePathSample& s : ps.eclipsePath) {
        if (!s.center.valid) continue;
        if (!best || std::fabs(s.jdTd - e.maximumTd) < std::fabs(best->jdTd - e.maximumTd))
            best = &s;
    }
    if (!best) return false;
    lon = best->center.longitudeDeg;
    lat = best->center.latitudeDeg;
    return true;
}

std::string localStamp(double jdTd, float tzHours) {
    Date d = setFromJD(eclipseTdToUtcJD(jdTd) + tzHours / 24.0 + 0.5 / 86400.0);
    char buf[48];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d",
                  d.Y, d.M, d.D, d.h, d.m, (int)d.s);
    return buf;
}

// A cache, because MSC::calc is a full ephemeris evaluation and this runs every
// frame. Recomputed when the clock or the place has actually moved.
SkyView cachedSkyView(double jdTd, double lon, double lat, double altKm) {
    static SkyView cache;
    static double cT = -1e30, cLon = 1e30, cLat = 1e30, cAlt = 1e30;
    if (std::fabs(jdTd - cT) > 0.2 / 86400.0 || lon != cLon || lat != cLat || altKm != cAlt) {
        cache = computeSkyView(jdTd, lon, lat, altKm);
        cT = jdTd; cLon = lon; cLat = lat; cAlt = altKm;
    }
    return cache;
}

} // namespace

bool DrawGroundEclipseView(Scene& scene, PanelState& ps, const EclipseEvent& e,
                           ImVec2 origin, float w, float h, bool hovered) {
    ImGuiIO& io = ImGui::GetIO();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 p0 = origin, p1{origin.x + w, origin.y + h};
    const bool solar = (e.kind == EclipseEvent::Solar);

    double ut = scene.clock().jd - kJ2K;
    const double nowTd = ut + dt_T(ut);

    double lon = ps.observerLongitude, lat = ps.observerLatitude;
    bool followed = false;
    if (solar && ps.groundFollowCenter && centreLineObserver(ps, e, lon, lat)) followed = true;

    SkyView sky = cachedSkyView(nowTd, lon, lat, ps.observerAltitudeKm);

    // ---- Input: look around, and zoom the field of view ---------------------
    ps.groundFovDeg = std::clamp(ps.groundFovDeg, 0.6f, 110.0f);
    if (hovered) {
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
            float ppd = (float)(0.5 * std::min(w, h) / std::tan(deg2rad(ps.groundFovDeg * 0.5)))
                      * (float)(kPI / 180.0);
            ps.groundLookYawDeg   -= io.MouseDelta.x / std::max(ppd, 1e-3f);
            ps.groundLookPitchDeg += io.MouseDelta.y / std::max(ppd, 1e-3f);
        }
        if (io.MouseWheel != 0.0f)
            ps.groundFovDeg *= std::pow(0.88f, std::clamp(io.MouseWheel, -8.0f, 8.0f));
        if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            ps.groundLookYawDeg = ps.groundLookPitchDeg = 0.0f;
    }
    ps.groundFovDeg = std::clamp(ps.groundFovDeg, 0.6f, 110.0f);
    ps.groundLookYawDeg = std::clamp(ps.groundLookYawDeg, -120.0f, 120.0f);
    ps.groundLookPitchDeg = std::clamp(ps.groundLookPitchDeg, -75.0f, 75.0f);

    const SkyBodyView& target = solar ? sky.sun : sky.moon;
    SkyProjection proj;
    proj.centre = ImVec2(origin.x + w * 0.5f, origin.y + h * 0.5f);
    proj.k = (float)(0.5 * std::min(w, h) / std::tan(deg2rad(ps.groundFovDeg * 0.5)));
    proj.viewAz = target.azimuthDeg + ps.groundLookYawDeg;
    proj.viewAlt = std::clamp(target.altitudeDeg + ps.groundLookPitchDeg, -85.0, 85.0);

    // Two independent terms: how much of the Sun is left (the eclipse) and where
    // the Sun is (the ordinary day and night). A lunar eclipse leaves the first
    // alone - it is the Moon being eclipsed, not the sky - and lets the second
    // decide, which is why the Moon can be in the umbra before the sky is dark.
    const float light = solar ? (float)sky.daylight : 1.0f;
    const float sunLight = clamp01f((float)((sky.sun.altitudeDeg + 6.0) / 10.0));
    const float skyLight = light * sunLight;

    // ---- Sky ---------------------------------------------------------------
    dl->PushClipRect(p0, p1, true);
    const int bands = 44;
    for (int i = 0; i < bands; ++i) {
        float y0 = p0.y + (h * i) / bands, y1 = p0.y + (h * (i + 1)) / bands;
        RGB c0 = skyColour(proj.altitudeAtRow(y0), sky.sun.altitudeDeg, light);
        RGB c1 = skyColour(proj.altitudeAtRow(y1), sky.sun.altitudeDeg, light);
        dl->AddRectFilledMultiColor(ImVec2(p0.x, y0), ImVec2(p1.x, y1),
                                    toCol(c0), toCol(c0), toCol(c1), toCol(c1));
    }
    float starVis = clamp01f(1.0f - skyLight * 2.4f);
    drawStars(dl, proj, p0, p1, starVis);

    // ---- The bodies --------------------------------------------------------
    const bool targetUp = target.altitudeDeg > -target.radiusDeg;
    if (targetUp) {
        ImVec2 tp = proj.project(target.azimuthDeg, target.altitudeDeg);
        if (solar) drawSolarSky(dl, sky, proj, tp);
        else       drawLunarSky(dl, sky, proj, tp);
    }
    drawGround(dl, proj, ps, p0, p1, skyLight);
    dl->PopClipRect();

    // ---- Readout -----------------------------------------------------------
    // Same translucent slab as the body card in the orbital view: the view is
    // the subject, and a framed window would read as furniture.
    {
        std::vector<std::pair<std::string, std::string>> rows;
        char buf[96];
        std::snprintf(buf, sizeof(buf), "%.3f°%s  %.3f°%s",
                      std::fabs(lon), lon >= 0 ? "E" : "W",
                      std::fabs(lat), lat >= 0 ? "N" : "S");
        rows.push_back({UI(ps, "观测地", "Observer"),
                        std::string(buf) + (followed ? UI(ps, "  中心线", "  centre line") : "")});
        rows.push_back({UI(ps, "时刻", "Time"), localStamp(nowTd, ps.timezoneHours)});
        if (solar) {
            std::snprintf(buf, sizeof(buf), "%.3f", sky.magnitude);
            rows.push_back({UI(ps, "食分", "Magnitude"), buf});
            // A hair under total is not total, and rounding it up to 100%
            // contradicts the phase on the line below.
            double pct = sky.obscuration * 100.0;
            if (!sky.total && pct > 99.9) pct = 99.9;
            std::snprintf(buf, sizeof(buf), "%.1f%%", pct);
            rows.push_back({UI(ps, "面积食分", "Obscuration"), buf});
            std::snprintf(buf, sizeof(buf), "%.1f° / %.1f°",
                          sky.sun.altitudeDeg, sky.sun.azimuthDeg);
            rows.push_back({UI(ps, "高度/方位", "Alt / Az"), buf});
            const char* state = sky.total   ? UI(ps, "全食", "Totality")
                              : sky.annular ? UI(ps, "环食", "Annular")
                              : sky.obscuration > 0.0 ? UI(ps, "偏食中", "Partial")
                                                      : UI(ps, "未开始 / 已结束", "Not in progress");
            rows.push_back({UI(ps, "阶段", "Phase"), state});
        } else {
            std::snprintf(buf, sizeof(buf), "%.3f", sky.umbralMagnitude);
            rows.push_back({UI(ps, "本影食分", "Umbral mag."), buf});
            std::snprintf(buf, sizeof(buf), "%.3f", sky.penumbralMagnitude);
            rows.push_back({UI(ps, "半影食分", "Penumbral mag."), buf});
            std::snprintf(buf, sizeof(buf), "%.1f° / %.1f°",
                          sky.moon.altitudeDeg, sky.moon.azimuthDeg);
            rows.push_back({UI(ps, "高度/方位", "Alt / Az"), buf});
            const char* state = sky.umbralMagnitude >= 1.0 ? UI(ps, "月全食", "Total")
                              : sky.umbralMagnitude > 0.0  ? UI(ps, "月偏食", "Partial")
                              : sky.penumbralMagnitude > 0.0 ? UI(ps, "半影食", "Penumbral")
                                                             : UI(ps, "未开始 / 已结束", "Not in progress");
            rows.push_back({UI(ps, "阶段", "Phase"), state});
        }

        float labelW = 0.0f, valueW = 0.0f;
        for (auto& r : rows) {
            labelW = std::max(labelW, ImGui::CalcTextSize(r.first.c_str()).x);
            valueW = std::max(valueW, ImGui::CalcTextSize(r.second.c_str()).x);
        }
        float lineH = ImGui::GetTextLineHeightWithSpacing();
        float padX = UiS(11.0f), padY = UiS(9.0f), gap = UiS(12.0f);
        ImVec2 c0{p0.x + UiS(12.0f), p0.y + UiS(46.0f)};
        ImVec2 c1{c0.x + padX * 2 + labelW + gap + valueW, c0.y + padY * 2 + lineH * rows.size()};
        dl->AddRectFilled(c0, c1, IM_COL32(8, 12, 22, 170), UiS(6.0f));
        dl->AddRect(c0, c1, IM_COL32(120, 160, 210, 70), UiS(6.0f), 0, 1.0f);
        float ty = c0.y + padY;
        for (auto& r : rows) {
            dl->AddText(ImVec2(c0.x + padX, ty), IM_COL32(158, 186, 220, 225), r.first.c_str());
            dl->AddText(ImVec2(c0.x + padX + labelW + gap, ty), IM_COL32(232, 240, 250, 245),
                        r.second.c_str());
            ty += lineH;
        }

        // When the body is under the horizon there is nothing to look at, and
        // saying why is more use than an empty sky.
        if (!targetUp) {
            const char* msg = solar
                ? UI(ps, "此地此刻太阳在地平线下",
                         "The Sun is below the horizon here")
                : UI(ps, "此地此刻月亮在地平线下",
                         "The Moon is below the horizon here");
            ImVec2 ts = ImGui::CalcTextSize(msg);
            ImVec2 mp{proj.centre.x - ts.x * 0.5f, proj.centre.y - ts.y * 0.5f};
            dl->AddRectFilled(ImVec2(mp.x - UiS(10.0f), mp.y - UiS(6.0f)),
                              ImVec2(mp.x + ts.x + UiS(10.0f), mp.y + ts.y + UiS(6.0f)),
                              IM_COL32(10, 14, 24, 190), UiS(5.0f));
            dl->AddText(mp, IM_COL32(240, 214, 170, 240), msg);
        }
    }

    // ---- Hint --------------------------------------------------------------
    {
        char buf[128];
        std::snprintf(buf, sizeof(buf), "%s  %.1f°",
                      UI(ps, "拖动看四周，滚轮缩放，双击归位。视场",
                             "Drag to look around, wheel to zoom, double-click to recentre. FOV"),
                      ps.groundFovDeg);
        ImVec2 ts = ImGui::CalcTextSize(buf);
        ImVec2 hp{p0.x + UiS(12.0f), p1.y - ts.y - UiS(14.0f)};
        dl->AddRectFilled(ImVec2(hp.x - UiS(8.0f), hp.y - UiS(5.0f)),
                          ImVec2(hp.x + ts.x + UiS(8.0f), hp.y + ts.y + UiS(5.0f)),
                          IM_COL32(6, 10, 20, 140), UiS(4.0f));
        dl->AddText(hp, IM_COL32(150, 178, 208, 210), buf);
    }

    return true;
}

} // namespace sx
