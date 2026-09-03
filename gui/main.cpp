// Entry point: GLFW + OpenGL 3.3 (desktop) / WebGL2 via Emscripten + Dear ImGui host.
// gl_compat.h must be included BEFORE any GL/GLFW header; it picks glad or
// GLES3 headers depending on SXWNL_USE_GLES (set for Android and Emscripten).
#include "gles/gl_compat.h"
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#ifdef __EMSCRIPTEN__
#  include <emscripten.h>
#  include <emscripten/html5.h>
#endif
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <string>
#ifdef __APPLE__
#  include <mach-o/dyld.h>
#  include <limits.h>
#elif defined(__linux__)
#  include <unistd.h>
#  include <limits.h>
#elif defined(_WIN32)
#  include <windows.h>
#endif

#include "imgui.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_opengl3.h"

#include "camera.h"
#include "panels.h"
#include "ui_mobile.h"
#include "renderer.h"
#include "scene.h"

#include "../lunar/lunar.h"
#include "../mylib/tool.h"
#include "../mylib/lat_lon_data.h"

static void glfwError(int code, const char* desc) {
    std::fprintf(stderr, "GLFW error %d: %s\n", code, desc);
}

// Return the directory containing the running executable (no trailing slash).
static std::string executableDir() {
#ifdef __APPLE__
    char buf[PATH_MAX];
    uint32_t size = sizeof(buf);
    if (_NSGetExecutablePath(buf, &size) == 0) {
        std::string p(buf);
        auto pos = p.rfind('/');
        if (pos != std::string::npos) return p.substr(0, pos);
    }
#elif defined(__linux__)
    char buf[PATH_MAX];
    ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (len > 0) {
        buf[len] = '\0';
        std::string p(buf);
        auto pos = p.rfind('/');
        if (pos != std::string::npos) return p.substr(0, pos);
    }
#elif defined(_WIN32)
    char buf[MAX_PATH];
    if (GetModuleFileNameA(nullptr, buf, MAX_PATH)) {
        std::string p(buf);
        auto pos = p.rfind('\\');
        if (pos != std::string::npos) return p.substr(0, pos);
    }
#endif
    return ".";
}

// Merges the packaged zodiac/planet symbol face into the face just added.
// Searched exe-relative like the CJK font; missing is not an error, the signs
// just fall back to "?" as they did before it was bundled.
static void mergeAstroSymbols(float sizePixels) {
#ifdef __EMSCRIPTEN__
    // Fetched into MEMFS by Module.preRun before main() runs.
    if (sx::AddAstroSymbolFont("/resources/fonts/NotoSansSymbols-Astro.ttf", sizePixels)) {
        std::fprintf(stderr, "[font] merged symbols /resources/fonts/NotoSansSymbols-Astro.ttf\n");
        return;
    }
#endif
    std::string exeDir = executableDir();
    for (const std::string& base : {exeDir, exeDir + "/..", exeDir + "/../.."}) {
        std::string p = base + "/resources/fonts/NotoSansSymbols-Astro.ttf";
        if (sx::AddAstroSymbolFont(p.c_str(), sizePixels)) {
            std::fprintf(stderr, "[font] merged symbols %s\n", p.c_str());
            return;
        }
    }
    std::fprintf(stderr, "[font] no symbol font found; zodiac signs will show as '?'.\n");
}

#ifdef __EMSCRIPTEN__
// Set once in main() from the browser's device-pixel canvas size, before any
// font is built. A phone reports its canvas in CSS-px * devicePixelRatio,
// often 2-3x a desktop monitor's count for the same visual size, so a flat
// point size that looks right on desktop renders illegibly small on a phone
// unless the atlas (and layout metrics, via SetUiScale) grow to match.
// Mirrors android_main.cpp's computeUiScale(): density alone gives physically
// correct sizes but ignores how few pixels a phone actually has, so cap by a
// pixel budget that keeps a useful number of rows on the short edge.
static float computeWasmUiScale(int fbShortEdge, float devicePixelRatio) {
    float densityScale = devicePixelRatio > 0.0f ? devicePixelRatio : 1.0f;
    float fitScale = fbShortEdge > 1 ? (float)fbShortEdge / 480.0f : 2.0f;
    float scale = std::min(densityScale, fitScale);
    return std::clamp(scale, 1.0f, 3.0f);
}
static float g_wasmUiScale = 1.0f;

