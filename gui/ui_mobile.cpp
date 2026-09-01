#include "ui_mobile.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

#include "imgui.h"
#include "imgui_internal.h"   // ClearActiveID: hand a press over to the scroller

namespace sx {

// ---------------------------------------------------------------------------
//  Safe area
// ---------------------------------------------------------------------------
static float g_insetL = 0.0f, g_insetT = 0.0f, g_insetR = 0.0f, g_insetB = 0.0f;

void SetSafeAreaInsets(float left, float top, float right, float bottom) {
    auto sane = [](float v) { return (v > 0.0f && v < 400.0f) ? v : 0.0f; };
    g_insetL = sane(left);
    g_insetT = sane(top);
    g_insetR = sane(right);
    g_insetB = sane(bottom);
}

// ---------------------------------------------------------------------------
//  Pages
// ---------------------------------------------------------------------------
enum MobilePage {
    PG_SOLAR = 0, PG_CALENDAR, PG_EPHEM, PG_MOON, PG_ECLIPSE,
    PG_TERMS, PG_BAZI, PG_PARAMS, PG_SETTINGS, PG_HELP, PG_COUNT
};

enum IconKind {
    IC_SUN, IC_CALENDAR, IC_ORBIT, IC_MOON, IC_ECLIPSE,
    IC_TERMS, IC_GRID, IC_SLIDERS, IC_GEAR, IC_HELP, IC_MORE, IC_BACK
};

struct PageDef {
    const char* zh;      // page title, shown in the top bar
    const char* en;
    const char* navZh;   // short form, shown under the rail icon
    const char* navEn;
    IconKind    icon;
};

static const PageDef kPages[PG_COUNT] = {
    {"太阳系",       "Solar system", "太阳系", "Solar",  IC_SUN},
    {"农历历法", "Calendar",     "农历",       "Cal",    IC_CALENDAR},
    {"行星星历", "Ephemeris",    "星历",       "Eph",    IC_ORBIT},
    {"月相",             "Moon phase",   "月相",       "Moon",   IC_MOON},
    {"日月食",       "Eclipses",     "日月食", "Eclip",  IC_ECLIPSE},
    {"节气朔望", "Solar terms",  "节气",       "Terms",  IC_TERMS},
    {"八字升降", "Bazi",         "八字",       "Bazi",   IC_GRID},
    {"运行参数", "Parameters",   "参数",       "Params", IC_SLIDERS},
    {"设置",             "Settings",     "设置",       "Set",    IC_GEAR},
    {"帮助",             "Help",         "帮助",       "Help",   IC_HELP},
};

// Pages reachable straight from the navigation rail. Everything else lives one
// tap deeper, behind "more", so the rail keeps finger-sized targets.
static const int kPrimary[]   = {PG_SOLAR, PG_CALENDAR, PG_EPHEM, PG_MOON, PG_ECLIPSE};
static const int kPrimaryN    = (int)(sizeof(kPrimary) / sizeof(kPrimary[0]));
static const int kSecondary[] = {PG_TERMS, PG_BAZI, PG_PARAMS, PG_SETTINGS, PG_HELP};
static const int kSecondaryN  = (int)(sizeof(kSecondary) / sizeof(kSecondary[0]));

// Swipe order: the rail pages, then the "more" pages, so a left/right flick
// walks the whole app in a predictable sequence.
static int SwipeOrderIndex(int page) {
    for (int i = 0; i < kPrimaryN; ++i)   if (kPrimary[i] == page)   return i;
    for (int i = 0; i < kSecondaryN; ++i) if (kSecondary[i] == page) return kPrimaryN + i;
    return 0;
}
static int SwipeOrderPage(int index) {
    int n = kPrimaryN + kSecondaryN;
    index = (index % n + n) % n;
    return index < kPrimaryN ? kPrimary[index] : kSecondary[index - kPrimaryN];
}

// ---------------------------------------------------------------------------
//  Icons
// ---------------------------------------------------------------------------
// Drawn with ImDrawList rather than an icon font: the bundled CJK font has no
// symbol glyphs, and shipping a second atlas for a dozen shapes is not worth it.
static void DrawNavIcon(ImDrawList* dl, IconKind kind, ImVec2 c, float sz, ImU32 col) {
    const float r = sz * 0.5f;
    const float th = std::max(1.5f, sz * 0.09f);
    switch (kind) {
    case IC_SUN:
        dl->AddCircleFilled(c, r * 0.52f, col, 20);
        for (int i = 0; i < 8; ++i) {
            float a = (float)i * 3.14159265f / 4.0f;
            ImVec2 p0(c.x + std::cos(a) * r * 0.72f, c.y + std::sin(a) * r * 0.72f);
            ImVec2 p1(c.x + std::cos(a) * r * 1.00f, c.y + std::sin(a) * r * 1.00f);
            dl->AddLine(p0, p1, col, th);
        }
        break;
    case IC_CALENDAR: {
        ImVec2 a(c.x - r * 0.82f, c.y - r * 0.66f), b(c.x + r * 0.82f, c.y + r * 0.86f);
        dl->AddRect(a, b, col, sz * 0.12f, 0, th);
        dl->AddLine(ImVec2(a.x, a.y + sz * 0.26f), ImVec2(b.x, a.y + sz * 0.26f), col, th);
        dl->AddLine(ImVec2(c.x - r * 0.42f, a.y - sz * 0.12f),
                    ImVec2(c.x - r * 0.42f, a.y + sz * 0.06f), col, th);
        dl->AddLine(ImVec2(c.x + r * 0.42f, a.y - sz * 0.12f),
                    ImVec2(c.x + r * 0.42f, a.y + sz * 0.06f), col, th);
        for (int i = 0; i < 2; ++i)
            for (int j = 0; j < 3; ++j)
                dl->AddCircleFilled(ImVec2(a.x + sz * (0.26f + 0.24f * j),
                                           a.y + sz * (0.42f + 0.22f * i)),
                                    th * 0.75f, col, 8);
        break;
    }
    case IC_ORBIT: {
        // Hand-rolled tilted ellipse: ImDrawList::AddEllipse only exists in
        // newer Dear ImGui releases and this has to build against both pins.
        const float ca = 0.906f, sa = -0.423f;   // cos/sin of -25 degrees
        for (int i = 0; i < 28; ++i) {
            float t0 = (float)i * 6.2831853f / 28.0f;
            float t1 = (float)(i + 1) * 6.2831853f / 28.0f;
            float x0 = std::cos(t0) * r * 0.98f, y0 = std::sin(t0) * r * 0.46f;
            float x1 = std::cos(t1) * r * 0.98f, y1 = std::sin(t1) * r * 0.46f;
            dl->AddLine(ImVec2(c.x + x0 * ca - y0 * sa, c.y + x0 * sa + y0 * ca),
                        ImVec2(c.x + x1 * ca - y1 * sa, c.y + x1 * sa + y1 * ca), col, th);
        }
        dl->AddCircleFilled(c, r * 0.26f, col, 16);
        dl->AddCircleFilled(ImVec2(c.x + r * 0.86f, c.y - r * 0.30f), th * 1.3f, col, 10);
        break;
    }
    case IC_MOON: {
        dl->AddCircleFilled(c, r * 0.86f, col, 28);
        // Bite a crescent out of it using the panel background colour.
        dl->AddCircleFilled(ImVec2(c.x + r * 0.40f, c.y - r * 0.18f), r * 0.74f,
                            ImGui::GetColorU32(ImGuiCol_WindowBg), 28);
        break;
    }
    case IC_ECLIPSE:
        dl->AddCircle(c, r * 0.88f, col, 28, th);
        dl->AddCircleFilled(ImVec2(c.x + r * 0.30f, c.y), r * 0.56f, col, 24);
        break;
    case IC_TERMS:
        dl->AddCircle(c, r * 0.88f, col, 28, th);
        for (int i = 0; i < 4; ++i) {
            float a = (float)i * 3.14159265f / 2.0f;
            dl->AddLine(ImVec2(c.x + std::cos(a) * r * 0.60f, c.y + std::sin(a) * r * 0.60f),
                        ImVec2(c.x + std::cos(a) * r * 0.88f, c.y + std::sin(a) * r * 0.88f),
                        col, th);
        }
        dl->AddLine(c, ImVec2(c.x, c.y - r * 0.52f), col, th);
        break;
    case IC_GRID:
        for (int i = 0; i < 2; ++i)
            for (int j = 0; j < 2; ++j) {
                ImVec2 a(c.x + (i ? sz * 0.06f : -sz * 0.44f), c.y + (j ? sz * 0.06f : -sz * 0.44f));
                dl->AddRect(a, ImVec2(a.x + sz * 0.38f, a.y + sz * 0.38f), col, sz * 0.07f, 0, th);
            }
        break;
    case IC_SLIDERS:
        for (int i = 0; i < 3; ++i) {
            float y = c.y + sz * (-0.30f + 0.30f * i);
            dl->AddLine(ImVec2(c.x - r * 0.88f, y), ImVec2(c.x + r * 0.88f, y), col, th);
            dl->AddCircleFilled(ImVec2(c.x + r * (i == 1 ? 0.42f : -0.30f), y), th * 1.6f, col, 12);
        }
        break;
    case IC_GEAR:
        dl->AddCircle(c, r * 0.52f, col, 20, th);
        for (int i = 0; i < 6; ++i) {
            float a = (float)i * 3.14159265f / 3.0f;
            dl->AddLine(ImVec2(c.x + std::cos(a) * r * 0.60f, c.y + std::sin(a) * r * 0.60f),
                        ImVec2(c.x + std::cos(a) * r * 0.94f, c.y + std::sin(a) * r * 0.94f),
                        col, th * 1.4f);
        }
        break;
    case IC_HELP:
        dl->AddCircle(c, r * 0.88f, col, 26, th);
        dl->AddLine(ImVec2(c.x - r * 0.26f, c.y - r * 0.28f),
                    ImVec2(c.x + r * 0.20f, c.y - r * 0.40f), col, th);
        dl->AddLine(ImVec2(c.x + r * 0.20f, c.y - r * 0.40f),
                    ImVec2(c.x, c.y + r * 0.06f), col, th);
        dl->AddCircleFilled(ImVec2(c.x, c.y + r * 0.48f), th * 0.9f, col, 8);
        break;
    case IC_MORE:
        for (int i = 0; i < 3; ++i)
            dl->AddCircleFilled(ImVec2(c.x + sz * (-0.26f + 0.26f * i), c.y), th * 1.3f, col, 10);
        break;
    case IC_BACK:
        dl->AddLine(ImVec2(c.x + r * 0.34f, c.y - r * 0.62f), ImVec2(c.x - r * 0.34f, c.y), col, th * 1.3f);
        dl->AddLine(ImVec2(c.x - r * 0.34f, c.y), ImVec2(c.x + r * 0.34f, c.y + r * 0.62f), col, th * 1.3f);
        break;
    }
}

// ---------------------------------------------------------------------------
//  Touch-sized widgets
// ---------------------------------------------------------------------------
static bool NavButton(const char* id, IconKind icon, const char* label,
                      bool selected, ImVec2 size) {
    ImVec2 p = ImGui::GetCursorScreenPos();
    bool hit = ImGui::InvisibleButton(id, size);
    bool held = ImGui::IsItemActive();

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImU32 fg = selected ? IM_COL32(150, 205, 255, 255)
             : held     ? IM_COL32(190, 215, 245, 255)
                        : IM_COL32(132, 150, 178, 255);
    if (selected || held) {
        dl->AddRectFilled(p, ImVec2(p.x + size.x, p.y + size.y),
                          selected ? IM_COL32(28, 54, 96, 210) : IM_COL32(24, 38, 64, 170),
                          UiS(8.0f));
    }
    float iconSz = std::min(size.y * 0.42f, UiS(26.0f));
    float textH  = ImGui::GetTextLineHeight();
    float blockH = iconSz + UiS(4.0f) + textH;
    float top    = p.y + (size.y - blockH) * 0.5f;
    DrawNavIcon(dl, icon, ImVec2(p.x + size.x * 0.5f, top + iconSz * 0.5f), iconSz, fg);

    float tw = ImGui::CalcTextSize(label).x;
    dl->AddText(ImVec2(p.x + (size.x - tw) * 0.5f, top + iconSz + UiS(4.0f)), fg, label);
    if (selected) {
        dl->AddRectFilled(ImVec2(p.x + size.x * 0.30f, p.y + size.y - UiS(3.0f)),
                          ImVec2(p.x + size.x * 0.70f, p.y + size.y),
                          IM_COL32(96, 170, 255, 235), UiS(2.0f));
    }
    return hit;
}

// Full-width row used by the "more" sheet.
static bool ListRow(const char* id, IconKind icon, const char* label, bool selected) {
    float h = UiS(52.0f);
    ImVec2 p = ImGui::GetCursorScreenPos();
    float w = ImGui::GetContentRegionAvail().x;
    bool hit = ImGui::InvisibleButton(id, ImVec2(w, h));
    bool held = ImGui::IsItemActive();

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImU32 bg = held ? IM_COL32(34, 56, 96, 230)
             : selected ? IM_COL32(24, 42, 74, 210)
                        : IM_COL32(16, 24, 40, 190);
    dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), bg, UiS(8.0f));
    ImU32 fg = selected ? IM_COL32(150, 205, 255, 255) : IM_COL32(206, 218, 238, 255);
    DrawNavIcon(dl, icon, ImVec2(p.x + UiS(30.0f), p.y + h * 0.5f), UiS(24.0f), fg);
    dl->AddText(ImVec2(p.x + UiS(58.0f), p.y + (h - ImGui::GetTextLineHeight()) * 0.5f),
                fg, label);
    return hit;
}

