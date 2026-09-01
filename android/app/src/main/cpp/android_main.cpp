#include <android/asset_manager.h>
#include <android/configuration.h>
#include <android/input.h>
#include <android/log.h>
#include <android/window.h>
#include <jni.h>
#include <android_native_app_glue.h>
#include <EGL/egl.h>
#include <GLES3/gl3.h>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

#include "imgui.h"
#include "backends/imgui_impl_android.h"
#include "backends/imgui_impl_opengl3.h"

#include "camera.h"
#include "gles/gl_compat.h"
#include "panels.h"
#include "ui_mobile.h"
#include "renderer.h"
#include "scene.h"
#include "lunar.h"
#include "lat_lon_data.h"

#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "sxwnl", __VA_ARGS__)

// System window insets (status bar / navigation bar / display cutout) in
// physical pixels. MainActivity reports them from the UI thread whenever the
// window layout changes; the render loop reads them once per frame. Without
// these the navigation bar sits on top of the bottom row of the UI and eats
// every tap aimed at it.
namespace {
std::atomic<int> g_insetLeft{0}, g_insetTop{0}, g_insetRight{0}, g_insetBottom{0};
}

extern "C" JNIEXPORT void JNICALL
Java_top_qaiu_sxwnl_MainActivity_nativeSetInsets(JNIEnv*, jclass,
                                                 jint left, jint top,
                                                 jint right, jint bottom) {
    g_insetLeft.store(left, std::memory_order_relaxed);
    g_insetTop.store(top, std::memory_order_relaxed);
    g_insetRight.store(right, std::memory_order_relaxed);
    g_insetBottom.store(bottom, std::memory_order_relaxed);
}