// Three sizes, same reasoning as Android's buildFonts(): a body face, a
// smaller one for dense tables/readouts, and a larger one for page titles.
// All three carry the full CJK range - calendar notes and festival names
// reach well past any hand-picked subset, and re-scanning source text for a
// glyph list every time new text is added is exactly the fragility this
// replaces.
static bool wasmBuildFonts(float uiScale) {
    ImGuiIO& io = ImGui::GetIO();
    const char* fontPath = "/resources/fonts/NotoSansCJKsc-Regular.otf";
    FILE* probe = std::fopen(fontPath, "rb");
    if (!probe) {
        std::fprintf(stderr, "[font] wasm CJK font missing at %s\n", fontPath);
        return false;
    }
    std::fclose(probe);

    const ImWchar* ranges = io.Fonts->GetGlyphRangesChineseFull();
    const float bodyPx  = std::round(16.0f * uiScale);
    const float smallPx = std::round(13.0f * uiScale);
    const float titlePx = std::round(19.0f * uiScale);
    ImFont* fontBody  = io.Fonts->AddFontFromFileTTF(fontPath, bodyPx, nullptr, ranges);
    if (fontBody) mergeAstroSymbols(bodyPx);
    ImFont* fontSmall = fontBody ? io.Fonts->AddFontFromFileTTF(fontPath, smallPx, nullptr, ranges)
                                 : nullptr;
    if (fontSmall) mergeAstroSymbols(smallPx);
    ImFont* fontTitle = fontBody ? io.Fonts->AddFontFromFileTTF(fontPath, titlePx, nullptr, ranges)
                                 : nullptr;
    if (fontTitle) mergeAstroSymbols(titlePx);
    io.FontDefault = fontBody;
    sx::SetUiFonts(fontBody, fontSmall, fontTitle);
    std::fprintf(stderr, "[font] loaded wasm %s (uiScale=%.2f, body=%.0fpx)\n",
                 fontPath, uiScale, bodyPx);
    return true;
}

// ---- Touch gestures (pinch-to-zoom / two-finger pan) -----------------------
// GLFW has no multi-touch concept; the emscripten GLFW shim maps touch onto a
// single emulated mouse pointer, which is enough for one-finger drag-to-rotate
// but reads a second finger as the first one jumping across the screen. These
// callbacks read raw touch events directly (bypassing that emulation) and
// hand the frame loop an accumulated zoom factor and pan delta once two
// fingers are down, mirroring android_main.cpp's onInputEvent latch: once
// latched, every finger up to the last one lifting is swallowed here, and
// ImGui is told the mouse button is up so nothing underneath reads the
// emulated pointer as a rotate drag.
static float g_pinchZoom   = 1.0f;
static float g_panX        = 0.0f;
static float g_panY        = 0.0f;
static float g_prevDist    = 0.0f;
static float g_prevCx      = 0.0f;
static float g_prevCy      = 0.0f;
static bool  g_pinchLatched = false;
static float g_wasmDpr     = 1.0f;

// Fingers still on the glass after this event. Emscripten merges
// event.changedTouches into touches[], so a touchend still lists the finger
// that just left (flagged isChanged) - counting numTouches alone would never
// reach zero and the latch below would never release.
static int wasmTouchesDown(int eventType, const EmscriptenTouchEvent* e) {
    const bool lifting = (eventType == EMSCRIPTEN_EVENT_TOUCHEND ||
                          eventType == EMSCRIPTEN_EVENT_TOUCHCANCEL);
    int down = 0;
    for (int i = 0; i < e->numTouches && i < 32; ++i)
        if (!(lifting && e->touches[i].isChanged)) ++down;
    return down;
}