static bool IconOnlyButton(const char* id, IconKind icon, float sz) {
    ImVec2 p = ImGui::GetCursorScreenPos();
    bool hit = ImGui::InvisibleButton(id, ImVec2(sz, sz));
    bool held = ImGui::IsItemActive();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (held)
        dl->AddRectFilled(p, ImVec2(p.x + sz, p.y + sz), IM_COL32(38, 62, 105, 220), UiS(6.0f));
    DrawNavIcon(dl, icon, ImVec2(p.x + sz * 0.5f, p.y + sz * 0.5f), sz * 0.62f,
                held ? IM_COL32(225, 238, 255, 255) : IM_COL32(178, 204, 238, 255));
    return hit;
}

// ---------------------------------------------------------------------------
//  Drag-to-scroll and swipe paging
// ---------------------------------------------------------------------------
// ImGui has no touch scrolling of its own: a finger drag looks exactly like a
// left-mouse drag, so it either does nothing or gets swallowed by whatever
// widget happened to be under the finger. This tracks the gesture, decides once
// whether it is a vertical scroll or a horizontal page swipe, and on a scroll
// takes the press away from that widget (ClearActiveID) so the content moves
// instead of the button firing.
struct DragState {
    bool   active = false;
    bool   decided = false;
    bool   horizontal = false;
    bool   startedOnWidget = false;
    ImVec2 start{0.0f, 0.0f};
    float  velocity = 0.0f;   // px/s, drives the flick coast
    float  swipeDx = 0.0f;
};
static DragState g_drag;

