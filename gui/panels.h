// ImGui panels: menu bar, left sidebar, 3D viewport, and a single tabbed
// "tools" window that hosts all the calendar/ephemeris/terms/bazi/moonphase
// content panels.
#ifndef SXWNL_GUI_PANELS_H
#define SXWNL_GUI_PANELS_H

#include <string>
#include <vector>
#include "camera.h"
#include "renderer.h"
#include "scene.h"
#include "imgui.h"
#include "../eph/eclipse.h"

namespace sx {

// Icon drawing helpers.
// All icons are drawn via ImDrawList so no external font or SVG file is needed.

// Play triangle (pointing right)
inline void DrawIconPlay(ImDrawList* dl, ImVec2 p, float sz, ImU32 col) {
    dl->AddTriangleFilled(
        ImVec2(p.x + sz*0.18f, p.y + sz*0.12f),
        ImVec2(p.x + sz*0.18f, p.y + sz*0.88f),
        ImVec2(p.x + sz*0.88f, p.y + sz*0.50f), col);
}
// Pause (two vertical bars)
inline void DrawIconPause(ImDrawList* dl, ImVec2 p, float sz, ImU32 col) {
    float bw = sz * 0.22f, g = sz * 0.18f;
    float x1 = p.x + sz*0.18f, x2 = p.x + sz*0.60f;
    float y0 = p.y + sz*0.14f, y1 = p.y + sz*0.86f;
    dl->AddRectFilled(ImVec2(x1, y0), ImVec2(x1+bw, y1), col);
    dl->AddRectFilled(ImVec2(x2, y0), ImVec2(x2+bw, y1), col);
}
// Home / "today" icon (house silhouette)
inline void DrawIconHome(ImDrawList* dl, ImVec2 p, float sz, ImU32 col) {
    float cx = p.x + sz*0.50f;
    // roof triangle
    dl->AddTriangleFilled(
        ImVec2(cx,         p.y + sz*0.12f),
        ImVec2(p.x+sz*0.10f, p.y + sz*0.52f),
        ImVec2(p.x+sz*0.90f, p.y + sz*0.52f), col);
    // body rectangle
    dl->AddRectFilled(
        ImVec2(p.x+sz*0.22f, p.y+sz*0.50f),
        ImVec2(p.x+sz*0.78f, p.y+sz*0.88f), col);
    // door cutout (slightly darker / transparent)
    ImU32 door = IM_COL32(0, 0, 0, 90);
    dl->AddRectFilled(
        ImVec2(cx-sz*0.11f, p.y+sz*0.60f),
        ImVec2(cx+sz*0.11f, p.y+sz*0.88f), door);
}
// Jump-to-date: an arrow pointing right into a vertical line
inline void DrawIconJump(ImDrawList* dl, ImVec2 p, float sz, ImU32 col) {
    float my = p.y + sz*0.50f;
    // shaft
    dl->AddLine(ImVec2(p.x+sz*0.12f, my), ImVec2(p.x+sz*0.68f, my), col, sz*0.14f);
    // arrowhead
    dl->AddTriangleFilled(
        ImVec2(p.x+sz*0.55f, my - sz*0.28f),
        ImVec2(p.x+sz*0.55f, my + sz*0.28f),
        ImVec2(p.x+sz*0.88f, my), col);
    // vertical bar at right
    dl->AddRectFilled(
        ImVec2(p.x+sz*0.82f, p.y+sz*0.16f),
        ImVec2(p.x+sz*0.94f, p.y+sz*0.84f), col);
}

// Render a button whose content is drawn by a callback.
// Returns true if clicked (same semantics as ImGui::Button).
template<typename DrawFn>
inline bool IconButton(const char* id, float sz, ImU32 iconColor, DrawFn draw) {
    ImVec2 pos  = ImGui::GetCursorScreenPos();
    bool   hit  = ImGui::InvisibleButton(id, ImVec2(sz, sz));
    bool   hov  = ImGui::IsItemHovered();
    bool   act  = ImGui::IsItemActive();

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImU32 bg = act ? IM_COL32(60,90,150,220)
             : hov ? IM_COL32(45,70,120,200)
                   : IM_COL32(28,42,75, 180);
    float rnd = 4.0f;
    dl->AddRectFilled(pos, ImVec2(pos.x+sz, pos.y+sz), bg, rnd);
    dl->AddRect      (pos, ImVec2(pos.x+sz, pos.y+sz),
                      IM_COL32(80,120,200,120), rnd, 0, 1.0f);
    draw(dl, pos, sz, hov ? IM_COL32(200,220,255,255) : iconColor);
    return hit;
}


struct PanelState {
    // Shared date inputs
    int year = 2024, month = 1, day = 1, hour = 12, minute = 0;
    int calYear = 2024, calMonth = 1;
    int selectedCalendarDay = 1;
    int termYear = 2024;
    int ephBodyIdx = 0;
    bool ephFollowSelection = false;
    int selectedBody = 3;
    bool useChinese = true;
    bool leftCollapsed = false;
    bool toolsCollapsed = false;
    float leftPanelWidth = 250.0f;
    float toolsPanelWidth = 380.0f;
    float moonPhaseYaw = 0.0f;
    float moonPhasePitch = 0.0f;

