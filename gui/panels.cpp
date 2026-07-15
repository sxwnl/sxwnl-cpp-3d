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

// Small helpers.
static const char* UI(const PanelState& ps, const char* zh, const char* en) {
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
    return ps.leftCollapsed ? kRailW : std::clamp(ps.leftPanelWidth, kSideMinW, kSideMaxW);
}

static float toolsPanelWidth(const PanelState& ps) {
    return ps.toolsCollapsed ? kRailW : std::clamp(ps.toolsPanelWidth, kToolsMinW, kToolsMaxW);
}

static void normalizePanelWidths(PanelState& ps, float displayW) {
    ps.leftPanelWidth = std::clamp(ps.leftPanelWidth, kSideMinW, kSideMaxW);
    ps.toolsPanelWidth = std::clamp(ps.toolsPanelWidth, kToolsMinW, kToolsMaxW);

    float leftW = ps.leftCollapsed ? kRailW : ps.leftPanelWidth;
    float toolsW = ps.toolsCollapsed ? kRailW : ps.toolsPanelWidth;
    float overflow = leftW + toolsW + kViewportMinW - std::max(displayW, kViewportMinW + kRailW * 2.0f);
    if (overflow <= 0.0f) return;

    if (!ps.toolsCollapsed) {
        float reduce = std::min(overflow, ps.toolsPanelWidth - kToolsMinW);
        ps.toolsPanelWidth -= reduce;
        overflow -= reduce;
    }
    if (overflow > 0.0f && !ps.leftCollapsed) {
        float reduce = std::min(overflow, ps.leftPanelWidth - kSideMinW);
        ps.leftPanelWidth -= reduce;
    }
}

static bool PanelTopCollapseButton(const char* id, const char* label, bool collapsed, bool leftSide) {
    ImVec2 p = ImGui::GetCursorScreenPos();
    float w = std::max(ImGui::GetContentRegionAvail().x, 24.0f);
    float h = 26.0f;
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
    float cx = p.x + 12.0f;
    float cy = p.y + h * 0.5f;
    dl->AddTriangleFilled(ImVec2(cx + dir * 4.5f, cy),
                          ImVec2(cx - dir * 3.5f, cy - 6.0f),
                          ImVec2(cx - dir * 3.5f, cy + 6.0f),
                          fg);
    if (!collapsed && label && label[0]) {
        dl->AddText(ImVec2(p.x + 28.0f, p.y + 5.0f), IM_COL32(190, 214, 245, 245), label);
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

    ImGui::SetNextWindowPos(ImVec2(centerX - kResizeHandleW * 0.5f, topY), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(kResizeHandleW, h), ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::Begin(id, nullptr, flags);
    ImGui::InvisibleButton("##drag", ImVec2(kResizeHandleW, h));
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
    float cx = p.x + kResizeHandleW * 0.5f;
    dl->AddRectFilled(ImVec2(cx - 1.5f, p.y + 4.0f), ImVec2(cx + 1.5f, p.y + h - 4.0f), col, 2.0f);
    ImGui::End();
    ImGui::PopStyleVar();
}

static void SectionHeader(const PanelState& ps, const char* zh, const char* en) {
    const char* label = UI(ps, zh, en);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetCursorScreenPos();
    float w = ImGui::GetContentRegionAvail().x;
    float h = ImGui::GetTextLineHeight() + 10.0f;
    dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), IM_COL32(12, 21, 38, 150), 4.0f);
    dl->AddRectFilled(ImVec2(p.x, p.y + 4.0f), ImVec2(p.x + 3.0f, p.y + h - 4.0f),
                      IM_COL32(85, 158, 230, 220), 2.0f);
    ImGui::SetCursorScreenPos(ImVec2(p.x + 9.0f, p.y + 5.0f));
    ImGui::TextColored(ImVec4(0.67f, 0.82f, 1.0f, 1.0f), "%s", label);
    ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + h + 4.0f));
}