static EM_BOOL wasmTouchCallback(int eventType, const EmscriptenTouchEvent* e, void*) {
    const int n = wasmTouchesDown(eventType, e);
    const bool moved = (eventType == EMSCRIPTEN_EVENT_TOUCHMOVE);

    if (n >= 2 && !g_pinchLatched) {
        g_pinchLatched = true;
        g_prevDist = 0.0f;
        ImGui::GetIO().AddMouseButtonEvent(0, false);
    }

    if (g_pinchLatched) {
        // A finger arriving or leaving reshuffles touches[], so only a move
        // carries a usable delta; anything else re-baselines.
        if (!moved) {
            g_prevDist = 0.0f;
        } else if (n >= 2) {
            // clientX/Y are CSS pixels straight off the DOM event; the camera
            // is driven in canvas (device) pixels like the mouse path is.
            const float x0 = (float)e->touches[0].clientX * g_wasmDpr;
            const float y0 = (float)e->touches[0].clientY * g_wasmDpr;
            const float x1 = (float)e->touches[1].clientX * g_wasmDpr;
            const float y1 = (float)e->touches[1].clientY * g_wasmDpr;
            const float dx = x0 - x1, dy = y0 - y1;
            const float dist = std::sqrt(dx * dx + dy * dy);
            const float cx = (x0 + x1) * 0.5f, cy = (y0 + y1) * 0.5f;

            if (dist > 1.0f) {
                if (g_prevDist <= 0.0f) {
                    g_prevDist = dist; g_prevCx = cx; g_prevCy = cy;
                } else {
                    // Spreading (dist grows) zooms in, i.e. the camera moves
                    // closer, so the factor is prevDist/dist: <1 spreading.
                    g_pinchZoom *= (g_prevDist / dist);
                    g_panX += cx - g_prevCx;
                    g_panY += cy - g_prevCy;
                    g_prevDist = dist; g_prevCx = cx; g_prevCy = cy;
                }
            }
        }
        // Hold the latch until every finger is off the glass: releasing on the
        // second-to-last one hands ImGui a lone finger mid-screen, which it
        // reads as the start of a rotate drag.
        if (n == 0) {
            g_pinchLatched = false;
            g_prevDist = 0.0f;
        }
        return EM_TRUE;
    }
    return EM_FALSE;
}
#endif