namespace {

struct EglState {
    EGLDisplay display = EGL_NO_DISPLAY;
    EGLSurface surface = EGL_NO_SURFACE;
    EGLContext context = EGL_NO_CONTEXT;
    int width = 0;
    int height = 0;
};

bool createEgl(ANativeWindow* window, EglState& egl) {
    const EGLint configAttrs[] = {
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8, EGL_DEPTH_SIZE, 24,
        EGL_NONE
    };
    const EGLint contextAttrs[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
    EGLConfig config = nullptr;
    EGLint count = 0;
    EGLint format = 0;

    egl.display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (egl.display == EGL_NO_DISPLAY || !eglInitialize(egl.display, nullptr, nullptr) ||
        !eglChooseConfig(egl.display, configAttrs, &config, 1, &count) || count != 1 ||
        !eglGetConfigAttrib(egl.display, config, EGL_NATIVE_VISUAL_ID, &format)) {
        return false;
    }

    ANativeWindow_setBuffersGeometry(window, 0, 0, format);
    egl.surface = eglCreateWindowSurface(egl.display, config, window, nullptr);
    egl.context = eglCreateContext(egl.display, config, EGL_NO_CONTEXT, contextAttrs);
    if (egl.surface == EGL_NO_SURFACE || egl.context == EGL_NO_CONTEXT ||
        !eglMakeCurrent(egl.display, egl.surface, egl.surface, egl.context)) {
        return false;
    }

    eglQuerySurface(egl.display, egl.surface, EGL_WIDTH, &egl.width);
    eglQuerySurface(egl.display, egl.surface, EGL_HEIGHT, &egl.height);
    eglSwapInterval(egl.display, 1);
    return true;
}

void destroyEgl(EglState& egl) {
    if (egl.display != EGL_NO_DISPLAY) {
        eglMakeCurrent(egl.display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (egl.context != EGL_NO_CONTEXT) eglDestroyContext(egl.display, egl.context);
        if (egl.surface != EGL_NO_SURFACE) eglDestroySurface(egl.display, egl.surface);
        eglTerminate(egl.display);
    }
    egl = {};
}

void makeParentDirs(const std::string& path) {
    for (size_t i = 1; i < path.size(); ++i) {
        if (path[i] != '/') continue;
        std::string dir = path.substr(0, i);
        if (!dir.empty() && mkdir(dir.c_str(), 0755) != 0 && errno != EEXIST) {
            LOGE("Cannot create asset directory %s: %s", dir.c_str(), std::strerror(errno));
        }
    }
}

bool copyAsset(AAssetManager* manager, const std::string& assetPath,
               const std::string& outputPath) {
    AAsset* asset = AAssetManager_open(manager, assetPath.c_str(), AASSET_MODE_STREAMING);
    if (!asset) {
        LOGE("Cannot open asset %s", assetPath.c_str());
        return false;
    }

    struct stat info {};
    if (stat(outputPath.c_str(), &info) == 0 &&
        info.st_size == AAsset_getLength64(asset)) {
        AAsset_close(asset);
        return true;
    }

    makeParentDirs(outputPath);
    const std::string temporaryPath = outputPath + ".tmp";
    FILE* output = std::fopen(temporaryPath.c_str(), "wb");
    if (!output) {
        LOGE("Cannot write asset %s", outputPath.c_str());
        AAsset_close(asset);
        return false;
    }

    char buffer[64 * 1024];
    int bytes = 0;
    bool ok = true;
    while ((bytes = AAsset_read(asset, buffer, sizeof(buffer))) > 0) {
        if (std::fwrite(buffer, 1, static_cast<size_t>(bytes), output) !=
            static_cast<size_t>(bytes)) {
            ok = false;
            break;
        }
    }
    ok = ok && bytes == 0 && std::fclose(output) == 0;
    AAsset_close(asset);
    if (ok) ok = std::rename(temporaryPath.c_str(), outputPath.c_str()) == 0;
    if (!ok) std::remove(temporaryPath.c_str());
    return ok;
}

// Copies the packaged resources out of the APK on first run. onStep runs
// before each file with a 0..1 fraction so the splash screen can advance.
void prepareResources(android_app* app,
                      const std::function<void(float, const char*)>& onStep) {
    static const char* files[] = {
        "fonts/NotoSansCJKsc-Regular.otf",
        "world_b.bin",
        "moon/Moon2K.obj",
        "moon/Textures/Bump_2K.png",
        "moon/Textures/Diffuse_2K.png",
        "planet/8k-solar-system.obj",
        "planet/tex/2k_neptune.jpg",
        "planet/tex/2k_uranus.jpg",
        "planet/tex/4k_venus_atmosphere.jpg",
        "planet/tex/8k_earth_clouds.jpg",
        "planet/tex/8k_earth_daymap.jpg",
        "planet/tex/8k_jupiter.jpg",
        "planet/tex/8k_mars.jpg",
        "planet/tex/8k_mercury.jpg",
        "planet/tex/8k_saturn.jpg",
        "planet/tex/8k_saturn_ring_UV-mapped.png",
        "planet/tex/8k_sun.jpg",
        "planet/tex/8k_venus_surface.jpg",
    };

    const std::string root = std::string(app->activity->internalDataPath) + "/resources";
    const int total = (int)(sizeof(files) / sizeof(files[0]));
    int index = 0;
    for (const char* file : files) {
        if (onStep) onStep((float)index / (float)total, file);
        ++index;
        if (!copyAsset(app->activity->assetManager, "resources/" + std::string(file),
                       root + "/" + file)) {
            LOGE("Resource extraction incomplete");
        }
    }
}

// ---- Startup splash ---------------------------------------------------------
// First launch has a long silent stretch: ~30 MB of assets are unpacked from the
// APK, a full CJK glyph atlas is rasterised, and thirteen 8K planet textures are
// decoded. Previously that was an unbroken black screen. This draws the app mark
// - the ring from the launcher icon - and uses the ring itself as the progress
// arc, with a bright bead riding its leading edge.

// Ring gradient: violet at the top-left running to cyan-white at the bottom
// right, matching the launcher icon.
ImU32 ringColorAt(float t) {
    static const float stops[][4] = {
        {0.00f, 0.753f, 0.298f, 0.961f},   // violet-magenta
        {0.45f, 0.431f, 0.424f, 0.941f},   // indigo
        {0.75f, 0.208f, 0.784f, 0.871f},   // cyan
        {1.00f, 0.624f, 0.953f, 0.902f},   // cyan-white
    };
    t = std::clamp(t, 0.0f, 1.0f);
    int i = 0;
    while (i < 2 && t > stops[i + 1][0]) ++i;
    float span = stops[i + 1][0] - stops[i][0];
    float k = span > 0.0f ? (t - stops[i][0]) / span : 0.0f;
    float r = stops[i][1] + (stops[i + 1][1] - stops[i][1]) * k;
    float g = stops[i][2] + (stops[i + 1][2] - stops[i][2]) * k;
    float b = stops[i][3] + (stops[i + 1][3] - stops[i][3]) * k;
    return IM_COL32((int)(r * 255), (int)(g * 255), (int)(b * 255), 255);
}

void drawSplashFrame(android_app* app, EglState& egl, float progress,
                     const char* stage, float uiScale) {
    // Keep servicing the looper: a multi-second load without pumping events
    // looks like a hang to the system and can trip an ANR.
    int events = 0;
    android_poll_source* source = nullptr;
    while (ALooper_pollOnce(0, nullptr, &events,
                            reinterpret_cast<void**>(&source)) >= 0) {
        if (source) source->process(app, source);
        if (app->destroyRequested || !app->window) return;
    }

    eglQuerySurface(egl.display, egl.surface, EGL_WIDTH, &egl.width);
    eglQuerySurface(egl.display, egl.surface, EGL_HEIGHT, &egl.height);

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplAndroid_NewFrame();
    ImGui::NewFrame();

    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    const ImVec2 c(egl.width * 0.5f, egl.height * 0.5f);
    const float  radius = std::min(egl.width, egl.height) * 0.16f;
    const float  thick  = std::max(2.0f, radius * 0.152f);   // diameter x 0.076
    const float  start  = -1.5707963f;                       // 12 o clock
    const int    segs   = 96;

    // Dim full ring, then the lit arc on top of it.
    for (int i = 0; i < segs; ++i) {
        float a0 = start + 6.2831853f * (float)i / segs;
        float a1 = start + 6.2831853f * (float)(i + 1) / segs;
        dl->AddLine(ImVec2(c.x + std::cos(a0) * radius, c.y + std::sin(a0) * radius),
                    ImVec2(c.x + std::cos(a1) * radius, c.y + std::sin(a1) * radius),
                    IM_COL32(46, 58, 92, 220), thick);
    }
    const float p = std::clamp(progress, 0.0f, 1.0f);
    const int lit = (int)(segs * p);
    for (int i = 0; i < lit; ++i) {
        float t0 = (float)i / segs, t1 = (float)(i + 1) / segs;
        float a0 = start + 6.2831853f * t0;
        float a1 = start + 6.2831853f * t1;
        dl->AddLine(ImVec2(c.x + std::cos(a0) * radius, c.y + std::sin(a0) * radius),
                    ImVec2(c.x + std::cos(a1) * radius, c.y + std::sin(a1) * radius),
                    ringColorAt(t0), thick);
    }

    // The diamond-ring bead, sitting at the head of the lit arc.
    {
        float a = start + 6.2831853f * p;
        ImVec2 b(c.x + std::cos(a) * radius, c.y + std::sin(a) * radius);
        float bead = radius * 0.10f;
        for (int i = 4; i >= 1; --i) {
            dl->AddCircleFilled(b, bead * (float)i * 0.9f,
                                IM_COL32(190, 245, 255, 26), 20);
        }
        dl->AddCircleFilled(b, bead, IM_COL32(240, 253, 255, 255), 20);
        float arm = radius * 0.44f, diag = radius * 0.28f;
        ImU32 flare = IM_COL32(207, 251, 255, 210);
        dl->AddLine(ImVec2(b.x - arm, b.y), ImVec2(b.x + arm, b.y), flare, thick * 0.30f);
        dl->AddLine(ImVec2(b.x, b.y - arm), ImVec2(b.x, b.y + arm), flare, thick * 0.30f);
        float d = diag * 0.7071f;
        dl->AddLine(ImVec2(b.x - d, b.y - d), ImVec2(b.x + d, b.y + d), flare, thick * 0.20f);
        dl->AddLine(ImVec2(b.x - d, b.y + d), ImVec2(b.x + d, b.y - d), flare, thick * 0.20f);
    }

    // Wordmark and the current stage. Before the CJK atlas exists this renders
    // in the built-in ASCII font, so both strings stay ASCII-safe.
    const float baseSz = 15.0f * uiScale;
    auto centred = [&](const char* text, float dy, ImU32 col, float scale) {
        ImFont* font = ImGui::GetFont();
        float sz = baseSz * scale;
        ImVec2 ts = font->CalcTextSizeA(sz, FLT_MAX, 0.0f, text);
        dl->AddText(font, sz, ImVec2(c.x - ts.x * 0.5f, c.y + dy), col, text);
    };
    centred("SXWNL", radius + 24.0f * uiScale, IM_COL32(226, 236, 250, 240), 1.6f);
    centred("3D Astronomical Calendar", radius + 54.0f * uiScale,
            IM_COL32(120, 145, 185, 220), 1.0f);
    if (stage && stage[0])
        centred(stage, radius + 84.0f * uiScale, IM_COL32(96, 118, 155, 200), 0.95f);

    ImGui::Render();
    glViewport(0, 0, egl.width, egl.height);
    glClearColor(0.020f, 0.031f, 0.063f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    eglSwapBuffers(egl.display, egl.surface);
}

// ---- Multi-touch camera gestures --------------------------------------------
// Two fingers drive the 3-D camera directly: changing their spacing zooms, and
// moving their midpoint pans. onInputEvent() runs on the same thread as the main
// loop (during source->process), so plain globals are safe; the loop drains the
// pending deltas once per frame.
//
// The tricky part is handing the gesture back. ImGui's Android backend maps
// touch onto a mouse, and a two-finger gesture always starts and ends with one
// finger down - so without care the backend sees the finger jump across the
// screen and the viewport reads it as a rotate drag. That is why a pinch used to
// leave the view swung round. The fix is a latch: once two fingers are down,
// every motion event is swallowed until the LAST finger lifts, and ImGui is told
// the button went up at the moment the gesture began.
float g_pendingPinchZoom = 1.0f;  // accumulated multiplicative zoom factor
float g_pendingPanX      = 0.0f;  // two-finger pan, screen px, drained per frame
float g_pendingPanY      = 0.0f;
float g_prevPinchDist    = 0.0f;  // finger spacing at last sample (px)
float g_prevPinchCx      = 0.0f;  // midpoint of the two fingers at last sample
float g_prevPinchCy      = 0.0f;
bool  g_pinchLatched     = false; // a multi-touch gesture owns the input stream
int   g_activePointers   = 0;
// Only the solar-system page owns the 3-D camera; on the other pages a
// two-finger gesture must not silently move a view that is not on screen.
bool  g_pinchEnabled     = true;

int32_t onInputEvent(android_app*, AInputEvent* event) {
    if (AInputEvent_getType(event) != AINPUT_EVENT_TYPE_MOTION)
        return ImGui_ImplAndroid_HandleInputEvent(event);

    const int32_t action = AMotionEvent_getAction(event) & AMOTION_EVENT_ACTION_MASK;
    const size_t  count  = AMotionEvent_getPointerCount(event);
    const bool    lastUp = (action == AMOTION_EVENT_ACTION_UP ||
                            action == AMOTION_EVENT_ACTION_CANCEL);

    if (count >= 2 && g_pinchEnabled && !g_pinchLatched) {
        // Gesture start. Release the mouse button ImGui thinks is held by the
        // first finger, so no rotate drag is left running underneath us.
        g_pinchLatched = true;
        g_prevPinchDist = 0.0f;
        ImGui::GetIO().AddMouseButtonEvent(0, false);
    }

    if (g_pinchLatched) {
        if (count >= 2) {
            const float x0 = AMotionEvent_getX(event, 0), y0 = AMotionEvent_getY(event, 0);
            const float x1 = AMotionEvent_getX(event, 1), y1 = AMotionEvent_getY(event, 1);
            const float dx = x0 - x1, dy = y0 - y1;
            const float dist = std::sqrt(dx * dx + dy * dy);
            const float cx = (x0 + x1) * 0.5f, cy = (y0 + y1) * 0.5f;

            if (g_prevPinchDist <= 0.0f || action == AMOTION_EVENT_ACTION_POINTER_DOWN ||
                action == AMOTION_EVENT_ACTION_POINTER_UP) {
                // A finger arrived or left: re-baseline instead of reporting the
                // discontinuity as a huge zoom/pan.
                g_prevPinchDist = dist;
                g_prevPinchCx = cx;
                g_prevPinchCy = cy;
            } else if (action == AMOTION_EVENT_ACTION_MOVE && dist > 1.0f) {
                // Spreading (dist grows) zooms in, i.e. the camera moves closer,
                // so the factor is prevDist/dist: <1 spreading, >1 pinching.
                g_pendingPinchZoom *= (g_prevPinchDist / dist);
                g_pendingPanX += cx - g_prevPinchCx;
                g_pendingPanY += cy - g_prevPinchCy;
                g_prevPinchDist = dist;
                g_prevPinchCx = cx;
                g_prevPinchCy = cy;
            }
        }
        g_activePointers = static_cast<int>(count);
        // Hold the latch until every finger is off the glass. Letting go earlier
        // would hand ImGui a lone finger sitting mid-screen, which it would read
        // as the start of a drag.
        if (lastUp || count == 0) {
            g_pinchLatched = false;
            g_prevPinchDist = 0.0f;
        }
        return 1; // consume
    }

    g_prevPinchDist = 0.0f;
    g_activePointers = static_cast<int>(count);
    return ImGui_ImplAndroid_HandleInputEvent(event);
}

void runWindow(android_app* app) {
    EglState egl;
    if (!createEgl(app->window, egl)) {
        LOGE("Unable to initialize EGL/GLES 3");
        destroyEgl(egl);
        return;
    }

    // UI scale, from two constraints at once.
    //
    // Density alone (the old rule) gives physically correct sizes but ignores how
    // few pixels a phone actually has: at 400 dpi it lands on 2.5x, and a 1080 px
    // tall screen then fits barely a dozen rows of text - which is exactly the
    // "everything is huge and nothing is usable" problem. So take the density
    // scale, then cap it by a pixel budget: the short edge has to hold a useful
    // number of rows no matter how dense the panel is.
    float densityScale = 0.0f;
    if (app->config) {
        int32_t density = AConfiguration_getDensity(app->config);
        if (density > 0 && density != ACONFIGURATION_DENSITY_ANY &&
            density != ACONFIGURATION_DENSITY_NONE) {
            densityScale = static_cast<float>(density) / 160.0f;
        }
    }
    const float shortEdge = static_cast<float>(std::min(egl.width, egl.height));
    const float fitScale = shortEdge > 1.0f ? shortEdge / 480.0f : 2.0f;
    float uiScale = densityScale > 0.0f ? std::min(densityScale, fitScale) : fitScale;
    uiScale = std::clamp(uiScale, 1.0f, 3.0f);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    ImGui::GetStyle().ScaleAllSizes(uiScale);
    ImGui_ImplAndroid_Init(app->window);
    ImGui_ImplOpenGL3_Init(SXWNL_GLSL_VERSION_DIRECTIVE);

    // Paint the brand background straight away. The window is showing the theme
    // drawable until the first swap, and an unpainted GL surface in between
    // would flash black.
    glViewport(0, 0, egl.width, egl.height);
    glClearColor(0.020f, 0.031f, 0.063f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    eglSwapBuffers(egl.display, egl.surface);

    // The font has to come out of the APK before anything can be drawn with it,
    // and the glyph atlas has to be complete before the first ImGui frame -
    // adding faces later would leave the backend holding a stale GL texture, and
    // the call to rebuild it differs between ImGui versions. So: unpack just the
    // font, build the atlas once with the real faces, and only then start the
    // splash. Everything slow after this point paints a frame.
    const std::string resourceDir =
        std::string(app->activity->internalDataPath) + "/resources";
    const std::string fontRel = "fonts/NotoSansCJKsc-Regular.otf";
    copyAsset(app->activity->assetManager, "resources/" + fontRel,
              resourceDir + "/" + fontRel);
    chdir(app->activity->internalDataPath);

    const std::string fontPath = resourceDir + "/" + fontRel;
    ImGuiIO& io = ImGui::GetIO();
    // Three sizes: body (the default), a denser face for tables and long text
    // readouts, and a slightly larger one for page titles and the calendar. Body
    // and small carry the full CJK range because calendar notes and festival
    // names reach well past the common set; the title face only ever shows short
    // labels, so the common range keeps its share of the atlas small.
    const float bodyPx  = std::round(15.0f * uiScale);
    const float smallPx = std::round(12.5f * uiScale);
    const float titlePx = std::round(17.5f * uiScale);
    ImFont* fontBody  = io.Fonts->AddFontFromFileTTF(fontPath.c_str(), bodyPx, nullptr,
                            io.Fonts->GetGlyphRangesChineseFull());
    ImFont* fontSmall = fontBody ? io.Fonts->AddFontFromFileTTF(fontPath.c_str(), smallPx,
                            nullptr, io.Fonts->GetGlyphRangesChineseFull()) : nullptr;
    ImFont* fontTitle = fontBody ? io.Fonts->AddFontFromFileTTF(fontPath.c_str(), titlePx,
                            nullptr, io.Fonts->GetGlyphRangesChineseSimplifiedCommon()) : nullptr;
    if (!fontBody) {
        LOGE("Cannot load %s; falling back to the built-in font", fontPath.c_str());
        io.Fonts->AddFontDefault();
    }
    io.FontDefault = fontBody;
    sx::SetUiFonts(fontBody, fontSmall, fontTitle);

    // First frame: rasterising the full CJK range happens inside it.
    drawSplashFrame(app, egl, 0.06f, "preparing fonts", uiScale);
    prepareResources(app, [&](float f, const char*) {
        drawSplashFrame(app, egl, 0.10f + 0.38f * f, "unpacking resources", uiScale);
    });
    if (app->destroyRequested || !app->window) { destroyEgl(egl); return; }

    // Touch sizing: fatter scrollbars and grabs, softer corners, and a few extra
    // pixels of slop around every widget so fingertips land where they aim.
    ImGuiStyle& style = ImGui::GetStyle();
    style.ScrollbarSize   = std::round(16.0f * uiScale);
    style.GrabMinSize     = std::round(18.0f * uiScale);
    style.FrameRounding   = std::round(4.0f * uiScale);
    style.GrabRounding    = style.FrameRounding;
    style.ScrollbarRounding = style.FrameRounding;
    style.WindowRounding  = 0.0f;
    style.TouchExtraPadding = ImVec2(std::round(3.0f * uiScale),
                                     std::round(4.0f * uiScale));

    constexpr double kDefaultLongitude = 116.4;
    constexpr double kDefaultLatitude = 39.9;
    init_ob();
    jw.J = kDefaultLongitude;
    jw.W = kDefaultLatitude;

    sx::Scene scene;
    sx::Renderer renderer;
    drawSplashFrame(app, egl, 0.55f, "starting renderer", uiScale);
    if (!renderer.init()) {
        LOGE("Unable to initialize GLES renderer");
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplAndroid_Shutdown();
        ImGui::DestroyContext();
        destroyEgl(egl);
        return;
    }
    renderer.loadModels(resourceDir, [&](float f, const char* what) {
        char label[64];
        std::snprintf(label, sizeof(label), "loading %s", what ? what : "");
        drawSplashFrame(app, egl, 0.57f + 0.41f * f, label, uiScale);
    });
    drawSplashFrame(app, egl, 1.0f, "ready", uiScale);

    gx::OrbitCamera camera;
    sx::RenderOptions renderOptions;
    sx::PanelState panelState;
    // Scale panel widths, rails, splitters and fixed-size cards to match the
    // density-scaled font. Must precede LoadAppSettings so persisted widths
    // clamp against the scaled min/max bounds.
    sx::SetUiScale(uiScale);
    sx::SetTouchMode(true);
    sx::LoadAppSettings(renderOptions, panelState);
    scene.clock().speedDaysPerSec =
        static_cast<float>(sx::speedToDaysPerSecond(panelState.speedUnit,
                                                    panelState.speedAmount));
    {
        Date date = sx::localDateFromUtcJD(scene.clock().jd, panelState.timezoneHours);
        panelState.year = panelState.calYear = panelState.termYear = date.Y;
        panelState.month = panelState.calMonth = date.M;
        panelState.day = date.D;
        panelState.hour = date.h;
        panelState.eclipseYear = date.Y;
        panelState.eclipseMonth = date.M;
    }

    auto previous = std::chrono::steady_clock::now();
    while (!app->destroyRequested && app->window) {
        int events = 0;
        android_poll_source* source = nullptr;
        while (ALooper_pollOnce(0, nullptr, &events,
                                reinterpret_cast<void**>(&source)) >= 0) {
            if (source) source->process(app, source);
            if (app->destroyRequested || !app->window) break;
        }
        if (app->destroyRequested || !app->window) break;

        auto now = std::chrono::steady_clock::now();
        double delta = std::chrono::duration<double>(now - previous).count();
        previous = now;
        scene.clock().advance(delta);
        scene.update();
        camera.updateFocus(static_cast<float>(delta));

        // Two-finger camera gestures only mean anything while the 3-D view is on
        // screen; elsewhere the deltas are drained and dropped.
        const bool cameraPage = (panelState.mobilePage == 0);
        g_pinchEnabled = cameraPage;
        if (cameraPage) {
            if (g_pendingPinchZoom != 1.0f) camera.zoom(g_pendingPinchZoom);
            if (g_pendingPanX != 0.0f || g_pendingPanY != 0.0f)
                camera.pan(g_pendingPanX, g_pendingPanY);
        }
        g_pendingPinchZoom = 1.0f;
        g_pendingPanX = g_pendingPanY = 0.0f;

        sx::SetSafeAreaInsets(
            static_cast<float>(g_insetLeft.load(std::memory_order_relaxed)),
            static_cast<float>(g_insetTop.load(std::memory_order_relaxed)),
            static_cast<float>(g_insetRight.load(std::memory_order_relaxed)),
            static_cast<float>(g_insetBottom.load(std::memory_order_relaxed)));
        ImGui::GetIO().FontGlobalScale = panelState.fontScale;

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplAndroid_NewFrame();
        ImGui::NewFrame();
        sx::DrawMobileUI(renderer, scene, camera, renderOptions, panelState);
        ImGui::Render();

        eglQuerySurface(egl.display, egl.surface, EGL_WIDTH, &egl.width);
        eglQuerySurface(egl.display, egl.surface, EGL_HEIGHT, &egl.height);
        glViewport(0, 0, egl.width, egl.height);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        eglSwapBuffers(egl.display, egl.surface);
    }

    sx::SaveAppSettings(renderOptions, panelState);
    renderer.shutdown();
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplAndroid_Shutdown();
    ImGui::DestroyContext();
    destroyEgl(egl);
}

} // namespace

void android_main(android_app* app) {
    app->onInputEvent = onInputEvent;
    ANativeActivity_setWindowFlags(app->activity, AWINDOW_FLAG_KEEP_SCREEN_ON, 0);

    while (!app->destroyRequested) {
        while (!app->window && !app->destroyRequested) {
            int events = 0;
            android_poll_source* source = nullptr;
            if (ALooper_pollOnce(-1, nullptr, &events,
                                 reinterpret_cast<void**>(&source)) >= 0 &&
                source) {
                source->process(app, source);
            }
        }
        if (!app->destroyRequested) runWindow(app);
    }
}
