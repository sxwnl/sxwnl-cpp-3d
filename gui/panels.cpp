#include "panels.h"
#include "ground_view.h"

#include <algorithm>
#include <cfloat>
#include <cstdio>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <vector>

#include "imgui.h"

#include "../lunar/lunar.h"
#include "../lunar/lunar_ob.h"
#include "../lunar/huangli.h"
#include "../eph/eph.h"
#include "../eph/eph0.h"
#include "../eph/eph_show.h"
#include "../mylib/tool.h"
#include "../mylib/lat_lon_data.h"

namespace sx {

static const double kPI  = 3.14159265358979323846;
static const double kJ2K = 2451545.0;
static const float kSideMinW = 190.0f;
static const float kSideMaxW = 460.0f;
static const float kToolsMinW = 300.0f;
static const float kToolsMaxW = 660.0f;
static const float kViewportMinW = 360.0f;
static const float kResizeHandleW = 11.0f;
static const float kRailW = 34.0f;
static const char* kAppIniPath = "sxwnl_gui.ini";

// ---- Global UI scale (HiDPI / touch) ---------------------------------------
// 1.0 on desktop (all S()/scaled-accessor calls collapse to the base value, so
// desktop behaviour is unchanged). Android sets this from screen density so the
// whole layout — panel widths, rails, splitters, fixed-size cards and the
// custom-drawn controls — grows together with the scaled font.
static float g_uiScale = 1.0f;
void SetUiScale(float scale) { g_uiScale = std::clamp(scale, 1.0f, 4.0f); }
float GetUiScale() { return g_uiScale; }
static inline float S(float v) { return v * g_uiScale; }
float UiS(float v) { return v * g_uiScale; }

float UiAnimEase(double startTime, float seconds) {
    if (seconds <= 0.0f) return 1.0f;
    float t = (float)((ImGui::GetTime() - startTime) / seconds);
    if (t <= 0.0f) return 0.0f;
    if (t >= 1.0f) return 1.0f;
    float inv = 1.0f - t;
    return 1.0f - inv * inv * inv;          // ease out cubic
}

float UiAnimApproach(float tau) {
    float dt = ImGui::GetIO().DeltaTime;
    if (tau <= 0.0f || dt <= 0.0f) return 1.0f;
    return 1.0f - std::exp(-dt / tau);
}

// Touch mode suppresses hover-only affordances (tooltips), which on a
// touchscreen fire on every tap and cover the thing that was just tapped.
static bool g_touchMode = false;
void SetTouchMode(bool on) { g_touchMode = on; }
bool GetTouchMode() { return g_touchMode; }

// Font registry. The desktop build leaves these null and everything renders in
// the single default font; Android registers three sizes so the mobile shell
// can use a larger face for titles and a denser one for tables and readouts.
static ImFont* g_fontBody  = nullptr;
static ImFont* g_fontSmall = nullptr;
static ImFont* g_fontTitle = nullptr;
void SetUiFonts(ImFont* body, ImFont* small_, ImFont* title) {
    g_fontBody = body; g_fontSmall = small_; g_fontTitle = title;
}
ImFont* UiFontBody()  { return g_fontBody; }
ImFont* UiFontSmall() { return g_fontSmall ? g_fontSmall : g_fontBody; }
ImFont* UiFontTitle() { return g_fontTitle ? g_fontTitle : g_fontBody; }

// Astronomy symbols: sun/moon, the planet signs and the twelve zodiac signs
// (U+263D-U+2653), plus U+26E2 (the astronomical sign for Uranus). Noto Sans
// CJK carries none of them, which is why 星座 used to print as "天秤座??" - the
// zodiac sign and its variation selector both fell back to the missing-glyph
// mark. They come from a 5 KB subset of Noto Sans Symbols merged into the same
// ImFont, so text can mix Chinese and signs in one string.
const ImWchar* AstroSymbolGlyphRange() {
    static const ImWchar ranges[] = { 0x263D, 0x2653, 0x26E2, 0x26E2, 0 };
    return ranges;
}

bool AddAstroSymbolFont(const char* path, float sizePixels) {
    if (!path || !path[0]) return false;
    FILE* probe = std::fopen(path, "rb");
    if (!probe) return false;
    std::fclose(probe);

    ImFontConfig cfg;
    cfg.MergeMode = true;
    // Noto Sans Symbols declares a much taller ascent+descent than Noto Sans
    // CJK (2050 vs 1448 units per em), and the rasterizer fits that box to the
    // requested pixel height - so at the same size its signs come out about
    // half as tall as the Chinese text and sit low. These two numbers scale
    // them back up and put them on the same visual line.
    cfg.ExtraSizeScale = 1.55f;
    cfg.GlyphOffset = ImVec2(0.0f, -sizePixels * 0.15f);
    return ImGui::GetIO().Fonts->AddFontFromFileTTF(
               path, sizePixels, &cfg, AstroSymbolGlyphRange()) != nullptr;
}

static float sSideMinW()   { return kSideMinW   * g_uiScale; }
static float sSideMaxW()   { return kSideMaxW   * g_uiScale; }
static float sToolsMinW()  { return kToolsMinW  * g_uiScale; }
static float sToolsMaxW()  { return kToolsMaxW  * g_uiScale; }
static float sViewportMinW(){ return kViewportMinW * g_uiScale; }
static float sRailW()      { return kRailW      * g_uiScale; }
static float sResizeHandleW(){ return kResizeHandleW * g_uiScale; }

// Small helpers.
const char* UI(const PanelState& ps, const char* zh, const char* en) {
    return ps.useChinese ? zh : en;
}

static bool parseBool(const std::string& v, bool fallback) {
    if (v == "1" || v == "true" || v == "True") return true;
    if (v == "0" || v == "false" || v == "False") return false;
    return fallback;
}

static float parseFloat(const std::string& v, float fallback) {
    char* end = nullptr;
    float f = std::strtof(v.c_str(), &end);
    return (end && end != v.c_str()) ? f : fallback;
}

static int parseInt(const std::string& v, int fallback) {
    char* end = nullptr;
    long n = std::strtol(v.c_str(), &end, 10);
    return (end && end != v.c_str()) ? (int)n : fallback;
}

static float leftPanelWidth(const PanelState& ps) {
    return ps.leftCollapsed ? sRailW() : std::clamp(ps.leftPanelWidth, sSideMinW(), sSideMaxW());
}

static float toolsPanelWidth(const PanelState& ps) {
    return ps.toolsCollapsed ? sRailW() : std::clamp(ps.toolsPanelWidth, sToolsMinW(), sToolsMaxW());
}

static void normalizePanelWidths(PanelState& ps, float displayW) {
    ps.leftPanelWidth = std::clamp(ps.leftPanelWidth, sSideMinW(), sSideMaxW());
    ps.toolsPanelWidth = std::clamp(ps.toolsPanelWidth, sToolsMinW(), sToolsMaxW());

    float leftW = ps.leftCollapsed ? sRailW() : ps.leftPanelWidth;
    float toolsW = ps.toolsCollapsed ? sRailW() : ps.toolsPanelWidth;
    float overflow = leftW + toolsW + sViewportMinW() - std::max(displayW, sViewportMinW() + sRailW() * 2.0f);
    if (overflow <= 0.0f) return;

    if (!ps.toolsCollapsed) {
        float reduce = std::min(overflow, ps.toolsPanelWidth - sToolsMinW());
        ps.toolsPanelWidth -= reduce;
        overflow -= reduce;
    }
    if (overflow > 0.0f && !ps.leftCollapsed) {
        float reduce = std::min(overflow, ps.leftPanelWidth - sSideMinW());
        ps.leftPanelWidth -= reduce;
    }
}

static bool PanelTopCollapseButton(const char* id, const char* label, bool collapsed, bool leftSide) {
    ImVec2 p = ImGui::GetCursorScreenPos();
    float w = std::max(ImGui::GetContentRegionAvail().x, S(24.0f));
    float h = S(26.0f);
    bool hit = ImGui::InvisibleButton(id, ImVec2(w, h));
    bool hov = ImGui::IsItemHovered();
    bool act = ImGui::IsItemActive();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImU32 bg = act ? IM_COL32(38, 62, 105, 235)
             : hov ? IM_COL32(30, 50, 88, 230)
                   : IM_COL32(14, 23, 42, 220);
    ImU32 bd = hov ? IM_COL32(104, 150, 220, 150) : IM_COL32(55, 82, 125, 120);
    ImU32 fg = hov ? IM_COL32(225, 236, 255, 255) : IM_COL32(178, 204, 238, 255);
    dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), bg, 4.0f);
    dl->AddRect(p, ImVec2(p.x + w, p.y + h), bd, 4.0f, 0, 1.0f);

    float dir = (collapsed ? 1.0f : -1.0f) * (leftSide ? 1.0f : -1.0f);
    float cx = p.x + S(12.0f);
    float cy = p.y + h * 0.5f;
    dl->AddTriangleFilled(ImVec2(cx + dir * S(4.5f), cy),
                          ImVec2(cx - dir * S(3.5f), cy - S(6.0f)),
                          ImVec2(cx - dir * S(3.5f), cy + S(6.0f)),
                          fg);
    if (!collapsed && label && label[0]) {
        dl->AddText(ImVec2(p.x + S(28.0f), p.y + (h - ImGui::GetFontSize()) * 0.5f),
                    IM_COL32(190, 214, 245, 245), label);
    }
    return hit;
}

static void DrawSplitterOverlay(const char* id, float centerX, float topY, float height,
                                bool leftPanel, float& width, float minW, float maxW) {
    ImGuiIO& io = ImGui::GetIO();
    float h = std::max(24.0f, height);
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
                             ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                             ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
                             ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBackground |
                             ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav;

    float hw = sResizeHandleW();
    ImGui::SetNextWindowPos(ImVec2(centerX - hw * 0.5f, topY), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(hw, h), ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::Begin(id, nullptr, flags);
    ImGui::InvisibleButton("##drag", ImVec2(hw, h));
    bool hovered = ImGui::IsItemHovered();
    bool active = ImGui::IsItemActive();
    if (hovered || active) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    if (active && io.MouseDelta.x != 0.0f) {
        width += leftPanel ? io.MouseDelta.x : -io.MouseDelta.x;
        width = std::clamp(width, minW, maxW);
    }

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImU32 col = active ? IM_COL32(120, 170, 245, 210)
              : hovered ? IM_COL32(92, 136, 210, 175)
                        : IM_COL32(45, 68, 108, 95);
    ImVec2 p = ImGui::GetWindowPos();
    float cx = p.x + hw * 0.5f;
    dl->AddRectFilled(ImVec2(cx - 1.5f, p.y + 4.0f), ImVec2(cx + 1.5f, p.y + h - 4.0f), col, 2.0f);

    // A grip at the middle. A 3 px line is a fair target for a mouse only once
    // you know it is a target at all: the handle reads as a border until
    // something on it says otherwise, and then the pointer has to be put on the
    // border exactly. The knob is both the sign and the bigger thing to aim at.
    float cy = p.y + h * 0.5f;
    float r  = S(active ? 6.5f : hovered ? 6.0f : 5.0f);
    ImU32 knob = active ? IM_COL32(150, 195, 255, 245)
               : hovered ? IM_COL32(120, 168, 235, 225)
                         : IM_COL32(88, 126, 186, 205);
    dl->AddCircleFilled(ImVec2(cx, cy), r + S(1.0f), IM_COL32(10, 16, 28, 190), 20);
    dl->AddCircleFilled(ImVec2(cx, cy), r, knob, 20);
    // Three dots inside it, the usual sign for "this is a handle".
    ImU32 dot = IM_COL32(14, 22, 38, active || hovered ? 235 : 200);
    for (int i = -1; i <= 1; ++i)
        dl->AddCircleFilled(ImVec2(cx, cy + i * S(3.4f)), S(0.9f), dot, 8);
    ImGui::End();
    ImGui::PopStyleVar();
}

void SectionHeader(const PanelState& ps, const char* zh, const char* en) {
    const char* label = UI(ps, zh, en);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetCursorScreenPos();
    float w = ImGui::GetContentRegionAvail().x;
    float h = ImGui::GetTextLineHeight() + S(10.0f);
    dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), IM_COL32(12, 21, 38, 150), 4.0f);
    dl->AddRectFilled(ImVec2(p.x, p.y + S(4.0f)), ImVec2(p.x + S(3.0f), p.y + h - S(4.0f)),
                      IM_COL32(85, 158, 230, 220), 2.0f);
    ImGui::SetCursorScreenPos(ImVec2(p.x + S(9.0f), p.y + S(5.0f)));
    ImGui::TextColored(ImVec4(0.67f, 0.82f, 1.0f, 1.0f), "%s", label);
    ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + h + S(4.0f)));
}