static void loadChineseFont() {
    ImGuiIO& io = ImGui::GetIO();
    const float kFontSize = 16.0f;

#ifdef __EMSCRIPTEN__
    if (wasmBuildFonts(g_wasmUiScale)) {
        sx::SetUiScale(g_wasmUiScale);
        sx::SetTouchMode(true);
        return;
    }
#endif

    // ── 1. resources/fonts/ 打包字体（优先，跨平台可用）──────────────────────
    // 相对可执行文件目录搜索，不依赖工作目录
    std::string exeDir = executableDir();
    const char* bundled[] = {
        "NotoSansCJKsc-Regular.otf",
        "NotoSansSC-Regular.ttf",
        "wqy-microhei.ttc",
    };
    for (const char* fname : bundled) {
        for (const std::string& base : {exeDir, exeDir + "/..", exeDir + "/../.."}) {
            std::string p = base + "/resources/fonts/" + fname;
            FILE* f = std::fopen(p.c_str(), "rb");
            if (f) {
                std::fclose(f);
                io.Fonts->AddFontFromFileTTF(p.c_str(), kFontSize, nullptr,
                                             io.Fonts->GetGlyphRangesChineseFull());
                std::fprintf(stderr, "[font] loaded bundled %s\n", p.c_str());
                mergeAstroSymbols(kFontSize);
                return;
            }
        }
    }

    // ── 2. 系统 / 用户已安装字体 ─────────────────────────────────────────────
    // 把 $HOME/Library/Fonts/ 作为动态路径
    std::string homeNoto;
    if (const char* home = std::getenv("HOME")) {
        homeNoto = std::string(home) + "/Library/Fonts/NotoSansCJKsc-Regular.otf";
    }
    const char* kHomeNoto = homeNoto.empty() ? nullptr : homeNoto.c_str();
    const char* system[] = {
        // macOS – Noto CJK via Homebrew cask (~/Library/Fonts/)
        kHomeNoto,
        // macOS 系统内置中文字体（Tahoe/Sequoia 均稳定存在）
        "/System/Library/Fonts/STHeiti Light.ttc",
        "/System/Library/Fonts/STHeiti Medium.ttc",
        "/System/Library/Fonts/Hiragino Sans GB.ttc",
        "/System/Library/Fonts/Supplemental/Songti.ttc",
        "/Library/Fonts/Arial Unicode.ttf",
        // macOS 旧版 PingFang 路径（Monterey / Ventura）
        "/System/Library/Fonts/PingFang.ttc",
        // Linux
        "/usr/share/fonts/truetype/wqy/wqy-microhei.ttc",
        "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
        "/usr/share/fonts/noto-cjk/NotoSansCJKsc-Regular.otf",
        // Windows
        "C:/Windows/Fonts/msyh.ttc",
        "C:/Windows/Fonts/msyh.ttf",
        "C:/Windows/Fonts/simhei.ttf",
        "C:/Windows/Fonts/simsun.ttc",
    };
    for (const char* path : system) {
        if (!path) continue;
        FILE* f = std::fopen(path, "rb");
        if (f) {
            std::fclose(f);
            io.Fonts->AddFontFromFileTTF(path, kFontSize, nullptr,
                                         io.Fonts->GetGlyphRangesChineseFull());
            std::fprintf(stderr, "[font] loaded system %s\n", path);
            mergeAstroSymbols(kFontSize);
            return;
        }
    }
    io.Fonts->AddFontDefault();
    std::fprintf(stderr, "[font] no CJK font found; UI text may show as squares.\n");
}

// Locate the resources/ root directory.
// Probes relative to the executable first, then the working directory.
static std::string findResourceDir() {
    std::string exeDir = executableDir();
    // Candidate base directories: exe-relative first, then cwd-relative
    std::string bases[] = {
        exeDir,                   // e.g. build/resources
        exeDir + "/..",           // e.g. build/../resources
        exeDir + "/../..",        // deeper nested builds
        ".",                      // cwd
        "..",
        "../..",
    };
    for (const std::string& base : bases) {
        std::string probe = base + "/resources/planet/8k-solar-system.obj";
        FILE* f = std::fopen(probe.c_str(), "rb");
        if (f) { std::fclose(f); return base + "/resources"; }
    }
    std::fprintf(stderr, "[res] resource directory not found; rendering with solid colors.\n");
    return "";
}

int main() {
    glfwSetErrorCallback(glfwError);
    if (!glfwInit()) { std::fprintf(stderr, "Failed to init GLFW\n"); return 1; }
#if defined(SXWNL_USE_GLES)
    glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_ES_API);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
#else
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#endif
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif

#ifdef __EMSCRIPTEN__
    int winW = EM_ASM_INT({ return Math.max(320, window.innerWidth  * (window.devicePixelRatio || 1) | 0); });
    int winH = EM_ASM_INT({ return Math.max(240, window.innerHeight * (window.devicePixelRatio || 1) | 0); });
    double dpr = EM_ASM_DOUBLE({ return window.devicePixelRatio || 1; });
    g_wasmDpr = dpr > 0.0 ? (float)dpr : 1.0f;
    g_wasmUiScale = computeWasmUiScale(std::min(winW, winH), (float)dpr);
    GLFWwindow* window = glfwCreateWindow(winW, winH, "寿星天文历 - 3D太阳系",
                                          nullptr, nullptr);
#else
    GLFWwindow* window = glfwCreateWindow(1440, 900, "寿星天文历 - 3D太阳系",
                                          nullptr, nullptr);
#endif
    if (!window) {
        std::fprintf(stderr, "Failed to create window\n");
        glfwTerminate(); return 1;
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

#if !defined(SXWNL_USE_GLES)
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        std::fprintf(stderr, "Failed to load OpenGL via GLAD\n"); return 1;
    }