// Returns -1 / 0 / +1: how far to step through the swipe order this frame.
// Call once, inside the scrollable window, after its content has been emitted.
static int HandleGestures(bool allowSwipe, bool allowScroll) {
    ImGuiIO& io = ImGui::GetIO();
    const float decideAt = UiS(12.0f);
    const float swipeAt  = UiS(80.0f);
    int step = 0;

    bool hovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows |
                                          ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
    if (io.MouseDown[0]) {
        if (!g_drag.active && hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            g_drag = DragState{};
            g_drag.active = true;
            g_drag.start = io.MousePos;
            // A press that landed on a control keeps that control: dragging a
            // slider sideways must move the slider, not flip the page.
            g_drag.startedOnWidget = ImGui::IsAnyItemActive();
        }
        if (g_drag.active) {
            ImVec2 d(io.MousePos.x - g_drag.start.x, io.MousePos.y - g_drag.start.y);
            if (!g_drag.decided &&
                (std::fabs(d.x) > decideAt || std::fabs(d.y) > decideAt)) {
                g_drag.decided = true;
                g_drag.horizontal = std::fabs(d.x) > std::fabs(d.y) * 1.4f;
                bool takeOver = g_drag.horizontal
                    ? (allowSwipe && !g_drag.startedOnWidget)
                    : allowScroll;
                if (takeOver) ImGui::ClearActiveID();
                if (g_drag.horizontal && g_drag.startedOnWidget) g_drag.active = false;
            }
            if (g_drag.active && g_drag.decided) {
                if (g_drag.horizontal) {
                    if (allowSwipe) g_drag.swipeDx = d.x;
                } else if (allowScroll && io.MouseDelta.y != 0.0f) {
                    ImGui::SetScrollY(ImGui::GetScrollY() - io.MouseDelta.y);
                    g_drag.velocity = -io.MouseDelta.y / std::max(io.DeltaTime, 1e-4f);
                }
            }
        }
    } else {
        if (g_drag.active) {
            if (g_drag.decided && g_drag.horizontal && allowSwipe &&
                std::fabs(g_drag.swipeDx) > swipeAt) {
                step = g_drag.swipeDx < 0.0f ? 1 : -1;
                g_drag.velocity = 0.0f;
            }
            g_drag.active = false;
        }
        // Flick coast, decaying to a stop in roughly a third of a second.
        if (allowScroll && std::fabs(g_drag.velocity) > UiS(6.0f)) {
            ImGui::SetScrollY(ImGui::GetScrollY() + g_drag.velocity * io.DeltaTime);
            g_drag.velocity *= std::pow(0.002f, io.DeltaTime);
        } else {
            g_drag.velocity = 0.0f;
        }
    }
    return step;
}