static void InfoRow(const PanelState& ps, const char* zh, const char* en, const char* value) {
    ImGui::PushID(zh);
    if (ImGui::BeginTable("##info_row", 2, ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("label", ImGuiTableColumnFlags_WidthFixed, ps.useChinese ? 70.0f : 92.0f);
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

static bool SmallArrowStepButton(const char* id, ImGuiDir dir, float size) {
    ImVec2 pos = ImGui::GetCursorScreenPos();
    bool clicked = ImGui::InvisibleButton(id, ImVec2(size, size));
    bool hovered = ImGui::IsItemHovered();
    bool active = ImGui::IsItemActive();

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImU32 bg = active ? IM_COL32(62, 92, 150, 220)
             : hovered ? IM_COL32(42, 64, 112, 210)
                       : IM_COL32(30, 42, 76, 190);
    ImU32 col = hovered ? IM_COL32(218, 232, 255, 255)
                        : IM_COL32(155, 185, 230, 255);
    dl->AddRectFilled(pos, ImVec2(pos.x + size, pos.y + size), bg, 4.0f);
    dl->AddRect(pos, ImVec2(pos.x + size, pos.y + size), IM_COL32(78, 110, 170, 130), 4.0f);

    float cx = pos.x + size * 0.5f;
    float cy = pos.y + size * 0.5f;
    float w = size * 0.34f;
    float h = size * 0.42f;
    if (dir == ImGuiDir_Left) {
        dl->AddTriangleFilled(ImVec2(cx - w * 0.45f, cy),
                              ImVec2(cx + w * 0.45f, cy - h * 0.5f),
                              ImVec2(cx + w * 0.45f, cy + h * 0.5f), col);
    } else {
        dl->AddTriangleFilled(ImVec2(cx + w * 0.45f, cy),
                              ImVec2(cx - w * 0.45f, cy - h * 0.5f),
                              ImVec2(cx - w * 0.45f, cy + h * 0.5f), col);
    }
    return clicked;
}

static void DrawSteppedIntField(const char* id, const char* label, int& value) {
    ImGui::PushID(id);
    float arrowW = ImGui::GetFrameHeight() * 0.82f;
    float labelW = ImGui::CalcTextSize(label).x;
    float fullW = ImGui::GetContentRegionAvail().x;
    float baseX = ImGui::GetCursorPosX();
    float labelX = baseX + std::max(arrowW + 2.0f, (fullW - labelW) * 0.5f);
    float rightX = baseX + std::max(arrowW + labelW + 4.0f, fullW - arrowW);

    ImGui::SetCursorPosX(baseX);
    if (SmallArrowStepButton("prev", ImGuiDir_Left, arrowW)) --value;
    ImGui::SameLine();
    ImGui::SetCursorPosX(labelX);
    ImGui::TextUnformatted(label);
    ImGui::SameLine();
    ImGui::SetCursorPosX(rightX);
    if (SmallArrowStepButton("next", ImGuiDir_Right, arrowW)) ++value;

    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputInt("##value", &value, 0, 0);
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
                ImGui::TextUnformatted(UI(ps, "\u65f6", "Hour"));
            }
            if (minute) {
                ImGui::TableSetColumnIndex(cols - 1);
                ImGui::TextUnformatted(UI(ps, "\u5206", "Minute"));
            }

            ImGui::TableNextRow();
            if (hour) {
                ImGui::TableSetColumnIndex(0);
                std::snprintf(id, sizeof(id), "##h%s", suffix);
                ImGui::SetNextItemWidth(-1.0f);
                ImGui::InputInt(id, hour, 0, 0);
            }
            if (minute) {
                ImGui::TableSetColumnIndex(cols - 1);
                std::snprintf(id, sizeof(id), "##min%s", suffix);
                ImGui::SetNextItemWidth(-1.0f);
                ImGui::InputInt(id, minute, 0, 0);
            }
            ImGui::EndTable();
        }
    }
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
    size_t pos = s.find(" ");
    if (pos != std::string::npos) {
        pos += std::string(" ").size();
        s = s.substr(0, pos);
    }
    return s;
}