#endif

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    // Keep ImGui's own ini for internal state; project display switches are
    // stored separately in sxwnl_gui.ini by Load/SaveAppSettings().
#ifdef __EMSCRIPTEN__
    io.IniFilename = nullptr;
#else
    io.IniFilename = "imgui.ini";
#endif
    ImGui::StyleColorsDark();

    // Polished dark-space theme.
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowPadding     = ImVec2(10.0f, 9.0f);
    style.FramePadding      = ImVec2(8.0f, 5.0f);
    style.CellPadding       = ImVec2(6.0f, 5.0f);
    style.ItemSpacing       = ImVec2(8.0f, 7.0f);
    style.ItemInnerSpacing  = ImVec2(6.0f, 5.0f);
    style.IndentSpacing     = 16.0f;
    style.ScrollbarSize     = 13.0f;
    style.GrabMinSize       = 11.0f;
    style.WindowRounding    = 5.0f;
    style.ChildRounding     = 5.0f;
    style.FrameRounding     = 4.0f;
    style.PopupRounding     = 5.0f;
    style.ScrollbarRounding = 5.0f;
    style.GrabRounding      = 4.0f;
    style.TabRounding       = 4.0f;
    style.WindowBorderSize  = 1.0f;
    style.ChildBorderSize   = 1.0f;
    style.FrameBorderSize   = 1.0f;

    ImVec4* colors = style.Colors;
    colors[ImGuiCol_Text]                  = ImVec4(0.88f, 0.92f, 0.98f, 1.00f);
    colors[ImGuiCol_TextDisabled]          = ImVec4(0.50f, 0.58f, 0.70f, 1.00f);
    colors[ImGuiCol_WindowBg]              = ImVec4(0.045f, 0.052f, 0.078f, 0.98f);
    colors[ImGuiCol_ChildBg]               = ImVec4(0.055f, 0.064f, 0.095f, 0.86f);
    colors[ImGuiCol_PopupBg]               = ImVec4(0.045f, 0.052f, 0.078f, 0.98f);
    colors[ImGuiCol_Border]                = ImVec4(0.20f, 0.30f, 0.46f, 0.45f);
    colors[ImGuiCol_BorderShadow]          = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_FrameBg]               = ImVec4(0.085f, 0.105f, 0.155f, 1.00f);
    colors[ImGuiCol_FrameBgHovered]        = ImVec4(0.125f, 0.165f, 0.245f, 1.00f);
    colors[ImGuiCol_FrameBgActive]         = ImVec4(0.16f, 0.22f, 0.34f, 1.00f);
    colors[ImGuiCol_TitleBg]               = ImVec4(0.035f, 0.040f, 0.060f, 1.00f);
    colors[ImGuiCol_TitleBgActive]         = ImVec4(0.075f, 0.105f, 0.165f, 1.00f);
    colors[ImGuiCol_MenuBarBg]             = ImVec4(0.035f, 0.040f, 0.060f, 1.00f);
    colors[ImGuiCol_ScrollbarBg]           = ImVec4(0.035f, 0.040f, 0.060f, 0.90f);
    colors[ImGuiCol_ScrollbarGrab]         = ImVec4(0.18f, 0.26f, 0.40f, 0.90f);
    colors[ImGuiCol_ScrollbarGrabHovered]  = ImVec4(0.25f, 0.36f, 0.54f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabActive]   = ImVec4(0.34f, 0.48f, 0.70f, 1.00f);
    colors[ImGuiCol_CheckMark]             = ImVec4(0.48f, 0.86f, 1.00f, 1.00f);
    colors[ImGuiCol_SliderGrab]            = ImVec4(0.34f, 0.62f, 0.95f, 1.00f);
    colors[ImGuiCol_SliderGrabActive]      = ImVec4(0.58f, 0.82f, 1.00f, 1.00f);
    colors[ImGuiCol_Button]                = ImVec4(0.115f, 0.175f, 0.285f, 0.95f);
    colors[ImGuiCol_ButtonHovered]         = ImVec4(0.18f, 0.285f, 0.46f, 1.00f);
    colors[ImGuiCol_ButtonActive]          = ImVec4(0.25f, 0.39f, 0.62f, 1.00f);
    colors[ImGuiCol_Header]                = ImVec4(0.12f, 0.19f, 0.31f, 0.86f);
    colors[ImGuiCol_HeaderHovered]         = ImVec4(0.18f, 0.30f, 0.49f, 1.00f);
    colors[ImGuiCol_HeaderActive]          = ImVec4(0.24f, 0.38f, 0.61f, 1.00f);
    colors[ImGuiCol_Separator]             = ImVec4(0.22f, 0.34f, 0.52f, 0.50f);
    colors[ImGuiCol_SeparatorHovered]      = ImVec4(0.36f, 0.55f, 0.78f, 0.70f);
    colors[ImGuiCol_SeparatorActive]       = ImVec4(0.48f, 0.72f, 0.95f, 0.90f);
    colors[ImGuiCol_Tab]                   = ImVec4(0.075f, 0.105f, 0.165f, 0.95f);
    colors[ImGuiCol_TabHovered]            = ImVec4(0.19f, 0.30f, 0.48f, 1.00f);
    colors[ImGuiCol_TabActive]             = ImVec4(0.14f, 0.24f, 0.40f, 1.00f);
    colors[ImGuiCol_TableHeaderBg]         = ImVec4(0.105f, 0.15f, 0.24f, 1.00f);
    colors[ImGuiCol_TableBorderStrong]     = ImVec4(0.24f, 0.34f, 0.50f, 0.75f);
    colors[ImGuiCol_TableBorderLight]      = ImVec4(0.18f, 0.25f, 0.38f, 0.55f);
    colors[ImGuiCol_TableRowBg]            = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_TableRowBgAlt]         = ImVec4(0.11f, 0.14f, 0.20f, 0.18f);

    loadChineseFont();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init(SXWNL_GLSL_VERSION_DIRECTIVE);