// ---------------------------------------------------------------------------
//  Pages that only exist on mobile
// ---------------------------------------------------------------------------
static void DrawSettingsPage(Scene& scene, RenderOptions& ropt, PanelState& ps) {
    SectionHeader(ps, "界面", "Interface");
    ImGui::TextDisabled("%s", UI(ps, "文字大小", "Text size"));
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::SliderFloat("##font_scale", &ps.fontScale, 0.70f, 1.40f, "%.2f x");
    if (ImGui::Button(UI(ps, "恢复默认##fs", "Reset##fs")))
        ps.fontScale = 1.0f;

    ImGui::TextDisabled("%s", UI(ps, "语言", "Language"));
    if (ImGui::RadioButton("中文", ps.useChinese)) ps.useChinese = true;
    ImGui::SameLine();
    if (ImGui::RadioButton("English", !ps.useChinese)) ps.useChinese = false;

    // Only offered where the desktop shell is actually reachable.
    if (!GetTouchMode() && ps.mobilePreview) {
        if (ImGui::Button(UI(ps, "返回桌面布局", "Back to desktop layout")))
            ps.mobilePreview = false;
    }

    ImGui::Spacing();
    SectionHeader(ps, "时间与速度", "Time and speed");
    DrawTimeControls(scene, ps);
    DrawJumpDate(scene, ps);

    ImGui::Spacing();
    DrawDisplaySettings(scene, ropt, ps);
}