static void InfoRow(const PanelState& ps, const char* zh, const char* en, const char* value) {
    ImGui::PushID(zh);
    if (ImGui::BeginTable("##info_row", 2, ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("label", ImGuiTableColumnFlags_WidthFixed, S(ps.useChinese ? 70.0f : 92.0f));
        ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextDisabled("%s", UI(ps, zh, en));
        ImGui::TableSetColumnIndex(1);
        ImGui::TextUnformatted(value);
        ImGui::EndTable();
    }
    ImGui::PopID();
}

static const char* BodyLabel(const PanelState& ps, const BodyInfo& b) {
    if (ps.useChinese) return b.name.c_str();
    if (b.pinyin == "sun")     return "Sun";
    if (b.pinyin == "mercury") return "Mercury";
    if (b.pinyin == "venus")   return "Venus";
    if (b.pinyin == "earth")   return "Earth";
    if (b.pinyin == "mars")    return "Mars";
    if (b.pinyin == "jupiter") return "Jupiter";
    if (b.pinyin == "saturn")  return "Saturn";
    if (b.pinyin == "uranus")  return "Uranus";
    if (b.pinyin == "neptune") return "Neptune";
    if (b.pinyin == "pluto")   return "Pluto";
    return b.name.c_str();
}

static int EphIndexForBody(const BodyInfo& b) {
    if (b.pinyin == "mercury") return 0;
    if (b.pinyin == "venus")   return 1;
    if (b.pinyin == "mars")    return 2;
    if (b.pinyin == "jupiter") return 3;
    if (b.pinyin == "saturn")  return 4;
    if (b.pinyin == "uranus")  return 5;
    if (b.pinyin == "neptune") return 6;
    if (b.pinyin == "pluto")   return 7;
    return -1;
}

static void SyncDateFromScene(PanelState& ps, const Scene& scene) {
    Date d = localDateFromUtcJD(scene.clock().jd, ps.timezoneHours);
    ps.year = d.Y;
    ps.month = d.M;
    ps.day = d.D;
    ps.hour = d.h;
    ps.minute = d.m;
}

static bool OpenSelectedEphemeris(PanelState& ps, const Scene& scene) {
    const auto& bodies = scene.bodies();
    int ephIdx = -1;
    if (ps.selectedBody >= 0 && ps.selectedBody < (int)bodies.size()) {
        ephIdx = EphIndexForBody(bodies[ps.selectedBody]);
    }
    SyncDateFromScene(ps, scene);
    if (ephIdx >= 0) ps.ephBodyIdx = ephIdx;
    ps.ephFollowSelection = (ephIdx >= 0);
    ps.ephSig = -1;
    ps.activeTab = 2;
    ps.toolsCollapsed = false;
    return ephIdx >= 0;
}

static const char* MoonPhaseLabel(const PanelState& ps, const std::string& name) {
    if (ps.useChinese) return name.c_str();
    if (name.find("new") != std::string::npos) return "New moon";
    if (name.find("crescent") != std::string::npos) return "Crescent";
    if (name.find("first") != std::string::npos) return "First quarter";
    if (name.find("gibbous") != std::string::npos) return "Waxing gibbous";
    if (name.find("full") != std::string::npos) return "Full moon";
    if (name.find("waning") != std::string::npos) return "Waning gibbous";
    if (name.find("last") != std::string::npos) return "Last quarter";
    return name.c_str();
}

static std::string ellipsizeText(const std::string& text, float maxWidth) {
    if (text.empty() || maxWidth <= 0.0f) return "";
    if (ImGui::CalcTextSize(text.c_str()).x <= maxWidth) return text;

    const char* ellipsis = "...";
    float ellipsisW = ImGui::CalcTextSize(ellipsis).x;
    if (ellipsisW > maxWidth) return "";

    size_t best = 0;
    for (size_t i = 0; i < text.size();) {
        unsigned char ch = static_cast<unsigned char>(text[i]);
        size_t step = 1;
        if ((ch & 0x80) == 0x00) step = 1;
        else if ((ch & 0xE0) == 0xC0) step = 2;
        else if ((ch & 0xF0) == 0xE0) step = 3;
        else if ((ch & 0xF8) == 0xF0) step = 4;

        size_t next = std::min(i + step, text.size());
        std::string candidate = text.substr(0, next) + ellipsis;
        if (ImGui::CalcTextSize(candidate.c_str()).x > maxWidth) break;
        best = next;
        i = next;
    }
    if (best == 0) return "";
    return text.substr(0, best) + ellipsis;
}

static void DrawSteppedIntField(const char* id, const char* label, int& value) {
    ImGui::PushID(id);
    if (label && label[0]) ImGui::TextDisabled("%s", label);
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputInt("##value", &value, 1, 10);
    ImGui::PopID();
}

// `withToday` adds a fourth cell holding a "today" button and returns whether
// it was pressed. It belongs on this row rather than under it: getting back to
// today is the same kind of act as stepping the month, and a stray button
// somewhere below reads as unrelated.
static bool DrawDateFields(PanelState& ps, const char* suffix,
                           int& year, int& month, int& day,
                           int* hour = nullptr, int* minute = nullptr,
                           bool withToday = false) {
    char id[64];
    bool today = false;
    const char* yearLabel = ps.useChinese ? "\345\271\264" : "Year";
    const char* monthLabel = ps.useChinese ? "\346\234\210" : "Month";
    const char* dayLabel = ps.useChinese ? "\346\227\245" : "Day";
    const char* todayLabel = UI(ps, "\u4eca\u5929", "Today");
    std::snprintf(id, sizeof(id), "date_fields_%s", suffix);
    if (ImGui::BeginTable(id, withToday ? 4 : 3, ImGuiTableFlags_SizingStretchSame)) {
        if (withToday) {
            ImGui::TableSetupColumn("y", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("m", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("d", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("t", ImGuiTableColumnFlags_WidthFixed,
                                    ImGui::CalcTextSize(todayLabel).x + S(20.0f));
        }
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        std::snprintf(id, sizeof(id), "year_%s", suffix);
        DrawSteppedIntField(id, yearLabel, year);

        ImGui::TableSetColumnIndex(1);
        std::snprintf(id, sizeof(id), "month_%s", suffix);
        DrawSteppedIntField(id, monthLabel, month);

        ImGui::TableSetColumnIndex(2);
        std::snprintf(id, sizeof(id), "day_%s", suffix);
        DrawSteppedIntField(id, dayLabel, day);

        if (withToday) {
            ImGui::TableSetColumnIndex(3);
            // An empty label line, so the button sits on the same baseline as
            // the three fields rather than riding up against their captions.
            ImGui::TextDisabled(" ");
            std::snprintf(id, sizeof(id), "today_%s", suffix);
            ImGui::PushID(id);
            today = ImGui::Button(todayLabel, ImVec2(-FLT_MIN, 0.0f));
            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    if (hour || minute) {
        int cols = minute ? 2 : 1;
        std::snprintf(id, sizeof(id), "time_fields_%s", suffix);
        if (ImGui::BeginTable(id, cols, ImGuiTableFlags_SizingStretchSame)) {
            ImGui::TableNextRow();
            if (hour) {
                ImGui::TableSetColumnIndex(0);
                std::snprintf(id, sizeof(id), "hour_%s", suffix);
                DrawSteppedIntField(id, UI(ps, "\u65f6", "Hour"), *hour);
            }
            if (minute) {
                ImGui::TableSetColumnIndex(cols - 1);
                std::snprintf(id, sizeof(id), "min_%s", suffix);
                DrawSteppedIntField(id, UI(ps, "\u5206", "Minute"), *minute);
            }
            ImGui::EndTable();
        }
    }
    return today;
}

// The next 节气 after a given day, as a name plus whole days remaining.
//
// Solar terms sit every 15 degrees of the Sun's apparent longitude, so rather
// than scanning the calendar this asks the ephemeris directly: round the
// current solar longitude up to the next multiple of 15 degrees and solve for
// the instant it is reached.
struct NextTerm {
    bool valid = false;
    std::string name;
    int daysAway = 0;
    std::string timeStr;
};

static NextTerm nextSolarTerm(double localJD, double timezoneHours) {
    NextTerm r;
    const double jd2k = localJD - timezoneHours / 24.0 - kJ2K;   // UTC, from J2000
    const double t = (jd2k + dt_T(jd2k)) / 36525.0;              // TD centuries

    double lon = S_aLon(t, -1);                                  // radians
    const double step = kPI / 12.0;                              // 15 degrees
    double k = std::floor(lon / step) + 1.0;

    // S_aLon_t works in absolute solar longitude, so keep the same revolution.
    const double W = k * step;
    const double tt = S_aLon_t(W);
    const double termJd2k = tt * 36525.0 - dt_T(tt * 36525.0);
    const double termLocal = termJd2k + kJ2K + timezoneHours / 24.0;

    double diff = termLocal - localJD;
    if (!std::isfinite(diff) || diff < 0.0 || diff > 40.0) return r;

    // Index 0 of the name table is 冬至, which is where W = 270 degrees falls.
    int idx = (int)std::lround(k) % 24;
    idx = ((idx - 18) % 24 + 24) % 24;

    r.valid = true;
    r.name = str_jqmc[idx];
    r.daysAway = (int)std::ceil(diff - 1e-6);
    r.timeStr = std::string(JD2str(termLocal).c_str());
    return r;
}

static std::string eventTimeStr(double T, double timezoneHours) {
    return std::string(JD2str(T*36525 + kJ2K + timezoneHours/24.0 - dt_T(T*36525)).c_str());
}

static std::string lunarMonthDay(const OB_DAY& d) {
    std::string s;
    if (!d.Lleap.empty()) s += d.Lleap.c_str();
    s += d.Lmc.c_str();
    s += "\u6708";
    s += d.Ldc.c_str();
    return s;
}

static std::string compactDayNote(const OB_DAY& d) {
    if (!d.jqmc.empty()) return d.jqmc.c_str();
    if (!d.A.empty()) return d.A.c_str();
    if (!d.B.empty()) return d.B.c_str();
    if (!d.yxmc.empty()) return d.yxmc.c_str();
    return "";
    if (d.Ldi == 0) {
        std::string s = d.Lmc.c_str();
        s += "\u6708";
        return s;
    }
    return d.Ldc.c_str();
}

static std::string cleanZodiacName(const OB_DAY& d) {
    std::string s = d.XiZ.c_str();
    // Drop U+FE0F. The sign itself (U+2648-2653) comes from the merged symbol
    // face, but the emoji-presentation selector has no glyph in any face here
    // and would print as a second "?" after it.
    const std::string vs16 = "\xEF\xB8\x8F";
    for (size_t pos = s.find(vs16); pos != std::string::npos; pos = s.find(vs16, pos))
        s.erase(pos, vs16.size());
    // Trailing space from an older data table, if present.
    while (!s.empty() && s.back() == ' ') s.pop_back();
    return s;
}

// ---------------------------------------------------------------------------
//  Copy to clipboard
// ---------------------------------------------------------------------------
// Every readout on these pages is text the engine already formatted, and the
// thing people do with a computed ephemeris or eclipse table is paste it
// somewhere else. One button per block, with a short "copied" acknowledgement -
// on a phone there is no other sign that anything happened.
static std::string g_copiedId;
static double      g_copiedUntil = -1.0;

void DrawCopyButton(const PanelState& ps, const char* id, const std::string& text) {
    ImGui::PushID(id);
    ImGui::BeginDisabled(text.empty());
    if (ImGui::Button(UI(ps, "\u590d\u5236", "Copy"))) {
        ImGui::SetClipboardText(text.c_str());
        g_copiedId = id;
        g_copiedUntil = ImGui::GetTime() + 1.6;
    }
    ImGui::EndDisabled();
    if (g_copiedId == id && ImGui::GetTime() < g_copiedUntil) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.55f, 0.92f, 0.62f, 1.0f), "%s",
                           UI(ps, "\u5df2\u590d\u5236", "Copied"));
    }
    ImGui::PopID();
}

const char* CalendarLabel(const PanelState& ps) {
    return ps.showAlmanac ? UI(ps, "\u9ec4\u5386", "Almanac")
                          : UI(ps, "\u519c\u5386", "Calendar");
}

// Wraps a list of short terms onto as many lines as the panel needs. ImGui has
// no flow layout, so measure and break by hand; 宜/忌 lists routinely run past
// twenty entries and a single Text() line would just clip.
static void DrawTermFlow(const std::vector<std::string>& items, ImU32 col) {
    if (items.empty()) return;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float avail = ImGui::GetContentRegionAvail().x;
    const float space = ImGui::CalcTextSize(" ").x;
    const float lineH = ImGui::GetTextLineHeightWithSpacing();
    ImVec2 origin = ImGui::GetCursorScreenPos();
    float x = 0.0f, y = 0.0f;
    for (const std::string& t : items) {
        const float w = ImGui::CalcTextSize(t.c_str()).x;
        if (x > 0.0f && x + w > avail) { x = 0.0f; y += lineH; }
        dl->AddText(ImVec2(origin.x + x, origin.y + y), col, t.c_str());
        x += w + space * 2.0f;
    }
    ImGui::Dummy(ImVec2(avail, y + lineH));
}

// The twelve double-hours, the part of an almanac people actually consult
// before picking a time of day. Kept as a compact grid with the 宜忌 for one
// selected hour underneath, because the full lists are far too wide to sit in
// twelve columns on a phone.
static void DrawShiChenTable(const PanelState& ps, const OB_DAY& d) {
    static std::string cachedDay;
    static std::vector<ShiChen> cached;
    const std::string day(d.Lday2.c_str());
    if (day != cachedDay) {
        cached = computeShiChen(day);
        cachedDay = day;
    }
    if (cached.empty()) return;

    static int selected = 0;
    selected = std::clamp(selected, 0, (int)cached.size() - 1);

    if (!ImGui::TreeNode(UI(ps, "\u65f6\u8fb0\u5409\u51f6", "Hourly luck"))) return;

    const ImVec4 good(0.45f, 0.95f, 0.55f, 1.0f);
    const ImVec4 bad(1.0f, 0.55f, 0.50f, 1.0f);

    if (ImGui::BeginTable("##shichen", 5,
            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
            ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn(UI(ps, "\u65f6\u8fb0", "Hour"));
        ImGui::TableSetupColumn(UI(ps, "\u65f6\u95f4", "Time"));
        ImGui::TableSetupColumn(UI(ps, "\u5e72\u652f", "Ganzhi"));
        ImGui::TableSetupColumn(UI(ps, "\u661f\u795e", "Deity"));
        ImGui::TableSetupColumn(UI(ps, "\u5409\u51f6", "Luck"));
        ImGui::TableHeadersRow();

        for (int i = 0; i < (int)cached.size(); ++i) {
            const ShiChen& sc = cached[i];
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::PushID(i);
            if (ImGui::Selectable(sc.zhi.c_str(), selected == i,
                                  ImGuiSelectableFlags_SpanAllColumns))
                selected = i;
            ImGui::PopID();
            ImGui::TableSetColumnIndex(1); ImGui::TextUnformatted(sc.range.c_str());
            ImGui::TableSetColumnIndex(2); ImGui::TextUnformatted(sc.ganZhi.c_str());
            ImGui::TableSetColumnIndex(3); ImGui::TextUnformatted(sc.tianShen.c_str());
            ImGui::TableSetColumnIndex(4);
            const bool lucky = (sc.luck == "\u5409");
            ImGui::TextColored(lucky ? good : bad, "%s", sc.luck.c_str());
        }
        ImGui::EndTable();
    }

    const ShiChen& sc = cached[selected];
    ImGui::Spacing();
    ImGui::TextColored(ImVec4(0.95f, 0.83f, 0.45f, 1.0f), "%s%s  %s  %s%s%s",
                       sc.zhi.c_str(), UI(ps, "\u65f6", " hour"),
                       sc.ganZhi.c_str(),
                       UI(ps, "\u51b2", "clash "), sc.chong.c_str(),
                       sc.shengXiao.c_str());
    ImGui::TextColored(good, "%s", UI(ps, "\u65f6\u5b9c", "Good for"));
    DrawTermFlow(sc.yi, IM_COL32(150, 230, 165, 245));
    ImGui::TextColored(bad, "%s", UI(ps, "\u65f6\u5fcc", "Avoid"));
    DrawTermFlow(sc.ji, IM_COL32(245, 150, 140, 245));

    ImGui::TreePop();
}

void DrawAlmanacDetails(const PanelState& ps, const OB_DAY& d) {
    // Cached because the packed tables are scanned linearly and the selected day
    // is redrawn every frame.
    static std::string cachedKey;
    static HuangLi cached;
    const std::string key = std::string(d.Lmonth2.c_str()) + "/" +
                            std::string(d.Lday2.c_str()) + "/" + std::to_string(d.week);
    if (key != cachedKey) {
        cached = computeHuangLi(std::string(d.Lmonth2.c_str()),
                                std::string(d.Lday2.c_str()), d.week);
        cachedKey = key;
    }
    const HuangLi& h = cached;
    if (!h.valid) return;

    ImGui::SeparatorText(UI(ps, "\u9ec4\u5386", "Almanac"));

    ImGui::TextColored(ImVec4(0.55f, 0.95f, 0.62f, 1.0f), "%s", UI(ps, "\u5b9c", "Good for"));
    DrawTermFlow(h.yi, IM_COL32(150, 230, 165, 245));
    ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.50f, 1.0f), "%s", UI(ps, "\u5fcc", "Avoid"));
    DrawTermFlow(h.ji, IM_COL32(245, 150, 140, 245));

    ImGui::Spacing();
    ImGui::TextColored(ImVec4(0.62f, 0.85f, 1.0f, 1.0f), "%s", UI(ps, "\u5409\u795e\u5b9c\u8d8b", "Auspicious"));
    DrawTermFlow(h.jiShen, IM_COL32(160, 205, 245, 235));
    ImGui::TextColored(ImVec4(0.95f, 0.78f, 0.45f, 1.0f), "%s", UI(ps, "\u51f6\u795e\u5b9c\u5fcc", "Inauspicious"));
    DrawTermFlow(h.xiongSha, IM_COL32(238, 198, 120, 235));

    ImGui::Spacing();
    char row[192];
    std::snprintf(row, sizeof(row), "%s  %s(%s)",
                  h.zhiXing.c_str(), h.tianShen.c_str(), h.tianShenType.c_str());
    InfoRow(ps, "\u503c\u795e", "Day officer", row);
    std::snprintf(row, sizeof(row), "%s %s  %s %s",
                  h.xiu.c_str(), h.xiuLuck.c_str(), h.xiuZheng.c_str(), h.xiuAnimal.c_str());
    InfoRow(ps, "\u661f\u5bbf", "Mansion", row);
    std::snprintf(row, sizeof(row), "%s%s  %s%s",
                  UI(ps, "\u51b2", "Clash "), h.chongShengXiao.c_str(),
                  UI(ps, "\u714e", "Sha "), h.sha.c_str());
    InfoRow(ps, "\u51b2\u714e", "Clash", row);
    // 彭祖百忌 is two sentences, one for the day's stem and one for its
    // branch. Both belong in the value column - emitted as a separate widget
    // the branch line fell outside the table and wrapped back to the left
    // margin, reading as if it belonged to nothing.
    InfoRow(ps, "\u5f6d\u7956", "Pengzu",
            (h.pengZuGan + "\n" + h.pengZuZhi).c_str());

    DrawShiChenTable(ps, d);
}

// The day panel as plain text. Mirrors DrawCalendarDayDetails field for field,
// including the almanac block when that mode is on, so what gets pasted is what
// was on screen.
std::string CalendarDayText(const PanelState& ps, const OB_DAY& d) {
    char buf[256];
    std::snprintf(buf, sizeof(buf), "%s %04d-%02d-%02d\n",
                  UI(ps, "\u516c\u5386:", "Solar:"), d.y, d.m, d.d);
    std::string out = buf;
    out += std::string(UI(ps, "\u519c\u5386: ", "Lunar: ")) + lunarMonthDay(d) + "\n";
    std::snprintf(buf, sizeof(buf), ps.useChinese ? "%s %s\u5e74 %s\u6708 %s\u65e5\n"
                                                  : "%s %s year %s month %s day\n",
                  UI(ps, "\u5e72\u652f:", "Ganzhi:"),
                  d.Lyear2.c_str(), d.Lmonth2.c_str(), d.Lday2.c_str());
    out += buf;
    if (d.y < 1949) {
        std::string nh(OBB::getNH(d.y).c_str());
        if (!nh.empty()) out += std::string(UI(ps, "\u7eaa\u5143: ", "Era: ")) + nh + "\n";
    }
    std::string zodiac = cleanZodiacName(d);
    if (!zodiac.empty()) out += std::string(UI(ps, "\u661f\u5ea7: ", "Zodiac: ")) + zodiac + "\n";
    if (!d.jqmc.empty()) out += std::string(UI(ps, "\u8282\u6c14: ", "Solar term: ")) +
                                d.jqmc.c_str() + "  " + d.jqsj.c_str() + "\n";
    if (!d.yxmc.empty()) out += std::string(UI(ps, "\u6708\u76f8: ", "Moon phase: ")) +
                                d.yxmc.c_str() + "  " + d.yxsj.c_str() + "\n";
    if (!d.A.empty()) out += std::string(UI(ps, "\u8282\u65e5: ", "Festival: ")) + d.A.c_str() + "\n";
    if (!d.B.empty()) out += std::string(UI(ps, "\u7eaa\u5ff5\u65e5: ", "Event: ")) + d.B.c_str() + "\n";
    if (!d.C.empty()) out += std::string(UI(ps, "\u5176\u4ed6: ", "Other: ")) + d.C.c_str() + "\n";

    if (ps.showAlmanac) {
        HuangLi h = computeHuangLi(std::string(d.Lmonth2.c_str()),
                                   std::string(d.Lday2.c_str()), d.week);
        if (h.valid) {
            auto join = [](const std::vector<std::string>& v) {
                std::string r;
                for (size_t i = 0; i < v.size(); ++i) r += (i ? " " : "") + v[i];
                return r;
            };
            out += std::string(UI(ps, "\u5b9c: ", "Good for: ")) + join(h.yi) + "\n";
            out += std::string(UI(ps, "\u5fcc: ", "Avoid: ")) + join(h.ji) + "\n";
            out += std::string(UI(ps, "\u5409\u795e\u5b9c\u8d8b: ", "Auspicious: ")) + join(h.jiShen) + "\n";
            out += std::string(UI(ps, "\u51f6\u795e\u5b9c\u5fcc: ", "Inauspicious: ")) + join(h.xiongSha) + "\n";
            std::snprintf(buf, sizeof(buf), "%s %s  %s(%s)\n", UI(ps, "\u503c\u795e:", "Day officer:"),
                          h.zhiXing.c_str(), h.tianShen.c_str(), h.tianShenType.c_str());
            out += buf;
            std::snprintf(buf, sizeof(buf), "%s %s %s  %s %s\n", UI(ps, "\u661f\u5bbf:", "Mansion:"),
                          h.xiu.c_str(), h.xiuLuck.c_str(), h.xiuZheng.c_str(), h.xiuAnimal.c_str());
            out += buf;
            out += h.pengZuGan + "\n" + h.pengZuZhi + "\n";
        }
    }
    return out;
}

void DrawCalendarDayDetails(const PanelState& ps, const OB_DAY& d) {
    ImGui::SeparatorText(UI(ps, "\u5f53\u5929\u8be6\u60c5", "Day details"));
    ImGui::Text("%s %04d-%02d-%02d", UI(ps, "\u516c\u5386:", "Solar:"), d.y, d.m, d.d);
    ImGui::Text("%s %s", UI(ps, "\u519c\u5386:", "Lunar:"), lunarMonthDay(d).c_str());
    if (ps.useChinese) {
        ImGui::Text("%s %s\u5e74 %s\u6708 %s\u65e5",
                    UI(ps, "\u5e72\u652f:", "Ganzhi:"),
                    d.Lyear2.c_str(), d.Lmonth2.c_str(), d.Lday2.c_str());
    } else {
        ImGui::Text("%s %s year %s month %s day",
                    UI(ps, "\u5e72\u652f:", "Ganzhi:"),
                    d.Lyear2.c_str(), d.Lmonth2.c_str(), d.Lday2.c_str());
    }
    // 干支 year with its zodiac animal, e.g. 丙午 马年. Lyear2 counts from
    // 立春, which is the boundary the 干支 year actually uses.
    {
        const std::string gz(d.Lyear2.c_str());
        std::string animal;
        for (int i = 0; i < 12; ++i) {
            if (gz.size() >= 3 && gz.compare(gz.size() - 3, 3, str_zhi[i]) == 0) {
                animal = str_sxmc[i];
                break;
            }
        }
        if (animal.empty()) ImGui::Text("%s %s", UI(ps, "\u5e72\u652f\u5e74:", "Ganzhi year:"), gz.c_str());
        else ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.45f, 1.0f), "%s %s%s%s",
                                UI(ps, "\u5e72\u652f\u5e74:", "Ganzhi year:"),
                                gz.c_str(), animal.c_str(),
                                UI(ps, "\u5e74", " year"));
    }

    // Reign era. Modern years are just the Gregorian count, so only show this
    // where it carries information - the table ends its last named era in 1949.
    if (d.y < 1949) {
        std::string nh(OBB::getNH(d.y).c_str());
        if (!nh.empty())
            ImGui::TextColored(ImVec4(0.72f, 0.86f, 1.0f, 1.0f), "%s %s",
                               UI(ps, "\u7eaa\u5143:", "Era:"), nh.c_str());
    }

    std::string zodiac = cleanZodiacName(d);
    if (!zodiac.empty()) ImGui::Text("%s %s", UI(ps, "\u661f\u5ea7:", "Zodiac:"), zodiac.c_str());
    if (!d.jqmc.empty()) ImGui::TextColored({0.45f,0.95f,0.55f,1.0f},
                                           "%s %s  %s", UI(ps, "\u8282\u6c14:", "Solar term:"),
                                           d.jqmc.c_str(), d.jqsj.c_str());
    if (!d.yxmc.empty()) ImGui::TextColored({0.95f,0.80f,0.35f,1.0f},
                                           "%s %s  %s", UI(ps, "\u6708\u76f8:", "Moon phase:"),
                                           d.yxmc.c_str(), d.yxsj.c_str());
    if (!d.A.empty()) ImGui::TextColored({1.0f,0.58f,0.48f,1.0f}, "%s %s",
                                        UI(ps, "\u8282\u65e5:", "Festival:"), d.A.c_str());
    if (!d.B.empty()) ImGui::TextColored({0.75f,0.85f,1.0f,1.0f}, "%s %s",
                                        UI(ps, "\u7eaa\u5ff5\u65e5:", "Event:"), d.B.c_str());
    if (!d.C.empty()) ImGui::TextDisabled("%s %s", UI(ps, "\u5176\u4ed6:", "Other:"), d.C.c_str());
    if (ps.showAlmanac) DrawAlmanacDetails(ps, d);
}

// ============================================================================
//  2-D Moon phase disk
// ============================================================================
// illum        : [0,1]  0=new moon, 1=full moon
// limbAngleDeg : direction of the lit limb, degrees clockwise from screen-up
//                (Scene computes it from the real sky geometry).
static void DrawMoonDisk(ImDrawList* dl, ImVec2 center, float r,
                         float illum, float limbAngleDeg) {
    const int   N  = 64;
    const float PI = 3.14159265f;
    const ImU32 darkCol = IM_COL32(12, 12, 26, 255);
    const ImU32 litCol  = IM_COL32(238, 218, 175, 255);

    dl->AddCircleFilled(center, r, darkCol, N * 2);
    dl->AddCircle(center, r + 0.5f, IM_COL32(90, 90, 130, 200), N * 2, 1.5f);

    if (illum < 0.004f) return;      // new moon, nothing lit

    if (illum > 0.996f) {            // full moon
        dl->AddCircleFilled(center, r, litCol, N * 2);
        dl->AddCircle(ImVec2(center.x + r*0.18f, center.y - r*0.22f),
                      r*0.12f, IM_COL32(180,160,120,60), 16, 1.0f);
        dl->AddCircle(ImVec2(center.x - r*0.25f, center.y + r*0.30f),
                      r*0.08f, IM_COL32(180,160,120,60), 16, 1.0f);
        return;
    }

    // Everything is built with the lit limb along +x and then rotated: +x sits
    // 90 degrees clockwise from up, hence the offset. Which side is lit is
    // already carried by the angle, so there is no separate waxing flip.
    const float phi = (limbAngleDeg - 90.0f) * PI / 180.0f;
    const float cs = std::cos(phi), sn = std::sin(phi);
    auto place = [&](float x, float y) {
        return ImVec2(center.x + x * cs - y * sn,
                      center.y + x * sn + y * cs);
    };

    // Two convex fills, never a concave one.
    //
    // The obvious construction - trace the crescent outline and fill it - has
    // two problems. AddConvexPolyFilled fans from the first vertex and so paints
    // straight across the bay, turning every crescent into a half moon; and
    // AddConcavePolyFilled ear-clips an outline whose two ends coincide exactly
    // at the poles, which is degenerate input it gives up on for some values of
    // tx, so the phase flickered between a thin crescent and a half moon as the
    // clock ran. Painting a half disc and then a half ellipse over it has no
    // triangulation step at all: both pieces are convex by construction.
    std::vector<ImVec2> arc;
    arc.reserve(N + 1);

    // Lit hemisphere. The open arc is closed by its own diameter.
    for (int i = 0; i <= N; ++i) {
        const float t = PI * (float)i / N;
        arc.push_back(place(r * std::sin(t), -r * std::cos(t)));
    }
    dl->AddConvexPolyFilled(arc.data(), (int)arc.size(), litCol);

    // Terminator. tx is the ellipse's semi-axis across the disc:
    //   illum < 0.5 => tx > 0, the ellipse lies on the lit side and is painted
    //                  dark, carving the hemisphere down to a crescent;
    //   illum > 0.5 => tx < 0, it lies on the dark side and is painted lit,
    //                  extending the hemisphere into a gibbous.
    const float tx = r * (1.0f - 2.0f * illum);
    arc.clear();
    for (int i = 0; i <= N; ++i) {
        const float t = PI * (float)i / N;
        arc.push_back(place(tx * std::sin(t), -r * std::cos(t)));
    }
    dl->AddConvexPolyFilled(arc.data(), (int)arc.size(),
                            tx > 0.0f ? darkCol : litCol);
}

// ============================================================================
//  Per-panel content helpers (called from the tab bar)
// ============================================================================

void DrawParamsContent(Scene& scene, PanelState& ps) {
    const auto& bodies = scene.bodies();
    const auto& states = scene.states();

    if (ImGui::BeginTable("params", 6,
            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
            ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn(UI(ps, "\u5929\u4f53", "Body"));
        ImGui::TableSetupColumn(UI(ps, "\u65e5\u5fc3\u9ec4\u7ecf", "L deg"));
        ImGui::TableSetupColumn(UI(ps, "\u9ec4\u7eac", "B deg"));
        ImGui::TableSetupColumn(UI(ps, "\u5411\u5f84(AU)", "R AU"));
        ImGui::TableSetupColumn(UI(ps, "\u5730\u5fc3\u8ddd(AU)", "Geo AU"));
        ImGui::TableSetupColumn(UI(ps, "\u89d2\u901f\u5ea6/\u65e5", "speed deg/day"));
        ImGui::TableHeadersRow();
        for (size_t i = 0; i < bodies.size(); ++i) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            bool sel = (ps.selectedBody == (int)i);
            if (ImGui::Selectable(BodyLabel(ps, bodies[i]), sel,
                                  ImGuiSelectableFlags_SpanAllColumns))
                ps.selectedBody = (int)i;
            ImGui::TableSetColumnIndex(1); ImGui::Text("%.3f", states[i].L);
            ImGui::TableSetColumnIndex(2); ImGui::Text("%.3f", states[i].B);
            ImGui::TableSetColumnIndex(3); ImGui::Text("%.4f", states[i].R);
            ImGui::TableSetColumnIndex(4); ImGui::Text("%.4f", states[i].geoDistAU);
            ImGui::TableSetColumnIndex(5); ImGui::Text("%.4f", states[i].speedDegPerDay);
        }
        ImGui::EndTable();
    }
}

