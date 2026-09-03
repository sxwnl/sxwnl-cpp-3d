#include "panels.h"

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
static const float kResizeHandleW = 7.0f;
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

static void DrawDateFields(PanelState& ps, const char* suffix,
                           int& year, int& month, int& day,
                           int* hour = nullptr, int* minute = nullptr) {
    char id[64];
    const char* yearLabel = ps.useChinese ? "\345\271\264" : "Year";
    const char* monthLabel = ps.useChinese ? "\346\234\210" : "Month";
    const char* dayLabel = ps.useChinese ? "\346\227\245" : "Day";
    std::snprintf(id, sizeof(id), "date_fields_%s", suffix);
    if (ImGui::BeginTable(id, 3, ImGuiTableFlags_SizingStretchSame)) {
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
    DrawDateFields(ps, "cal", ps.calYear, ps.calMonth, ps.selectedCalendarDay);
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
    if (ImGui::BeginTable("cal_cards", 7, ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_PadOuterX)) {
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
    if (ImGui::BeginChild("##eclipse_results", ImVec2(0, S(116)), true)) {
        for (int i = 0; i < (int)ps.eclipseEvents.size(); ++i) {
            EclipseEvent& e = ps.eclipseEvents[i];
            std::string label = EclipseTimeText(e.maximumTd, ps) + "  " +
                                EclipseKindText(ps, e) + "  " + EclipseTypeText(ps, e);
            if (ImGui::Selectable(label.c_str(), ps.selectedEclipse == i)) SelectEclipse(ps, i);
        }
    }
    ImGui::EndChild();
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

    const double* stages = e.contactsTd;
    double first = e.kind == EclipseEvent::Solar ? stages[0] : (stages[3] ? stages[3] : stages[0]);
    double last = e.kind == EclipseEvent::Solar ? stages[2] : (stages[4] ? stages[4] : stages[2]);
    if (ImGui::Button(UI(ps,"\u8df3\u5230\u98df\u751a","Jump to maximum"))) {
        scene.clock().jd = eclipseTdToUtcJD(e.maximumTd);
        scene.clock().playing = false;
    }
    ImGui::SameLine();
    if (ImGui::Button(UI(ps,"\u4ece\u98df\u59cb\u6f14\u793a","Play from start"))) {
        scene.clock().jd = eclipseTdToUtcJD(first);
        if (!ps.eclipseDemoActive) {
            ps.eclipseSavedSpeed = scene.clock().speedDaysPerSec;
            ps.eclipseSavedUnit  = ps.speedUnit;
            ps.eclipseSavedAmount = ps.speedAmount;
        }
        ps.eclipseDemoActive = true;
        // Pace the run to the eclipse's own length rather than a fixed rate:
        // a solar eclipse spans ~3-6 h from first to last contact, a lunar one
        // longer, so a constant "1 minute per second" makes some crawl. Aim for
        // a fixed wall-clock run, clamped so it never gets absurd either way.
        double span = (last > first) ? (last - first) : (3.0 / 24.0);
        double perSec = span / kEclipseDemoSeconds;
        perSec = std::clamp(perSec, 0.5 / 1440.0, 15.0 / 1440.0);
        scene.clock().speedDaysPerSec = (float)perSec;
        // Keep the sidebar preset in step; otherwise it keeps showing the old
        // rate and the next nudge of that control snaps the demo speed away.
        ps.speedUnit = 0;                                  // seconds / real second
        ps.speedAmount = (float)(perSec * 86400.0);
        scene.clock().playing = true;
    }
    ImGui::SameLine();
    if (ImGui::Button(scene.clock().playing ? UI(ps,"\u6682\u505c","Pause") : UI(ps,"\u7ee7\u7eed","Resume")))
        scene.clock().playing = !scene.clock().playing;
    if (ps.eclipseDemoActive && SceneUtcToTd(scene) > last) {
        scene.clock().playing = false;
        scene.clock().speedDaysPerSec = ps.eclipseSavedSpeed;
        ps.speedUnit   = ps.eclipseSavedUnit;
        ps.speedAmount = ps.eclipseSavedAmount;
        ps.eclipseDemoActive = false;
    }
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

// "09-23 08:05" in the reader's own time zone - the card has no room for the
// full stamp the eclipse page prints, and the year is almost always this one.
static std::string ShortEventTime(double jdTd, const PanelState& ps) {
    Date d = setFromJD(eclipseTdToUtcJD(jdTd) + ps.timezoneHours / 24.0 + 0.5 / 1440.0);
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%02d-%02d %02d:%02d", d.M, d.D, d.h, d.m);
    return buf;
}

// Rows the card shows for the selected body. Returned as label/value pairs so
// the drawing code can size the two columns independently.
struct CardRow { std::string label, value; };

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

// Draws the card and returns true if the pointer is over it, so the viewport
// can leave the camera alone while the reader is using it.
static bool DrawSelectedBodyCard(Scene& scene, PanelState& ps, ImVec2 anchor,
                                 ImVec2 origin, float vpW, float vpH,
                                 bool& jumpRequested, double& jumpToTd) {
    const int xt = SelectionXt(scene, ps);
    if (xt == -999) return false;

    const char* title = ps.selectedMoon
        ? UI(ps, "月球", "Moon")
        : BodyLabel(ps, scene.bodies()[ps.selectedBody]);
    std::vector<CardRow> rows = SelectionRows(scene, ps);
    const std::vector<AstroEvent>& events =
        CachedBodyEvents(ps, xt, SceneUtcToTd(scene));

    const float pad     = S(9.0f);
    const float lineH   = ImGui::GetTextLineHeightWithSpacing();
    const float gap     = S(10.0f);
    const char* evTitle = UI(ps, "即将发生的天象",
                                 "Upcoming events");

    // Width: the widest of the title, every label+value pair, and every event
    // row. Measured up front so the slab never reflows as the numbers tick.
    float labelW = 0.0f, valueW = 0.0f, evW = ImGui::CalcTextSize(evTitle).x;
    for (const CardRow& r : rows) {
        labelW = std::max(labelW, ImGui::CalcTextSize(r.label.c_str()).x);
        valueW = std::max(valueW, ImGui::CalcTextSize(r.value.c_str()).x);
    }
    float evTimeW = 0.0f;
    std::vector<std::string> evTimes, evNames;
    evTimes.reserve(events.size());
    evNames.reserve(events.size());
    for (const AstroEvent& e : events) {
        evTimes.push_back(ShortEventTime(e.jdTd, ps));
        std::string nm = astroEventName(e.kind, ps.useChinese);
        if (!e.detail.empty()) nm += "  " + e.detail;
        evNames.push_back(nm);
    }
    for (const std::string& t : evTimes)
        evTimeW = std::max(evTimeW, ImGui::CalcTextSize(t.c_str()).x);
    for (const std::string& n : evNames)
        evW = std::max(evW, evTimeW + gap + ImGui::CalcTextSize(n.c_str()).x);

    float bodyW = std::max(labelW + gap + valueW, evW);
    float cardW = std::max(bodyW, ImGui::CalcTextSize(title).x + S(24.0f)) + pad * 2.0f;
    float cardH = pad * 2.0f + lineH * (1.0f + (float)rows.size());
    if (!events.empty()) cardH += S(6.0f) + lineH * (1.0f + (float)events.size());

    // Prefer the right of the body, flip when that would run off the edge,
    // then clamp so the whole slab stays inside the viewport either way.
    float x = anchor.x + S(26.0f);
    if (x + cardW > vpW - S(8.0f)) x = anchor.x - S(26.0f) - cardW;
    x = std::clamp(x, S(8.0f), std::max(S(8.0f), vpW - cardW - S(8.0f)));
    float y = std::clamp(anchor.y - cardH * 0.35f, S(8.0f),
                         std::max(S(8.0f), vpH - cardH - S(8.0f)));

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

    float ty = p0.y + pad;
    dl->AddText(ImVec2(p0.x + pad, ty), IM_COL32(255, 218, 120, 245), title);

    // Close affordance. An InvisibleButton rather than a hit test on the glyph
    // so it takes the click before the viewport's own picking sees it.
    const float cs = lineH;
    ImVec2 cp{p1.x - pad - cs, p0.y + pad - S(1.0f)};
    ImVec2 keep = ImGui::GetCursorScreenPos();
    ImGui::SetCursorScreenPos(cp);
    bool closeClicked = ImGui::InvisibleButton("##vp_card_close", ImVec2(cs, cs));
    bool closeHover = ImGui::IsItemHovered();
    ImGui::SetCursorScreenPos(keep);
    ImU32 xcol = closeHover ? IM_COL32(255, 190, 180, 255) : IM_COL32(150, 175, 205, 190);
    float xin = cs * 0.28f;
    dl->AddLine(ImVec2(cp.x + xin, cp.y + xin), ImVec2(cp.x + cs - xin, cp.y + cs - xin), xcol, 1.6f);
    dl->AddLine(ImVec2(cp.x + cs - xin, cp.y + xin), ImVec2(cp.x + xin, cp.y + cs - xin), xcol, 1.6f);
    if (closeClicked) {
        ps.selectedMoon = false;
        ps.selectedBody = -1;
    }
    ty += lineH;

    for (const CardRow& r : rows) {
        dl->AddText(ImVec2(p0.x + pad, ty), IM_COL32(140, 172, 208, 225), r.label.c_str());
        dl->AddText(ImVec2(p0.x + pad + labelW + gap, ty),
                    IM_COL32(225, 236, 250, 245), r.value.c_str());
        ty += lineH;
    }

    if (!events.empty()) {
        ty += S(6.0f);
        dl->AddLine(ImVec2(p0.x + pad, ty - S(3.0f)), ImVec2(p1.x - pad, ty - S(3.0f)),
                    IM_COL32(90, 130, 180, 80), 1.0f);
        dl->AddText(ImVec2(p0.x + pad, ty), IM_COL32(126, 205, 172, 235), evTitle);
        ty += lineH;
        for (size_t i = 0; i < events.size(); ++i) {
            // Each row jumps the clock to its event; that is the whole point
            // of listing them next to the body they happen to.
            ImVec2 rp{p0.x + pad, ty};
            char id[32];
            std::snprintf(id, sizeof(id), "##vp_ev_%zu", i);
            ImGui::SetCursorScreenPos(rp);
            bool clicked = ImGui::InvisibleButton(id, ImVec2(cardW - pad * 2.0f, lineH));
            bool hov = ImGui::IsItemHovered();
            ImGui::SetCursorScreenPos(keep);
            if (hov)
                dl->AddRectFilled(ImVec2(rp.x - S(3.0f), rp.y),
                                  ImVec2(p1.x - pad + S(3.0f), rp.y + lineH),
                                  IM_COL32(70, 110, 170, 70), S(3.0f));
            if (clicked) { jumpRequested = true; jumpToTd = events[i].jdTd; }
            dl->AddText(rp, IM_COL32(168, 196, 228, 235), evTimes[i].c_str());
            dl->AddText(ImVec2(rp.x + evTimeW + gap, ty),
                        hov ? IM_COL32(255, 236, 190, 255) : IM_COL32(226, 236, 248, 240),
                        evNames[i].c_str());
            ty += lineH;
        }
    }

    ImGui::Dummy(ImVec2(0.0f, 0.0f));
    ImVec2 mp = ImGui::GetIO().MousePos;
    return mp.x >= p0.x && mp.x <= p1.x && mp.y >= p0.y && mp.y <= p1.y;
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
    if (selected && ps.vpEclipseGeometry) {
        eclipseOverlay.active = true;
        eclipseOverlay.solar  = (selected->kind == EclipseEvent::Solar);
        eclipseOverlay.jdTd   = nowTd;
        eclipseOverlay.path   = &ps.eclipsePath;
        eclipseOverlay.limits = ps.eclipseShowLimits ? &ps.eclipseLimits : nullptr;
        if (!eclipseOverlay.solar) {
            // How much of the Moon's disc Earth's umbra covers, as a fraction
            // of its diameter - the same magnitude the eclipse page prints.
            LunarShadowGeometry g = lunarShadowGeometry(nowTd);
            if (g.valid && g.moonRadius > 0.0) {
                double sep = std::sqrt(g.x * g.x + g.y * g.y);
                double mag = (g.umbraRadius + g.moonRadius - sep) / (2.0 * g.moonRadius);
                eclipseOverlay.lunarShade = (float)std::clamp(mag, 0.0, 1.0);
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
            cardHovered = DrawSelectedBodyCard(scene, ps, ImVec2(ax, ay), origin,
                                               (float)w, (float)h,
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

    if (viewportHovered && !cardHovered) {
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
        if (!isDragging && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
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
        if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
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

    // Top-left: current date and playback state.
    {
        SimClock& clk = scene.clock();
        Date d = localDateFromUtcJD(clk.jd, ps.timezoneHours);
        char buf[80];
        std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d  %s",
                      d.Y, d.M, d.D, d.h, d.m,
                      clk.playing ? UI(ps, "\u64ad\u653e\u4e2d", "playing")
                                  : UI(ps, "\u6682\u505c", "paused"));
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
    out << "mobilePage=" << ps.mobilePage << "\n";
    out << "mobileSheetOpen=" << (ps.mobileSheetOpen ? 1 : 0) << "\n";
    out << "mobilePreview=" << (ps.mobilePreview ? 1 : 0) << "\n";
    out << "fontScale=" << ps.fontScale << "\n";
    out << "showAlmanac=" << (ps.showAlmanac ? 1 : 0) << "\n";
    out << "moonRealOrientation=" << (ps.moonRealOrientation ? 1 : 0) << "\n";
}

} // namespace sx