static void DrawHelpPage(PanelState& ps) {
    SectionHeader(ps, "操作说明", "Gestures");
    struct Row { const char* zh; const char* en; };
    static const Row rows[] = {
        {"单指拖动内容: 上下滚动",
         "One-finger drag: scroll the page"},
        {"左右滑动: 切换页面",
         "Swipe left/right: change page"},
        {"太阳系页单指拖动: 旋转视角",
         "Solar page, one finger: rotate the view"},
        {"双指开合: 缩放 3D 视图",
         "Pinch: zoom the 3D view"},
        {"双指拖动: 平移 3D 视角",
         "Two-finger drag: pan the 3D view"},
        {"点击天体: 选中并聚焦",
         "Tap a body: select and focus"},
        {"底部拉手: 展开/收起控制面板",
         "Bottom handle: show/hide the control sheet"},
    };
    for (const Row& r : rows) ImGui::BulletText("%s", UI(ps, r.zh, r.en));

    ImGui::Spacing();
    SectionHeader(ps, "关于", "About");
    ImGui::TextDisabled("SXWNL 3D  -  寿星万年历");
    ImGui::TextWrapped("%s", UI(ps,
        "天文算法与桌面版完全一致，"
        "仅界面布局针对触屏重新设计。",
        "The astronomy engine is identical to the desktop build; only the "
        "layout is redesigned for touch."));
}