#ifdef __EMSCRIPTEN__
    emscripten_set_touchstart_callback("#canvas", nullptr, EM_FALSE, wasmTouchCallback);
    emscripten_set_touchmove_callback("#canvas", nullptr, EM_FALSE, wasmTouchCallback);
    emscripten_set_touchend_callback("#canvas", nullptr, EM_FALSE, wasmTouchCallback);
    emscripten_set_touchcancel_callback("#canvas", nullptr, EM_FALSE, wasmTouchCallback);
#endif

    // Engine init – default observer: Beijing
    init_ob();
    jw.J = 116.4;
    jw.W = 39.9;

    sx::Scene    scene;
    sx::Renderer renderer;
    if (!renderer.init()) {
        std::fprintf(stderr, "Renderer init failed\n"); return 1;
    }

    // Load OBJ meshes + textures.
    // Desktop: the 8K textures take several seconds to decompress; we paint a
    // loading screen so the OS doesn't mark the window "Not Responding".
    // Wasm: textures are HTTP fetches decoded by the browser, meshes go into
    // MEMFS in the background. Do not block — the tab would freeze until the
    // last JPEG arrived.
#ifdef __EMSCRIPTEN__
    renderer.loadModels("");
#else
    {
        std::string resDir = findResourceDir();
        if (!resDir.empty()) {
            // Show a simple "加载中..." frame before blocking load
            ImGui_ImplOpenGL3_NewFrame();
            ImGui_ImplGlfw_NewFrame();
            ImGui::NewFrame();
            ImGui::SetNextWindowPos(ImVec2(0, 0));
            ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
            ImGui::Begin("##loading",nullptr,
                ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoInputs|
                ImGuiWindowFlags_NoNav|ImGuiWindowFlags_NoBackground);
            ImVec2 c = ImGui::GetIO().DisplaySize;
            ImGui::SetCursorPos(ImVec2(c.x*0.5f-80, c.y*0.5f-20));
            ImGui::TextColored({0.7f,0.9f,1.0f,1.0f},
                               "加载天体模型和纹理...");
            ImGui::End();
            ImGui::Render();
            int fbw,fbh; glfwGetFramebufferSize(window,&fbw,&fbh);
            glViewport(0,0,fbw,fbh);
            glClearColor(0.04f,0.04f,0.08f,1.0f);
            glClear(GL_COLOR_BUFFER_BIT);
            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
            glfwSwapBuffers(window);
            glfwPollEvents();

            renderer.loadModels(resDir);
        }
    }