void DrawCalendarContent(PanelState& ps) {
    // A month that changes under a swipe with nothing moving reads as a redraw
    // rather than as a step, and gives no clue which way it went. The grid
    // slides in from the side it came from and fades up; 0.22 s, which is long
    // enough to be seen and short enough never to be waited for.
    static long long s_calShown = -1;
    static double s_calAnimAt = -10.0;
    static float  s_calAnimDir = 0.0f;
    const long long calKey = (long long)ps.calYear * 12 + ps.calMonth;
    if (s_calShown != calKey) {
        if (s_calShown >= 0) {
            s_calAnimDir = (calKey > s_calShown) ? 1.0f : -1.0f;
            s_calAnimAt = ImGui::GetTime();
        }
        s_calShown = calKey;
    }
    if (DrawDateFields(ps, "cal", ps.calYear, ps.calMonth, ps.selectedCalendarDay,
                       nullptr, nullptr, true)) {
        Date now = localDateFromUtcJD(nowJD(), ps.timezoneHours);
        ps.calYear = now.Y;
        ps.calMonth = now.M;
        ps.selectedCalendarDay = now.D;
    }
    if (ps.calMonth < 1) ps.calMonth = 1;
    if (ps.calMonth > 12) ps.calMonth = 12;
    if (ps.selectedCalendarDay < 1) ps.selectedCalendarDay = 1;
    static OB_LUN lun;
    long long sig = (long long)ps.calYear * 100 + ps.calMonth;
    if (sig != ps.calSig) { lun = yueLiCalc(ps.calYear, ps.calMonth); ps.calSig = sig; }
    if (ps.selectedCalendarDay > lun.dn) ps.selectedCalendarDay = lun.dn;

    int grid[6][7];
    for (int r = 0; r < 6; ++r) for (int c = 0; c < 7; ++c) grid[r][c] = -1;
    int rows = 0;
    for (int i = 0; i < lun.dn; ++i) {
        int wr = lun.day[i].weeki, wc = lun.day[i].week;
        if (wr >= 0 && wr < 6 && wc >= 0 && wc < 7) grid[wr][wc] = i;
        if (wr + 1 > rows) rows = wr + 1;
    }
    const char* wkZh[7] = {"\u65e5","\u4e00","\u4e8c","\u4e09","\u56db","\u4e94","\u516d"};
    const char* wkEn[7] = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
    Date today = localDateFromUtcJD(nowJD(), ps.timezoneHours);
    // Cell height follows the font: the day number, the lunar day and the note
    // stack inside it, so a fixed pixel height clips once UI scale > 1. Tuned to
    // reproduce the previous desktop numbers exactly at a 16 px font.
    const float calLineH = ImGui::GetTextLineHeight();
    const float calStep  = calLineH * 1.2f;               // baseline-to-baseline
    const float calTop   = S(4.0f);
    const float calCellH = calTop + calStep * 2.0f + calLineH * 0.86f + S(2.0f);
    // One inset for the weekday header and everything inside a day cell.
    const float kCalTextInset = S(6.0f);
    int detailIdx = -1;
    const float calAnim = UiAnimEase(s_calAnimAt, 0.22f);
    const float calFullW = ImGui::GetContentRegionAvail().x;
    if (calAnim < 1.0f) {
        // The width is pinned so the grid slides whole instead of being
        // squeezed by the shrinking space to its right.
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (1.0f - calAnim) * s_calAnimDir * UiS(34.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha,
                            ImGui::GetStyle().Alpha * (0.25f + 0.75f * calAnim));
    }
    if (ImGui::BeginTable("cal_cards", 7, ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_PadOuterX,
                          ImVec2(calFullW, 0.0f))) {
        for (int c = 0; c < 7; ++c) {
            ImGui::TableSetupColumn(ps.useChinese ? wkZh[c] : wkEn[c]);
        }
        ImGui::TableNextRow();
        for (int c = 0; c < 7; ++c) {
            ImGui::TableSetColumnIndex(c);
            // Drawn rather than emitted as text so the label starts at exactly
            // the same inset as the day number below it; ImGui::Text() would sit
            // flush against the cell edge and read as a column out of step.
            ImVec2 hp = ImGui::GetCursorScreenPos();
            float hw = ImGui::GetContentRegionAvail().x;
            ImGui::Dummy(ImVec2(hw, calLineH));
            bool weekend = (c == 0 || c == 6);
            ImGui::GetWindowDrawList()->AddText(
                ImVec2(hp.x + kCalTextInset, hp.y),
                weekend ? IM_COL32(226, 186, 132, 255) : IM_COL32(166, 199, 242, 255),
                ps.useChinese ? wkZh[c] : wkEn[c]);
        }
        for (int r = 0; r < rows; ++r) {
            ImGui::TableNextRow();
            for (int c = 0; c < 7; ++c) {
                ImGui::TableSetColumnIndex(c);
                int idx = grid[r][c];
                if (idx < 0) {
                    ImGui::Dummy(ImVec2(0, calCellH));
                    continue;
                }
                OB_DAY& d = lun.day[idx];
                bool isToday = (d.y == today.Y && d.m == today.M && d.d == today.D);
                bool selected = (d.d == ps.selectedCalendarDay);
                ImVec2 p = ImGui::GetCursorScreenPos();
                float cellW = ImGui::GetContentRegionAvail().x;
                float cellH = calCellH;
                ImGui::InvisibleButton(("##calday" + std::to_string(d.d)).c_str(), ImVec2(cellW, cellH));
                bool hovered = ImGui::IsItemHovered();
                if (ImGui::IsItemClicked()) ps.selectedCalendarDay = d.d;

                ImDrawList* dl = ImGui::GetWindowDrawList();
                // Weekend columns carry a slightly warmer, slightly lighter
                // ground than the weekdays. Deliberately faint: it should read
                // as a rhythm across the month, not as a highlight competing
                // with today and the selected day.
                bool weekend = (c == 0 || c == 6);
                ImU32 bg = selected ? IM_COL32(34, 58, 105, 220)
                         : hovered  ? IM_COL32(32, 44, 72, 220)
                         : isToday  ? IM_COL32(42, 50, 42, 210)
                         : weekend  ? IM_COL32(28, 26, 38, 205)
                                    : IM_COL32(18, 20, 32, 200);
                dl->AddRectFilled(p, ImVec2(p.x + cellW, p.y + cellH), bg, 4.0f);
                dl->AddRect(p, ImVec2(p.x + cellW, p.y + cellH),
                            isToday  ? IM_COL32(120,210,130,180)
                          : weekend  ? IM_COL32(96, 90, 112, 110)
                                     : IM_COL32(70, 88, 125, 110), 4.0f);

                ImU32 dayCol = weekend ? IM_COL32(255,205,130,255)
                                       : IM_COL32(230,235,245,255);
                float tx = p.x + kCalTextInset;
                dl->AddText(ImVec2(tx, p.y + calTop), dayCol, std::to_string(d.d).c_str());
                std::string lunar = (d.Ldi == 0) ? (std::string(d.Lmc.c_str()) + "\u6708")
                                                 : std::string(d.Ldc.c_str());
                lunar = ellipsizeText(lunar, cellW - kCalTextInset * 2.0f);
                dl->AddText(ImVec2(tx, p.y + calTop + calStep), IM_COL32(165,185,215,230),
                            lunar.c_str());
                std::string note = compactDayNote(d);
                if (!note.empty()) {
                    note = ellipsizeText(note, cellW - kCalTextInset * 2.0f);
                    ImU32 noteCol = !d.jqmc.empty() ? IM_COL32(120,235,130,240)
                                 : !d.A.empty()    ? IM_COL32(255,150,125,240)
                                                   : IM_COL32(225,195,105,230);
                    ImFont* font = ImGui::GetFont();
                    float noteSize = ImGui::GetFontSize() * 0.86f;
                    dl->AddText(font, noteSize,
                                ImVec2(tx, p.y + calTop + calStep * 2.0f), noteCol,
                                note.c_str());
                }

                if (hovered && !g_touchMode) {
                    // Tooltip stays compact: the almanac lists are far too long
                    // to hang off the cursor, so they only go in the panel below.
                    PanelState brief = ps;
                    brief.showAlmanac = false;
                    ImGui::BeginTooltip();
                    DrawCalendarDayDetails(brief, d);
                    ImGui::EndTooltip();
                }
                if (d.d == ps.selectedCalendarDay) detailIdx = idx;
            }
        }
        ImGui::EndTable();
    }
    if (calAnim < 1.0f) ImGui::PopStyleVar();
    // Next solar term, with the countdown people actually look for.
    {
        double jd = utcJDFromLocalDate(
            Date{ps.calYear, ps.calMonth,
                 std::max(1, std::min(ps.selectedCalendarDay, lun.dn)), 12, 0, 0.0},
            ps.timezoneHours) + ps.timezoneHours / 24.0;
        NextTerm nt = nextSolarTerm(jd, ps.timezoneHours);
        if (nt.valid) {
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(0.45f, 0.95f, 0.55f, 1.0f), "%s %s",
                               UI(ps, "\u4e0b\u4e00\u8282\u6c14:", "Next term:"),
                               nt.name.c_str());
            ImGui::SameLine();
            if (nt.daysAway <= 0)
                ImGui::TextDisabled("%s", UI(ps, "(\u5c31\u5728\u4eca\u5929)", "(today)"));
            else
                ImGui::TextDisabled(UI(ps, "(\u8fd8\u6709 %d \u5929  %s)", "(in %d days, %s)"),
                                    nt.daysAway, nt.timeStr.c_str());
        }
    }

    if (detailIdx >= 0) {
        DrawCalendarDayDetails(ps, lun.day[detailIdx]);
        ImGui::Spacing();
        DrawCopyButton(ps, "copy_day", CalendarDayText(ps, lun.day[detailIdx]));
    }
}

void DrawEphemerisContent(PanelState& ps, const Scene& scene) {
    static const int   xtArr[]   = {1,2,3,4,5,6,7,8,10};
    static const char* nameZh[] = {"\u6c34\u661f","\u91d1\u661f","\u706b\u661f","\u6728\u661f","\u571f\u661f","\u5929\u738b\u661f","\u6d77\u738b\u661f","\u51a5\u738b\u661f","\u6708\u4eae"};
    static const char* nameEn[] = {"Mercury","Venus","Mars","Jupiter","Saturn",
                                   "Uranus","Neptune","Pluto","Moon"};
    if (ps.ephBodyIdx < 0 || ps.ephBodyIdx >= IM_ARRAYSIZE(xtArr)) ps.ephBodyIdx = 0;
    if (ps.ephFollowSelection) {
        SyncDateFromScene(ps, scene);
    }
    int beforeBody = ps.ephBodyIdx;
    ImGui::SetNextItemWidth(S(120.0f));
    ImGui::Combo(UI(ps, "\u5929\u4f53##eph", "Body##eph"), &ps.ephBodyIdx,
                 ps.useChinese ? nameZh : nameEn, IM_ARRAYSIZE(nameEn));
    if (ps.ephBodyIdx != beforeBody) ps.ephFollowSelection = false;
    int beforeY = ps.year, beforeM = ps.month, beforeD = ps.day;
    int beforeH = ps.hour, beforeMin = ps.minute;
    DrawDateFields(ps, "eph", ps.year, ps.month, ps.day, &ps.hour);
    if (ps.year != beforeY || ps.month != beforeM || ps.day != beforeD ||
        ps.hour != beforeH || ps.minute != beforeMin) {
        ps.ephFollowSelection = false;
    }

    long long sig = ((((long long)ps.year*13+ps.month)*32+ps.day)*24+ps.hour)*16+ps.ephBodyIdx;
    if (sig != ps.ephSig) {
        ps.ephSig = sig;
        double jd = utcJDFromLocalDate(
            Date{ps.year,ps.month,ps.day,ps.hour,ps.minute,0.0}, ps.timezoneHours) - kJ2K;
        jd += dt_T(jd);
        double L = jw.J/180.0*kPI, fa = jw.W/180.0*kPI;
        int xt = xtArr[ps.ephBodyIdx];
        ps.ephText = std::string(JD2str(jd+kJ2K).c_str()) + " TD\n" +
                     std::string(xingX(xt,jd,L,fa).c_str());
    }
    ImGui::Separator();
    // Header line first, so a pasted block still says which body and which
    // instant it belongs to.
    DrawCopyButton(ps, "copy_eph",
                   std::string(ps.useChinese ? nameZh[ps.ephBodyIdx] : nameEn[ps.ephBodyIdx]) +
                   "\n" + ps.ephText);
    ImGui::BeginChild("eph_out", ImVec2(0,0), false, ImGuiWindowFlags_HorizontalScrollbar);
    ImGui::TextUnformatted(ps.ephText.c_str());
    ImGui::EndChild();
}

void DrawTermsContent(PanelState& ps) {
    ImGui::SetNextItemWidth(S(120.0f));
    ImGui::InputInt(UI(ps, "\u5e74##term", "Year##term"), &ps.termYear);
    long long termSig = (long long)ps.termYear * 1000
                      + (long long)std::llround(ps.timezoneHours * 4.0f);
    if (ps.termSig != termSig) {
        ps.termSig = termSig;
        int y = ps.termYear - 2000;
        std::string s = ps.useChinese ? "\u301024 \u8282\u6c14\u3011\n" : "[24 Solar Terms]\n";
        for (int i = 0; i < 24; ++i) {
            double T = S_aLon_t((y + i*15/360.0 + 1) * 2*kPI);
            s += eventTimeStr(T, ps.timezoneHours) + "  " + std::string(str_jqmc[(i+6)%24]) + "\n";
        }
        s += ps.useChinese ? "\n\u3010\u6714(\u65b0\u6708) / \u671b(\u6ee1\u6708)\u3011\n"
                           : "\n[New moon / Full moon]\n";
        int n0 = (int)((double)y * (365.2422/29.53058886));
        for (int i = 0; i < 14; ++i) {
            double Ts = MS_aLon_t((n0+i)*2*kPI);
            double Tw = MS_aLon_t((n0+i+0.5)*2*kPI);
                s += (ps.useChinese ? "\u6714 " : "New  ") + eventTimeStr(Ts, ps.timezoneHours)
               + (ps.useChinese ? "    \u671b " : "    Full ")
                    + eventTimeStr(Tw, ps.timezoneHours) + "\n";
        }
        ps.termText = s;
    }
    ImGui::SameLine();
    DrawCopyButton(ps, "copy_terms", std::to_string(ps.termYear) + "\n" + ps.termText);
    ImGui::BeginChild("term_out", ImVec2(0,0), false, ImGuiWindowFlags_HorizontalScrollbar);
    ImGui::TextUnformatted(ps.termText.c_str());
    ImGui::EndChild();
}

void DrawBaziContent(PanelState& ps) {
    DrawDateFields(ps, "bz", ps.year, ps.month, ps.day, &ps.hour, &ps.minute);

    long long sig = ((((long long)ps.year*13+ps.month)*32+ps.day)*24+ps.hour)*60+ps.minute;
    if (sig != ps.baziSig) {
        ps.baziSig = sig;
        double jd = utcJDFromLocalDate(
            Date{ps.year,ps.month,ps.day,ps.hour,ps.minute,0.0}, ps.timezoneHours);
        MLBZ ob = {};
        OBB::mingLiBaZi(jd - kJ2K, jw.J/(180.0/kPI), ob);
        char buf[256];
        std::snprintf(buf, sizeof(buf),
            ps.useChinese
                ? "\u516b\u5b57: %s\u5e74%s\u6708%s\u65e5%s\u65f6\n\u771f\u592a\u9633\u65f6: %s"
                : "Bazi: %s year %s month %s day %s hour\nTrue solar time: %s",
            ob.bz_jn.c_str(), ob.bz_jy.c_str(),
            ob.bz_jr.c_str(), ob.bz_js.c_str(),
            ob.bz_zty.c_str());
        ps.baziText = buf;
        // 拆分纪时字符串为单独 token
        ps.baziJSItems.clear();
        ps.baziJSIdx = ob.bz_js_idx;
        std::string js(ob.bz_JS.c_str());
        size_t pos = 0;
        while (pos < js.size()) {
            size_t sp = js.find(' ', pos);
            if (sp == std::string::npos) { ps.baziJSItems.push_back(js.substr(pos)); break; }
            ps.baziJSItems.push_back(js.substr(pos, sp - pos));
            pos = sp + 1;
        }
        ps.shengjiangText = std::string(shengjiang(ps.year,ps.month,ps.day).c_str());
    }
    ImGui::SeparatorText(UI(ps, "\u516b\u5b57", "Bazi"));
    ImGui::TextUnformatted(ps.baziText.c_str());
    // 纪时：逐 token 渲染，当前时辰用红色高亮
    ImGui::Text("%s", UI(ps, "\u7eaa\u65f6:", "Hour mark:"));
    for (int i = 0; i < (int)ps.baziJSItems.size(); i++) {
        ImGui::SameLine(0, i == 0 ? 4.0f : 4.0f);
        if (i == ps.baziJSIdx)
            ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "%s", ps.baziJSItems[i].c_str());
        else
            ImGui::TextUnformatted(ps.baziJSItems[i].c_str());
    }
    ImGui::SeparatorText(UI(ps, "\u5347\u964d", "Rise/Set"));
    {
        char head[64];
        std::snprintf(head, sizeof(head), "%04d-%02d-%02d %02d:%02d\n",
                      ps.year, ps.month, ps.day, ps.hour, ps.minute);
        std::string js;
        for (size_t i = 0; i < ps.baziJSItems.size(); ++i)
            js += (i ? " " : "") + ps.baziJSItems[i];
        DrawCopyButton(ps, "copy_bazi",
                       head + ps.baziText + "\n" +
                       std::string(UI(ps, "\u7eaa\u65f6: ", "Hour marks: ")) + js + "\n\n" +
                       ps.shengjiangText);
    }
    ImGui::BeginChild("sj", ImVec2(0,0), false, ImGuiWindowFlags_HorizontalScrollbar);
    ImGui::TextUnformatted(ps.shengjiangText.c_str());
    ImGui::EndChild();
}

void DrawMoonPhaseContent(Renderer& renderer, Scene& scene, PanelState& ps) {
    DrawTransportBar(scene, ps);
    ImGui::Separator();

    const MoonData& md = scene.moon();
    if (!md.valid) { ImGui::TextDisabled("%s", UI(ps, "(\u8ba1\u7b97\u4e2d...)", "(calculating...)")); return; }

    // UI section.
    ImGui::SeparatorText(UI(ps, "\u6708\u76f8", "Moon phase"));
    ImGui::Text("%s", UI(ps, "\u6708\u76f8\u540d\u79f0:", "Phase:")); ImGui::SameLine();
    ImGui::TextColored({0.95f,0.85f,0.4f,1.0f}, "%s", MoonPhaseLabel(ps, md.phaseName));
    ImGui::Text("%s %.1f %s", UI(ps, "\u6708\u9f84:", "Moon age:"), md.ageDays, UI(ps, "\u5929", "days"));
    ImGui::Text("%s %s (%.1f%%)",
                UI(ps, "\u76c8\u4e8f:", "Illumination:"),
                md.waxing ? UI(ps, "\u6e10\u76c8", "waxing") : UI(ps, "\u6e10\u4e8f", "waning"),
                md.illumination * 100.0);
    ImGui::Text("%s %.2f deg", UI(ps, "\u6708\u65e5\u89d2\u8ddd:", "Elongation:"), md.elongationDeg);
    ImGui::Checkbox(UI(ps, "\u771f\u5b9e\u53d6\u5411(\u4eae\u8fb9\u65b9\u4f4d\u89d2)",
                          "True orientation (bright limb angle)"),
                    &ps.moonRealOrientation);
    if (ps.moonRealOrientation)
        ImGui::TextDisabled("%s %.1f deg", UI(ps, "\u4eae\u8fb9\u65b9\u4f4d\u89d2:", "Limb angle:"),
                            md.brightLimbAngleDeg);
    ImGui::Spacing();

    // Illumination progress bar
    ImGui::ProgressBar((float)md.illumination, ImVec2(-1.0f, S(8.0f)), "");
    ImGui::Spacing();

    // UI section.
    ImVec2 avail = ImGui::GetContentRegionAvail();
    const float gap = ImGui::GetStyle().ItemSpacing.x;
    const bool stacked = avail.x < S(470.0f);
    const float maxPreview = stacked ? S(360.0f) : S(230.0f);
    float col = stacked ? std::min(avail.x, maxPreview)
                        : std::min((avail.x - gap) * 0.5f, maxPreview);
    if (col < S(80.0f)) col = S(80.0f);

    // UI section.
    ImVec2 canvasP = ImGui::GetCursorScreenPos();
    float  side    = col - S(8.0f);
    if (side < S(60.0f)) side = S(60.0f);
    ImGui::InvisibleButton("moon_canvas", ImVec2(side, side));
    ImDrawList* dl = ImGui::GetWindowDrawList();

    dl->AddRectFilled(canvasP,
                      ImVec2(canvasP.x + side, canvasP.y + side),
                      IM_COL32(8, 8, 20, 255));

    static const float kStars[][2] = {
        {0.08f,0.12f},{0.85f,0.07f},{0.92f,0.45f},{0.05f,0.72f},
        {0.50f,0.04f},{0.75f,0.90f},{0.20f,0.88f},{0.95f,0.80f},
        {0.40f,0.95f},{0.15f,0.50f},
    };
    for (auto& s : kStars)
        dl->AddCircleFilled(
            ImVec2(canvasP.x + s[0]*side, canvasP.y + s[1]*side),
            S(1.2f), IM_COL32(220,220,240,180));

    // Straight up means "textbook diagram": terminator vertical, lit side right
    // while waxing. The real angle is what you would actually see from the
    // configured observing site.
    const float limbAngle = ps.moonRealOrientation
        ? (float)md.brightLimbAngleDeg
        : (md.waxing ? 90.0f : 270.0f);
    DrawMoonDisk(dl, ImVec2(canvasP.x + side*0.5f, canvasP.y + side*0.5f),
                 side * 0.38f, (float)md.illumination, limbAngle);

    {
        const char* lbl = MoonPhaseLabel(ps, md.phaseName);
        ImVec2 ts = ImGui::CalcTextSize(lbl);
        dl->AddText(ImVec2(canvasP.x + (side - ts.x)*0.5f,
                           canvasP.y + side - ts.y - 4.f),
                    IM_COL32(220,200,120,230), lbl);
    }

    // UI section.
    if (stacked) {
        ImGui::Spacing();
    } else {
        ImGui::SameLine(0.0f, gap);
    }
    ImVec2 p3d = ImGui::GetCursorScreenPos();
    float  s3d = side; // same square size

    // Drag the 3-D moon image to rotate the model.
    ImGui::InvisibleButton("moon_3d_drag", ImVec2(s3d, s3d));
    if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
        ImGuiIO& io = ImGui::GetIO();
        ps.moonPhaseYaw += io.MouseDelta.x * 0.55f;
        ps.moonPhasePitch = std::clamp(ps.moonPhasePitch + io.MouseDelta.y * 0.55f, -80.0f, 80.0f);
    }
    bool moon3dHovered = ImGui::IsItemHovered();
    renderer.renderMoonPhase((float)md.elongationDeg, limbAngle,
                             ps.moonPhaseYaw, ps.moonPhasePitch);

    unsigned int moonTex = renderer.moonPhaseTex();
    if (moonTex) {
        ImGui::SetCursorScreenPos(p3d);
        ImGui::Image((ImTextureID)(intptr_t)moonTex,
                     ImVec2(s3d, s3d),
                     ImVec2(0, 1), ImVec2(1, 0)); // y-flip
        if (moon3dHovered && !g_touchMode) {
            ImGui::BeginTooltip();
            ImGui::TextUnformatted(UI(ps, "\u62d6\u52a8\u65cb\u8f6c 3D \u6708\u7403", "Drag to rotate 3D moon"));
            ImGui::EndTooltip();
        }
    } else {
        ImGui::Dummy(ImVec2(s3d, s3d));
    }

    // Label below the 3-D render
    {
        ImDrawList* dl2 = ImGui::GetWindowDrawList();
        const char* lbl = UI(ps, "3D \u6708\u76f8", "3D moon phase");
        ImVec2 ts = ImGui::CalcTextSize(lbl);
        dl2->AddText(ImVec2(p3d.x + (s3d - ts.x)*0.5f, p3d.y + s3d - ts.y - 4.f),
                     IM_COL32(160,180,220,200), lbl);
    }
}

static std::string EclipseTimeText(double jdTd, const PanelState& ps) {
    if (jdTd == 0.0 || !std::isfinite(jdTd)) return "--";
    double localJD = eclipseTdToUtcJD(jdTd) + ps.timezoneHours / 24.0;
    return std::string(JD2str(localJD).c_str());
}

static const char* EclipseKindText(const PanelState& ps, const EclipseEvent& e) {
    if (e.kind == EclipseEvent::Solar) return UI(ps, "\u65e5\u98df", "Solar");
    return UI(ps, "\u6708\u98df", "Lunar");
}

static std::string EclipseTypeText(const PanelState& ps, const EclipseEvent& e) {
    if (!ps.useChinese) return e.type;
    if (e.kind == EclipseEvent::Lunar) {
        if (e.type == "\u5168") return "\u6708\u5168\u98df";
        if (e.type == "\u504f") return "\u6708\u504f\u98df";
        return "\u534a\u5f71\u6708\u98df";
    }
    if (e.type == "T") return "\u65e5\u5168\u98df";
    if (e.type == "A") return "\u65e5\u73af\u98df";
    if (e.type == "P") return "\u65e5\u504f\u98df";
    if (!e.type.empty() && e.type[0] == 'H') return "\u5168\u73af\u98df";
    if (!e.type.empty() && e.type[0] == 'T') return "\u65e5\u5168\u98df";
    if (!e.type.empty() && e.type[0] == 'A') return "\u65e5\u73af\u98df";
    return e.type;
}

// Wall-clock seconds one eclipse demo should take from first to last contact.
static const double kEclipseDemoSeconds = 45.0;

// Render a clock rate as "N 秒/真实秒" etc., auto-picking the unit so the
// number stays readable — 1/1440 d/s reads as "1 分/真实秒", not "0.00069 日/秒".
std::string FormatSpeed(const PanelState& ps, double daysPerSec) {
    char buf[96];
    double a = std::fabs(daysPerSec);
    const char* unit;
    double v;
    if (a < 1.0 / 1440.0)      { v = daysPerSec * 86400.0; unit = UI(ps, "秒", "s"); }
    else if (a < 1.0 / 24.0)   { v = daysPerSec * 1440.0;  unit = UI(ps, "分", "min"); }
    else if (a < 1.0)          { v = daysPerSec * 24.0;    unit = UI(ps, "小时", "h"); }
    else if (a < 365.25)       { v = daysPerSec;           unit = UI(ps, "日", "d"); }
    else                       { v = daysPerSec / 365.25;  unit = UI(ps, "年", "yr"); }
    std::snprintf(buf, sizeof(buf), "%.4g %s/%s", v, unit,
                  UI(ps, "真实秒", "real s"));
    return buf;
}

static double SceneUtcToTd(const Scene& scene) {
    double ut = scene.clock().jd - kJ2K;
    return ut + dt_T(ut);
}

// First and last contact, whichever kind of eclipse it is: the span a
// demonstration should run over.
static void EclipseSpan(const EclipseEvent& e, double& first, double& last) {
    const double* s = e.contactsTd;
    if (e.kind == EclipseEvent::Solar) { first = s[0]; last = s[2]; }
    else { first = s[3] ? s[3] : s[0]; last = s[4] ? s[4] : s[2]; }
}

// Wind the clock back to first contact and run from there.
//
// Pace the run to the eclipse's own length rather than a fixed rate: a solar
// eclipse spans ~3-6 h from first to last contact, a lunar one longer, so a
// constant "1 minute per second" makes some of them crawl. Aim for a fixed
// wall-clock run, clamped so it never gets absurd either way.
// Frame the eclipse the way a diagram of one is drawn: from the side of the
// light, so the cone is seen across rather than down, and a little above the
// ecliptic so the Moon's orbit reads as a ring instead of a line. Called once
// when the study view opens; after that the camera belongs to the viewer.
static void PlaceEclipseFocusCamera(const Scene& scene, gx::OrbitCamera& cam,
                                    float aspect) {
    int earthIdx = -1;
    const auto& bodies = scene.bodies();
    for (size_t i = 0; i < bodies.size(); ++i)
        if (bodies[i].xt == 0) { earthIdx = (int)i; break; }
    if (earthIdx < 0 || !scene.moon().valid) return;

    const gx::Vec3 earth = scene.states()[earthIdx].world;
    const gx::Vec3 axis  = scene.shadowAxis();
    // Where the Moon is about to be, not where it still is: the focus flag was
    // only set a moment ago and Scene has not run an update under it yet, so
    // reading moon().worldPos here would frame the old artistic offset.
    const float earthR = std::max(scene.states()[earthIdx].displayRadius, 0.22f);
    const float reach  = scene.eclipseFocus().moonRadii * earthR;

    // Square across the light is where a shadow cone has its full length on
    // screen - but it also puts the point the cone lands on exactly on Earth's
    // silhouette, where it cannot be seen. So the camera is swung a third of
    // the way round towards the Sun: the cone keeps four fifths of its length
    // and the shadow comes off the limb onto the disc where it belongs.
    gx::Vec3 side = gx::cross(axis, gx::Vec3{0.0f, 1.0f, 0.0f});
    if (gx::length(side) < 1e-4f) side = {1.0f, 0.0f, 0.0f};
    side = gx::normalize(side);
    const float swing = 36.0f * (float)kPI / 180.0f;
    const float lift  = 20.0f * (float)kPI / 180.0f;
    gx::Vec3 base = gx::normalize(side * std::cos(swing) - axis * std::sin(swing));
    gx::Vec3 dir  = gx::normalize(base * std::cos(lift)
                                  + gx::Vec3{0.0f, 1.0f, 0.0f} * std::sin(lift));

    // Centred on Earth, which is where the orbit is centred too, and nudged a
    // little towards the Moon so the cone is not crowded against one edge.
    cam.target = earth - axis * (reach * 0.20f);
    // The orbit is a ring two Moon-distances wide and the Earth inside it is a
    // ninth of one, so what fits the ring leaves the Earth small. The width is
    // what binds on a desktop and the height on a phone, so both are measured
    // and the larger wins; the ring is allowed to run a little past the sides
    // rather than shrink everything to fit its last degree in.
    const float tanHalf = std::tan(cam.fovy * 0.5f);
    float needV = reach * std::sin(lift) * 1.25f + earthR * 2.6f;
    float needH = (reach * 0.92f + earthR * 1.4f) / std::max(aspect, 0.62f);
    cam.distance = std::clamp(std::max(needV, needH) / tanHalf, 0.5f, 4000.0f);
    cam.pitch = std::asin(std::clamp(dir.y, -1.0f, 1.0f));
    cam.yaw   = std::atan2(dir.z, dir.x);
    cam.focusing = false;
}

