// Entry point: GLFW + OpenGL 3.3 + Dear ImGui host.
// glad.h must be included BEFORE any GL/GLFW header.
#include <glad/glad.h>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
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

static void loadChineseFont() {
    ImGuiIO& io = ImGui::GetIO();

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
                io.Fonts->AddFontFromFileTTF(p.c_str(), 16.0f, nullptr,
                                             io.Fonts->GetGlyphRangesChineseFull());
                std::fprintf(stderr, "[font] loaded bundled %s\n", p.c_str());
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
            io.Fonts->AddFontFromFileTTF(path, 16.0f, nullptr,
                                         io.Fonts->GetGlyphRangesChineseFull());
            std::fprintf(stderr, "[font] loaded system %s\n", path);
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
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif

    GLFWwindow* window = glfwCreateWindow(1440, 900, "寿星天文历 - 3D太阳系",
                                          nullptr, nullptr);
    if (!window) {
        std::fprintf(stderr, "Failed to create window\n");
        glfwTerminate(); return 1;
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        std::fprintf(stderr, "Failed to load OpenGL via GLAD\n"); return 1;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    // Keep ImGui's own ini for internal state; project display switches are
    // stored separately in sxwnl_gui.ini by Load/SaveAppSettings().
    io.IniFilename = "imgui.ini";
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
    ImGui_ImplOpenGL3_Init("#version 330");

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
    // The 8K textures take several seconds to decompress; we paint a loading
    // screen every ~0.1s so the OS doesn't mark the window "Not Responding".
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

    gx::OrbitCamera cam;
    if (const char* dist = std::getenv("SXWNL_CAMERA_DISTANCE")) {
        float d = std::strtof(dist, nullptr);
        if (d > 0.2f && d < 4000.0f) cam.distance = d;
    }
    sx::RenderOptions ropt;
    sx::PanelState    ps;
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

    double last = glfwGetTime();
    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
        double now = glfwGetTime();
        double dt  = now - last; last = now;

        scene.clock().advance(dt);
        scene.update();
        cam.updateFocus((float)dt);
        glfwSetWindowTitle(window, ps.useChinese ? "寿星天文历 - 3D太阳系"
                                                 : "SXWNL Calendar - 3D Solar System");

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        // Menu bar is drawn first so its height is available for panel positioning.
        sx::DrawMainMenuBar(scene, ropt, ps);

        sx::DrawSidebar(scene, ropt, ps, cam);
        sx::DrawViewportPanel(renderer, scene, cam, ropt, ps);
        sx::DrawToolsPanel(renderer, scene, ps);
        sx::DrawPanelSplitters(ps);

        ImGui::Render();
        int fbw, fbh;
        glfwGetFramebufferSize(window, &fbw, &fbh);
        glViewport(0, 0, fbw, fbh);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);
    }

    renderer.shutdown();
    sx::SaveAppSettings(ropt, ps);
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