#endif

    gx::OrbitCamera cam;
    if (const char* dist = std::getenv("SXWNL_CAMERA_DISTANCE")) {
        float d = std::strtof(dist, nullptr);
        if (d > 0.2f && d < 4000.0f) cam.distance = d;
    }
    sx::RenderOptions ropt;
    sx::PanelState    ps;
#ifdef __EMSCRIPTEN__
    // Phone default: text one size up from the desktop baseline, same as
    // Android. A saved preference (once wasm settings persist across
    // reloads) still wins, since this only sets the value LoadAppSettings
    // reads its clamp from.
    if (g_wasmUiScale > 1.0f) ps.fontScale = 1.4f;
    // The side rails are stored in the same pixels the font is rasterised
    // in, so they have to grow with it - left at the desktop default a
    // HiDPI browser clamps them to the minimum and clips every label.
    ps.leftPanelWidth  *= g_wasmUiScale;
    ps.toolsPanelWidth *= g_wasmUiScale;
#endif
    sx::LoadAppSettings(ropt, ps);
    scene.clock().speedDaysPerSec =
        (float)sx::speedToDaysPerSecond(ps.speedUnit, ps.speedAmount);
    if (const char* tab = std::getenv("SXWNL_ACTIVE_TAB")) {
        int t = std::atoi(tab);
        if (t >= 0 && t <= 6) ps.activeTab = t;
    }
    {
        Date d = sx::localDateFromUtcJD(scene.clock().jd, ps.timezoneHours);
        ps.year = ps.calYear = ps.termYear = d.Y;
        ps.month = ps.calMonth = d.M;
        ps.day  = d.D;
        ps.hour = d.h;
        ps.eclipseYear = d.Y;
        ps.eclipseMonth = d.M;
    }

    // Browsers (Emscripten) require a non-blocking main loop driven by
    // requestAnimationFrame, so the per-frame body is a captureless lambda
    // shared by the desktop while() loop and emscripten_set_main_loop_arg().
    struct MainLoopCtx {
        GLFWwindow*        window;
        sx::Scene*         scene;
        sx::Renderer*      renderer;
        gx::OrbitCamera*   cam;
        sx::RenderOptions* ropt;
        sx::PanelState*    ps;
        double             last;
    } ctx{window, &scene, &renderer, &cam, &ropt, &ps, glfwGetTime()};

    auto frame = [](void* argPtr) {
        MainLoopCtx& c          = *static_cast<MainLoopCtx*>(argPtr);
        GLFWwindow* window      = c.window;
        sx::Scene& scene        = *c.scene;
        sx::Renderer& renderer  = *c.renderer;
        gx::OrbitCamera& cam    = *c.cam;
        sx::RenderOptions& ropt = *c.ropt;
        sx::PanelState& ps      = *c.ps;

        glfwPollEvents();
#ifdef __EMSCRIPTEN__
        bool wasmPhoneViewport = false;
        {
            int dw = EM_ASM_INT({
                return Math.max(320, window.innerWidth * (window.devicePixelRatio || 1) | 0);
            });
            int dh = EM_ASM_INT({
                return Math.max(240, window.innerHeight * (window.devicePixelRatio || 1) | 0);
            });
            int cw = 0, ch = 0;
            glfwGetWindowSize(window, &cw, &ch);
            if (cw != dw || ch != dh) glfwSetWindowSize(window, dw, dh);
            double dpr = EM_ASM_DOUBLE({ return window.devicePixelRatio || 1; });
            g_wasmDpr = dpr > 0.0 ? (float)dpr : 1.0f;   // browser zoom / monitor change
            double cssShort = std::min(dw, dh) / (dpr > 0.0 ? dpr : 1.0);
            // Below this CSS-px short edge the desktop's three-column shell
            // no longer fits; switch to the phone shell instead of shrinking
            // it further. Re-evaluated live so rotating the device works.
            wasmPhoneViewport = cssShort < 700.0;
        }
#endif
        double now = glfwGetTime();
        double dt  = now - c.last; c.last = now;

        scene.clock().advance(dt);
        scene.update();
        cam.updateFocus((float)dt);
        glfwSetWindowTitle(window, ps.useChinese ? "寿星天文历 - 3D太阳系"
                                                 : "SXWNL Calendar - 3D Solar System");

#ifdef __EMSCRIPTEN__
        bool useMobileShell = ps.mobilePreview || wasmPhoneViewport;
        // Pinch goes to the 3-D camera on the solar-system page, and to text
        // size everywhere else - same split as Android's drawFrame(). The
        // desktop shell has no pages, and a tablet or touch laptop lands on
        // it at full width, so there the gesture always drives the camera.
        if (g_pinchZoom != 1.0f || g_panX != 0.0f || g_panY != 0.0f) {
            if (!useMobileShell || ps.mobilePage == 0) {
                if (g_pinchZoom != 1.0f) cam.zoom(g_pinchZoom);
                if (g_panX != 0.0f || g_panY != 0.0f) cam.pan(g_panX, g_panY);
            } else if (g_pinchZoom != 1.0f) {
                const float before = ps.fontScale;
                ps.fontScale = std::clamp(before / g_pinchZoom, sx::kFontScaleMin, sx::kFontScaleMax);
                if (ps.fontScale != before) sx::NoteFontScaleChanged();
            }
        }
        g_pinchZoom = 1.0f;
        g_panX = g_panY = 0.0f;
#else
        bool useMobileShell = ps.mobilePreview;
#endif

        // Text size is a user setting on both shells; Android drives it with a
        // pinch, here it is the slider on the settings page of the phone layout.
        ImGui::GetIO().FontGlobalScale = ps.fontScale;

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        if (useMobileShell) {
            // Same shell Android runs, so the phone layout can be checked here
            // without a device. View > Phone layout turns it off again on
            // desktop; on wasm a narrow viewport turns it back on regardless.
            sx::DrawMobileUI(renderer, scene, cam, ropt, ps);
        } else {
            // Menu bar is drawn first so its height is available for panel positioning.
            sx::DrawMainMenuBar(scene, ropt, ps);

            sx::DrawSidebar(scene, ropt, ps, cam);
            sx::DrawViewportPanel(renderer, scene, cam, ropt, ps);
            sx::DrawToolsPanel(renderer, scene, ps);
            sx::DrawPanelSplitters(ps);
        }

#ifdef __EMSCRIPTEN__
        if (renderer.loadedMeshes() == 0) {
            ImGui::SetNextWindowPos(ImVec2(12, 12), ImGuiCond_Always);
            ImGui::SetNextWindowBgAlpha(0.55f);
            ImGui::Begin("##wasm_loading", nullptr,
                         ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                         ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove);
            ImGui::TextDisabled("加载天体网格...");
            ImGui::End();
        }
#endif

        ImGui::Render();
        int fbw, fbh;
        glfwGetFramebufferSize(window, &fbw, &fbh);
        glViewport(0, 0, fbw, fbh);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);
    };

#ifdef __EMSCRIPTEN__
    // main() must return immediately on this path; the browser tab tears
    // down the WebAssembly instance on page close, so there is no matching
    // shutdown call here.
    emscripten_set_main_loop_arg(frame, &ctx, 0, 1);
#else
    while (!glfwWindowShouldClose(window)) {
        frame(&ctx);
    }

    renderer.shutdown();
    sx::SaveAppSettings(ropt, ps);
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
#endif
    return 0;
}