static void StartEclipseDemo(Scene& scene, PanelState& ps, const EclipseEvent& e) {
    double first = 0.0, last = 0.0;
    EclipseSpan(e, first, last);
    scene.clock().jd = eclipseTdToUtcJD(first);
    if (!ps.eclipseDemoActive) {
        ps.eclipseSavedSpeed  = scene.clock().speedDaysPerSec;
        ps.eclipseSavedUnit   = ps.speedUnit;
        ps.eclipseSavedAmount = ps.speedAmount;
    }
    ps.eclipseDemoActive = true;
    double span = (last > first) ? (last - first) : (3.0 / 24.0);
    double perSec = std::clamp(span / kEclipseDemoSeconds, 0.5 / 1440.0, 15.0 / 1440.0);
    scene.clock().speedDaysPerSec = (float)perSec;
    // Keep the sidebar preset in step; otherwise it keeps showing the old rate
    // and the next nudge of that control snaps the demo speed away.
    ps.speedUnit = 0;                                  // seconds / real second
    ps.speedAmount = (float)(perSec * 86400.0);
    scene.clock().playing = true;
}

// End a demonstration and give the clock its own rate back. Left on the
// eclipse's crawl - or snapped back to five days a second while still running -
// the globe strobes through day and night, which is not what the viewer asked
// to look at.
static void StopEclipseDemo(Scene& scene, PanelState& ps) {
    if (!ps.eclipseDemoActive) return;
    scene.clock().playing = false;
    scene.clock().speedDaysPerSec = ps.eclipseSavedSpeed;
    ps.speedUnit   = ps.eclipseSavedUnit;
    ps.speedAmount = ps.eclipseSavedAmount;
    ps.eclipseDemoActive = false;
}

// Stop it once it has run past last contact. Called from the viewport as well
// as the eclipse page, so a run started from either still ends when the eclipse
// does.
static void UpdateEclipseDemo(Scene& scene, PanelState& ps) {
    if (!ps.eclipseDemoActive) return;
    if (ps.selectedEclipse < 0 || ps.selectedEclipse >= (int)ps.eclipseEvents.size()) return;
    double first = 0.0, last = 0.0;
    EclipseSpan(ps.eclipseEvents[ps.selectedEclipse], first, last);
    if (SceneUtcToTd(scene) <= last) return;
    StopEclipseDemo(scene, ps);
}

static bool ProjectGlobePoint(double lonDeg, double latDeg, float yawDeg, float pitchDeg,
                              ImVec2 center, float radius, ImVec2& out) {
    double lon = (lonDeg + yawDeg) * kPI / 180.0;
    double lat = latDeg * kPI / 180.0;
    double pitch = pitchDeg * kPI / 180.0;
    double x = std::cos(lat) * std::sin(lon);
    double y = std::sin(lat);
    double z = std::cos(lat) * std::cos(lon);
    double y2 = y * std::cos(pitch) - z * std::sin(pitch);
    double z2 = y * std::sin(pitch) + z * std::cos(pitch);
    out = ImVec2(center.x + (float)x * radius, center.y - (float)y2 * radius);
    return z2 >= 0.0;
}

static void DrawGlobeGrid(ImDrawList* dl, ImVec2 center, float radius,
                          float yawDeg, float pitchDeg) {
    for (int lat = -60; lat <= 60; lat += 30) {
        ImVec2 prev{}; bool prevOk = false;
        for (int lon = -180; lon <= 180; lon += 4) {
            ImVec2 p; bool ok = ProjectGlobePoint(lon, lat, yawDeg, pitchDeg, center, radius, p);
            if (ok && prevOk) dl->AddLine(prev, p, IM_COL32(95,155,190,70), 1.0f);
            prev = p; prevOk = ok;
        }
    }
    for (int lon = -150; lon <= 180; lon += 30) {
        ImVec2 prev{}; bool prevOk = false;
        for (int lat = -90; lat <= 90; lat += 3) {
            ImVec2 p; bool ok = ProjectGlobePoint(lon, lat, yawDeg, pitchDeg, center, radius, p);
            if (ok && prevOk) dl->AddLine(prev, p, IM_COL32(95,155,190,70), 1.0f);
            prev = p; prevOk = ok;
        }
    }
}

static EclipseGeoPoint EclipseLinePoint(const EclipsePathSample& s, int line) {
    if (line == 0) return s.center;
    if (line == 1) return s.penumbraNorth;
    if (line == 2) return s.penumbraSouth;
    if (line == 3) return s.umbraNorth;
    return s.umbraSouth;
}

static void DrawEclipsePathOnGlobe(ImDrawList* dl, const PanelState& ps,
                                   ImVec2 center, float radius) {
    const ImU32 colors[] = {
        IM_COL32(255,210,75,245), IM_COL32(100,190,255,170), IM_COL32(100,190,255,170),
        IM_COL32(255,105,85,225), IM_COL32(255,105,85,225)
    };
    const float widths[] = {2.6f, 1.2f, 1.2f, 2.0f, 2.0f};
    for (int line = 0; line < 5; ++line) {
        ImVec2 prev{}; bool prevOk = false; double prevLon = 0.0;
        for (const EclipsePathSample& sample : ps.eclipsePath) {
            EclipseGeoPoint gp = EclipseLinePoint(sample, line);
            ImVec2 p;
            bool ok = gp.valid && ProjectGlobePoint(gp.longitudeDeg, gp.latitudeDeg,
                                                     ps.eclipseGlobeYaw, ps.eclipseGlobePitch,
                                                     center, radius, p);
            if (ok && prevOk && std::fabs(gp.longitudeDeg - prevLon) < 180.0)
                dl->AddLine(prev, p, colors[line], widths[line]);
            prev = p; prevOk = ok; prevLon = gp.longitudeDeg;
        }
    }
}

// 界线配色沿用原版 vml.js: 中心线/南北界/日出日没食甚线/初亏复圆环为红(col1),
// 0.5 半影界为绿(col2)。
static void LimitCurveStyle(EclipseLimitCurve::Kind kind, ImU32& col, float& w) {
    switch (kind) {
        case EclipseLimitCurve::CenterLine:        col = IM_COL32(255,150,150,235); w = 1.8f; break;
        case EclipseLimitCurve::UmbraLimit:        col = IM_COL32(255, 96, 96,225); w = 1.6f; break;
        case EclipseLimitCurve::PenumbraLimit:     col = IM_COL32(255,120,120,205); w = 1.3f; break;
        case EclipseLimitCurve::HalfPenumbraLimit: col = IM_COL32(128,240,128,205); w = 1.3f; break;
        case EclipseLimitCurve::SunriseSunset:     col = IM_COL32(255,120,120,205); w = 1.3f; break;
        case EclipseLimitCurve::ContactLimit:      col = IM_COL32(255,120,120,215); w = 1.4f; break;
        default:                                   col = IM_COL32(255,120,120,205); w = 1.3f; break;
    }
}

static void DrawEclipseLimitsOnGlobe(ImDrawList* dl, const PanelState& ps,
                                     ImVec2 center, float radius) {
    if (!ps.eclipseLimits.valid) return;
    for (const EclipseLimitCurve& c : ps.eclipseLimits.curves) {
        ImU32 col; float w;
        LimitCurveStyle(c.kind, col, w);
        ImVec2 prev{}; bool prevOk = false; double prevLon = 0.0;
        for (const EclipseGeoPoint& gp : c.points) {
            ImVec2 p;
            bool ok = gp.valid && ProjectGlobePoint(gp.longitudeDeg, gp.latitudeDeg,
                                                    ps.eclipseGlobeYaw, ps.eclipseGlobePitch,
                                                    center, radius, p);
            if (ok && prevOk && std::fabs(gp.longitudeDeg - prevLon) < 180.0)
                dl->AddLine(prev, p, col, w);
            prev = p; prevOk = ok; prevLon = gp.longitudeDeg;
        }
    }
}

// 2D 回退视图里的行政区界。GL 纹理视图走 renderEclipseGlobe 内的 VAO，
// 这里直接用渲染器保留的经纬度副本投影绘制，两种模式才都有这一层。
static void DrawBoundariesOnGlobe(ImDrawList* dl, const Renderer& renderer,
                                  const PanelState& ps, ImVec2 center, float radius) {
    const std::vector<float>& seg = renderer.boundarySegments();
    // Belt and braces against ImGui's 16-bit draw-list indices: the loader
    // already decimates to a budget, but a data change must never be able to
    // corrupt a whole frame of geometry. Four vertices per line, 65536 available.
    const size_t kMaxLines = 12000;
    const size_t limit = std::min(seg.size(), kMaxLines * 4);
    for (size_t i = 0; i + 3 < limit; i += 4) {
        ImVec2 a, b;
        bool oka = ProjectGlobePoint(seg[i],   seg[i+1], ps.eclipseGlobeYaw,
                                     ps.eclipseGlobePitch, center, radius, a);
        bool okb = ProjectGlobePoint(seg[i+2], seg[i+3], ps.eclipseGlobeYaw,
                                     ps.eclipseGlobePitch, center, radius, b);
        if (oka && okb) dl->AddLine(a, b, IM_COL32(150,170,200,90), 1.0f);
    }
}

// 太阳画在真实的太阳直射点方向上，随视角一起转，而不是固定在面板角落。
// 直射点在正面时同时标出直射点本身(天顶点)。
static void DrawGlobeSunMarker(ImDrawList* dl, const PanelState& ps, double jdTd,
                               ImVec2 center, float radius) {
    EclipseGeoPoint sub = solarSubpoint(jdTd);
    if (!sub.valid) return;

    ImVec2 p;
    bool front = ProjectGlobePoint(sub.longitudeDeg, sub.latitudeDeg,
                                   ps.eclipseGlobeYaw, ps.eclipseGlobePitch,
                                   center, radius, p);
    ImVec2 d(p.x - center.x, p.y - center.y);
    float len = std::sqrt(d.x * d.x + d.y * d.y);
    if (len < 1e-3f) { d = ImVec2(0.0f, -1.0f); len = 1.0f; }
    ImVec2 dir(d.x / len, d.y / len);

    // 正面时按直射点所在的方向外推，背面时贴在对应的边缘外侧。
    float base = front ? std::min(len, radius) : radius;
    ImVec2 sun(center.x + dir.x * (base + radius * 0.30f),
               center.y + dir.y * (base + radius * 0.30f));
    float sunR = std::max(9.0f, radius * 0.11f);

    if (front) {
        // 直射点(太阳位于天顶处)标记 + 指向太阳的光线
        dl->AddCircle(p, radius * 0.045f, IM_COL32(255,225,140,180), 24, 1.4f);
        dl->AddLine(p, ImVec2(sun.x - dir.x * sunR, sun.y - dir.y * sunR),
                    IM_COL32(255,210,92,150), 1.4f);
    }
    for (int i = 5; i >= 0; --i) {
        float q = (float)i / 5.0f;
        dl->AddCircleFilled(sun, sunR * (0.55f + q * 0.75f),
                            IM_COL32(255, 176 + (int)(55*q), 45, (int)(12 + 26*q)), 24);
    }
    dl->AddCircleFilled(sun, sunR, front ? IM_COL32(255,221,100,255)
                                         : IM_COL32(150,125,70,190), 24);
    dl->AddText(ImVec2(sun.x - sunR, sun.y + sunR + 3.0f),
                front ? IM_COL32(255,225,130,245) : IM_COL32(170,150,110,200),
                UI(ps, "太阳", "Sun"));
}

static void DrawSolarGlobe(Renderer& renderer, const Scene& scene, PanelState& ps, float side) {
    ImVec2 origin = ImGui::GetCursorScreenPos();
    // Single source of truth for the base map: borders are exactly "not texture"
    // (and only when the data is actually present).
    ps.eclipseShowBoundaries = !ps.eclipseShowTexture && renderer.hasBoundaries();

    // ── texture mode: 3D OpenGL render ──────────────────────────────────────
    if (ps.eclipseShowTexture) {
        double td = SceneUtcToTd(scene);
        renderer.renderEclipseGlobe(ps.eclipseGlobeYaw, ps.eclipseGlobePitch,
                                    ps.eclipsePath, td, false,
                                    ps.eclipseShowLimits ? &ps.eclipseLimits : nullptr);

        // Drag interaction sits on an invisible button above the image
        ImGui::InvisibleButton("##eclipse_globe", ImVec2(side, side));
        if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
            ImGuiIO& io = ImGui::GetIO();
            ps.eclipseGlobeYaw   += io.MouseDelta.x * 0.45f;
            ps.eclipseGlobePitch  = std::clamp(ps.eclipseGlobePitch + io.MouseDelta.y * 0.45f,
                                               -85.0f, 85.0f);
        }

        unsigned int tex = renderer.eclipseGlobeTex();
        if (tex) {
            ImGui::SetCursorScreenPos(origin);
            ImGui::Image((ImTextureID)(intptr_t)tex, ImVec2(side, side),
                         ImVec2(0,1), ImVec2(1,0)); // y-flip (OpenGL ↔ ImGui)
        }

        // Solar direction indicator, drawn in the panel layer so it stays
        // legible over the night side. Its position tracks the real subsolar
        // point, so it swings around as the globe is dragged.
        ImDrawList* dl = ImGui::GetWindowDrawList();
        if (!ps.eclipsePath.empty()) {
            ImVec2 c(origin.x + side * 0.5f, origin.y + side * 0.5f);
            DrawGlobeSunMarker(dl, ps, td, c, side * 0.5f * 0.96f);
        }

        // Drag hint
        dl->AddText(ImVec2(origin.x + 9, origin.y + 8), IM_COL32(220,235,255,210),
                    UI(ps, "3D \u5730\u7403\u4eea\uff1a\u62d6\u52a8\u65cb\u8f6c",
                           "3D globe: drag to rotate"));

    // ── fallback: 2D projected globe ────────────────────────────────────────
    } else {
        ImGui::InvisibleButton("##eclipse_globe", ImVec2(side, side));
        if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
            ImGuiIO& io = ImGui::GetIO();
            ps.eclipseGlobeYaw   += io.MouseDelta.x * 0.45f;
            ps.eclipseGlobePitch  = std::clamp(ps.eclipseGlobePitch + io.MouseDelta.y * 0.45f,
                                               -85.0f, 85.0f);
        }
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 c(origin.x + side * 0.5f, origin.y + side * 0.5f);
        float  r = side * 0.44f;
        dl->AddRectFilled(origin, ImVec2(origin.x + side, origin.y + side),
                          IM_COL32(4,9,19,255), 6.0f);
        for (int i = 30; i >= 0; --i) {
            float q = (float)i / 30.0f;
            dl->AddCircleFilled(c, r * q,
                IM_COL32((int)(12+18*q),(int)(42+50*q),(int)(68+78*q),255), 96);
        }
        dl->AddCircle(c, r, IM_COL32(125,195,235,230), 96, 1.8f);
        DrawGlobeGrid(dl, c, r, ps.eclipseGlobeYaw, ps.eclipseGlobePitch);
        if (ps.eclipseShowBoundaries) DrawBoundariesOnGlobe(dl, renderer, ps, c, r);
        if (ps.eclipseShowLimits) DrawEclipseLimitsOnGlobe(dl, ps, c, r);
        DrawEclipsePathOnGlobe(dl, ps, c, r);

        if (!ps.eclipsePath.empty()) {
            double td = SceneUtcToTd(scene);
            const EclipsePathSample* nearest = &ps.eclipsePath.front();
            for (const EclipsePathSample& s : ps.eclipsePath)
                if (std::fabs(s.jdTd - td) < std::fabs(nearest->jdTd - td)) nearest = &s;
            ImVec2 p;
            if (nearest->center.valid && ProjectGlobePoint(nearest->center.longitudeDeg,
                    nearest->center.latitudeDeg, ps.eclipseGlobeYaw, ps.eclipseGlobePitch, c, r, p)) {
                dl->AddCircleFilled(p, r * 0.105f, IM_COL32(5,5,8,70), 40);
                dl->AddCircleFilled(p, r * 0.055f, IM_COL32(4,4,5,150), 40);
                dl->AddCircleFilled(p, 3.5f, IM_COL32(255,75,55,255), 24);
            }
            DrawGlobeSunMarker(dl, ps, td, c, r);
        }
        dl->AddText(ImVec2(origin.x + 9, origin.y + 8), IM_COL32(190,220,245,230),
                    UI(ps, "3D \u5730\u7403\u4eea\uff1a\u62d6\u52a8\u65cb\u8f6c",
                           "3D globe: drag to rotate"));
    }

    // ── base map: texture or borders, not both ────────────────────────────────────────────────
    // The textured Earth and the administrative outline are two ways of drawing
    // the same sphere; stacking them buried the eclipse path under both. One
    // choice drives both flags, so "neither" and "both" cannot happen.
    ImGui::TextDisabled("%s", UI(ps, "\u5e95\u56fe", "Base map"));
    ImGui::SameLine();
    if (ImGui::RadioButton(UI(ps, "\u7eb9\u7406\u5730\u7403", "Texture"), ps.eclipseShowTexture))
        ps.eclipseShowTexture = true;
    ImGui::SameLine();
    // \u884c\u653f\u533a\u5212\u53ea\u5728\u7f3a\u5c11 world_b.bin \u65f6\u7981\u7528\u3002
    if (!renderer.hasBoundaries()) ImGui::BeginDisabled();
    if (ImGui::RadioButton(UI(ps, "\u884c\u653f\u533a\u5212", "Admin borders"), !ps.eclipseShowTexture))
        ps.eclipseShowTexture = false;
    if (!renderer.hasBoundaries()) ImGui::EndDisabled();
    ImGui::Checkbox(UI(ps, "\u754c\u7ebf\u56fe", "Limit curves"), &ps.eclipseShowLimits);
    if (!renderer.hasBoundaries()) {
        ImGui::SameLine();
        ImGui::TextDisabled("(world_b.bin missing)");
    }
}

static void DrawLunarShadowView(const Scene& scene, PanelState& ps, float side) {
    ImVec2 origin = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##lunar_shadow", ImVec2(side, side));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 c(origin.x + side * 0.5f, origin.y + side * 0.5f);
    dl->AddRectFilled(origin, ImVec2(origin.x + side, origin.y + side), IM_COL32(4,5,13,255), 6.0f);
    double td = SceneUtcToTd(scene);
    LunarShadowGeometry g = lunarShadowGeometry(td);
    float scale = g.valid ? side * 0.31f / (float)g.penumbraRadius : 1.0f;
    float pr = g.valid ? (float)g.penumbraRadius * scale : side * 0.31f;
    float ur = g.valid ? (float)g.umbraRadius * scale : side * 0.20f;
    dl->AddCircleFilled(c, pr, IM_COL32(65,55,75,75), 96);
    dl->AddCircle(c, pr, IM_COL32(150,135,175,120), 96, 1.2f);
    dl->AddCircleFilled(c, ur, IM_COL32(24,8,12,205), 96);
    dl->AddCircle(c, ur, IM_COL32(175,65,60,200), 96, 1.6f);
    if (g.valid) {
        ImVec2 m(c.x + (float)g.x * scale, c.y - (float)g.y * scale);
        float mr = std::max(5.0f, (float)g.moonRadius * scale);
        for (int i = 18; i >= 0; --i) {
            float q = (float)i / 18.0f;
            dl->AddCircleFilled(ImVec2(m.x - mr*0.18f*(1.0f-q), m.y - mr*0.12f*(1.0f-q)),
                                mr*q, IM_COL32((int)(115+105*q),(int)(70+120*q),(int)(58+120*q),255),64);
        }
        dl->AddCircle(m, mr, IM_COL32(235,220,205,240), 64, 1.2f);
    }
    dl->AddText(ImVec2(origin.x + 9, origin.y + 8), IM_COL32(190,205,235,230),
                UI(ps, "3D \u6708\u98df\u5730\u5f71\u6f14\u793a", "3D lunar shadow simulation"));
}