static void DrawCalendarDayDetails(const PanelState& ps, const OB_DAY& d) {
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
}

// ============================================================================
//  2-D Moon phase disk
// ============================================================================
// illum  : [0,1]  0=new moon, 1=full moon
// waxing : true means the lit side is on the right.
static void DrawMoonDisk(ImDrawList* dl, ImVec2 center, float r,
                         float illum, bool waxing) {
    const int   N  = 64;
    const float PI = 3.14159265f;

    // Dark background circle
    dl->AddCircleFilled(center, r, IM_COL32(12, 12, 26, 255), N);
    dl->AddCircle(center, r + 0.5f, IM_COL32(90, 90, 130, 200), N, 1.5f);

    if (illum < 0.004f) return;      // new moon, nothing to draw

    ImU32 litCol = IM_COL32(238, 218, 175, 255);

    if (illum > 0.996f) {            // full moon
        dl->AddCircleFilled(center, r, litCol, N);
        // subtle surface marks
        dl->AddCircle(ImVec2(center.x + r*0.18f, center.y - r*0.22f),
                      r*0.12f, IM_COL32(180,160,120,60), 16, 1.0f);
        dl->AddCircle(ImVec2(center.x - r*0.25f, center.y + r*0.30f),
                      r*0.08f, IM_COL32(180,160,120,60), 16, 1.0f);
        return;
    }

    // General case: construct lit-region polygon.
    // tx = terminator x-axis bulge at the equator.
    //   illum=0   => tx=+r  (crescent)
    //   illum=0.5 => tx=0   (quarter, straight line)
    //   illum=1   => tx=-r  (gibbous reaches far left)
    float tx   = r * (1.0f - 2.0f * illum);
    float sign = waxing ? 1.0f : -1.0f;

    // Path = outer lit semicircle plus the terminator ellipse.
    std::vector<ImVec2> pts;
    pts.reserve(N*2 + 4);
    for (int i = 0; i <= N; ++i) {
        float t = PI * (float)i / N;
        pts.push_back(ImVec2(center.x + sign * r  * std::sin(t),
                             center.y             - r  * std::cos(t)));
    }
    for (int i = N; i >= 0; --i) {
        float t = PI * (float)i / N;
        pts.push_back(ImVec2(center.x + sign * tx * std::sin(t),
                             center.y             - r  * std::cos(t)));
    }
    dl->AddConvexPolyFilled(pts.data(), (int)pts.size(), litCol);
}

// ============================================================================
//  Per-panel content helpers (called from the tab bar)
// ============================================================================