// ---------------------------------------------------------------------------
//  Solar-system page: full-bleed 3D with a collapsible control sheet
// ---------------------------------------------------------------------------
static void DrawSolarSheet(Scene& scene, gx::OrbitCamera& cam, RenderOptions& ropt,
                           PanelState& ps, ImVec2 contentPos, ImVec2 contentSize,
                           bool landscape) {
    const float handle = UiS(34.0f);
    float sheetW, sheetH, sheetX, sheetY;

    if (landscape) {
        // Wide screen: dock the sheet on the trailing edge so the 3-D view keeps
        // its full height.
        sheetW = ps.mobileSheetOpen
               ? std::min(std::max(contentSize.x * 0.36f, UiS(260.0f)), UiS(380.0f))
               : handle;
        sheetH = contentSize.y;
        sheetX = contentPos.x + contentSize.x - sheetW;
        sheetY = contentPos.y;
    } else {
        sheetW = contentSize.x;
        sheetH = ps.mobileSheetOpen ? std::min(contentSize.y * 0.52f, UiS(360.0f)) : handle;
        sheetX = contentPos.x;
        sheetY = contentPos.y + contentSize.y - sheetH;
    }

    ImGui::SetNextWindowPos(ImVec2(sheetX, sheetY), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(sheetW, sheetH), ImGuiCond_Always);
    // Collapsed, the sheet is only a grab strip, so keep it near-opaque - a
    // translucent sliver over a black starfield is invisible.
    ImGui::PushStyleColor(ImGuiCol_WindowBg,
                          ps.mobileSheetOpen ? ImVec4(0.043f, 0.063f, 0.106f, 0.95f)
                                             : ImVec4(0.063f, 0.086f, 0.145f, 0.92f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                        landscape && !ps.mobileSheetOpen ? ImVec2(0.0f, UiS(4.0f))
                                                         : ImVec2(UiS(8.0f), UiS(4.0f)));
    ImGui::Begin("##solar_sheet", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                 ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar |
                 ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBringToFrontOnFocus);

    // Grab handle. Hit target spans the sheet so a thumb finds it without aiming:
    // the full width of a bottom sheet, the full height of a side one.
    {
        const bool sideDocked = landscape;
        ImVec2 p = ImGui::GetCursorScreenPos();
        float w = ImGui::GetContentRegionAvail().x;
        float h = sideDocked && !ps.mobileSheetOpen
                ? std::max(ImGui::GetContentRegionAvail().y, UiS(40.0f))
                : std::max(handle - UiS(8.0f), UiS(20.0f));
        if (ImGui::InvisibleButton("##sheet_handle", ImVec2(w, h)))
            ps.mobileSheetOpen = !ps.mobileSheetOpen;

        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImU32 col = ImGui::IsItemActive() ? IM_COL32(165, 205, 255, 240)
                                          : IM_COL32(120, 155, 205, 215);
        float cx = p.x + w * 0.5f, cy = p.y + h * 0.5f;
        // The pill runs along the edge the sheet slides out from, so it reads as
        // "pull this way": horizontal on a bottom sheet, vertical on a side one.
        if (sideDocked) {
            dl->AddRectFilled(ImVec2(cx - UiS(2.0f), cy - UiS(20.0f)),
                              ImVec2(cx + UiS(2.0f), cy + UiS(20.0f)), col, UiS(2.0f));
        } else {
            dl->AddRectFilled(ImVec2(cx - UiS(20.0f), cy - UiS(2.0f)),
                              ImVec2(cx + UiS(20.0f), cy + UiS(2.0f)), col, UiS(2.0f));
        }
        // Chevron pointing the way it will move, plus a label where there is room.
        float ax = sideDocked ? cx : cx + UiS(34.0f);
        float ay = sideDocked ? cy - UiS(34.0f) : cy;
        float d = UiS(5.0f);
        if (sideDocked) {
            float s2 = ps.mobileSheetOpen ? 1.0f : -1.0f;
            dl->AddTriangleFilled(ImVec2(ax, ay + s2 * d), ImVec2(ax - d, ay - s2 * d),
                                  ImVec2(ax + d, ay - s2 * d), col);
        } else {
            float s2 = ps.mobileSheetOpen ? 1.0f : -1.0f;
            dl->AddTriangleFilled(ImVec2(ax, ay + s2 * d), ImVec2(ax - d, ay - s2 * d),
                                  ImVec2(ax + d, ay - s2 * d), col);
        }
        if (!ps.mobileSheetOpen && !sideDocked) {
            const char* hint = UI(ps, "控制面板", "Controls");
            ImVec2 ts = ImGui::CalcTextSize(hint);
            dl->AddText(ImVec2(cx - UiS(34.0f) - ts.x, cy - ts.y * 0.5f), col, hint);
        }
    }

    if (ps.mobileSheetOpen) {
        ImGui::PushFont(UiFontSmall());
        ImGui::BeginChild("##sheet_scroll", ImVec2(0, 0), false);
        DrawClockCard(scene, ps);
        DrawTimeControls(scene, ps);
        DrawJumpDate(scene, ps);
        ImGui::Spacing();
        DrawDisplaySettings(scene, ropt, ps);
        DrawSelectedBodyInfo(scene, ps, cam);
        ImGui::Dummy(ImVec2(0, UiS(12.0f)));
        HandleGestures(false, true);
        ImGui::EndChild();
        ImGui::PopFont();
    }

    ImGui::End();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
}

// ---------------------------------------------------------------------------
//  Chrome
// ---------------------------------------------------------------------------
static void DrawTopBar(Scene& scene, PanelState& ps, float x, float y,
                       float w, float h, bool showBack, bool& moreOpen) {
    ImGui::SetNextWindowPos(ImVec2(x, y - g_insetT), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(w, h + g_insetT), ImGuiCond_Always);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.035f, 0.051f, 0.086f, 1.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(UiS(8.0f), UiS(4.0f)));
    ImGui::Begin("##topbar", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                 ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::SetCursorPosY(g_insetT + UiS(4.0f));

    float btn = UiS(34.0f);
    if (showBack) {
        if (IconOnlyButton("##back", IC_BACK, btn)) {
            ps.mobilePage = PG_SOLAR;
            moreOpen = false;
        }
        ImGui::SameLine();
    }

    const PageDef& pg = kPages[std::clamp(ps.mobilePage, 0, (int)PG_COUNT - 1)];
    const float rowTop = ImGui::GetCursorPosY();
    ImGui::PushFont(UiFontTitle());
    ImGui::SetCursorPosY(rowTop + (btn - ImGui::GetTextLineHeight()) * 0.5f);
    ImGui::TextColored(ImVec4(0.95f, 0.77f, 0.30f, 1.0f), "%s", UI(ps, pg.zh, pg.en));
    ImGui::PopFont();

    // Right side: live clock plus a play/pause toggle, reachable from any page.
    SimClock& clk = scene.clock();
    Date d = localDateFromUtcJD(clk.jd, ps.timezoneHours);
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d", d.Y, d.M, d.D, d.h, d.m);
    float tw = ImGui::CalcTextSize(buf).x;
    float rightW = tw + btn * 2.0f + UiS(26.0f);
    if (ImGui::GetContentRegionAvail().x > rightW) {
        ImGui::SameLine(w - rightW);
        ImGui::SetCursorPosY(rowTop + (btn - ImGui::GetTextLineHeight()) * 0.5f);
        ImGui::TextColored(ImVec4(0.72f, 0.90f, 1.0f, 0.92f), "%s", buf);

        ImGui::SameLine(0.0f, UiS(8.0f));
        ImGui::SetCursorPosY(rowTop);
        ImU32 icol = IM_COL32(180, 210, 255, 235);
        if (clk.playing) {
            if (IconButton("##topplay", btn, icol,
                    [](ImDrawList* dd, ImVec2 pp, float ss, ImU32 cc) { DrawIconPause(dd, pp, ss, cc); }))
                clk.playing = false;
        } else {
            if (IconButton("##topplay", btn, icol,
                    [](ImDrawList* dd, ImVec2 pp, float ss, ImU32 cc) { DrawIconPlay(dd, pp, ss, cc); }))
                clk.playing = true;
        }
        ImGui::SameLine(0.0f, UiS(4.0f));
        ImGui::SetCursorPosY(rowTop);
        if (IconOnlyButton("##topmore", IC_MORE, btn)) moreOpen = !moreOpen;
    }

    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddLine(ImVec2(x, y + h - 1.0f), ImVec2(x + w, y + h - 1.0f),
                IM_COL32(52, 76, 118, 180), 1.0f);
    ImGui::End();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
}

static void DrawNavRail(PanelState& ps, bool landscape, float x, float y,
                        float w, float h, bool& moreOpen) {
    ImGui::SetNextWindowPos(ImVec2(x, y), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(w, h), ImGuiCond_Always);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.035f, 0.051f, 0.086f, 1.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));
    ImGui::Begin("##navrail", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                 ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollWithMouse);

    const int n = kPrimaryN + 1;   // + the "more" entry
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (landscape) {
        dl->AddLine(ImVec2(x + w - 1.0f, y), ImVec2(x + w - 1.0f, y + h),
                    IM_COL32(52, 76, 118, 180), 1.0f);
        float itemW = w - g_insetL;
        float itemH = std::min((h - g_insetB) / (float)n, UiS(84.0f));
        ImGui::SetCursorPos(ImVec2(g_insetL, UiS(4.0f)));
        for (int i = 0; i < kPrimaryN; ++i) {
            int page = kPrimary[i];
            char id[32];
            std::snprintf(id, sizeof(id), "##nav%d", page);
            if (NavButton(id, kPages[page].icon, UI(ps, kPages[page].navZh, kPages[page].navEn),
                          ps.mobilePage == page, ImVec2(itemW, itemH))) {
                ps.mobilePage = page;
                moreOpen = false;
            }
            ImGui::SetCursorPosX(g_insetL);
        }
        if (NavButton("##navmore", IC_MORE, UI(ps, "更多", "More"),
                      moreOpen, ImVec2(itemW, itemH)))
            moreOpen = !moreOpen;
    } else {
        dl->AddLine(ImVec2(x, y), ImVec2(x + w, y), IM_COL32(52, 76, 118, 180), 1.0f);
        float itemW = (w - g_insetL - g_insetR) / (float)n;
        float itemH = h - g_insetB;
        ImGui::SetCursorPos(ImVec2(g_insetL, 0.0f));
        for (int i = 0; i < kPrimaryN; ++i) {
            int page = kPrimary[i];
            char id[32];
            std::snprintf(id, sizeof(id), "##nav%d", page);
            if (NavButton(id, kPages[page].icon, UI(ps, kPages[page].navZh, kPages[page].navEn),
                          ps.mobilePage == page, ImVec2(itemW, itemH))) {
                ps.mobilePage = page;
                moreOpen = false;
            }
            ImGui::SameLine(0.0f, 0.0f);
        }
        if (NavButton("##navmore", IC_MORE, UI(ps, "更多", "More"),
                      moreOpen, ImVec2(itemW, itemH)))
            moreOpen = !moreOpen;
    }

    ImGui::End();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
}