static void DrawLightConeSpace(const Scene& scene, PanelState& ps,
                               const EclipseEvent& event, float side) {
    ImVec2 origin = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##light_cone", ImVec2(side, side));
    if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
        ImGuiIO& io = ImGui::GetIO();
        ps.eclipseSpaceYaw += io.MouseDelta.x * 0.35f;
        ps.eclipseSpacePitch = std::clamp(ps.eclipseSpacePitch + io.MouseDelta.y * 0.35f,
                                          -65.0f, 65.0f);
    }
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 end(origin.x + side, origin.y + side);
    dl->AddRectFilled(origin, end, IM_COL32(3,5,13,255), 6.0f);
    for (int i = 0; i < 55; ++i) {
        unsigned int h = (unsigned int)(i * 2654435761u);
        float x = origin.x + 6.0f + (h % 1000) / 1000.0f * (side - 12.0f);
        float y = origin.y + 6.0f + ((h >> 10) % 1000) / 1000.0f * (side - 12.0f);
        dl->AddCircleFilled(ImVec2(x,y), 0.8f, IM_COL32(190,205,235,100));
    }
    float cy = origin.y + side * (0.53f + ps.eclipseSpacePitch / 520.0f);
    float perspective = 0.86f + 0.14f * std::cos(ps.eclipseSpaceYaw * (float)kPI / 180.0f);
    ImVec2 sun(origin.x + side*0.13f, cy);
    ImVec2 blocker, target;
    float blockerR, targetR;
    const char* blockerName;
    const char* targetName;
    if (event.kind == EclipseEvent::Solar) {
        blocker = ImVec2(origin.x + side*(0.13f + 0.40f*perspective), cy - side*0.025f);
        target = ImVec2(origin.x + side*(0.13f + 0.71f*perspective), cy);
        blockerR = side*0.035f; targetR = side*0.095f;
        blockerName = UI(ps, "\u6708\u7403", "Moon"); targetName = UI(ps, "\u5730\u7403", "Earth");
    } else {
        blocker = ImVec2(origin.x + side*(0.13f + 0.39f*perspective), cy);
        target = ImVec2(origin.x + side*(0.13f + 0.71f*perspective), cy - side*0.018f);
        blockerR = side*0.075f; targetR = side*0.042f;
        blockerName = UI(ps, "\u5730\u7403", "Earth"); targetName = UI(ps, "\u6708\u7403", "Moon");
    }
    float moonTrack = (float)std::clamp((SceneUtcToTd(scene) - event.maximumTd) * 24.0,
                                        -4.0, 4.0) * side * 0.055f;
    if (event.kind == EclipseEvent::Solar) blocker.y += moonTrack;
    else target.y += moonTrack;
    float sunR = side * 0.13f;

    // Umbra/penumbra as the actual tangent cones of the drawn spheres, rather
    // than a triangle run to a fixed screen position. For a light source of
    // radius Rs at distance d, a blocker of radius Rb casts an umbra of length
    // L = d*Rb/(Rs-Rb); the cone is bounded by the outer tangents and the
    // penumbra by the inner (crossing) pair.
    //
    // Sizes here are schematic, so the blocker radius is chosen to put that
    // apex where it physically belongs: short of the Earth for an annular
    // eclipse, past its surface for a total one, and well beyond the Moon for
    // a lunar eclipse. That is the whole distinction between 日全食 and
    // 日环食, and a fixed apex could never show it.
    float dx = blocker.x - sun.x, dy = blocker.y - sun.y;
    float d  = std::sqrt(dx*dx + dy*dy);
    if (d < 1.0f) d = 1.0f;
    ImVec2 u(dx / d, dy / d);              // sun -> blocker
    ImVec2 pp(-u.y, u.x);                  // perpendicular
    float gap = std::sqrt((target.x-blocker.x)*(target.x-blocker.x) +
                          (target.y-blocker.y)*(target.y-blocker.y));

    float wantL;
    if (event.kind == EclipseEvent::Solar) {
        bool annular = !event.type.empty() && (event.type[0] == 'A');
        wantL = annular ? (gap - targetR) * 0.72f     // apex falls short of Earth
                        : (gap - targetR) * 1.25f;    // apex reaches past the surface
    } else {
        wantL = (gap + targetR) * 1.35f;              // Earth's umbra clears the Moon
    }
    if (wantL < side * 0.05f) wantL = side * 0.05f;
    blockerR = sunR * wantL / (d + wantL);            // invert L = d*Rb/(Rs-Rb)

    ImVec2 apex(blocker.x + u.x * wantL, blocker.y + u.y * wantL);
    ImVec2 sT(sun.x + pp.x*sunR, sun.y + pp.y*sunR), sB(sun.x - pp.x*sunR, sun.y - pp.y*sunR);
    ImVec2 bT(blocker.x + pp.x*blockerR, blocker.y + pp.y*blockerR);
    ImVec2 bB(blocker.x - pp.x*blockerR, blocker.y - pp.y*blockerR);

    // Penumbra: inner tangents, crossing at the apex and widening past it.
    float far = side * 1.6f;
    auto extend = [&](ImVec2 a, ImVec2 b) {
        float ex = b.x-a.x, ey = b.y-a.y, n = std::sqrt(ex*ex+ey*ey);
        if (n < 1e-3f) return b;
        return ImVec2(a.x + ex/n*far, a.y + ey/n*far);
    };
    dl->AddTriangleFilled(sT, sB, apex, IM_COL32(255,205,80,20));
    dl->AddLine(sT, extend(sT, bB), IM_COL32(255,205,90,70), 1.0f);
    dl->AddLine(sB, extend(sB, bT), IM_COL32(255,205,90,70), 1.0f);

    // Umbra: outer tangents converging on the apex.
    dl->AddTriangleFilled(bT, bB, apex, IM_COL32(12,12,24,190));
    dl->AddLine(bT, apex, IM_COL32(125,145,190,180), 1.3f);
    dl->AddLine(bB, apex, IM_COL32(125,145,190,180), 1.3f);

    for (int i = 20; i >= 0; --i) {
        float q=(float)i/20.0f;
        dl->AddCircleFilled(ImVec2(sun.x-sunR*0.18f*(1-q),sun.y-sunR*0.18f*(1-q)),sunR*q,
                            IM_COL32(255,(int)(145+90*q),(int)(30+70*q),255),64);
    }
    dl->AddCircleFilled(blocker, blockerR, event.kind==EclipseEvent::Solar ? IM_COL32(145,150,160,255) : IM_COL32(45,105,170,255), 48);
    dl->AddCircleFilled(target, targetR, event.kind==EclipseEvent::Solar ? IM_COL32(45,105,170,255) : IM_COL32(150,145,140,255), 48);
    dl->AddText(ImVec2(sun.x-sunR*0.45f, sun.y+sunR+8), IM_COL32(255,220,120,240), UI(ps,"\u592a\u9633","Sun"));
    dl->AddText(ImVec2(blocker.x-blockerR, blocker.y+blockerR+8), IM_COL32(210,220,240,230), blockerName);
    dl->AddText(ImVec2(target.x-targetR, target.y+targetR+8), IM_COL32(210,220,240,230), targetName);
    dl->AddText(ImVec2(origin.x+9,origin.y+8), IM_COL32(190,220,245,230),
                UI(ps,"\u592a\u9633\u2014\u5730\u7403\u2014\u6708\u7403\u4e09\u4f53\u5149\u9525\u7a7a\u95f4","Sun-Earth-Moon light-cone space"));
    dl->AddText(ImVec2(origin.x+9,origin.y+27), IM_COL32(125,155,195,210),
                UI(ps,"\u62d6\u52a8\u65cb\u8f6c\u89c6\u89d2\uff0c\u5929\u4f53\u5c3a\u5bf8\u4e0e\u8ddd\u79bb\u5df2\u5938\u5f20","Drag to orbit; body sizes and distances are exaggerated"));
    (void)scene;
}

static void SelectEclipse(PanelState& ps, int index) {
    if (index < 0 || index >= (int)ps.eclipseEvents.size()) return;
    ps.selectedEclipse = index;
    EclipseEvent& event = ps.eclipseEvents[index];
    if (event.kind == EclipseEvent::Solar) {
        calculateLocalSolarEclipse(event, ps.observerLongitude, ps.observerLatitude,
                                   ps.observerAltitudeKm, ps.eclipseNasaRadius);
        ps.eclipsePath = sampleSolarEclipsePath(event, 2.0);
        ps.eclipseLimits = computeSolarEclipseLimits(event);
        // Frame the globe on the eclipse path. Both the 2-D projection and the
        // GL model transform reduce to z = cos(lat - pitch) * cos(lon + yaw)
        // for the facing test, so the centre is brought forward by yaw =
        // -centreLon and pitch = +centreLat. The latitude term is damped so a
        // near-polar path does not tip the globe all the way over.
        ps.eclipseGlobeYaw = (float)-event.centerLongitudeDeg;
        ps.eclipseGlobePitch = (float)event.centerLatitudeDeg * 0.35f;
    } else {
        ps.eclipsePath.clear();
        ps.eclipseLimits = EclipseLimits{};
    }
}

// The selected eclipse as plain text: the same numbers the panel shows, in the
// same order, so a pasted block reads like the screen it came from.
static std::string EclipseExportText(const PanelState& ps, const EclipseEvent& e) {
    char buf[256];
    std::string out = std::string(EclipseKindText(ps, e)) + "  " + EclipseTypeText(ps, e) + "\n";
    out += std::string(UI(ps, "\u98df\u751a: ", "Maximum: ")) +
           EclipseTimeText(e.maximumTd, ps);
    std::snprintf(buf, sizeof(buf), "  (UTC%+.2f)\n", ps.timezoneHours);
    out += buf;
    std::snprintf(buf, sizeof(buf), "%s %.4f\n", UI(ps, "\u98df\u5206:", "Magnitude:"), e.magnitude);
    out += buf;
    if (e.kind == EclipseEvent::Solar) {
        std::snprintf(buf, sizeof(buf), "%s %.2f, %.2f\n",
                      UI(ps, "\u4e2d\u5fc3\u7ecf\u7eac:", "Center lon/lat:"),
                      e.centerLongitudeDeg, e.centerLatitudeDeg);
        out += buf;
        std::snprintf(buf, sizeof(buf), "%s %.1f km   %s %.1f s\n",
                      UI(ps, "\u98df\u5e26\u5bbd:", "Path width:"), e.pathWidthKm,
                      UI(ps, "\u4e2d\u5fc3\u6301\u7eed:", "Central duration:"),
                      e.durationDays * 86400.0);
        out += buf;
        if (!e.localType.empty()) {
            std::snprintf(buf, sizeof(buf), "%s %.6f, %.6f  %s %s  %s %.4f\n",
                          UI(ps, "\u89c2\u6d4b\u70b9:", "Observer:"),
                          ps.observerLongitude, ps.observerLatitude,
                          UI(ps, "\u5730\u65b9\u7c7b\u578b:", "Local type:"), e.localType.c_str(),
                          UI(ps, "\u98df\u5206:", "Magnitude:"), e.localMagnitude);
            out += buf;
        }
    }
    out += std::string(UI(ps, "\u3010\u98df\u9636\u65f6\u523b\u3011", "[Contact times]")) + "\n";
    static const char* solarNames[] = {"C1", "C4", "C2", "C3"};
    static const int   solarIdx[]   = {0, 2, 3, 4};
    if (e.kind == EclipseEvent::Solar) {
        out += "MAX " + EclipseTimeText(e.maximumTd, ps) + "\n";
        for (int i = 0; i < 4; ++i) {
            if (!e.contactsTd[solarIdx[i]]) continue;
            out += std::string(solarNames[i]) + "  " +
                   EclipseTimeText(e.contactsTd[solarIdx[i]], ps) + "\n";
        }
    } else {
        static const char* names[] = {"U1", "MAX", "U4", "P1", "P4", "U2", "U3"};
        for (int i = 0; i < 7; ++i) {
            if (!e.contactsTd[i]) continue;
            out += std::string(names[i]) + "  " + EclipseTimeText(e.contactsTd[i], ps) + "\n";
        }
    }
    return out;
}

void DrawEclipseContent(Renderer& renderer, Scene& scene, PanelState& ps) {
    DrawTransportBar(scene, ps);
    ImGui::Separator();

    if (ImGui::BeginTable("##eclipse_search", 3, ImGuiTableFlags_SizingStretchSame)) {
        ImGui::TableNextColumn(); ImGui::TextDisabled("%s", UI(ps,"\u8d77\u59cb\u5e74","Start year")); ImGui::SetNextItemWidth(-FLT_MIN); ImGui::InputInt("##ec_year", &ps.eclipseYear);
        ImGui::TableNextColumn(); ImGui::TextDisabled("%s", UI(ps,"\u6708","Month")); ImGui::SetNextItemWidth(-FLT_MIN); ImGui::InputInt("##ec_month", &ps.eclipseMonth);
        ImGui::TableNextColumn(); ImGui::TextDisabled("%s", UI(ps,"\u6570\u91cf","Count")); ImGui::SetNextItemWidth(-FLT_MIN); ImGui::InputInt("##ec_count", &ps.eclipseCount);
        ImGui::EndTable();
    }
    ps.eclipseMonth = std::clamp(ps.eclipseMonth, 1, 12);
    ps.eclipseCount = std::clamp(ps.eclipseCount, 1, 100);
    const char* filterZh[] = {"\u5168\u90e8", "\u65e5\u98df", "\u6708\u98df"};
    const char* filterEn[] = {"All", "Solar", "Lunar"};
    ImGui::SetNextItemWidth(S(140.0f));
    ImGui::Combo("##eclipse_filter", &ps.eclipseFilter, ps.useChinese ? filterZh : filterEn, 3);
    ImGui::SameLine();
    if (ImGui::Button(UI(ps,"\u641c\u7d22\u65e5\u6708\u98df","Search eclipses"))) {
        ps.eclipseEvents = searchEclipses(ps.eclipseYear, ps.eclipseMonth,
                                          ps.eclipseCount, ps.eclipseFilter);
        ps.selectedEclipse = -1;
        ps.eclipsePath.clear();
        if (!ps.eclipseEvents.empty()) SelectEclipse(ps, 0);
    }

    if (ps.eclipseEvents.empty()) {
        ImGui::Spacing();
        ImGui::TextDisabled("%s", UI(ps,"\u8bbe\u7f6e\u8d77\u59cb\u65e5\u671f\u540e\u641c\u7d22\u3002","Choose a start date and search."));
        return;
    }

    ImGui::SeparatorText(UI(ps,"\u641c\u7d22\u7ed3\u679c","Results"));
    {
        // The whole search, one event per line - the list is what people want
        // when they are collecting dates rather than studying one eclipse.
        std::string all;
        for (const EclipseEvent& ev : ps.eclipseEvents)
            all += EclipseTimeText(ev.maximumTd, ps) + "  " + EclipseKindText(ps, ev) +
                   "  " + EclipseTypeText(ps, ev) + "\n";
        DrawCopyButton(ps, "copy_eclipse_list", all);
    }
    // On a touchscreen the list is not put in a scrolling box of its own. A
    // panel that scrolls inside a page that scrolls means every drag has to
    // guess which one it belongs to, and the loser is whichever one the finger
    // happened to start over; the page already scrolls by drag, so the list
    // simply rides along in it.
    const bool inlineList = g_touchMode;
    if (inlineList || ImGui::BeginChild("##eclipse_results", ImVec2(0, S(116)), true)) {
        for (int i = 0; i < (int)ps.eclipseEvents.size(); ++i) {
            EclipseEvent& e = ps.eclipseEvents[i];
            std::string label = EclipseTimeText(e.maximumTd, ps) + "  " +
                                EclipseKindText(ps, e) + "  " + EclipseTypeText(ps, e);
            bool picked = ImGui::Selectable(label.c_str(), ps.selectedEclipse == i,
                                            0, ImVec2(0, inlineList ? S(30.0f) : 0.0f));
            // A drag that started on a row was meant to scroll the page, not to
            // choose the row it began over.
            if (picked && inlineList) {
                ImVec2 dd = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left);
                if (dd.x * dd.x + dd.y * dd.y > S(9.0f) * S(9.0f)) picked = false;
            }
            if (picked) SelectEclipse(ps, i);
        }
    }
    if (!inlineList) ImGui::EndChild();
    if (ps.selectedEclipse < 0 || ps.selectedEclipse >= (int)ps.eclipseEvents.size()) return;
    EclipseEvent& e = ps.eclipseEvents[ps.selectedEclipse];

    ImGui::SeparatorText(UI(ps,"\u8be6\u60c5\u4e0e\u6f14\u793a","Details and simulation"));
    DrawCopyButton(ps, "copy_eclipse", EclipseExportText(ps, e));
    ImGui::TextColored(ImVec4(1.0f,0.78f,0.30f,1.0f), "%s  %s", EclipseKindText(ps,e), EclipseTypeText(ps,e).c_str());
    ImGui::Text("%s %s  (UTC%+.2f)", UI(ps,"\u98df\u751a:","Maximum:"), EclipseTimeText(e.maximumTd, ps).c_str(), ps.timezoneHours);
    ImGui::Text("%s %.4f", UI(ps,"\u98df\u5206:","Magnitude:"), e.magnitude);
    if (e.kind == EclipseEvent::Solar) {
        ImGui::Text("%s %.2f, %.2f", UI(ps,"\u4e2d\u5fc3\u7ecf\u7eac:","Center lon/lat:"), e.centerLongitudeDeg, e.centerLatitudeDeg);
        ImGui::Text("%s %.1f km   %s %.1f s", UI(ps,"\u98df\u5e26\u5bbd:","Path width:"), e.pathWidthKm,
                    UI(ps,"\u4e2d\u5fc3\u6301\u7eed:","Central duration:"), e.durationDays*86400.0);
        if (ImGui::TreeNode(UI(ps,"\u5730\u65b9\u89c2\u6d4b\u70b9","Local observer"))) {
            ImGui::InputDouble(UI(ps,"\u7ecf\u5ea6","Longitude"), &ps.observerLongitude, 0.1, 1.0, "%.6f");
            ImGui::InputDouble(UI(ps,"\u7eac\u5ea6","Latitude"), &ps.observerLatitude, 0.1, 1.0, "%.6f");
            ImGui::InputDouble(UI(ps,"\u6d77\u62d4 km","Altitude km"), &ps.observerAltitudeKm, 0.01, 0.1, "%.3f");
            ImGui::Checkbox("NASA radius", &ps.eclipseNasaRadius);
            if (ImGui::Button(UI(ps,"\u91cd\u65b0\u8ba1\u7b97\u5730\u65b9\u98df","Recalculate local eclipse")))
                calculateLocalSolarEclipse(e, ps.observerLongitude, ps.observerLatitude,
                                           ps.observerAltitudeKm, ps.eclipseNasaRadius);
            ImGui::Text("%s %s  %s %.4f", UI(ps,"\u5730\u65b9\u7c7b\u578b:","Local type:"),
                        e.localType.empty()?"--":e.localType.c_str(), UI(ps,"\u98df\u5206:","Magnitude:"), e.localMagnitude);
            ImGui::TreePop();
        }
    }

    double first = 0.0, last = 0.0;
    EclipseSpan(e, first, last);
    if (ImGui::Button(UI(ps,"\u8df3\u5230\u98df\u751a","Jump to maximum"))) {
        scene.clock().jd = eclipseTdToUtcJD(e.maximumTd);
        scene.clock().playing = false;
    }
    ImGui::SameLine();
    if (ImGui::Button(UI(ps,"\u4ece\u98df\u59cb\u6f14\u793a","Play from start")))
        StartEclipseDemo(scene, ps, e);
    ImGui::SameLine();
    if (ImGui::Button(scene.clock().playing ? UI(ps,"\u6682\u505c","Pause") : UI(ps,"\u7ee7\u7eed","Resume")))
        scene.clock().playing = !scene.clock().playing;
    ImGui::SameLine();
    // The 3-D viewport is where the demonstration plays; this only says which
    // way to watch it from.
    if (ImGui::Button(UI(ps,"\u5730\u9762\u89c6\u89d2","Ground view"))) {
        ps.vpEclipseView = PanelState::EV_Ground;
        ps.groundLookYawDeg = ps.groundLookPitchDeg = 0.0f;
    }
    UpdateEclipseDemo(scene, ps);
    float progress = last > first ? (float)((SceneUtcToTd(scene)-first)/(last-first)) : 0.0f;
    progress = std::clamp(progress, 0.0f, 1.0f);
    ImGui::ProgressBar(progress, ImVec2(-FLT_MIN, S(8.0f)), "");

    const char* modesZh[] = {"3D \u98df\u5f71", "\u4e09\u4f53\u5149\u9525"};
    const char* modesEn[] = {"3D shadow", "Three-body light cone"};
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::Combo("##eclipse_view", &ps.eclipseViewMode, ps.useChinese ? modesZh : modesEn, 2);
    float side = std::clamp(ImGui::GetContentRegionAvail().x, S(180.0f), S(520.0f));
    if (ps.eclipseViewMode == 1) DrawLightConeSpace(scene, ps, e, side);
    else if (e.kind == EclipseEvent::Solar) DrawSolarGlobe(renderer, scene, ps, side);
    else DrawLunarShadowView(scene, ps, side);

    if (ImGui::TreeNode(UI(ps,"\u98df\u9636\u65f6\u523b","Contact times"))) {
        if (e.kind == EclipseEvent::Solar) {
            ImGui::Text("C1  %s", EclipseTimeText(e.contactsTd[0], ps).c_str());
            ImGui::Text("MAX %s", EclipseTimeText(e.maximumTd, ps).c_str());
            ImGui::Text("C4  %s", EclipseTimeText(e.contactsTd[2], ps).c_str());
            if (e.contactsTd[3]) ImGui::Text("C2  %s", EclipseTimeText(e.contactsTd[3], ps).c_str());
            if (e.contactsTd[4]) ImGui::Text("C3  %s", EclipseTimeText(e.contactsTd[4], ps).c_str());
        } else {
            const char* names[] = {"U1", "MAX", "U4", "P1", "P4", "U2", "U3"};
            for (int i=0;i<7;++i) if (e.contactsTd[i]) ImGui::Text("%s  %s", names[i], EclipseTimeText(e.contactsTd[i], ps).c_str());
        }
        ImGui::TreePop();
    }
}

// ============================================================================
//  Public API
// ============================================================================

void DrawMainMenuBar(Scene& scene, RenderOptions& ropt, PanelState& ps) {
    if (!ImGui::BeginMainMenuBar()) return;

    ImGui::TextColored({0.95f,0.77f,0.30f,1.0f}, "%s", UI(ps, "\u5bff\u661f\u5929\u6587\u5386", "SXWNL Calendar"));
    ImGui::SameLine();
    ImGui::TextDisabled("3D");
    ImGui::Separator();

    if (ImGui::BeginMenu(UI(ps, "\u89c6\u56fe", "View"))) {
        if (ImGui::MenuItem(UI(ps, "\u663e\u793a\u8f68\u9053", "Show orbits"), nullptr, &ropt.showOrbits)) {}
        if (ImGui::MenuItem(UI(ps, "\u663e\u793a\u6708\u7403", "Show moon"), nullptr, &ropt.showMoon))   {}
        if (ImGui::MenuItem(UI(ps, "\u6807\u7b7e\u540d\u79f0", "Show labels"), nullptr, &ropt.showLabels)) {}
        ImGui::Separator();
        if (ImGui::MenuItem(UI(ps, "\u56de\u5230\u4eca\u5929", "Today")))  scene.clock().jd = nowJD();
        if (ImGui::MenuItem(UI(ps, "\u64ad\u653e/\u6682\u505c", "Play/Pause"))) scene.clock().playing = !scene.clock().playing;
        ImGui::Separator();
        // Lets the Android layout be reviewed on the desktop build.
        if (ImGui::MenuItem(UI(ps, "\u624b\u673a\u5e03\u5c40", "Phone layout"),
                            nullptr, &ps.mobilePreview)) {}
        if (ImGui::MenuItem(UI(ps, "\u9ec4\u5386(\u5b9c\u5fcc/\u795e\u715e)", "Almanac"),
                            nullptr, &ps.showAlmanac)) {}
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu(UI(ps, "\u5de5\u5177", "Tools"))) {
        if (ImGui::MenuItem(UI(ps, "\u8fd0\u884c\u53c2\u6570", "Parameters"),  nullptr, false)) ps.activeTab = 0;
        if (ImGui::MenuItem(UI(ps, "\u519c\u5386\u5386\u6cd5", "Calendar"),  nullptr, false)) ps.activeTab = 1;
        if (ImGui::MenuItem(UI(ps, "\u884c\u661f\u661f\u5386", "Ephemeris"),  nullptr, false)) ps.activeTab = 2;
        if (ImGui::MenuItem(UI(ps, "\u8282\u6c14\u6714\u671b", "Terms"),  nullptr, false)) ps.activeTab = 3;
        if (ImGui::MenuItem(UI(ps, "\u516b\u5b57", "Bazi"),  nullptr, false)) ps.activeTab = 4;
        if (ImGui::MenuItem(UI(ps, "\u6708\u76f8", "Moon phase"), nullptr, false)) ps.activeTab = 5;
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu(UI(ps, "\u8bed\u8a00", "Language"))) {
        if (ImGui::MenuItem("\u4e2d\u6587", nullptr, ps.useChinese)) ps.useChinese = true;
        if (ImGui::MenuItem("English", nullptr, !ps.useChinese)) ps.useChinese = false;
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu(UI(ps, "\u5e2e\u52a9", "Help"))) {
        ImGui::TextDisabled("SXWNL GUI v2.1");
        ImGui::TextDisabled("%s", UI(ps, "\u9f20\u6807\u5de6\u952e\u62d6\u62fd: \u65cb\u8f6c\u89c6\u89d2", "Left drag: rotate view"));
        ImGui::TextDisabled("%s", UI(ps, "\u9f20\u6807\u53f3\u952e\u62d6\u62fd: \u5e73\u79fb\u89c6\u89d2", "Right drag: pan view"));
        ImGui::TextDisabled("%s", UI(ps, "\u6eda\u8f6e: \u7f29\u653e", "Mouse wheel: zoom"));
        ImGui::TextDisabled("%s", UI(ps, "\u5de6\u952e\u5355\u51fb: \u9009\u4e2d\u5929\u4f53", "Left click: select body"));
        ImGui::TextDisabled("%s", UI(ps, "\u5de6\u952e\u53cc\u51fb: \u805a\u7126\u5929\u4f53", "Double click: focus body"));
        ImGui::EndMenu();
    }

    // Live wall clock, right-aligned. The desktop had no "what time is it now"
    // anywhere - only the simulation clock - so the two were easy to confuse.
    {
        Date now = localDateFromUtcJD(nowJD(), ps.timezoneHours);
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d  %02d:%02d:%02d",
                      now.Y, now.M, now.D, now.h, now.m, (int)now.s);
        float w = ImGui::CalcTextSize(buf).x;
        float avail = ImGui::GetContentRegionAvail().x;
        if (avail > w + 24.0f) {
            ImGui::SameLine(ImGui::GetWindowWidth() - w - 16.0f);
            ImGui::TextColored(ImVec4(0.95f, 0.83f, 0.45f, 1.0f), "%s", buf);
        }
    }

    ImGui::EndMainMenuBar();
}

    // UI section.
// ---------------------------------------------------------------------------
//  Shared control blocks
// ---------------------------------------------------------------------------
// These used to sit inline in DrawSidebar. They are separate functions now so
// the mobile shell can arrange the very same controls into its own pages
// without duplicating any logic.