    // Local-time display and continuous playback preset.
    float timezoneHours = 8.0f;
    int speedUnit = 2;              // 0=seconds, 1=hours, 2=days per real second
    float speedAmount = 5.0f;

    // Active tab index in DrawToolsPanel (0=params, 1=calendar, 5=moonphase, 6=eclipse)
    int activeTab = 0;

    // Cached engine outputs
    long long calSig  = -1;   std::string calErr;
    long long ephSig  = -1;   std::string ephText;
    long long termSig = -1;   std::string termText;
    long long baziSig = -1;   std::string baziText, shengjiangText;
    int baziJSIdx = 0;   std::vector<std::string> baziJSItems;

    // Eclipse search, observer and visualization state.
    int eclipseYear = 2026, eclipseMonth = 1, eclipseCount = 12;
    int eclipseFilter = 0; // 0=all, 1=solar, 2=lunar
    int selectedEclipse = -1;
    double observerLongitude = 116.383333;
    double observerLatitude = 39.9;
    double observerAltitudeKm = 0.0;
    bool eclipseNasaRadius = false;
    std::vector<EclipseEvent> eclipseEvents;
    std::vector<EclipsePathSample> eclipsePath;
    float eclipseGlobeYaw = 0.0f;
    float eclipseGlobePitch = 0.0f;
    float eclipseSpaceYaw = -10.0f;
    float eclipseSpacePitch = 12.0f;
    int eclipseViewMode = 0; // 0=globe/shadow, 1=three-body light cone
    bool eclipseDemoActive = false;
    float eclipseSavedSpeed = 5.0f;
    bool eclipseShowTexture    = true;   // 3D textured Earth in globe view
    bool eclipseShowBoundaries = false;  // admin boundary overlay (needs world_b.bin)
};

// Top menu bar (call before any window so it sits above the dockspace).
void DrawMainMenuBar(Scene& scene, RenderOptions& ropt, PanelState& ps);

// Left collapsible control sidebar.
void DrawSidebar(Scene& scene, RenderOptions& ropt, PanelState& ps, gx::OrbitCamera& cam);

// 3D viewport panel (unchanged from v1).
void DrawViewportPanel(Renderer& renderer, Scene& scene, gx::OrbitCamera& cam,
                       RenderOptions& ropt, PanelState& ps);

// Single window with tab bar containing all tool panels.
// Renderer is needed so the moon-phase tab can call renderMoonPhase().
void DrawToolsPanel(Renderer& renderer, Scene& scene, PanelState& ps);

// Transparent splitters drawn above panels so side widths can be dragged reliably.
void DrawPanelSplitters(PanelState& ps);

// Project-specific settings persisted beside imgui's layout ini.
void LoadAppSettings(RenderOptions& ropt, PanelState& ps);
void SaveAppSettings(const RenderOptions& ropt, const PanelState& ps);

} // namespace sx
#endif