static void DrawMoreSheet(PanelState& ps, ImVec2 pos, ImVec2 size, bool& moreOpen) {
    float w = std::min(size.x - UiS(24.0f), UiS(420.0f));
    float rowH = UiS(52.0f) + ImGui::GetStyle().ItemSpacing.y;
    float h = std::min(size.y - UiS(24.0f), rowH * (float)kSecondaryN + UiS(56.0f));
    ImGui::SetNextWindowPos(ImVec2(pos.x + (size.x - w) * 0.5f, pos.y + UiS(12.0f)),
                            ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(w, h), ImGuiCond_Always);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.055f, 0.078f, 0.129f, 0.98f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, UiS(12.0f));
    ImGui::Begin("##more_sheet", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                 ImGuiWindowFlags_NoCollapse);
    SectionHeader(ps, "更多功能", "More tools");
    for (int i = 0; i < kSecondaryN; ++i) {
        int page = kSecondary[i];
        char id[32];
        std::snprintf(id, sizeof(id), "##more%d", page);
        if (ListRow(id, kPages[page].icon, UI(ps, kPages[page].zh, kPages[page].en),
                    ps.mobilePage == page)) {
            ps.mobilePage = page;
            moreOpen = false;
        }
    }
    ImGui::End();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();

    // Tapping anywhere outside dismisses the sheet.
    ImGuiIO& io = ImGui::GetIO();
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGui::IsAnyItemHovered()) {
        ImVec2 m = io.MousePos;
        ImVec2 a(pos.x + (size.x - w) * 0.5f, pos.y + UiS(12.0f));
        if (m.x < a.x || m.x > a.x + w || m.y < a.y || m.y > a.y + h) moreOpen = false;
    }
}