void DrawClockCard(Scene& scene, PanelState& ps) {
    SimClock& clk = scene.clock();
    Date cur = localDateFromUtcJD(clk.jd, ps.timezoneHours);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.075f, 0.095f, 0.145f, 0.90f));
    float cardH = ImGui::GetTextLineHeightWithSpacing() * 2.0f   // label + date
                + S(28.0f)                                       // icon button row
                + ImGui::GetStyle().WindowPadding.y * 2.0f
                + S(2.0f);
    ImGui::BeginChild("##clock_card", ImVec2(0, cardH), true, ImGuiWindowFlags_NoScrollbar);
    ImGui::TextDisabled("%s", UI(ps, "\u6a21\u62df\u65f6\u95f4", "Simulation time"));
    ImGui::TextColored({0.72f,0.90f,1.0f,1.0f},
        "%04d-%02d-%02d  %02d:%02d", cur.Y, cur.M, cur.D, cur.h, cur.m);
    ImGui::Spacing();

    // Play/Pause + Today buttons with drawn icons
    {
        float gap  = ImGui::GetStyle().ItemSpacing.x;
        float sz   = S(28.0f); // icon button size
        ImU32 icol = IM_COL32(180, 210, 255, 230);

        // play / pause
        if (clk.playing) {
            if (IconButton("##pause", sz, icol,
                    [](ImDrawList* d, ImVec2 p, float s, ImU32 c){ DrawIconPause(d,p,s,c); }))
                clk.playing = false;
        } else {
            if (IconButton("##play", sz, icol,
                    [](ImDrawList* d, ImVec2 p, float s, ImU32 c){ DrawIconPlay(d,p,s,c); }))
                clk.playing = true;
        }
        ImGui::SameLine();
        // today
        if (IconButton("##today", sz, icol,
                [](ImDrawList* d, ImVec2 p, float s, ImU32 c){ DrawIconHome(d,p,s,c); })) {
            clk.jd = nowJD();
            clk.playing = false;
        }
        ImGui::SameLine();
        ImGui::TextColored(clk.playing ? ImVec4(0.45f, 0.95f, 0.62f, 1.0f)
                                       : ImVec4(0.66f, 0.72f, 0.84f, 1.0f),
                           "%s", clk.playing ? UI(ps, "\u64ad\u653e\u4e2d", "Playing") : UI(ps, "\u6682\u505c", "Paused"));
        // Current rate, in whichever unit reads most naturally.
        ImGui::SameLine();
        ImGui::TextDisabled("%s", FormatSpeed(ps, clk.speedDaysPerSec).c_str());
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();
}

void DrawTimeControls(Scene& scene, PanelState& ps) {
    SimClock& clk = scene.clock();
    {
        float oldTimezone = ps.timezoneHours;
        ImGui::TextDisabled("%s", UI(ps, "\u65f6\u533a", "Time zone"));
        const char* tzPreview = std::fabs(ps.timezoneHours - 8.0f) < 0.001f
            ? UI(ps, "\u5317\u4eac/\u4e0a\u6d77 UTC+08:00", "Beijing/Shanghai UTC+08:00")
            : UI(ps, "\u81ea\u5b9a\u4e49 UTC \u504f\u79fb", "Custom UTC offset");
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::BeginCombo("##timezone", tzPreview)) {
            if (ImGui::Selectable(UI(ps, "\u5317\u4eac/\u4e0a\u6d77 UTC+08:00", "Beijing/Shanghai UTC+08:00"),
                                  std::fabs(ps.timezoneHours - 8.0f) < 0.001f))
                ps.timezoneHours = 8.0f;
            if (ImGui::Selectable("UTC+00:00", std::fabs(ps.timezoneHours) < 0.001f))
                ps.timezoneHours = 0.0f;
            ImGui::EndCombo();
        }
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::SliderFloat("##timezone_offset", &ps.timezoneHours, -12.0f, 14.0f,
                               "UTC%+.2f h"))
            ps.timezoneHours = std::round(ps.timezoneHours * 4.0f) / 4.0f;
        if (oldTimezone != ps.timezoneHours) {
            ps.ephSig = ps.termSig = ps.baziSig = -1;
            SyncDateFromScene(ps, scene);
        }

        ImGui::TextDisabled("%s", UI(ps, "\u901f\u5ea6\u9884\u8bbe", "Speed preset"));
        const char* unitsZh[] = {"\u79d2 / \u771f\u5b9e\u79d2", "\u5c0f\u65f6 / \u771f\u5b9e\u79d2", "\u65e5 / \u771f\u5b9e\u79d2"};
        const char* unitsEn[] = {"seconds / real second", "hours / real second", "days / real second"};
        ImGui::SetNextItemWidth(-FLT_MIN);
        bool speedChanged = ImGui::Combo("##speed_unit", &ps.speedUnit,
                                         ps.useChinese ? unitsZh : unitsEn, 3);
        ImGui::SetNextItemWidth(-FLT_MIN);
        speedChanged |= ImGui::DragFloat("##speed_amount", &ps.speedAmount,
                                         ps.speedUnit == 0 ? 0.1f : (ps.speedUnit == 1 ? 0.25f : 1.0f),
                                         -100000.0f, 100000.0f, "x = %.3f");
        if (!std::isfinite(ps.speedAmount)) ps.speedAmount = 0.0f;
        if (speedChanged)
            clk.speedDaysPerSec = (float)speedToDaysPerSecond(ps.speedUnit, ps.speedAmount);
        ImGui::TextDisabled("%s %.8g d/s", UI(ps, "\u6362\u7b97:", "Converted:"), clk.speedDaysPerSec);
    }

    ImGui::Spacing();
}

// Every page that animates off the simulation clock needs the same three
// things: what time it is showing, a way to start and stop it, and a rate. The
// solar system had them in the sidebar and nowhere else, so the moon-phase and
// eclipse views moved without any way to steer them.
void DrawTransportBar(Scene& scene, PanelState& ps) {
    SimClock& clk = scene.clock();
    Date cur = localDateFromUtcJD(clk.jd, ps.timezoneHours);

    const float sz = S(26.0f);
    const ImU32 icol = IM_COL32(180, 210, 255, 230);
    if (clk.playing) {
        if (IconButton("##tp_pause", sz, icol,
                [](ImDrawList* d, ImVec2 p, float s2, ImU32 c) { DrawIconPause(d, p, s2, c); }))
            clk.playing = false;
    } else {
        if (IconButton("##tp_play", sz, icol,
                [](ImDrawList* d, ImVec2 p, float s2, ImU32 c) { DrawIconPlay(d, p, s2, c); }))
            clk.playing = true;
    }
    ImGui::SameLine();
    if (IconButton("##tp_today", sz, icol,
            [](ImDrawList* d, ImVec2 p, float s2, ImU32 c) { DrawIconHome(d, p, s2, c); })) {
        clk.jd = nowJD();
        clk.playing = false;
    }
    ImGui::SameLine();
    ImGui::TextColored({0.72f, 0.90f, 1.0f, 1.0f}, "%04d-%02d-%02d %02d:%02d",
                       cur.Y, cur.M, cur.D, cur.h, cur.m);

    const char* unitsZh[] = {"\u79d2/\u79d2", "\u5c0f\u65f6/\u79d2", "\u65e5/\u79d2"};
    const char* unitsEn[] = {"sec/s", "hour/s", "day/s"};
    ImGui::SetNextItemWidth(S(110.0f));
    bool changed = ImGui::Combo("##tp_unit", &ps.speedUnit,
                                ps.useChinese ? unitsZh : unitsEn, 3);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-FLT_MIN);
    changed |= ImGui::DragFloat("##tp_amount", &ps.speedAmount,
                                ps.speedUnit == 0 ? 0.1f : (ps.speedUnit == 1 ? 0.25f : 1.0f),
                                -100000.0f, 100000.0f, "x = %.3f");
    if (!std::isfinite(ps.speedAmount)) ps.speedAmount = 0.0f;
    if (changed)
        clk.speedDaysPerSec = (float)speedToDaysPerSecond(ps.speedUnit, ps.speedAmount);
}

void DrawJumpDate(Scene& scene, PanelState& ps) {
    SimClock& clk = scene.clock();
    SectionHeader(ps, "\u8df3\u8f6c\u65e5\u671f", "Jump date");
    DrawDateFields(ps, "jmp", ps.year, ps.month, ps.day);
    if (IconButton("##jmp", S(24.0f), IM_COL32(180,210,255,230),
            [](ImDrawList* d, ImVec2 p, float s, ImU32 c){ DrawIconJump(d,p,s,c); }))
        clk.jd = utcJDFromLocalDate(Date{ps.year, ps.month, ps.day, 12, 0, 0.0},
                        ps.timezoneHours);

    // UI section.
    ImGui::Spacing();
}

void DrawDisplaySettings(Scene& scene, RenderOptions& ropt, PanelState& ps) {
    SectionHeader(ps, "\u663e\u793a\u8bbe\u7f6e", "Display");
    if (ImGui::BeginTable("##display_switches", 2, ImGuiTableFlags_SizingStretchSame)) {
        ImGui::TableNextColumn(); ImGui::Checkbox(UI(ps, "\u663e\u793a\u8f68\u9053", "Show orbits"), &ropt.showOrbits);
        ImGui::TableNextColumn(); ImGui::Checkbox(UI(ps, "\u663e\u793a\u6708\u7403", "Show moon"), &ropt.showMoon);
        ImGui::TableNextColumn(); ImGui::Checkbox(UI(ps, "\u6807\u7b7e\u540d\u79f0", "Show labels"), &ropt.showLabels);
        ImGui::TableNextColumn(); ImGui::Checkbox(UI(ps, "\u663e\u793a\u5730\u8f74", "Earth axis"), &ropt.showEarthAxis);
        ImGui::TableNextColumn(); ImGui::Checkbox(UI(ps, "\u65f6\u7a7a\u7f51\u683c", "Spacetime grid"), &ropt.showGravityGrid);
        ImGui::TableNextColumn(); ImGui::Checkbox(UI(ps, "\u5c0f\u884c\u661f\u5e26", "Asteroid belt"), &ropt.showAsteroids);
        ImGui::TableNextColumn(); ImGui::Checkbox(UI(ps, "\u592a\u9633\u706b\u7130", "Solar flames"), &ropt.showSolarFlames);
        ImGui::EndTable();
    }
    if (ropt.showGravityGrid) {
        ImGui::TextDisabled("%s", UI(ps, "\u7f51\u683c\u5bc6\u5ea6", "Grid density"));
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::SliderFloat("##grav_density", &ropt.gravityGridDensity, 0.6f, 4.0f, "%.1f");
        ImGui::TextDisabled("%s", UI(ps, "\u76f8\u5bf9\u8bba\u66f2\u7387", "Relativity curve"));
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::SliderFloat("##grav_curve", &ropt.gravityGridCurvature, 0.1f, 3.0f, "%.1f");
    }

    ScaleParams& sc = scene.scale();
    ImGui::Checkbox(UI(ps, "\u5bf9\u6570\u8ddd\u79bb\u538b\u7f29", "Log distance"), &sc.logDistance);
    if (sc.logDistance) {
        ImGui::TextDisabled("%s", UI(ps, "\u5bf9\u6570 k", "Log k"));
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::SliderFloat("##logk", &sc.logK, 4.0f, 30.0f, "%.1f");
    } else {
        ImGui::TextDisabled("%s", UI(ps, "AU\u5230\u4e16\u754c", "AU to world"));
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::SliderFloat("##auworld", &sc.linearAUtoWorld, 1.0f, 40.0f, "%.1f");
    }
    ImGui::TextDisabled("%s", UI(ps, "\u884c\u661f\u5927\u5c0f", "Planet size"));
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::SliderFloat("##planet_size", &sc.sizeScale, 0.05f, 2.2f, "%.2f");
    // Background is now a real starfield; color control removed.
}

void DrawSelectedBodyInfo(Scene& scene, PanelState& ps, gx::OrbitCamera& cam) {
    // UI section.
    const auto& bodies = scene.bodies();
    const auto& states = scene.states();
    if (ps.selectedBody >= 0 && ps.selectedBody < (int)bodies.size()) {
        ImGui::Spacing();
        SectionHeader(ps, "\u9009\u4e2d\u5929\u4f53", "Selected body");
        const BodyInfo&  b = bodies[ps.selectedBody];
        const BodyState& s = states[ps.selectedBody];
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 dot = ImGui::GetCursorScreenPos();
        dl->AddCircleFilled(ImVec2(dot.x + S(7.0f), dot.y + S(8.0f)), S(5.0f),
                            IM_COL32((int)(b.color[0] * 255), (int)(b.color[1] * 255),
                                     (int)(b.color[2] * 255), 255), 18);
        ImGui::Indent(S(18.0f));
        ImGui::TextColored({b.color[0], b.color[1], b.color[2], 1.0f},
                           "%s", BodyLabel(ps, b));
        ImGui::Unindent(S(18.0f));
        char row[64];
        std::snprintf(row, sizeof(row), "%.4f AU", s.R);
        InfoRow(ps, "\u65e5\u5fc3\u8ddd", "Heliocentric", row);
        std::snprintf(row, sizeof(row), "%.4f AU", s.geoDistAU);
        InfoRow(ps, "\u5730\u5fc3\u8ddd", "Geocentric", row);
        std::snprintf(row, sizeof(row), "%.2f deg", s.L);
        InfoRow(ps, "\u9ec4\u7ecf", "Longitude", row);
        std::snprintf(row, sizeof(row), "%.3f deg", s.B);
        InfoRow(ps, "\u9ec4\u7eac", "Latitude", row);
        if (!b.isSun) {
            std::snprintf(row, sizeof(row), "%.4f deg/day", s.speedDegPerDay);
            InfoRow(ps, "\u89d2\u901f\u5ea6", "Angular speed", row);
        }
        if (ImGui::SmallButton(UI(ps, "\u884c\u661f\u661f\u5386", "Ephemeris"))) {
            OpenSelectedEphemeris(ps, scene);
        }
        ImGui::SameLine();
        if (ImGui::SmallButton(UI(ps, "\u805a\u7126", "Focus"))) {
            cam.focusOn(s.world, std::max(s.displayRadius, b.isSun ? 1.35f : 0.22f));
        }
    }
}

void DrawSidebar(Scene& scene, RenderOptions& ropt, PanelState& ps, gx::OrbitCamera& cam) {
    ImGuiIO& io = ImGui::GetIO();
    normalizePanelWidths(ps, io.DisplaySize.x);
    float menuH = ImGui::GetFrameHeight();
    ImGui::SetNextWindowPos(ImVec2(0, menuH), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(leftPanelWidth(ps), io.DisplaySize.y - menuH), ImGuiCond_Always);

    ImGui::Begin(UI(ps, "\u63a7\u5236\u53f0##sidebar", "Controls##sidebar"), nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
                 ImGuiWindowFlags_NoResize   | ImGuiWindowFlags_NoMove);

    if (PanelTopCollapseButton("##left_collapse", UI(ps, "\u63a7\u5236", "Controls"), ps.leftCollapsed, true))
        ps.leftCollapsed = !ps.leftCollapsed;
    if (ps.leftCollapsed) {
        ImGui::End();
        return;
    }

    // UI section.
    DrawClockCard(scene, ps);
    DrawTimeControls(scene, ps);
    DrawJumpDate(scene, ps);
    DrawDisplaySettings(scene, ropt, ps);
    DrawSelectedBodyInfo(scene, ps, cam);

    ImGui::End();
}
    // UI section.
// ---------------------------------------------------------------------------
//  Selected-body card
// ---------------------------------------------------------------------------
// A translucent slab beside the body rather than a real ImGui window: it has
// to sit over the 3-D view without stealing the drag that orbits the camera,
// and a window with a title bar and a border would read as a panel that had
// escaped the sidebar.

// The engine body index behind a viewport selection. -1 Sun, 0 Earth,
// 1..8 planets, 10 Moon - the numbering upcomingAstroEvents() expects.
static int SelectionXt(const Scene& scene, const PanelState& ps) {
    if (ps.selectedMoon) return 10;
    const auto& bodies = scene.bodies();
    if (ps.selectedBody < 0 || ps.selectedBody >= (int)bodies.size()) return -999;
    return bodies[ps.selectedBody].xt;
}

// One search costs a few milliseconds, so it runs at most once per simulated
// day per body. Playing at the default five days a second that is a handful of
// searches a second, and scrubbing fast just means the list trails by a day.
static const std::vector<AstroEvent>& CachedBodyEvents(PanelState& ps, int xt,
                                                       double jdTd) {
    long long day = (long long)std::floor(jdTd);
    if (!ps.eventCacheValid || ps.eventCacheXt != xt || ps.eventCacheDay != day) {
        ps.eventCacheXt = xt;
        ps.eventCacheDay = day;
        ps.eventCacheValid = true;
        ps.eventCache = upcomingAstroEvents(xt, jdTd, 6);
    }
    return ps.eventCache;
}

// Rows the card shows for the selected body. Returned as label/value pairs so
// the drawing code can size the two columns independently.
struct CardRow { std::string label, value; };

// "2027-09-23 08:05" in the reader's own time zone. The year is on it because
// the list runs a year out and "01-03" a fortnight from New Year is a date the
// reader cannot place; the zone it is all in is named once in the card title
// rather than repeated on every row.
static std::string ShortEventTime(double jdTd, const PanelState& ps) {
    Date d = setFromJD(eclipseTdToUtcJD(jdTd) + ps.timezoneHours / 24.0 + 0.5 / 1440.0);
    char buf[40];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d", d.Y, d.M, d.D, d.h, d.m);
    return buf;
}

// "UTC+8" / "UTC-3:30". Whole hours lose the minutes, which is almost always.
static std::string ZoneLabel(const PanelState& ps) {
    float h = ps.timezoneHours;
    int sign = h < 0 ? -1 : 1;
    float a = std::fabs(h);
    int hh = (int)a;
    int mm = (int)std::lround((a - (float)hh) * 60.0f);
    if (mm == 60) { mm = 0; ++hh; }
    char buf[24];
    if (mm) std::snprintf(buf, sizeof(buf), "UTC%c%d:%02d", sign < 0 ? '-' : '+', hh, mm);
    else    std::snprintf(buf, sizeof(buf), "UTC%c%d", sign < 0 ? '-' : '+', hh);
    return buf;
}

// The phases of an eclipse, in the order they happen, with the moment of each.
// Contacts the engine leaves at zero did not occur - a partial eclipse has no
// second or third contact - and are dropped rather than printed as blanks.
struct EclipsePhase { const char* code; std::string name; double jdTd; };

static std::vector<EclipsePhase> EclipsePhaseRows(const EclipseEvent& e,
                                                  const PanelState& ps) {
    std::vector<EclipsePhase> out;
    auto add = [&](const char* code, const char* zh, const char* en, double td) {
        if (td == 0.0) return;
        out.push_back({code, UI(ps, zh, en), td});
    };
    if (e.kind == EclipseEvent::Solar) {
        // An annular eclipse never goes dark, so its second and third contacts
        // open and close the ring rather than totality, and are named for it.
        const bool annular = !e.type.empty() && e.type[0] == 'A';
        add("C1", "初亏", "First contact",  e.contactsTd[0]);
        add("C2", annular ? "环食始" : "食既",
                  annular ? "Annularity begins" : "Totality begins", e.contactsTd[3]);
        add("MAX", "食甚", "Greatest",      e.maximumTd);
        add("C3", annular ? "环食终" : "生光",
                  annular ? "Annularity ends" : "Totality ends", e.contactsTd[4]);
        add("C4", "复圆", "Fourth contact", e.contactsTd[2]);
    } else {
        add("P1", "半影食始", "Penumbral begins", e.contactsTd[3]);
        add("U1", "初亏", "Partial begins",       e.contactsTd[0]);
        add("U2", "食既", "Total begins",         e.contactsTd[5]);
        add("MAX", "食甚", "Greatest",            e.maximumTd);
        add("U3", "生光", "Total ends",           e.contactsTd[6]);
        add("U4", "复圆", "Partial ends",         e.contactsTd[2]);
        add("P4", "半影食终", "Penumbral ends",   e.contactsTd[4]);
    }
    return out;
}

// The handful of numbers that say what kind of eclipse this is, as label/value
// pairs so the card can lay them out in its own two columns.
static std::vector<CardRow> EclipseParamRows(const EclipseEvent& e,
                                             const PanelState& ps, double nowTd) {
    std::vector<CardRow> rows;
    char buf[64];
    rows.push_back({UI(ps, "类型", "Type"), EclipseTypeText(ps, e)});
    std::snprintf(buf, sizeof(buf), "%.3f", e.magnitude);
    rows.push_back({UI(ps, "食分", "Magnitude"), buf});
    if (e.kind == EclipseEvent::Solar) {
        if (e.durationDays > 0.0) {
            int sec = (int)std::lround(e.durationDays * 86400.0);
            std::snprintf(buf, sizeof(buf), "%d m %02d s", sec / 60, sec % 60);
            rows.push_back({UI(ps, "中心持续", "Central dur."), buf});
        }
        if (e.pathWidthKm > 0.0) {
            std::snprintf(buf, sizeof(buf), "%.0f km", e.pathWidthKm);
            rows.push_back({UI(ps, "带宽", "Path width"), buf});
        }
        if (e.hasCenter) {
            std::snprintf(buf, sizeof(buf), "%.2f°%c %.2f°%c",
                          std::fabs(e.centerLongitudeDeg),
                          e.centerLongitudeDeg < 0 ? 'W' : 'E',
                          std::fabs(e.centerLatitudeDeg),
                          e.centerLatitudeDeg < 0 ? 'S' : 'N');
            rows.push_back({UI(ps, "中心点", "Greatest at"), buf});
        }
    } else {
        // How much bigger than the Moon the shadow it is crossing is - the one
        // number that decides total against partial, and how long it takes.
        LunarShadowGeometry g = lunarShadowGeometry(nowTd);
        if (g.valid && g.moonRadius > 0.0) {
            std::snprintf(buf, sizeof(buf), "%.2f / %.2f",
                          g.umbraRadius / g.moonRadius,
                          g.penumbraRadius / g.moonRadius);
            rows.push_back({UI(ps, "本影/半影", "Umbra/pen."), buf});
        }
    }
    return rows;
}

static std::vector<CardRow> SelectionRows(const Scene& scene, const PanelState& ps) {
    std::vector<CardRow> rows;
    char buf[64];
    auto add = [&](const char* label, const char* fmt, double v, const char* unit) {
        std::snprintf(buf, sizeof(buf), fmt, v);
        rows.push_back({label, std::string(buf) + unit});
    };

    if (ps.selectedMoon) {
        const MoonData& m = scene.moon();
        add(UI(ps, "月相", "Phase"), "%.1f", m.illumination * 100.0, "%");
        rows.back().value = MoonPhaseLabel(ps, m.phaseName) + std::string("  ") + rows.back().value;
        add(UI(ps, "月龄", "Age"), "%.2f", m.ageDays,
            UI(ps, " 日", " d"));
        add(UI(ps, "日月角距", "Elongation"), "%.2f", m.elongationDeg, "°");
        return rows;
    }

    const auto& bodies = scene.bodies();
    const auto& states = scene.states();
    if (ps.selectedBody < 0 || ps.selectedBody >= (int)bodies.size()) return rows;
    const BodyInfo&  b = bodies[ps.selectedBody];
    const BodyState& s = states[ps.selectedBody];

    if (!b.isSun) {
        add(UI(ps, "日心黄经", "Helio. lon"), "%.3f", s.L, "°");
        add(UI(ps, "日心黄纬", "Helio. lat"), "%.3f", s.B, "°");
        add(UI(ps, "向径", "Radius vec."), "%.5f", s.R, " AU");
        add(UI(ps, "角速度", "Ang. speed"), "%.4f", s.speedDegPerDay,
            UI(ps, " °/日", " °/d"));
    }
    if (b.xt != 0)
        add(UI(ps, "地心距", "Geocentric"), "%.5f", s.geoDistAU, " AU");
    add(UI(ps, "半径", "Radius"), "%.0f", b.realRadiusKm, " km");
    return rows;
}

