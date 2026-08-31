#include <android/asset_manager.h>
#include <android/configuration.h>
#include <android/input.h>
#include <android/log.h>
#include <android/window.h>
#include <android_native_app_glue.h>
#include <EGL/egl.h>
#include <GLES3/gl3.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

#include "imgui.h"
#include "backends/imgui_impl_android.h"
#include "backends/imgui_impl_opengl3.h"

#include "camera.h"
#include "gles/gl_compat.h"
#include "panels.h"
#include "renderer.h"
#include "scene.h"
#include "lunar.h"
#include "lat_lon_data.h"

#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "sxwnl", __VA_ARGS__)

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

std::string prepareResources(android_app* app) {
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
    for (const char* file : files) {
        if (!copyAsset(app->activity->assetManager, "resources/" + std::string(file),
                       root + "/" + file)) {
            LOGE("Resource extraction incomplete");
        }
    }
    return root;
}

// ---- Pinch-to-zoom gesture state -------------------------------------------
// Two-finger pinch adjusts the 3D solar-system camera distance. onInputEvent()
// runs on the same thread as the main loop (during source->process), so plain
// globals are safe. The loop consumes g_pendingZoom once per frame.
float g_pendingPinchZoom = 1.0f;  // accumulated multiplicative zoom factor
float g_prevPinchDist    = 0.0f;  // finger spacing at last pinch sample (px)
int   g_activePointers   = 0;

int32_t onInputEvent(android_app*, AInputEvent* event) {
    if (AInputEvent_getType(event) == AINPUT_EVENT_TYPE_MOTION) {
        const int32_t action = AMotionEvent_getAction(event) & AMOTION_EVENT_ACTION_MASK;
        const size_t  count  = AMotionEvent_getPointerCount(event);

        if (count >= 2) {
            const float dx = AMotionEvent_getX(event, 0) - AMotionEvent_getX(event, 1);
            const float dy = AMotionEvent_getY(event, 0) - AMotionEvent_getY(event, 1);
            const float dist = std::sqrt(dx * dx + dy * dy);

            if (action == AMOTION_EVENT_ACTION_POINTER_DOWN || g_prevPinchDist <= 0.0f) {
                // Gesture start (or a finger just changed): (re)establish baseline.
                g_prevPinchDist = dist;
            } else if (action == AMOTION_EVENT_ACTION_MOVE && dist > 1.0f) {
                // Fingers spreading (dist grows) zooms in -> camera distance shrinks,
                // so the factor is prevDist/dist (<1 when spreading, >1 when pinching).
                g_pendingPinchZoom *= (g_prevPinchDist / dist);
                g_prevPinchDist = dist;
            }
            g_activePointers = static_cast<int>(count);
            if (action == AMOTION_EVENT_ACTION_POINTER_UP) g_prevPinchDist = 0.0f;
            return 1; // consume: keep ImGui from treating the pinch as a drag/tap
        }

        // Zero or one finger: reset the pinch baseline and fall through to ImGui.
        g_prevPinchDist = 0.0f;
        g_activePointers = static_cast<int>(count);
    }
    return ImGui_ImplAndroid_HandleInputEvent(event);
}

void runWindow(android_app* app) {
    EglState egl;
    if (!createEgl(app->window, egl)) {
        LOGE("Unable to initialize EGL/GLES 3");
        destroyEgl(egl);
        return;
    }

    // Derive a UI scale from screen density so text and touch targets are not
    // tiny on high-DPI phones (density 160 dpi == scale 1.0, matching desktop).
    // Fall back to a pixel-size heuristic when the density is unavailable.
    float uiScale = 1.0f;
    bool densityValid = false;
    if (app->config) {
        int32_t density = AConfiguration_getDensity(app->config);
        if (density > 0 && density != ACONFIGURATION_DENSITY_ANY &&
            density != ACONFIGURATION_DENSITY_NONE) {
            uiScale = static_cast<float>(density) / 160.0f;
            densityValid = true;
        }
    }
    if (!densityValid) {
        float shortEdge = static_cast<float>(std::min(egl.width, egl.height));
        uiScale = shortEdge > 1.0f ? shortEdge / 480.0f : 1.75f;
    }
    uiScale = std::clamp(uiScale, 1.0f, 3.5f);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    ImGui::GetStyle().ScaleAllSizes(uiScale);
    ImGui_ImplAndroid_Init(app->window);
    ImGui_ImplOpenGL3_Init(SXWNL_GLSL_VERSION_DIRECTIVE);

    std::string resourceDir = prepareResources(app);
    chdir(app->activity->internalDataPath);
    std::string fontPath = resourceDir + "/fonts/NotoSansCJKsc-Regular.otf";
    ImGuiIO& io = ImGui::GetIO();
    // 20 dp base, converted to physical pixels via the density-derived scale.
    float fontPx = std::round(20.0f * uiScale);
    if (!io.Fonts->AddFontFromFileTTF(fontPath.c_str(), fontPx, nullptr,
                                      io.Fonts->GetGlyphRangesChineseFull())) {
        io.Fonts->AddFontDefault();
    }

    constexpr double kDefaultLongitude = 116.4;
    constexpr double kDefaultLatitude = 39.9;
    init_ob();
    jw.J = kDefaultLongitude;
    jw.W = kDefaultLatitude;

    sx::Scene scene;
    sx::Renderer renderer;
    if (!renderer.init()) {
        LOGE("Unable to initialize GLES renderer");
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplAndroid_Shutdown();
        ImGui::DestroyContext();
        destroyEgl(egl);
        return;
    }
    renderer.loadModels(resourceDir);

    gx::OrbitCamera camera;
    sx::RenderOptions renderOptions;
    sx::PanelState panelState;
    // Scale panel widths, rails, splitters and fixed-size cards to match the
    // density-scaled font. Must precede LoadAppSettings so persisted widths
    // clamp against the scaled min/max bounds.
    sx::SetUiScale(uiScale);
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

        // Apply any two-finger pinch accumulated since the last frame.
        if (g_pendingPinchZoom != 1.0f) {
            camera.zoom(g_pendingPinchZoom);
            g_pendingPinchZoom = 1.0f;
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplAndroid_NewFrame();
        ImGui::NewFrame();
        sx::DrawMainMenuBar(scene, renderOptions, panelState);
        sx::DrawSidebar(scene, renderOptions, panelState, camera);
        sx::DrawViewportPanel(renderer, scene, camera, renderOptions, panelState);
        sx::DrawToolsPanel(renderer, scene, panelState);
        sx::DrawPanelSplitters(panelState);
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