static void DrawParamsContent(Scene& scene, PanelState& ps) {
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

static void DrawCalendarContent(PanelState& ps) {
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
    int detailIdx = -1;
    if (ImGui::BeginTable("cal_cards", 7, ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_PadOuterX)) {
        for (int c = 0; c < 7; ++c) {
            ImGui::TableSetupColumn(ps.useChinese ? wkZh[c] : wkEn[c]);
        }
        ImGui::TableNextRow();
        for (int c = 0; c < 7; ++c) {
            ImGui::TableSetColumnIndex(c);
            ImGui::TextColored({0.65f,0.78f,0.95f,1.0f}, "%s", ps.useChinese ? wkZh[c] : wkEn[c]);
        }
        for (int r = 0; r < rows; ++r) {
            ImGui::TableNextRow();
            for (int c = 0; c < 7; ++c) {
                ImGui::TableSetColumnIndex(c);
                int idx = grid[r][c];
                if (idx < 0) {
                    ImGui::Dummy(ImVec2(0, 58.0f));
                    continue;
                }
                OB_DAY& d = lun.day[idx];
                bool isToday = (d.y == today.Y && d.m == today.M && d.d == today.D);
                bool selected = (d.d == ps.selectedCalendarDay);
                ImVec2 p = ImGui::GetCursorScreenPos();
                float cellW = ImGui::GetContentRegionAvail().x;
                float cellH = 58.0f;
                ImGui::InvisibleButton(("##calday" + std::to_string(d.d)).c_str(), ImVec2(cellW, cellH));
                bool hovered = ImGui::IsItemHovered();
                if (ImGui::IsItemClicked()) ps.selectedCalendarDay = d.d;

                ImDrawList* dl = ImGui::GetWindowDrawList();
                ImU32 bg = selected ? IM_COL32(34, 58, 105, 220)
                         : hovered  ? IM_COL32(32, 44, 72, 220)
                         : isToday  ? IM_COL32(42, 50, 42, 210)
                                    : IM_COL32(18, 20, 32, 200);
                dl->AddRectFilled(p, ImVec2(p.x + cellW, p.y + cellH), bg, 4.0f);
                dl->AddRect(p, ImVec2(p.x + cellW, p.y + cellH),
                            isToday ? IM_COL32(120,210,130,180) : IM_COL32(70,88,125,110), 4.0f);

                ImU32 dayCol = (c == 0 || c == 6) ? IM_COL32(255,205,130,255)
                                                  : IM_COL32(230,235,245,255);
                dl->AddText(ImVec2(p.x + 6, p.y + 4), dayCol, std::to_string(d.d).c_str());
                std::string lunar = (d.Ldi == 0) ? (std::string(d.Lmc.c_str()) + "\u6708")
                                                 : std::string(d.Ldc.c_str());
                lunar = ellipsizeText(lunar, cellW - 12.0f);
                dl->AddText(ImVec2(p.x + 6, p.y + 24), IM_COL32(165,185,215,230), lunar.c_str());
                std::string note = compactDayNote(d);
                if (!note.empty()) {
                    note = ellipsizeText(note, cellW - 12.0f);
                    ImU32 noteCol = !d.jqmc.empty() ? IM_COL32(120,235,130,240)
                                 : !d.A.empty()    ? IM_COL32(255,150,125,240)
                                                   : IM_COL32(225,195,105,230);
                    ImFont* font = ImGui::GetFont();
                    float noteSize = ImGui::GetFontSize() * 0.86f;
                    dl->AddText(font, noteSize, ImVec2(p.x + 6, p.y + 42), noteCol, note.c_str());
                }

                if (hovered) {
                    ImGui::BeginTooltip();
                    DrawCalendarDayDetails(ps, d);
                    ImGui::EndTooltip();
                }
                if (d.d == ps.selectedCalendarDay) detailIdx = idx;
            }
        }
        ImGui::EndTable();
    }
    if (detailIdx >= 0) DrawCalendarDayDetails(ps, lun.day[detailIdx]);
}

static void DrawEphemerisContent(PanelState& ps, const Scene& scene) {
    static const int   xtArr[]   = {1,2,3,4,5,6,7,8,10};
    static const char* nameZh[] = {"\u6c34\u661f","\u91d1\u661f","\u706b\u661f","\u6728\u661f","\u571f\u661f","\u5929\u738b\u661f","\u6d77\u738b\u661f","\u51a5\u738b\u661f","\u6708\u4eae"};
    static const char* nameEn[] = {"Mercury","Venus","Mars","Jupiter","Saturn",
                                   "Uranus","Neptune","Pluto","Moon"};
    if (ps.ephBodyIdx < 0 || ps.ephBodyIdx >= IM_ARRAYSIZE(xtArr)) ps.ephBodyIdx = 0;
    if (ps.ephFollowSelection) {
        SyncDateFromScene(ps, scene);
    }
    int beforeBody = ps.ephBodyIdx;
    ImGui::SetNextItemWidth(120);
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
    ImGui::BeginChild("eph_out", ImVec2(0,0), false, ImGuiWindowFlags_HorizontalScrollbar);
    ImGui::TextUnformatted(ps.ephText.c_str());
    ImGui::EndChild();
}

static void DrawTermsContent(PanelState& ps) {
    ImGui::SetNextItemWidth(100);
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
    ImGui::BeginChild("term_out", ImVec2(0,0), false, ImGuiWindowFlags_HorizontalScrollbar);
    ImGui::TextUnformatted(ps.termText.c_str());
    ImGui::EndChild();
}

static void DrawBaziContent(PanelState& ps) {
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
    ImGui::BeginChild("sj", ImVec2(0,0), false, ImGuiWindowFlags_HorizontalScrollbar);
    ImGui::TextUnformatted(ps.shengjiangText.c_str());
    ImGui::EndChild();
}

static void DrawMoonPhaseContent(Renderer& renderer, const Scene& scene, PanelState& ps) {
    const MoonData& md = scene.moon();
    if (!md.valid) { ImGui::TextDisabled(UI(ps, "(\u8ba1\u7b97\u4e2d...)", "(calculating...)")); return; }

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
    ImGui::Spacing();

    // Illumination progress bar
    ImGui::ProgressBar((float)md.illumination, ImVec2(-1.0f, 8.0f), "");
    ImGui::Spacing();

    // UI section.
    ImVec2 avail = ImGui::GetContentRegionAvail();
    const float gap = ImGui::GetStyle().ItemSpacing.x;
    const bool stacked = avail.x < 470.0f;
    const float maxPreview = stacked ? 360.0f : 230.0f;
    float col = stacked ? std::min(avail.x, maxPreview)
                        : std::min((avail.x - gap) * 0.5f, maxPreview);
    if (col < 80.f) col = 80.f;

    // UI section.
    ImVec2 canvasP = ImGui::GetCursorScreenPos();
    float  side    = col - 8.f;
    if (side < 60.f) side = 60.f;
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
            1.2f, IM_COL32(220,220,240,180));

    DrawMoonDisk(dl, ImVec2(canvasP.x + side*0.5f, canvasP.y + side*0.5f),
                 side * 0.38f, (float)md.illumination, md.waxing);

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
    renderer.renderMoonPhase((float)md.elongationDeg, md.waxing,
                             ps.moonPhaseYaw, ps.moonPhasePitch);

    unsigned int moonTex = renderer.moonPhaseTex();
    if (moonTex) {
        ImGui::SetCursorScreenPos(p3d);
        ImGui::Image((ImTextureID)(intptr_t)moonTex,
                     ImVec2(s3d, s3d),
                     ImVec2(0, 1), ImVec2(1, 0)); // y-flip
        if (moon3dHovered) {
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

static void DrawSolarGlobe(Renderer& renderer, const Scene& scene, PanelState& ps, float side) {
    ImVec2 origin = ImGui::GetCursorScreenPos();

    // ── texture mode: 3D OpenGL render ──────────────────────────────────────
    if (ps.eclipseShowTexture) {
        double td = SceneUtcToTd(scene);
        renderer.renderEclipseGlobe(ps.eclipseGlobeYaw, ps.eclipseGlobePitch,
                                    ps.eclipsePath, td, ps.eclipseShowBoundaries);

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

        // Solar direction indicator. It is deliberately rendered in the
        // panel layer so it stays clear and visible regardless of the camera
        // angle or night-side shading on the Earth texture.
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 sun(origin.x + side * 0.87f, origin.y + side * 0.16f);
        float sunR = std::max(11.0f, side * 0.048f);
        ImVec2 earthLightPoint(origin.x + side * 0.63f, origin.y + side * 0.35f);
        dl->AddLine(ImVec2(sun.x - sunR * 0.55f, sun.y + sunR * 0.55f),
                    earthLightPoint, IM_COL32(255,210,92,165), 1.5f);
        for (int i = 5; i >= 0; --i) {
            float q = (float)i / 5.0f;
            dl->AddCircleFilled(sun, sunR * (0.55f + q * 0.75f),
                                IM_COL32(255, 176 + (int)(55*q), 45, (int)(12 + 26*q)), 24);
        }
        dl->AddCircleFilled(sun, sunR, IM_COL32(255,221,100,255), 24);
        dl->AddText(ImVec2(sun.x - sunR, sun.y + sunR + 3.0f),
                    IM_COL32(255,225,130,245), UI(ps, "太阳", "Sun"));

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
        }
        dl->AddText(ImVec2(origin.x + 9, origin.y + 8), IM_COL32(190,220,245,230),
                    UI(ps, "3D \u5730\u7403\u4eea\uff1a\u62d6\u52a8\u65cb\u8f6c",
                           "3D globe: drag to rotate"));
    }

    // ── layer toggle controls ────────────────────────────────────────────────
    ImGui::Checkbox(UI(ps, "\u7eb9\u7406\u8d34\u56fe", "Texture"), &ps.eclipseShowTexture);
    ImGui::SameLine();
    if (!ps.eclipseShowTexture || !renderer.hasBoundaries()) ImGui::BeginDisabled();
    ImGui::Checkbox(UI(ps, "\u884c\u653f\u533a\u57df", "Admin borders"), &ps.eclipseShowBoundaries);
    if (!ps.eclipseShowTexture || !renderer.hasBoundaries()) ImGui::EndDisabled();
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
    ImVec2 coneTip(origin.x + side*0.94f, blocker.y);
    dl->AddTriangleFilled(ImVec2(sun.x, sun.y-sunR), ImVec2(sun.x, sun.y+sunR), coneTip,
                          IM_COL32(255,205,80,24));
    dl->AddTriangleFilled(ImVec2(blocker.x, blocker.y-blockerR),
                          ImVec2(blocker.x, blocker.y+blockerR), coneTip,
                          IM_COL32(12,12,24,190));
    dl->AddLine(ImVec2(blocker.x, blocker.y-blockerR), coneTip, IM_COL32(125,145,190,180), 1.3f);
    dl->AddLine(ImVec2(blocker.x, blocker.y+blockerR), coneTip, IM_COL32(125,145,190,180), 1.3f);
    dl->AddLine(ImVec2(sun.x, sun.y-sunR), ImVec2(target.x,target.y+targetR), IM_COL32(255,205,90,90), 1.0f);
    dl->AddLine(ImVec2(sun.x, sun.y+sunR), ImVec2(target.x,target.y-targetR), IM_COL32(255,205,90,90), 1.0f);

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
        ps.eclipseGlobeYaw = (float)-event.centerLongitudeDeg;
        ps.eclipseGlobePitch = (float)-event.centerLatitudeDeg * 0.35f;
    } else {
        ps.eclipsePath.clear();
    }
}

static void DrawEclipseContent(Renderer& renderer, Scene& scene, PanelState& ps) {
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
    ImGui::SetNextItemWidth(130.0f);
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
    if (ImGui::BeginChild("##eclipse_results", ImVec2(0, 116), true)) {
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
        if (!ps.eclipseDemoActive) ps.eclipseSavedSpeed = scene.clock().speedDaysPerSec;
        ps.eclipseDemoActive = true;
        scene.clock().speedDaysPerSec = 1.0f / 1440.0f; // one simulated minute per real second
        scene.clock().playing = true;
    }
    ImGui::SameLine();
    if (ImGui::Button(scene.clock().playing ? UI(ps,"\u6682\u505c","Pause") : UI(ps,"\u7ee7\u7eed","Resume")))
        scene.clock().playing = !scene.clock().playing;
    if (ps.eclipseDemoActive && SceneUtcToTd(scene) > last) {
        scene.clock().playing = false;
        scene.clock().speedDaysPerSec = ps.eclipseSavedSpeed;
        ps.eclipseDemoActive = false;
    }
    float progress = last > first ? (float)((SceneUtcToTd(scene)-first)/(last-first)) : 0.0f;
    progress = std::clamp(progress, 0.0f, 1.0f);
    ImGui::ProgressBar(progress, ImVec2(-FLT_MIN, 8.0f), "");

    const char* modesZh[] = {"3D \u98df\u5f71", "\u4e09\u4f53\u5149\u9525"};
    const char* modesEn[] = {"3D shadow", "Three-body light cone"};
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::Combo("##eclipse_view", &ps.eclipseViewMode, ps.useChinese ? modesZh : modesEn, 2);
    float side = std::clamp(ImGui::GetContentRegionAvail().x, 180.0f, 520.0f);
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

    ImGui::EndMainMenuBar();
}

    // UI section.
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
    SimClock& clk = scene.clock();
    Date cur = localDateFromUtcJD(clk.jd, ps.timezoneHours);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.075f, 0.095f, 0.145f, 0.90f));
    ImGui::BeginChild("##clock_card", ImVec2(0, 94), true, ImGuiWindowFlags_NoScrollbar);
    ImGui::TextDisabled("%s", UI(ps, "\u6a21\u62df\u65f6\u95f4", "Simulation time"));
    ImGui::TextColored({0.72f,0.90f,1.0f,1.0f},
        "%04d-%02d-%02d  %02d:%02d", cur.Y, cur.M, cur.D, cur.h, cur.m);
    ImGui::Spacing();

    // Play/Pause + Today buttons with drawn icons
    {
        float gap  = ImGui::GetStyle().ItemSpacing.x;
        float sz   = 28.0f; // icon button size
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
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();

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
    SectionHeader(ps, "\u8df3\u8f6c\u65e5\u671f", "Jump date");
    DrawDateFields(ps, "jmp", ps.year, ps.month, ps.day);
    if (IconButton("##jmp", 24.0f, IM_COL32(180,210,255,230),
            [](ImDrawList* d, ImVec2 p, float s, ImU32 c){ DrawIconJump(d,p,s,c); }))
        clk.jd = utcJDFromLocalDate(Date{ps.year, ps.month, ps.day, 12, 0, 0.0},
                        ps.timezoneHours);

    // UI section.
    ImGui::Spacing();
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
        dl->AddCircleFilled(ImVec2(dot.x + 7.0f, dot.y + 8.0f), 5.0f,
                            IM_COL32((int)(b.color[0] * 255), (int)(b.color[1] * 255),
                                     (int)(b.color[2] * 255), 255), 18);
        ImGui::Indent(18.0f);
        ImGui::TextColored({b.color[0], b.color[1], b.color[2], 1.0f},
                           "%s", BodyLabel(ps, b));
        ImGui::Unindent(18.0f);
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

    ImGui::End();
}

    // UI section.
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

    ImVec2 avail = ImGui::GetContentRegionAvail();
    int w = (int)avail.x, h = (int)avail.y;
    if (w < 16) w = 16; if (h < 16) h = 16;
    renderer.resize(w, h);
    renderer.render(scene, cam, ropt);

    ImVec2 origin = ImGui::GetCursorScreenPos();
    ImGui::Image((ImTextureID)(intptr_t)renderer.colorTexture(), avail,
                 ImVec2(0,1), ImVec2(1,0));

    bool isDragging = ImGui::IsMouseDragging(ImGuiMouseButton_Left) ||
                      ImGui::IsMouseDragging(ImGuiMouseButton_Right);
    auto hitBodyAtMouse = [&]() -> int {
        float mx = io.MousePos.x - origin.x;
        float my = io.MousePos.y - origin.y;
        const auto& states = scene.states();
        float minD = 46.0f;
        int hit = -1;
        for (size_t i = 0; i < states.size(); ++i) {
            float px, py;
            if (!gx::projectToScreen(renderer.viewProj(), states[i].world,
                                     (float)w, (float)h, px, py)) continue;
            float pickR = std::clamp(states[i].displayRadius * 18.0f / std::max(cam.distance, 0.25f),
                                     0.0f, 26.0f);
            float d = std::sqrt((mx - px) * (mx - px) + (my - py) * (my - py)) - pickR;
            if (d < minD) { minD = d; hit = (int)i; }
        }
        return hit;
    };

    if (ImGui::IsItemHovered()) {
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Left))
            cam.rotate(io.MouseDelta.x * 0.01f, io.MouseDelta.y * 0.01f);
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Right))
            cam.pan(io.MouseDelta.x, io.MouseDelta.y);
        if (io.MouseWheel != 0.0f)
            cam.zoom(io.MouseWheel > 0 ? 0.9f : 1.1f);

        // Click-to-select: fire on mouse release if total drag distance < 5 px
        if (!isDragging && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
            ImVec2 dd = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left);
            if (dd.x*dd.x + dd.y*dd.y < 25.0f) {
                int hit = hitBodyAtMouse();
                if (hit >= 0) {
                    ps.selectedBody = hit;
                    ps.activeTab    = 0; // jump to params tab
                    const BodyState& s = scene.states()[hit];
                    const BodyInfo& b = scene.bodies()[hit];
                    cam.focusOn(s.world, std::max(s.displayRadius, b.isSun ? 1.35f : 0.22f));
                }
            }
        }

        // Double-click: focus camera on selected body
        if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            int hit = hitBodyAtMouse();
            if (hit >= 0) ps.selectedBody = hit;
            if (ps.selectedBody >= 0 && ps.selectedBody < (int)scene.states().size()) {
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
        ImVec2 pos{origin.x + 10, origin.y + 9};
        ImVec2 pad{10.0f, 7.0f};
        dl->AddRectFilled(pos, ImVec2(pos.x + ts.x + pad.x * 2.0f,
                                      pos.y + ts.y + pad.y * 2.0f),
                          IM_COL32(8, 15, 28, 176), 5.0f);
        dl->AddRect(pos, ImVec2(pos.x + ts.x + pad.x * 2.0f,
                                pos.y + ts.y + pad.y * 2.0f),
                    IM_COL32(92, 137, 190, 95), 5.0f, 0, 1.0f);
        dl->AddText(ImVec2(pos.x + pad.x, pos.y + pad.y),
                    IM_COL32(188, 224, 255, 238), buf);
    }
    // UI section.
    {
        char buf[80];
        std::snprintf(buf, sizeof(buf), "build:obj-mesh  mesh:%d  tex:%d",
                      renderer.loadedMeshes(), renderer.loadedTextures());
        ImVec2 ts = ImGui::CalcTextSize(buf);
        ImVec2 pad{8.0f, 5.0f};
        ImVec2 pos{origin.x + (float)w - ts.x - pad.x * 2.0f - 10.0f,
                   origin.y + (float)h - ts.y - pad.y * 2.0f - 9.0f};
        dl->AddRectFilled(pos, ImVec2(pos.x + ts.x + pad.x * 2.0f,
                                      pos.y + ts.y + pad.y * 2.0f),
                          IM_COL32(6, 12, 22, 145), 4.0f);
        dl->AddText(ImVec2(pos.x + pad.x, pos.y + pad.y),
                    IM_COL32(138, 214, 154, 210), buf);
    }

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

    if (ps.activeTab < 0 || ps.activeTab > 6) ps.activeTab = 0;
    float gap = ImGui::GetStyle().ItemSpacing.x;
    float tabW = (ImGui::GetContentRegionAvail().x - gap * 2.0f) / 3.0f;
    if (tabW < 72.0f) tabW = 72.0f;
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.0f, 7.0f));
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
        if (ImGui::Button(tabNames[t], ImVec2(tabW, 0))) ps.activeTab = t;
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
                            true, ps.leftPanelWidth, kSideMinW, kSideMaxW);
    }
    if (!ps.toolsCollapsed) {
        DrawSplitterOverlay("##tools_splitter", io.DisplaySize.x - toolsPanelWidth(ps), menuH, h,
                            false, ps.toolsPanelWidth, kToolsMinW, kToolsMaxW);
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
    }
    ps.timezoneHours = std::clamp(ps.timezoneHours, -12.0f, 14.0f);
    ps.timezoneHours = std::round(ps.timezoneHours * 4.0f) / 4.0f;
    ps.speedUnit = std::clamp(ps.speedUnit, 0, 2);
    if (!std::isfinite(ps.speedAmount)) ps.speedAmount = 5.0f;
    ps.observerLongitude = std::clamp(ps.observerLongitude, -180.0, 180.0);
    ps.observerLatitude = std::clamp(ps.observerLatitude, -90.0, 90.0);
    ps.eclipseViewMode = std::clamp(ps.eclipseViewMode, 0, 1);
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
}

} // namespace sx