// Draws the card and returns true if the pointer is over it, so a tap there is
// not also read as a tap on the sky behind. Camera drags are deliberately NOT
// blocked: the card sits over the middle of the viewport and on a phone a
// finger sweeping the view around goes straight across it, which used to stall
// the rotation halfway. Nothing here takes a press, so a drag runs through the
// card untouched and only a press that stays put counts as a tap on it.
static bool DrawSelectedBodyCard(Scene& scene, PanelState& ps, ImVec2 anchor,
                                 ImVec2 origin, float vpW, float vpH,
                                 const EclipseEvent* eclipse,
                                 float parkX, float parkY,
                                 bool& jumpRequested, double& jumpToTd) {
    const int xt = SelectionXt(scene, ps);
    if (xt == -999) return false;

    const char* title = ps.selectedMoon
        ? UI(ps, "月球", "Moon")
        : BodyLabel(ps, scene.bodies()[ps.selectedBody]);
    std::vector<CardRow> rows = SelectionRows(scene, ps);
    const double nowTd = SceneUtcToTd(scene);

    // An eclipse takes over the lower half of the card from the general
    // almanac list, but only on the body it is happening to: the Moon's shadow
    // is Earth's business and Earth's shadow is the Moon's.
    const bool eclipseHere = eclipse &&
        (eclipse->kind == EclipseEvent::Solar ? (!ps.selectedMoon && xt == 0)
                                              : ps.selectedMoon);
    std::vector<EclipsePhase> phases;
    std::vector<CardRow>      eclipseRows;
    if (eclipseHere) {
        phases = EclipsePhaseRows(*eclipse, ps);
        eclipseRows = EclipseParamRows(*eclipse, ps, nowTd);
    }
    static const std::vector<AstroEvent> kNoEvents;
    const std::vector<AstroEvent>& events =
        eclipseHere ? kNoEvents : CachedBodyEvents(ps, xt, nowTd);

    // Two type sizes. The body of the card is a notch down from the interface
    // around it because it floats over the scene rather than sitting in a
    // panel, and the timestamps are a notch down again: they carry a year now,
    // and a full stamp set in the row type would set the card's whole width.
    ImFont* font = ImGui::GetFont();
    const float fMain = std::max(ImGui::GetFontSize() * 0.90f, S(10.0f));
    const float fTime = std::max(ImGui::GetFontSize() * 0.76f, S(9.0f));
    auto wMain = [&](const char* t) { return font->CalcTextSizeA(fMain, FLT_MAX, 0.0f, t).x; };
    auto wTime = [&](const char* t) { return font->CalcTextSizeA(fTime, FLT_MAX, 0.0f, t).x; };

    const float pad   = S(9.0f);
    const float lineH = std::floor(fMain * 1.42f);
    const float gap   = S(10.0f);
    const std::string zone = ZoneLabel(ps);
    const char* evTitle = eclipseHere
        ? (eclipse->kind == EclipseEvent::Solar ? UI(ps, "日食过程", "Solar eclipse")
                                                : UI(ps, "月食过程", "Lunar eclipse"))
        : UI(ps, "即将发生的天象", "Upcoming events");

    // Width: the widest of the title, every label/value pair, and every event
    // row. Measured up front so the slab never reflows as the numbers tick.
    float labelW = 0.0f, valueW = 0.0f;
    auto measurePairs = [&](const std::vector<CardRow>& rs) {
        for (const CardRow& r : rs) {
            labelW = std::max(labelW, wMain(r.label.c_str()));
            valueW = std::max(valueW, wMain(r.value.c_str()));
        }
    };
    measurePairs(rows);
    measurePairs(eclipseRows);

    float evW = wMain(evTitle) + gap + wTime(zone.c_str());
    float evTimeW = 0.0f, evNameW = 0.0f;
    std::vector<std::string> evTimes, evNames;
    if (eclipseHere) {
        for (const EclipsePhase& p : phases) {
            evNames.push_back(p.name);
            evTimes.push_back(ShortEventTime(p.jdTd, ps));
        }
    } else {
        for (const AstroEvent& e : events) {
            evTimes.push_back(ShortEventTime(e.jdTd, ps));
            std::string nm = astroEventName(e.kind, ps.useChinese);
            if (!e.detail.empty()) nm += "  " + e.detail;
            evNames.push_back(nm);
        }
    }
    for (const std::string& t : evTimes) evTimeW = std::max(evTimeW, wTime(t.c_str()));
    for (const std::string& n : evNames) evNameW = std::max(evNameW, wMain(n.c_str()));
    if (!evTimes.empty()) evW = std::max(evW, evNameW + gap + evTimeW);

    float bodyW = std::max(labelW + gap + valueW, evW);
    float cardW = std::max(bodyW, wMain(title) + gap + wTime(zone.c_str()) + S(26.0f))
                + pad * 2.0f;
    size_t pairRows = rows.size() + eclipseRows.size();
    float cardH = pad * 2.0f + lineH * (1.0f + (float)pairRows);
    if (!evTimes.empty()) cardH += S(6.0f) + lineH * (1.0f + (float)evTimes.size());
    if (eclipseHere && !eclipseRows.empty()) cardH += S(4.0f);

    // Normally the card hangs beside the body it belongs to. In the study view
    // that body is one end of a diagram that fills the frame, so the card is
    // parked in a top corner instead - the caller says which, picking the side
    // the other body is not on - and the leader line keeps them connected.
    float x, y;
    if (parkX >= 0.0f) {
        x = std::clamp(parkX < 0.5f ? S(10.0f) : vpW - cardW - S(10.0f),
                       S(8.0f), std::max(S(8.0f), vpW - cardW - S(8.0f)));
        y = std::clamp(parkY, S(8.0f), std::max(S(8.0f), vpH - cardH - S(8.0f)));
    } else {
        // Prefer the right of the body, flip when that would run off the edge,
        // then clamp so the whole slab stays inside the viewport either way.
        x = anchor.x + S(26.0f);
        if (x + cardW > vpW - S(8.0f)) x = anchor.x - S(26.0f) - cardW;
        x = std::clamp(x, S(8.0f), std::max(S(8.0f), vpW - cardW - S(8.0f)));
        y = std::clamp(anchor.y - cardH * 0.35f, S(8.0f),
                       std::max(S(8.0f), vpH - cardH - S(8.0f)));
    }

    ImVec2 p0{origin.x + x, origin.y + y};
    ImVec2 p1{p0.x + cardW, p0.y + cardH};
    ImDrawList* dl = ImGui::GetWindowDrawList();

    // Leader line back to the body, so a card pushed away from the edge still
    // reads as belonging to what was clicked.
    ImVec2 a{origin.x + anchor.x, origin.y + anchor.y};
    float lx = (a.x < p0.x) ? p0.x : p1.x;
    dl->AddLine(a, ImVec2(lx, std::clamp(a.y, p0.y + S(6.0f), p1.y - S(6.0f))),
                IM_COL32(150, 195, 250, 110), 1.0f);

    dl->AddRectFilled(p0, p1, IM_COL32(10, 17, 30, 168), S(7.0f));
    dl->AddRect(p0, p1, IM_COL32(104, 152, 208, 105), S(7.0f), 0, 1.0f);

    // Hit testing by hand rather than with widgets, so that nothing in here
    // ever becomes ImGui's active item and swallows a camera drag.
    const ImVec2 mp = ImGui::GetIO().MousePos;
    auto inRect = [&](const ImVec2& q0, const ImVec2& q1) {
        return mp.x >= q0.x && mp.x <= q1.x && mp.y >= q0.y && mp.y <= q1.y;
    };
    ImVec2 dd = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left);
    const float slop = S(6.0f);
    const bool tapNow = ImGui::IsMouseReleased(ImGuiMouseButton_Left) &&
                        (dd.x * dd.x + dd.y * dd.y) < slop * slop;
    auto tapped = [&](const ImVec2& q0, const ImVec2& q1) {
        return tapNow && inRect(q0, q1);
    };

    float ty = p0.y + pad;
    dl->AddText(font, fMain, ImVec2(p0.x + pad, ty), IM_COL32(255, 218, 120, 245), title);

    // Every time on this card is in one zone, so it is named here instead of
    // on each row. Sits left of the close mark.
    const float cs = lineH;
    ImVec2 cp{p1.x - pad - cs, p0.y + pad - S(1.0f)};
    dl->AddText(font, fTime,
                ImVec2(cp.x - S(5.0f) - wTime(zone.c_str()), ty + (fMain - fTime) * 0.6f),
                IM_COL32(132, 164, 200, 210), zone.c_str());

    bool closeHover = inRect(cp, ImVec2(cp.x + cs, cp.y + cs));
    ImU32 xcol = closeHover ? IM_COL32(255, 190, 180, 255) : IM_COL32(150, 175, 205, 190);
    float xin = cs * 0.30f;
    dl->AddLine(ImVec2(cp.x + xin, cp.y + xin), ImVec2(cp.x + cs - xin, cp.y + cs - xin), xcol, 1.6f);
    dl->AddLine(ImVec2(cp.x + cs - xin, cp.y + xin), ImVec2(cp.x + xin, cp.y + cs - xin), xcol, 1.6f);
    if (tapped(cp, ImVec2(cp.x + cs, cp.y + cs))) {
        ps.selectedMoon = false;
        ps.selectedBody = -1;
    }
    ty += lineH;

    auto drawPairs = [&](const std::vector<CardRow>& rs) {
        for (const CardRow& r : rs) {
            dl->AddText(font, fMain, ImVec2(p0.x + pad, ty),
                        IM_COL32(140, 172, 208, 225), r.label.c_str());
            dl->AddText(font, fMain, ImVec2(p0.x + pad + labelW + gap, ty),
                        IM_COL32(225, 236, 250, 245), r.value.c_str());
            ty += lineH;
        }
    };
    drawPairs(rows);

    if (!evTimes.empty() || !eclipseRows.empty()) {
        ty += S(6.0f);
        dl->AddLine(ImVec2(p0.x + pad, ty - S(3.0f)), ImVec2(p1.x - pad, ty - S(3.0f)),
                    IM_COL32(90, 130, 180, 80), 1.0f);
        dl->AddText(font, fMain, ImVec2(p0.x + pad, ty),
                    eclipseHere ? IM_COL32(255, 196, 128, 240) : IM_COL32(126, 205, 172, 235),
                    evTitle);
        ty += lineH;
        if (!eclipseRows.empty()) { drawPairs(eclipseRows); ty += S(4.0f); }

        // Which phase the clock is in now: the row for the contact just passed
        // is marked, so the list reads as a progress bar rather than a table.
        int current = -1;
        for (size_t i = 0; i < phases.size(); ++i)
            if (nowTd >= phases[i].jdTd) current = (int)i;

        for (size_t i = 0; i < evTimes.size(); ++i) {
            // Each row jumps the clock to its moment; that is the whole point
            // of listing them next to the body they happen to.
            ImVec2 rp{p0.x + pad, ty};
            ImVec2 r1{p1.x - pad, ty + lineH};
            bool hov = inRect(ImVec2(rp.x - S(3.0f), rp.y), r1);
            bool isNow = eclipseHere && (int)i == current;
            if (hov || isNow)
                dl->AddRectFilled(ImVec2(rp.x - S(3.0f), rp.y),
                                  ImVec2(p1.x - pad + S(3.0f), rp.y + lineH),
                                  hov ? IM_COL32(70, 110, 170, 70)
                                      : IM_COL32(110, 80, 40, 95), S(3.0f));
            double jd = eclipseHere ? phases[i].jdTd : events[i].jdTd;
            if (tapped(ImVec2(rp.x - S(3.0f), rp.y), r1)) {
                jumpRequested = true; jumpToTd = jd;
            }
            dl->AddText(font, fMain, rp,
                        hov ? IM_COL32(255, 236, 190, 255)
                            : (isNow ? IM_COL32(255, 214, 150, 250)
                                     : IM_COL32(226, 236, 248, 240)),
                        evNames[i].c_str());
            dl->AddText(font, fTime,
                        ImVec2(p1.x - pad - wTime(evTimes[i].c_str()),
                               ty + (fMain - fTime) * 0.6f),
                        IM_COL32(168, 196, 228, 235), evTimes[i].c_str());
            ty += lineH;
        }
    }

    ImGui::Dummy(ImVec2(0.0f, 0.0f));
    return inRect(p0, p1);
}

// Simulation time and transport, over the top-left of the viewport. Both ways
// of watching an eclipse need it, so it is its own function rather than a tail
// of the orbital path.
static std::string ViewportClockText(const Scene& scene, const PanelState& ps) {
    const SimClock& clk = scene.clock();
    Date d = localDateFromUtcJD(clk.jd, ps.timezoneHours);
    char buf[80];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d  %s",
                  d.Y, d.M, d.D, d.h, d.m,
                  clk.playing ? UI(ps, "播放中", "playing")
                              : UI(ps, "暂停", "paused"));
    return buf;
}

// The whole top-left cluster, badge plus the transport buttons touch builds put
// beside it. The eclipse pills have to keep clear of this, and on a phone that
// is most of the width.
static ImVec2 ViewportClockBadgeSize(const Scene& scene, const PanelState& ps) {
    ImVec2 ts = ImGui::CalcTextSize(ViewportClockText(scene, ps).c_str());
    ImVec2 pad{S(10.0f), S(7.0f)};
    float w = ts.x + pad.x * 2.0f;
    float h = ts.y + pad.y * 2.0f;
    if (g_touchMode) w += S(8.0f) + h + S(6.0f) + h;  // play and today
    return ImVec2(w, h);
}

static void DrawViewportClockBadge(Scene& scene, PanelState& ps, ImVec2 origin) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    SimClock& clk = scene.clock();
    std::string text = ViewportClockText(scene, ps);
    const char* buf = text.c_str();
    ImVec2 ts = ImGui::CalcTextSize(buf);
    ImVec2 pos{origin.x + S(10.0f), origin.y + S(9.0f)};
    ImVec2 pad{S(10.0f), S(7.0f)};
    dl->AddRectFilled(pos, ImVec2(pos.x + ts.x + pad.x * 2.0f,
                                  pos.y + ts.y + pad.y * 2.0f),
                      IM_COL32(8, 15, 28, 176), 5.0f);
    dl->AddRect(pos, ImVec2(pos.x + ts.x + pad.x * 2.0f,
                            pos.y + ts.y + pad.y * 2.0f),
                IM_COL32(92, 137, 190, 95), 5.0f, 0, 1.0f);
    dl->AddText(ImVec2(pos.x + pad.x, pos.y + pad.y),
                IM_COL32(188, 224, 255, 238), buf);

    if (g_touchMode) {
        const float bw = ts.x + pad.x * 2.0f;
        const float sz = ts.y + pad.y * 2.0f;
        const float bx = pos.x + bw + S(8.0f);
        const ImVec2 keep = ImGui::GetCursorScreenPos();
        const ImU32 icol = IM_COL32(180, 210, 255, 235);

        ImGui::SetCursorScreenPos(ImVec2(bx, pos.y));
        if (clk.playing) {
            if (IconButton("##vp_pause", sz, icol,
                    [](ImDrawList* d2, ImVec2 p2, float s2, ImU32 c2) {
                        DrawIconPause(d2, p2, s2, c2); }))
                clk.playing = false;
        } else {
            if (IconButton("##vp_play", sz, icol,
                    [](ImDrawList* d2, ImVec2 p2, float s2, ImU32 c2) {
                        DrawIconPlay(d2, p2, s2, c2); }))
                clk.playing = true;
        }

        ImGui::SetCursorScreenPos(ImVec2(bx + sz + S(6.0f), pos.y));
        if (IconButton("##vp_today", sz, icol,
                [](ImDrawList* d2, ImVec2 p2, float s2, ImU32 c2) {
                    DrawIconHome(d2, p2, s2, c2); })) {
            clk.jd = nowJD();
            clk.playing = false;
        }

        // Image() leaves the cursor one ItemSpacing below CursorMaxPos, so
        // simply restoring it counts as "moved the cursor past the content
        // bounds and never submitted anything" - which ImGui reports at
        // End(). A zero-size item settles the bookkeeping.
        ImGui::SetCursorScreenPos(keep);
        ImGui::Dummy(ImVec2(0.0f, 0.0f));
    }
}

// ---------------------------------------------------------------------------
//  Eclipse controls over the viewport
// ---------------------------------------------------------------------------
// A row of pills at the top right: which way to watch the selected eclipse,
// where to stand, and how to leave. They live in the view because they are
// about the view - choosing an eclipse already happened on the eclipse page,
// and going back there to change the camera would make two places out of one.
//
// Laid out right-aligned and wrapped rather than in one line: on a phone the
// clock badge already owns most of the top, and a single row simply ran under
// it. Rows that cannot clear the badge start below it.
namespace {
struct VpPill {
    const char* label;
    bool  active;
    int   action;
    float w = 0.0f, x = 0.0f, y = 0.0f;
};
enum {
    VP_ORBITAL = 0, VP_GROUND, VP_GEOMETRY, VP_LOCAL, VP_BEST,
    VP_PLAY, VP_FROM_C1, VP_EXIT
};
} // namespace

static float LayoutEclipsePills(const Scene& scene, const PanelState& ps,
                                const EclipseEvent* e, ImVec2 origin, float w,
                                ImVec2 badgeSize, std::vector<VpPill>& out) {
    out.clear();
    if (!e) return origin.y;
    const bool ground = (ps.vpEclipseView == PanelState::EV_Ground);
    out.push_back({UI(ps, "轨道视角", "Orbital"), !ground, VP_ORBITAL});
    out.push_back({UI(ps, "地面视角", "Ground"),   ground, VP_GROUND});
    if (!ground) {
        out.push_back({UI(ps, "光影几何", "Shadow geometry"),
                       ps.vpEclipseGeometry, VP_GEOMETRY});
    } else {
        // Two seats, not a toggle with a hidden other half: "somewhere it is
        // actually visible" and "where I live" are both reasonable questions
        // and neither is the obvious default.
        out.push_back({UI(ps, "本地观测", "Local"), !ps.groundBestSeat, VP_LOCAL});
        out.push_back({UI(ps, "最佳观测点", "Best seat"),
                       ps.groundBestSeat, VP_BEST});
    }
    out.push_back({scene.clock().playing ? UI(ps, "暂停", "Pause")
                                         : UI(ps, "播放", "Play"),
                   scene.clock().playing, VP_PLAY});
    out.push_back({UI(ps, "从食始", "From C1"), false, VP_FROM_C1});
    out.push_back({UI(ps, "退出演示", "Exit"), false, VP_EXIT});

    const float padX = S(10.0f), gap = S(6.0f), margin = S(10.0f);
    const float right = origin.x + w - margin;
    const float rowH = ImGui::GetTextLineHeight() + S(12.0f);
    for (VpPill& p : out) p.w = ImGui::CalcTextSize(p.label).x + padX * 2.0f;

    // Greedy packing, then each finished row is pushed right. The first row has
    // only what is left beside the badge; later rows have the full width.
    float firstRowW = right - (origin.x + margin + badgeSize.x + S(8.0f));
    float fullRowW  = w - margin * 2.0f;
    size_t i = 0;
    float y = origin.y + S(9.0f);
    bool firstRow = true;
    while (i < out.size()) {
        float limit = firstRow ? firstRowW : fullRowW;
        size_t first = i;
        float used = 0.0f;
        while (i < out.size()) {
            float next = used + (i > first ? gap : 0.0f) + out[i].w;
            if (i > first && next > limit) break;
            used = next;
            ++i;
        }
        if (i == first) {           // nothing fits beside the badge
            firstRow = false;
            y = origin.y + S(9.0f) + badgeSize.y + S(8.0f);
            continue;
        }
        float x = right - used;
        for (size_t k = first; k < i; ++k) {
            out[k].x = x;
            out[k].y = y;
            x += out[k].w + gap;
        }
        if (firstRow) {
            firstRow = false;
            y = std::max(y + rowH, origin.y + S(9.0f) + badgeSize.y) + S(8.0f);
        } else {
            y += rowH + S(6.0f);
        }
    }
    return y;
}

static void DrawEclipsePills(Scene& scene, PanelState& ps, const EclipseEvent* e,
                             const std::vector<VpPill>& pills) {
    if (!e || pills.empty()) return;
    const ImVec2 keep = ImGui::GetCursorScreenPos();
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(S(10.0f), S(6.0f)));
    for (size_t i = 0; i < pills.size(); ++i) {
        const VpPill& p = pills[i];
        ImGui::SetCursorScreenPos(ImVec2(p.x, p.y));
        if (p.active) {
            ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.20f, 0.36f, 0.58f, 0.94f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.25f, 0.44f, 0.68f, 0.96f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.30f, 0.50f, 0.76f, 1.00f));
        } else {
            ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.06f, 0.09f, 0.15f, 0.72f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.13f, 0.20f, 0.31f, 0.86f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.17f, 0.27f, 0.42f, 0.92f));
        }
        char id[64];
        std::snprintf(id, sizeof(id), "%s##vp_ecl_%d", p.label, (int)i);
        if (ImGui::Button(id, ImVec2(p.w, 0.0f))) {
            switch (p.action) {
                case VP_ORBITAL:  ps.vpEclipseView = PanelState::EV_Orbital; break;
                case VP_GROUND:   ps.vpEclipseView = PanelState::EV_Ground;
                                  ps.groundLookYawDeg = ps.groundLookPitchDeg = 0.0f; break;
                case VP_GEOMETRY: ps.vpEclipseGeometry = !ps.vpEclipseGeometry; break;
                case VP_LOCAL:    ps.groundBestSeat = false; break;
                case VP_BEST:     ps.groundBestSeat = true;  break;
                case VP_PLAY:     scene.clock().playing = !scene.clock().playing; break;
                case VP_FROM_C1:  StartEclipseDemo(scene, ps, *e); break;
                case VP_EXIT:
                    // Out of the demonstration entirely: the clock gets its own
                    // rate back - leaving it on the eclipse's crawl, or landing
                    // back on five days a second, is what made the globe strobe
                    // - and with nothing selected the viewport is the ordinary
                    // solar system again. The search list is untouched, so one
                    // tap on the eclipse page brings all of this back.
                    StopEclipseDemo(scene, ps);
                    ps.selectedEclipse = -1;
                    ps.eclipsePath.clear();
                    ps.eclipseLimits = EclipseLimits{};
                    ps.vpEclipseView = PanelState::EV_Orbital;
                    break;
            }
        }
        ImGui::PopStyleColor(3);
    }
    ImGui::PopStyleVar();
    // Image() left the cursor past the content bounds; restoring it and
    // submitting nothing is what ImGui complains about at End(), so a zero-size
    // item settles the bookkeeping.
    ImGui::SetCursorScreenPos(keep);
    ImGui::Dummy(ImVec2(0.0f, 0.0f));
}