// ---------------------------------------------------------------------------
//  Entry point
// ---------------------------------------------------------------------------
void DrawMobileUI(Renderer& renderer, Scene& scene, gx::OrbitCamera& cam,
                  RenderOptions& ropt, PanelState& ps) {
    ImGuiIO& io = ImGui::GetIO();
    static bool s_moreOpen = false;

    ps.mobilePage = std::clamp(ps.mobilePage, 0, (int)PG_COUNT - 1);

    const ImVec2 disp = io.DisplaySize;
    const bool landscape = disp.x > disp.y * 1.15f;

    const float topH = UiS(44.0f);
    const float railW = landscape ? UiS(84.0f) + g_insetL : 0.0f;
    const float railH = landscape ? 0.0f : UiS(62.0f) + g_insetB;

    const float contentX = railW;
    const float contentY = topH + g_insetT;
    const float contentW = disp.x - railW - g_insetR;
    const float contentH = disp.y - contentY - railH - (landscape ? g_insetB : 0.0f);

    DrawTopBar(scene, ps, contentX, contentY, contentW, topH,
               ps.mobilePage >= PG_TERMS, s_moreOpen);

    if (landscape) DrawNavRail(ps, true, 0.0f, contentY, railW, disp.y - contentY, s_moreOpen);
    else           DrawNavRail(ps, false, 0.0f, disp.y - railH, disp.x, railH, s_moreOpen);

    const ImVec2 contentPos(contentX, contentY);
    const ImVec2 contentSize(contentW, contentH);

    if (ps.mobilePage == PG_SOLAR) {
        // Full-bleed 3-D: no window padding, no scrolling, the sheet floats over it.
        ImGui::SetNextWindowPos(contentPos, ImGuiCond_Always);
        ImGui::SetNextWindowSize(contentSize, ImGuiCond_Always);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::Begin("##solar_page", nullptr,
                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                     ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoBringToFrontOnFocus);
        DrawViewportContent(renderer, scene, cam, ropt, ps);
        ImGui::End();
        ImGui::PopStyleVar();

        DrawSolarSheet(scene, cam, ropt, ps, contentPos, contentSize, landscape);
    } else {
        ImGui::SetNextWindowPos(contentPos, ImGuiCond_Always);
        ImGui::SetNextWindowSize(contentSize, ImGuiCond_Always);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(UiS(10.0f), UiS(8.0f)));
        ImGui::Begin("##page", nullptr,
                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoScrollWithMouse |
                     ImGuiWindowFlags_NoBringToFrontOnFocus);

        // Two deliberate exceptions to the body size: dense numeric readouts drop
        // to the smaller face so a phone screen still shows a useful number of
        // rows, and the calendar goes up a size because its grid is read at a
        // glance and its lunar-day text is the smallest thing in the app.
        bool dense = (ps.mobilePage == PG_PARAMS || ps.mobilePage == PG_EPHEM ||
                      ps.mobilePage == PG_TERMS  || ps.mobilePage == PG_BAZI);
        bool large = (ps.mobilePage == PG_CALENDAR);
        if (dense) ImGui::PushFont(UiFontSmall());
        else if (large) ImGui::PushFont(UiFontTitle());

        switch (ps.mobilePage) {
        case PG_CALENDAR: DrawCalendarContent(ps);                     break;
        case PG_EPHEM:    DrawEphemerisContent(ps, scene);             break;
        case PG_MOON:     DrawMoonPhaseContent(renderer, scene, ps);   break;
        case PG_ECLIPSE:  DrawEclipseContent(renderer, scene, ps);     break;
        case PG_TERMS:    DrawTermsContent(ps);                        break;
        case PG_BAZI:     DrawBaziContent(ps);                         break;
        case PG_PARAMS:   DrawParamsContent(scene, ps);                break;
        case PG_SETTINGS: DrawSettingsPage(scene, ropt, ps);           break;
        case PG_HELP:     DrawHelpPage(ps);                            break;
        default: break;
        }
        if (dense || large) ImGui::PopFont();

        ImGui::Dummy(ImVec2(0, UiS(16.0f)));
        // The eclipse globe and the 3-D moon want their own horizontal drag, so
        // swipe paging steps aside on those pages.
        bool allowSwipe = !s_moreOpen &&
                          ps.mobilePage != PG_ECLIPSE && ps.mobilePage != PG_MOON;
        int step = HandleGestures(allowSwipe, !s_moreOpen);
        if (step != 0)
            ps.mobilePage = SwipeOrderPage(SwipeOrderIndex(ps.mobilePage) + step);

        ImGui::End();
        ImGui::PopStyleVar();
    }

    if (s_moreOpen) DrawMoreSheet(ps, contentPos, contentSize, s_moreOpen);
}

} // namespace sx