// Fills the current window's content region with the 3-D scene plus its
// overlays (labels, clock badge, build badge). Shared by the desktop viewport
// panel and the mobile full-bleed solar-system page.
void DrawViewportContent(Renderer& renderer, Scene& scene, gx::OrbitCamera& cam,
                         RenderOptions& ropt, PanelState& ps) {
    ImGuiIO& io = ImGui::GetIO();
    ImVec2 avail = ImGui::GetContentRegionAvail();
    int w = (int)avail.x, h = (int)avail.y;
    if (w < 16) w = 16; if (h < 16) h = 16;
    renderer.resize(w, h);

    // Eclipse geometry rides on top of the ordinary orbital view rather than
    // replacing it: the eclipse pages and the solar system are the same scene,
    // so choosing an eclipse should not mean leaving.
    const double nowTd = SceneUtcToTd(scene);
    EclipseSceneOverlay eclipseOverlay;
    const EclipseEvent* selected = nullptr;
    if (ps.selectedEclipse >= 0 && ps.selectedEclipse < (int)ps.eclipseEvents.size())
        selected = &ps.eclipseEvents[ps.selectedEclipse];
    const bool groundMode = selected && ps.vpEclipseView == PanelState::EV_Ground;
    // The shadow-geometry pill is what turns the ordinary orbital view into a
    // study of one eclipse: the rest of the solar system leaves the frame, the
    // Moon goes back onto the true light axis, and the camera is put where the
    // geometry can actually be read. Everything is given back when it is off.
    const bool focusMode = selected && ps.vpEclipseGeometry && !groundMode;
    {
        Scene::EclipseFocus f;
        f.on = focusMode;
        scene.setEclipseFocus(f);
    }
    if (focusMode && !ps.vpEclipseFocusPlaced) {
        PlaceEclipseFocusCamera(scene, cam, (float)w / (float)h);
        // Select the body the eclipse is happening to, so its card comes up
        // with the contact times on it: opening a view of one eclipse and
        // having to hunt for its timings is a step nobody wants.
        if (selected->kind == EclipseEvent::Solar) {
            ps.selectedMoon = false;
            for (size_t i = 0; i < scene.bodies().size(); ++i)
                if (scene.bodies()[i].xt == 0) { ps.selectedBody = (int)i; break; }
        } else {
            ps.selectedMoon = true;
        }
        ps.vpEclipseFocusPlaced = true;
    } else if (!focusMode) {
        ps.vpEclipseFocusPlaced = false;
    }
    if (selected && ps.vpEclipseGeometry && !groundMode) {
        eclipseOverlay.active = true;
        eclipseOverlay.solar  = (selected->kind == EclipseEvent::Solar);
        eclipseOverlay.jdTd   = nowTd;
        eclipseOverlay.path   = &ps.eclipsePath;
        eclipseOverlay.limits = ps.eclipseShowLimits ? &ps.eclipseLimits : nullptr;
        eclipseOverlay.focus  = focusMode;
        if (!eclipseOverlay.solar) {
            // How much of the Moon's disc Earth's umbra covers, as a fraction
            // of its diameter - the same magnitude the eclipse page prints.
            LunarShadowGeometry g = lunarShadowGeometry(nowTd);
            if (g.valid && g.moonRadius > 0.0) {
                double sep = std::sqrt(g.x * g.x + g.y * g.y);
                double mag = (g.umbraRadius + g.moonRadius - sep) / (2.0 * g.moonRadius);
                eclipseOverlay.lunarShade = (float)std::clamp(mag, 0.0, 1.0);
                // All three are angular radii from Earth, so these ratios are
                // the shadow's true size at the Moon measured in Moon radii.
                eclipseOverlay.lunarUmbraMoonR    = (float)(g.umbraRadius / g.moonRadius);
                eclipseOverlay.lunarPenumbraMoonR = (float)(g.penumbraRadius / g.moonRadius);
            }
        }
    }
    renderer.render(scene, cam, ropt, eclipseOverlay.active ? &eclipseOverlay : nullptr);

    ImVec2 origin = ImGui::GetCursorScreenPos();
    ImGui::Image((ImTextureID)(intptr_t)renderer.colorTexture(), avail,
                 ImVec2(0,1), ImVec2(1,0));
    // Read while the image is still the last submitted item: everything below
    // adds widgets on top of it, and IsItemHovered() always means "the last
    // one submitted".
    const bool viewportHovered = ImGui::IsItemHovered();

    // A run started from either view has to end when the eclipse does, whichever
    // page the tools panel happens to be showing.
    UpdateEclipseDemo(scene, ps);

    // Standing on the ground is the same scene from a different distance, so it
    // is drawn over the viewport rather than opened as a page. Everything the
    // orbital view puts on top - the body card, the labels, the picking - would
    // be furniture in a sky, so those are skipped while it is up.
    if (groundMode) {
        // The pills are laid out before anything is drawn: the sky has to know
        // how much of its top the chrome has taken, or the readout card ends up
        // under it on a phone.
        ImVec2 badgeSize = ViewportClockBadgeSize(scene, ps);
        std::vector<VpPill> pills;
        float bottom = LayoutEclipsePills(scene, ps, selected, origin, (float)w,
                                          badgeSize, pills);
        float topInset = std::max(bottom - origin.y, badgeSize.y + S(9.0f)) + S(6.0f);
        DrawGroundEclipseView(scene, ps, *selected, origin, (float)w, (float)h,
                              viewportHovered, topInset);
        DrawViewportClockBadge(scene, ps, origin);
        DrawEclipsePills(scene, ps, selected, pills);
        return;
    }

    // The card goes in straight after the image and before any input is read.
    // It draws over the scene either way, but its buttons only get the click
    // ahead of the camera if the camera is told to stand down while the
    // pointer is inside it - IsItemHovered() on the image above cannot know
    // about widgets that have not been submitted yet.
    bool cardHovered = false;
    bool jumpRequested = false;
    double jumpToTd = 0.0;
    if (ps.selectedMoon || (ps.selectedBody >= 0 &&
                            ps.selectedBody < (int)scene.states().size())) {
        const gx::Vec3& anchorWorld = ps.selectedMoon
            ? scene.moon().worldPos : scene.states()[ps.selectedBody].world;
        bool onScreen = ps.selectedMoon ? scene.moon().valid : true;
        float ax, ay;
        if (onScreen && gx::projectToScreen(renderer.viewProj(), anchorWorld,
                                            (float)w, (float)h, ax, ay)) {
            // In the study view the card is parked in the top corner opposite
            // the far body, so it never lands on the axis being studied.
            float parkX = -1.0f, parkY = 0.0f;
            if (focusMode) {
                gx::Vec3 other = scene.moon().worldPos;
                if (ps.selectedMoon) {           // the other body is the Earth
                    for (size_t i = 0; i < scene.bodies().size(); ++i)
                        if (scene.bodies()[i].xt == 0) { other = scene.states()[i].world; break; }
                }
                float ox, oy;
                parkX = (gx::projectToScreen(renderer.viewProj(), other,
                                             (float)w, (float)h, ox, oy) && ox > (float)w * 0.5f)
                      ? 0.0f : 1.0f;
                // Clear of the clock badge and the row of pills, which own the
                // top of the viewport in this mode.
                parkY = ViewportClockBadgeSize(scene, ps).y + S(46.0f);
            }
            cardHovered = DrawSelectedBodyCard(scene, ps, ImVec2(ax, ay), origin,
                                               (float)w, (float)h, selected,
                                               parkX, parkY,
                                               jumpRequested, jumpToTd);
        }
    }
    if (jumpRequested) {
        scene.clock().jd = eclipseTdToUtcJD(jumpToTd);
        scene.clock().playing = false;
    }

    bool isDragging = ImGui::IsMouseDragging(ImGuiMouseButton_Left) ||
                      ImGui::IsMouseDragging(ImGuiMouseButton_Right);
    // Picking result: an index into scene.bodies(), or kPickMoon. The Moon is
    // not in that list but is the one body most worth asking about, since the
    // eclipses live there.
    const int kPickMoon = -2;
    auto hitBodyAtMouse = [&]() -> int {
        float mx = io.MousePos.x - origin.x;
        float my = io.MousePos.y - origin.y;
        const auto& states = scene.states();
        float minD = 46.0f;
        int hit = -1;
        auto consider = [&](const gx::Vec3& world, float displayRadius, int id) {
            float px, py;
            if (!gx::projectToScreen(renderer.viewProj(), world,
                                     (float)w, (float)h, px, py)) return;
            float pickR = std::clamp(displayRadius * 18.0f / std::max(cam.distance, 0.25f),
                                     0.0f, 26.0f);
            float d = std::sqrt((mx - px) * (mx - px) + (my - py) * (my - py)) - pickR;
            if (d < minD) { minD = d; hit = id; }
        };
        for (size_t i = 0; i < states.size(); ++i)
            consider(states[i].world, states[i].displayRadius, (int)i);
        // Last, so that when the Moon overlaps Earth on screen a tie goes to
        // the Moon - Earth stays reachable by its much larger disc.
        if (ropt.showMoon && scene.moon().valid)
            consider(scene.moon().worldPos, scene.moon().displayRadius, kPickMoon);
        return hit;
    };
    // Focus and select whatever picking returned.
    auto selectHit = [&](int hit, bool animate) {
        if (hit == kPickMoon) {
            ps.selectedMoon = true;
            const MoonData& m = scene.moon();
            cam.focusOn(m.worldPos, std::max(m.displayRadius, 0.12f), animate);
            return;
        }
        if (hit < 0) return;
        ps.selectedMoon = false;
        ps.selectedBody = hit;
        const BodyState& s = scene.states()[hit];
        const BodyInfo&  b = scene.bodies()[hit];
        cam.focusOn(s.world, std::max(s.displayRadius, b.isSun ? 1.35f : 0.22f), animate);
    };

    // The camera takes every drag, including one that crosses the card: the
    // card claims taps, not gestures. Only the click-to-select below has to
    // stand down over it, or a tap meant for a contact row would also pick
    // whatever body happens to be behind the card.
    if (viewportHovered) {
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Left))
            cam.rotate(io.MouseDelta.x * 0.01f, io.MouseDelta.y * 0.01f);
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Right))
            cam.pan(io.MouseDelta.x, io.MouseDelta.y);
        if (io.MouseWheel != 0.0f) {
            // Scale by how much was actually scrolled, not just its sign: a
            // trackpad sends many fractional deltas, and several notches can
            // land in one frame when the frame rate dips. One notch is 1.0,
            // so a mouse still steps by the same 0.9 it always did.
            cam.zoom(std::pow(0.9f, std::clamp(io.MouseWheel, -8.0f, 8.0f)));
        }

        // Click-to-select: fire on mouse release if total drag distance < 5 px
        if (!isDragging && !cardHovered && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
            ImVec2 dd = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left);
            if (dd.x*dd.x + dd.y*dd.y < 25.0f) {
                int hit = hitBodyAtMouse();
                if (hit != -1) {
                    selectHit(hit, false);
                    if (hit != kPickMoon) ps.activeTab = 0; // jump to params tab
                }
            }
        }

        // Double-click: focus camera on selected body
        if (!cardHovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            int hit = hitBodyAtMouse();
            if (hit != -1) {
                selectHit(hit, true);
            } else if (ps.selectedMoon && scene.moon().valid) {
                const MoonData& m = scene.moon();
                cam.focusOn(m.worldPos, std::max(m.displayRadius, 0.12f), true);
            } else if (ps.selectedBody >= 0 && ps.selectedBody < (int)scene.states().size()) {
                const BodyState& s = scene.states()[ps.selectedBody];
                const BodyInfo& b = scene.bodies()[ps.selectedBody];
                cam.focusOn(s.world, std::max(s.displayRadius, b.isSun ? 1.35f : 0.22f), true);
            }
        }
    }

    // Body labels and viewport overlays.
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const auto& states = scene.states();
    const auto& bodies = scene.bodies();

    if (ropt.showLabels) {
        for (size_t i = 0; i < states.size(); ++i) {
            if (focusMode && bodies[i].xt != 0) continue;  // Earth only
            float sx2, sy2;
            if (!gx::projectToScreen(renderer.viewProj(), states[i].world,
                                     (float)w, (float)h, sx2, sy2)) continue;
            bool selected = (ps.selectedBody == (int)i);
            ImU32 col = selected
                ? IM_COL32(255, 220, 80, 255)
                : IM_COL32((int)(bodies[i].color[0]*255),
                           (int)(bodies[i].color[1]*255),
                           (int)(bodies[i].color[2]*255), 210);
            const char* nm = BodyLabel(ps, bodies[i]);
            ImVec2 tp{origin.x + sx2 + 7, origin.y + sy2 - 7};
            // Drop-shadow for readability
            dl->AddText(ImVec2(tp.x+1, tp.y+1), IM_COL32(0,0,0,160), nm);
            dl->AddText(tp, col, nm);
        }
        // Moon label
        if (ropt.showMoon && scene.moon().valid) {
            float mx2, my2;
            if (gx::projectToScreen(renderer.viewProj(), scene.moon().worldPos,
                                    (float)w, (float)h, mx2, my2)) {
                dl->AddText(ImVec2(origin.x+mx2+6, origin.y+my2-5),
                            IM_COL32(0,0,0,160), UI(ps, "\u6708\u7403", "Moon"));
                dl->AddText(ImVec2(origin.x+mx2+5, origin.y+my2-6),
                            IM_COL32(200,200,220,220), UI(ps, "\u6708\u7403", "Moon"));
            }
        }
    }

    DrawViewportClockBadge(scene, ps, origin);
    {
        std::vector<VpPill> pills;
        LayoutEclipsePills(scene, ps, selected, origin, (float)w,
                           ViewportClockBadgeSize(scene, ps), pills);
        DrawEclipsePills(scene, ps, selected, pills);
    }
    // UI section.
    {
        char buf[80];
        std::snprintf(buf, sizeof(buf), "build:obj-mesh  mesh:%d  tex:%d",
                      renderer.loadedMeshes(), renderer.loadedTextures());
        ImVec2 ts = ImGui::CalcTextSize(buf);
        ImVec2 pad{S(8.0f), S(5.0f)};
        ImVec2 pos{origin.x + (float)w - ts.x - pad.x * 2.0f - S(10.0f),
                   origin.y + (float)h - ts.y - pad.y * 2.0f - S(9.0f)};
        dl->AddRectFilled(pos, ImVec2(pos.x + ts.x + pad.x * 2.0f,
                                      pos.y + ts.y + pad.y * 2.0f),
                          IM_COL32(6, 12, 22, 145), 4.0f);
        dl->AddText(ImVec2(pos.x + pad.x, pos.y + pad.y),
                    IM_COL32(138, 214, 154, 210), buf);
    }
}

void DrawViewportPanel(Renderer& renderer, Scene& scene, gx::OrbitCamera& cam,
                       RenderOptions& ropt, PanelState& ps) {
    ImGuiIO& io = ImGui::GetIO();
    normalizePanelWidths(ps, io.DisplaySize.x);
    float menuH   = ImGui::GetFrameHeight();
    float sideW   = leftPanelWidth(ps);
    float toolsW  = toolsPanelWidth(ps);
    float vpW     = io.DisplaySize.x - sideW - toolsW;
    if (vpW < 200) vpW = 200;
    ImGui::SetNextWindowPos(ImVec2(sideW, menuH), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(vpW, io.DisplaySize.y - menuH), ImGuiCond_Always);

    ImGui::Begin(UI(ps, "3D \u592a\u9633\u7cfb##vp", "3D Solar System##vp"), nullptr,
                 ImGuiWindowFlags_NoScrollbar |
                 ImGuiWindowFlags_NoCollapse  |
                 ImGuiWindowFlags_NoResize    | ImGuiWindowFlags_NoMove);

    DrawViewportContent(renderer, scene, cam, ropt, ps);

    ImGui::End();
}
    // UI section.
void DrawToolsPanel(Renderer& renderer, Scene& scene, PanelState& ps) {
    ImGuiIO& io = ImGui::GetIO();
    normalizePanelWidths(ps, io.DisplaySize.x);
    float menuH  = ImGui::GetFrameHeight();
    float toolsW = toolsPanelWidth(ps);
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - toolsW, menuH), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(toolsW, io.DisplaySize.y - menuH),  ImGuiCond_Always);

    ImGui::Begin(UI(ps, "\u5de5\u5177\u9762\u677f##tools", "Tools##tools"), nullptr,
                 ImGuiWindowFlags_NoTitleBar |
                 ImGuiWindowFlags_NoCollapse |
                 ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);

    if (PanelTopCollapseButton("##tools_collapse", UI(ps, "\u5de5\u5177", "Tools"), ps.toolsCollapsed, false))
        ps.toolsCollapsed = !ps.toolsCollapsed;
    if (ps.toolsCollapsed) {
        ImGui::End();
        return;
    }
    const char* tabNamesZh[] = {"\u8fd0\u884c\u53c2\u6570","\u519c\u5386\u5386\u6cd5","\u884c\u661f\u661f\u5386","\u8282\u6c14\u6714\u671b","\u516b\u5b57\u5347\u964d","\u6708\u76f8","\u65e5\u6708\u98df"};
    const char* tabNamesEn[] = {"Parameters","Calendar","Ephemeris","Terms","Bazi","Moon phase","Eclipses"};
    const char** tabNames = ps.useChinese ? tabNamesZh : tabNamesEn;
    // The calendar tab follows the almanac mode, like the mobile nav does.
    const char* calTab = CalendarLabel(ps);

    if (ps.activeTab < 0 || ps.activeTab > 6) ps.activeTab = 0;
    float gap = ImGui::GetStyle().ItemSpacing.x;
    float tabW = (ImGui::GetContentRegionAvail().x - gap * 2.0f) / 3.0f;
    if (tabW < S(72.0f)) tabW = S(72.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(S(8.0f), S(7.0f)));
    for (int t = 0; t < 7; ++t) {
        if (t > 0 && (t % 3) != 0) ImGui::SameLine();
        bool selected = (ps.activeTab == t);
        if (selected) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f,0.36f,0.58f,1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.24f,0.42f,0.66f,1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.28f,0.48f,0.74f,1.0f));
        } else {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.085f,0.115f,0.18f,0.92f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.14f,0.22f,0.34f,1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.18f,0.29f,0.45f,1.0f));
        }
        const char* label = (t == 1) ? calTab : tabNames[t];
        if (ImGui::Button(label, ImVec2(tabW, 0))) ps.activeTab = t;
        ImGui::PopStyleColor(3);
    }
    ImGui::PopStyleVar();

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    switch (ps.activeTab) {
        case 0: DrawParamsContent(scene, ps);   break;
        case 1: DrawCalendarContent(ps);        break;
        case 2: DrawEphemerisContent(ps, scene); break;
        case 3: DrawTermsContent(ps);           break;
        case 4: DrawBaziContent(ps);            break;
        case 5: DrawMoonPhaseContent(renderer, scene, ps); break;
        case 6: DrawEclipseContent(renderer, scene, ps); break;
    }
    ImGui::End();
}

void DrawPanelSplitters(PanelState& ps) {
    ImGuiIO& io = ImGui::GetIO();
    normalizePanelWidths(ps, io.DisplaySize.x);
    float menuH = ImGui::GetFrameHeight();
    float h = io.DisplaySize.y - menuH;

    if (!ps.leftCollapsed) {
        DrawSplitterOverlay("##left_splitter", leftPanelWidth(ps), menuH, h,
                            true, ps.leftPanelWidth, sSideMinW(), sSideMaxW());
    }
    if (!ps.toolsCollapsed) {
        DrawSplitterOverlay("##tools_splitter", io.DisplaySize.x - toolsPanelWidth(ps), menuH, h,
                            false, ps.toolsPanelWidth, sToolsMinW(), sToolsMaxW());
    }
    normalizePanelWidths(ps, io.DisplaySize.x);
}

void LoadAppSettings(RenderOptions& ropt, PanelState& ps) {
    std::ifstream in(kAppIniPath);
    if (!in) return;

    std::string line;
    bool inSection = false;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == ';' || line[0] == '#') continue;
        if (line.front() == '[' && line.back() == ']') {
            inSection = (line == "[sxwnl]");
            continue;
        }
        if (!inSection) continue;
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = line.substr(0, eq);
        std::string val = line.substr(eq + 1);

        if      (key == "showOrbits")        ropt.showOrbits = parseBool(val, ropt.showOrbits);
        else if (key == "showLabels")        ropt.showLabels = parseBool(val, ropt.showLabels);
        else if (key == "showMoon")          ropt.showMoon = parseBool(val, ropt.showMoon);
        else if (key == "showEarthAxis")     ropt.showEarthAxis = parseBool(val, ropt.showEarthAxis);
        else if (key == "showGravityGrid")   ropt.showGravityGrid = parseBool(val, ropt.showGravityGrid);
        else if (key == "showAsteroids")     ropt.showAsteroids = parseBool(val, ropt.showAsteroids);
        else if (key == "showSolarFlames")   ropt.showSolarFlames = parseBool(val, ropt.showSolarFlames);
        else if (key == "gravityGridDensity")   ropt.gravityGridDensity = parseFloat(val, ropt.gravityGridDensity);
        else if (key == "gravityGridCurvature") ropt.gravityGridCurvature = parseFloat(val, ropt.gravityGridCurvature);
        else if (key == "useChinese")        ps.useChinese = parseBool(val, ps.useChinese);
        else if (key == "leftCollapsed")     ps.leftCollapsed = parseBool(val, ps.leftCollapsed);
        else if (key == "toolsCollapsed")    ps.toolsCollapsed = parseBool(val, ps.toolsCollapsed);
        else if (key == "leftPanelWidth")    ps.leftPanelWidth = parseFloat(val, ps.leftPanelWidth);
        else if (key == "toolsPanelWidth")   ps.toolsPanelWidth = parseFloat(val, ps.toolsPanelWidth);
        else if (key == "activeTab")         ps.activeTab = parseInt(val, ps.activeTab);
        else if (key == "timezoneHours")     ps.timezoneHours = parseFloat(val, ps.timezoneHours);
        else if (key == "speedUnit")         ps.speedUnit = parseInt(val, ps.speedUnit);
        else if (key == "speedAmount")       ps.speedAmount = parseFloat(val, ps.speedAmount);
        else if (key == "observerLongitude") ps.observerLongitude = parseFloat(val, (float)ps.observerLongitude);
        else if (key == "observerLatitude")  ps.observerLatitude = parseFloat(val, (float)ps.observerLatitude);
        else if (key == "observerAltitudeKm") ps.observerAltitudeKm = parseFloat(val, (float)ps.observerAltitudeKm);
        else if (key == "eclipseViewMode")   ps.eclipseViewMode = parseInt(val, ps.eclipseViewMode);
        else if (key == "eclipseShowTexture")  ps.eclipseShowTexture = parseBool(val, ps.eclipseShowTexture);
        else if (key == "eclipseShowBoundaries") ps.eclipseShowBoundaries = parseBool(val, ps.eclipseShowBoundaries);
        else if (key == "vpEclipseGeometry") ps.vpEclipseGeometry = parseBool(val, ps.vpEclipseGeometry);
        else if (key == "groundBestSeat") ps.groundBestSeat = parseBool(val, ps.groundBestSeat);
        else if (key == "groundFovDeg")      ps.groundFovDeg = parseFloat(val, ps.groundFovDeg);
        else if (key == "mobilePage")        ps.mobilePage = parseInt(val, ps.mobilePage);
        else if (key == "mobileSheetOpen")   ps.mobileSheetOpen = parseBool(val, ps.mobileSheetOpen);
        else if (key == "mobilePreview")     ps.mobilePreview = parseBool(val, ps.mobilePreview);
        else if (key == "fontScale")         ps.fontScale = parseFloat(val, ps.fontScale);
        else if (key == "showAlmanac")       ps.showAlmanac = parseBool(val, ps.showAlmanac);
        else if (key == "moonRealOrientation") ps.moonRealOrientation = parseBool(val, ps.moonRealOrientation);
    }
    ps.timezoneHours = std::clamp(ps.timezoneHours, -12.0f, 14.0f);
    ps.timezoneHours = std::round(ps.timezoneHours * 4.0f) / 4.0f;
    ps.speedUnit = std::clamp(ps.speedUnit, 0, 2);
    if (!std::isfinite(ps.speedAmount)) ps.speedAmount = 5.0f;
    ps.observerLongitude = std::clamp(ps.observerLongitude, -180.0, 180.0);
    ps.observerLatitude = std::clamp(ps.observerLatitude, -90.0, 90.0);
    ps.eclipseViewMode = std::clamp(ps.eclipseViewMode, 0, 1);
    if (!std::isfinite(ps.groundFovDeg)) ps.groundFovDeg = 6.0f;
    ps.groundFovDeg = std::clamp(ps.groundFovDeg, 0.6f, 110.0f);
    if (!std::isfinite(ps.fontScale)) ps.fontScale = 1.0f;
    ps.fontScale = std::clamp(ps.fontScale, kFontScaleMin, kFontScaleMax);
}

void SaveAppSettings(const RenderOptions& ropt, const PanelState& ps) {
    std::ofstream out(kAppIniPath, std::ios::binary);
    if (!out) return;
    out << "[sxwnl]\n";
    out << "showOrbits=" << (ropt.showOrbits ? 1 : 0) << "\n";
    out << "showLabels=" << (ropt.showLabels ? 1 : 0) << "\n";
    out << "showMoon=" << (ropt.showMoon ? 1 : 0) << "\n";
    out << "showEarthAxis=" << (ropt.showEarthAxis ? 1 : 0) << "\n";
    out << "showGravityGrid=" << (ropt.showGravityGrid ? 1 : 0) << "\n";
    out << "showAsteroids=" << (ropt.showAsteroids ? 1 : 0) << "\n";
    out << "showSolarFlames=" << (ropt.showSolarFlames ? 1 : 0) << "\n";
    out << "gravityGridDensity=" << ropt.gravityGridDensity << "\n";
    out << "gravityGridCurvature=" << ropt.gravityGridCurvature << "\n";
    out << "useChinese=" << (ps.useChinese ? 1 : 0) << "\n";
    out << "leftCollapsed=" << (ps.leftCollapsed ? 1 : 0) << "\n";
    out << "toolsCollapsed=" << (ps.toolsCollapsed ? 1 : 0) << "\n";
    out << "leftPanelWidth=" << ps.leftPanelWidth << "\n";
    out << "toolsPanelWidth=" << ps.toolsPanelWidth << "\n";
    out << "activeTab=" << ps.activeTab << "\n";
    out << "timezoneHours=" << ps.timezoneHours << "\n";
    out << "speedUnit=" << ps.speedUnit << "\n";
    out << "speedAmount=" << ps.speedAmount << "\n";
    out << "observerLongitude=" << ps.observerLongitude << "\n";
    out << "observerLatitude=" << ps.observerLatitude << "\n";
    out << "observerAltitudeKm=" << ps.observerAltitudeKm << "\n";
    out << "eclipseViewMode=" << ps.eclipseViewMode << "\n";
    out << "eclipseShowTexture=" << (ps.eclipseShowTexture ? 1 : 0) << "\n";
    out << "eclipseShowBoundaries=" << (ps.eclipseShowBoundaries ? 1 : 0) << "\n";
    out << "vpEclipseGeometry=" << (ps.vpEclipseGeometry ? 1 : 0) << "\n";
    out << "groundBestSeat=" << (ps.groundBestSeat ? 1 : 0) << "\n";
    out << "groundFovDeg=" << ps.groundFovDeg << "\n";
    out << "mobilePage=" << ps.mobilePage << "\n";
    out << "mobileSheetOpen=" << (ps.mobileSheetOpen ? 1 : 0) << "\n";
    out << "mobilePreview=" << (ps.mobilePreview ? 1 : 0) << "\n";
    out << "fontScale=" << ps.fontScale << "\n";
    out << "showAlmanac=" << (ps.showAlmanac ? 1 : 0) << "\n";
    out << "moonRealOrientation=" << (ps.moonRealOrientation ? 1 : 0) << "\n";
}

} // namespace sx
