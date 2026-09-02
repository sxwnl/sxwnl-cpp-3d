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
    EGLConfig  config  = nullptr;
    int width = 0;
    int height = 0;
};

// The display and the rendering context are created once per process. The
// context owns every texture, mesh, shader and font atlas the app loads, so
// keeping it alive while the app sits in the background is what makes coming
// back instant instead of replaying the whole loading screen.
bool createEglContext(EglState& egl) {
    const EGLint configAttrs[] = {
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8, EGL_DEPTH_SIZE, 24,
        EGL_NONE
    };
    const EGLint contextAttrs[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
    EGLint count = 0;

    egl.display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (egl.display == EGL_NO_DISPLAY || !eglInitialize(egl.display, nullptr, nullptr) ||
        !eglChooseConfig(egl.display, configAttrs, &egl.config, 1, &count) || count != 1) {
        return false;
    }
    egl.context = eglCreateContext(egl.display, egl.config, EGL_NO_CONTEXT, contextAttrs);
    return egl.context != EGL_NO_CONTEXT;
}

// Binds the existing context to a surface for the window we have just been
// handed. Called again on every return to the foreground.
bool attachEglSurface(ANativeWindow* window, EglState& egl) {
    EGLint format = 0;
    if (!eglGetConfigAttrib(egl.display, egl.config, EGL_NATIVE_VISUAL_ID, &format))
        return false;

    ANativeWindow_setBuffersGeometry(window, 0, 0, format);
    egl.surface = eglCreateWindowSurface(egl.display, egl.config, window, nullptr);
    if (egl.surface == EGL_NO_SURFACE) return false;
    if (!eglMakeCurrent(egl.display, egl.surface, egl.surface, egl.context)) {
        eglDestroySurface(egl.display, egl.surface);
        egl.surface = EGL_NO_SURFACE;
        return false;
    }

    eglQuerySurface(egl.display, egl.surface, EGL_WIDTH, &egl.width);
    eglQuerySurface(egl.display, egl.surface, EGL_HEIGHT, &egl.height);
    eglSwapInterval(egl.display, 1);
    return true;
}

// Drops only the surface: the context, and everything loaded into it, survives.
void detachEglSurface(EglState& egl) {
    if (egl.display == EGL_NO_DISPLAY) return;
    eglMakeCurrent(egl.display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    if (egl.surface != EGL_NO_SURFACE) {
        eglDestroySurface(egl.display, egl.surface);
        egl.surface = EGL_NO_SURFACE;
    }
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
        "fonts/NotoSansSymbols-Astro.ttf",
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

// ---- Soft keyboard ----------------------------------------------------------
// ImGui's Android backend raises no IME of its own (its source says so outright)
// and the NDK exposes no equivalent of KeyEvent.getUnicodeChar(), so text fields
// used to be unreachable. MainActivity owns an invisible EditText that holds the
// IME focus and queues committed characters; these helpers drive it over JNI.
android_app* g_app = nullptr;

struct JniScope {
    JavaVM* vm = nullptr;
    JNIEnv* env = nullptr;
    bool attached = false;

    explicit JniScope(JavaVM* v) : vm(v) {
        if (!vm) return;
        if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK) {
            if (vm->AttachCurrentThread(&env, nullptr) == JNI_OK) attached = true;
            else env = nullptr;
        }
    }
    ~JniScope() { if (attached && vm) vm->DetachCurrentThread(); }
    JniScope(const JniScope&) = delete;
    JniScope& operator=(const JniScope&) = delete;
};

void callActivityVoid(const char* name) {
    if (!g_app || !g_app->activity) return;
    JniScope js(g_app->activity->vm);
    if (!js.env) return;
    jclass clazz = js.env->GetObjectClass(g_app->activity->clazz);
    jmethodID mid = js.env->GetMethodID(clazz, name, "()V");
    if (mid) js.env->CallVoidMethod(g_app->activity->clazz, mid);
    else js.env->ExceptionClear();
    js.env->DeleteLocalRef(clazz);
}

// Hands a string to android.content.ClipboardManager. Wired into ImGui as
// Platform_SetClipboardTextFn: the Android backend ships no clipboard at all, so
// without this the copy buttons would fill ImGui's in-process buffer and nothing
// would reach any other app.
void setClipboardText(ImGuiContext*, const char* text) {
    if (!text || !g_app || !g_app->activity) return;
    JniScope js(g_app->activity->vm);
    if (!js.env) return;
    jclass clazz = js.env->GetObjectClass(g_app->activity->clazz);
    jmethodID mid = js.env->GetMethodID(clazz, "setClipboardText", "(Ljava/lang/String;)V");
    if (mid) {
        jstring value = js.env->NewStringUTF(text);
        if (value) {
            js.env->CallVoidMethod(g_app->activity->clazz, mid, value);
            js.env->DeleteLocalRef(value);
        }
    } else {
        js.env->ExceptionClear();
    }
    js.env->DeleteLocalRef(clazz);
}

// Drains everything the IME has committed since the last frame. Backspace comes
// through as 8 because it produces no character of its own.
void pollUnicodeChars() {
    if (!g_app || !g_app->activity) return;
    JniScope js(g_app->activity->vm);
    if (!js.env) return;
    jclass clazz = js.env->GetObjectClass(g_app->activity->clazz);
    jmethodID mid = js.env->GetMethodID(clazz, "pollUnicodeChar", "()I");
    if (mid) {
        ImGuiIO& io = ImGui::GetIO();
        for (int guard = 0; guard < 256; ++guard) {
            const jint c = js.env->CallIntMethod(g_app->activity->clazz, mid);
            if (c == 0) break;
            if (c == 8) {
                io.AddKeyEvent(ImGuiKey_Backspace, true);
                io.AddKeyEvent(ImGuiKey_Backspace, false);
            } else {
                io.AddInputCharacter(static_cast<unsigned int>(c));
            }
        }
    } else {
        js.env->ExceptionClear();
    }
    js.env->DeleteLocalRef(clazz);
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

// ---- Multi-touch gestures ---------------------------------------------------
// Two fingers are read here and applied by the frame loop, which decides what
// they mean for the page on screen: the 3-D camera on the solar-system page
// (spacing zooms, midpoint pans), the text size everywhere else. onInputEvent()
// runs on the same thread as the main loop (during source->process), so plain
// globals are safe; the loop drains the pending deltas once per frame.
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

// Back-key navigation and the page the frame loop last drew. Read/written from
// both the input callback and the loop, which run on the same thread, but they
// are atomics to keep that assumption honest.
std::atomic<int>  g_currentPage{0};
std::atomic<bool> g_backPending{false};

int32_t onInputEvent(android_app*, AInputEvent* event) {
    if (AInputEvent_getType(event) == AINPUT_EVENT_TYPE_KEY) {
        const int32_t code   = AKeyEvent_getKeyCode(event);
        const int32_t action = AKeyEvent_getAction(event);

        // Back steps out of the current page before it leaves the app; from the
        // home page it falls through so Android can close us as usual.
        if (code == AKEYCODE_BACK) {
            if (g_currentPage.load(std::memory_order_relaxed) == 0)
                return ImGui_ImplAndroid_HandleInputEvent(event);
            if (action == AKEY_EVENT_ACTION_UP)
                g_backPending.store(true, std::memory_order_relaxed);
            return 1;
        }
        // Characters arrive through the hidden EditText (pollUnicodeChars), so
        // only the non-text keys need forwarding here.
        return ImGui_ImplAndroid_HandleInputEvent(event);
    }
    if (AInputEvent_getType(event) != AINPUT_EVENT_TYPE_MOTION)
        return ImGui_ImplAndroid_HandleInputEvent(event);

    const int32_t action = AMotionEvent_getAction(event) & AMOTION_EVENT_ACTION_MASK;
    const size_t  count  = AMotionEvent_getPointerCount(event);
    const bool    lastUp = (action == AMOTION_EVENT_ACTION_UP ||
                            action == AMOTION_EVENT_ACTION_CANCEL);

    if (count >= 2 && !g_pinchLatched) {
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

// ---------------------------------------------------------------------------
//  Engine
// ---------------------------------------------------------------------------
// Everything that must outlive a trip through the background. Previously the
// whole of this was local to a runWindow() call that returned the moment the
// window went away, so every task switch tore down the GL context and replayed
// the entire loading screen. Now only the EGL *surface* comes and goes.
struct Engine {
    EglState egl;
    sx::Scene scene;
    sx::Renderer renderer;
    gx::OrbitCamera camera;
    sx::RenderOptions renderOptions;
    sx::PanelState panelState;

    float uiScale = 1.0f;
    bool  contentReady = false;   // assets, fonts and models are loaded
    bool  fontsReady = false;     // glyph atlas built (survives a retried load)
    bool  imguiReady = false;
    bool  keyboardShown = false;
    std::chrono::steady_clock::time_point previous{};
};

// Derives the UI scale from two constraints at once.
//
// Density alone (the old rule) gives physically correct sizes but ignores how
// few pixels a phone actually has: at 400 dpi it lands on 2.5x, and a 1080 px
// tall screen then fits barely a dozen rows of text. So take the density scale,
// then cap it by a pixel budget so the short edge always holds a useful number
// of rows no matter how dense the panel is.
float computeUiScale(android_app* app, const EglState& egl) {
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
    float scale = densityScale > 0.0f ? std::min(densityScale, fitScale) : fitScale;
    return std::clamp(scale, 1.0f, 3.0f);
}

void applyTouchStyle(float uiScale) {
    // Fatter scrollbars and grabs, softer corners, and a few extra pixels of
    // slop around every widget so fingertips land where they aim.
    ImGuiStyle& style = ImGui::GetStyle();
    style.ScrollbarSize     = std::round(16.0f * uiScale);
    style.GrabMinSize       = std::round(18.0f * uiScale);
    style.FrameRounding     = std::round(4.0f * uiScale);
    style.GrabRounding      = style.FrameRounding;
    style.ScrollbarRounding = style.FrameRounding;
    style.WindowRounding    = 0.0f;
    style.TouchExtraPadding = ImVec2(std::round(3.0f * uiScale),
                                     std::round(4.0f * uiScale));
}

// Why bringing the engine up on a window ended. Retry and Fatal both leave a
// black screen right now, but only one of them is worth trying again: losing the
// window part-way through the first load (a task switch during a slow cold
// start) must not brick the app until it is force-stopped.
enum class Attach { Ok, Retry, Fatal };

// Unpacks the font and builds the glyph atlas.
//
// This has to happen before the first ImGui frame: adding faces later would
// leave the backend holding a stale GL texture, and the call to rebuild it
// differs between ImGui versions. Called at most once per process - loadContent
// can be re-entered after a retry, and adding the faces again would stack
// duplicates into the atlas.
void buildFonts(android_app* app, Engine& e, const std::string& resourceDir) {
    if (e.fontsReady) return;

    const std::string fontRel = "fonts/NotoSansCJKsc-Regular.otf";
    const std::string symbolRel = "fonts/NotoSansSymbols-Astro.ttf";
    copyAsset(app->activity->assetManager, "resources/" + fontRel,
              resourceDir + "/" + fontRel);
    copyAsset(app->activity->assetManager, "resources/" + symbolRel,
              resourceDir + "/" + symbolRel);
    chdir(app->activity->internalDataPath);

    const std::string fontPath = resourceDir + "/" + fontRel;
    const std::string symbolPath = resourceDir + "/" + symbolRel;
    ImGuiIO& io = ImGui::GetIO();
    // Three sizes: body (the default), a denser face for tables and long text
    // readouts, and a slightly larger one for page titles and the calendar. Body
    // and small carry the full CJK range because calendar notes and festival
    // names reach well past the common set; the title face only ever shows short
    // labels, so the common range keeps its share of the atlas small.
    const float bodyPx  = std::round(15.0f * e.uiScale);
    const float smallPx = std::round(12.5f * e.uiScale);
    const float titlePx = std::round(17.5f * e.uiScale);
    // Each face merges the zodiac/planet symbol subset straight after it is
    // added: the merge attaches to the face added last, so it has to sit
    // between the three AddFontFromFileTTF calls rather than after them.
    ImFont* fontBody  = io.Fonts->AddFontFromFileTTF(fontPath.c_str(), bodyPx, nullptr,
                            io.Fonts->GetGlyphRangesChineseFull());
    if (fontBody) sx::AddAstroSymbolFont(symbolPath.c_str(), bodyPx);
    ImFont* fontSmall = fontBody ? io.Fonts->AddFontFromFileTTF(fontPath.c_str(), smallPx,
                            nullptr, io.Fonts->GetGlyphRangesChineseFull()) : nullptr;
    if (fontSmall) sx::AddAstroSymbolFont(symbolPath.c_str(), smallPx);
    ImFont* fontTitle = fontBody ? io.Fonts->AddFontFromFileTTF(fontPath.c_str(), titlePx,
                            nullptr, io.Fonts->GetGlyphRangesChineseSimplifiedCommon()) : nullptr;
    if (fontTitle) sx::AddAstroSymbolFont(symbolPath.c_str(), titlePx);
    if (!fontBody) {
        LOGE("Cannot load %s; falling back to the built-in font", fontPath.c_str());
        io.Fonts->AddFontDefault();
    }
    io.FontDefault = fontBody;
    sx::SetUiFonts(fontBody, fontSmall, fontTitle);
    e.fontsReady = true;
}

// One-time load: assets, fonts, renderer, models. Runs behind the splash, so
// every slow step below paints a frame rather than holding a blank window.
Attach loadContent(android_app* app, Engine& e) {
    const std::string resourceDir =
        std::string(app->activity->internalDataPath) + "/resources";
    buildFonts(app, e, resourceDir);

    // First frame: rasterising the full CJK range happens inside it.
    drawSplashFrame(app, e.egl, 0.06f, "preparing fonts", e.uiScale);
    prepareResources(app, [&](float f, const char*) {
        drawSplashFrame(app, e.egl, 0.10f + 0.38f * f, "unpacking resources", e.uiScale);
    });
    // Backgrounded mid-load. Nothing past this point has run yet, so coming back
    // simply picks up here.
    if (app->destroyRequested || !app->window) return Attach::Retry;

    applyTouchStyle(e.uiScale);

    constexpr double kDefaultLongitude = 116.4;
    constexpr double kDefaultLatitude = 39.9;
    init_ob();
    jw.J = kDefaultLongitude;
    jw.W = kDefaultLatitude;

    drawSplashFrame(app, e.egl, 0.55f, "starting renderer", e.uiScale);
    if (!e.renderer.init()) {
        LOGE("Unable to initialize GLES renderer");
        return Attach::Fatal;
    }
    e.renderer.loadModels(resourceDir, [&](float f, const char* what) {
        char label[64];
        std::snprintf(label, sizeof(label), "loading %s", what ? what : "");
        drawSplashFrame(app, e.egl, 0.57f + 0.41f * f, label, e.uiScale);
    });
    drawSplashFrame(app, e.egl, 1.0f, "ready", e.uiScale);

    // Scale panel widths, rails and fixed-size cards to match the density-scaled
    // font. Must precede LoadAppSettings so persisted widths clamp correctly.
    sx::SetUiScale(e.uiScale);
    sx::SetTouchMode(true);
    // Phone default: text one size up from the desktop baseline. Set before
    // LoadAppSettings so a saved preference still wins.
    e.panelState.fontScale = 1.4f;
    sx::LoadAppSettings(e.renderOptions, e.panelState);
    e.scene.clock().speedDaysPerSec =
        static_cast<float>(sx::speedToDaysPerSecond(e.panelState.speedUnit,
                                                    e.panelState.speedAmount));
    {
        Date date = sx::localDateFromUtcJD(e.scene.clock().jd, e.panelState.timezoneHours);
        e.panelState.year = e.panelState.calYear = e.panelState.termYear = date.Y;
        e.panelState.month = e.panelState.calMonth = date.M;
        e.panelState.day = date.D;
        e.panelState.hour = date.h;
        e.panelState.eclipseYear = date.Y;
        e.panelState.eclipseMonth = date.M;
    }
    e.contentReady = true;
    return Attach::Ok;
}

// Brings the engine up on the window we have just been given. On the first call
// this builds the context and loads everything; afterwards it only recreates the
// surface, which is why returning from the background is instant.
Attach attachWindow(android_app* app, Engine& e) {
    const bool first = (e.egl.context == EGL_NO_CONTEXT);
    if (first && !createEglContext(e.egl)) {
        LOGE("Unable to initialize EGL/GLES 3");
        return Attach::Fatal;   // no GLES 3 here; retrying cannot help
    }
    if (!attachEglSurface(app->window, e.egl)) {
        // Nothing was left half-built, so the next window is worth a try.
        LOGE("Unable to create the window surface");
        return Attach::Retry;
    }

    if (!e.imguiReady) {
        e.uiScale = computeUiScale(app, e.egl);
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();

        // Keep ImGui's error recovery active - it repairs the frame state and is
        // what stops a mistake becoming a crash - but stop it reporting to the
        // screen. Its diagnostic is written for whoever is holding the debugger,
        // and on a shipped app it just drops a red "MESSAGE FROM DEAR IMGUI"
        // panel over the sky. The desktop build deliberately keeps the overlay:
        // that is the surface these get caught on.
        {
            ImGuiIO& io = ImGui::GetIO();
            io.ConfigErrorRecovery = true;
            io.ConfigErrorRecoveryEnableAssert = false;
            io.ConfigErrorRecoveryEnableDebugLog = false;
            io.ConfigErrorRecoveryEnableTooltip = false;
        }

        ImGui::GetPlatformIO().Platform_SetClipboardTextFn = setClipboardText;

        ImGui::StyleColorsDark();
        ImGui::GetStyle().ScaleAllSizes(e.uiScale);
        ImGui_ImplAndroid_Init(app->window);
        ImGui_ImplOpenGL3_Init(SXWNL_GLSL_VERSION_DIRECTIVE);
        e.imguiReady = true;
    } else {
        // Same ImGui context and the same GL objects; only the native window
        // handle changed, and the platform backend caches it for sizing.
        ImGui_ImplAndroid_Shutdown();
        ImGui_ImplAndroid_Init(app->window);
    }

    // Paint the brand background straight away: the window shows the theme
    // drawable until the first swap, and an unpainted surface flashes black.
    glViewport(0, 0, e.egl.width, e.egl.height);
    glClearColor(0.020f, 0.031f, 0.063f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    eglSwapBuffers(e.egl.display, e.egl.surface);

    if (!e.contentReady) {
        const Attach loaded = loadContent(app, e);
        if (loaded != Attach::Ok) return loaded;
    }
    e.previous = std::chrono::steady_clock::now();
    return Attach::Ok;
}

void detachWindow(Engine& e) {
    // The process can be killed while backgrounded, so persist here rather than
    // only on a clean exit.
    if (e.contentReady) sx::SaveAppSettings(e.renderOptions, e.panelState);
    detachEglSurface(e.egl);
}

void drawFrame(android_app* app, Engine& e) {
    auto now = std::chrono::steady_clock::now();
    double delta = std::chrono::duration<double>(now - e.previous).count();
    e.previous = now;
    // A long pause in the background would otherwise arrive as one huge step.
    if (delta > 0.5) delta = 0.5;

    e.scene.clock().advance(delta);
    e.scene.update();
    e.camera.updateFocus(static_cast<float>(delta));

    if (g_backPending.exchange(false)) e.panelState.mobilePage = 0;
    g_currentPage.store(e.panelState.mobilePage, std::memory_order_relaxed);

    // Where a pinch goes depends on the page. On the solar-system page it is
    // the 3-D camera - spreading flies closer, a two-finger drag pans. On every
    // other page there is no camera to drive, so the same gesture resizes the
    // text instead, which is the one thing a reader wants to change on a phone.
    const bool cameraPage = (e.panelState.mobilePage == 0);
    if (cameraPage) {
        if (g_pendingPinchZoom != 1.0f) e.camera.zoom(g_pendingPinchZoom);
        if (g_pendingPanX != 0.0f || g_pendingPanY != 0.0f)
            e.camera.pan(g_pendingPanX, g_pendingPanY);
    } else if (g_pendingPinchZoom != 1.0f) {
        // The camera factor is inverted (spreading pulls the camera in, so it
        // is < 1); text has to grow instead, hence the reciprocal.
        const float before = e.panelState.fontScale;
        e.panelState.fontScale = std::clamp(before / g_pendingPinchZoom,
                                            sx::kFontScaleMin, sx::kFontScaleMax);
        if (e.panelState.fontScale != before) sx::NoteFontScaleChanged();
    }
    g_pendingPinchZoom = 1.0f;
    g_pendingPanX = g_pendingPanY = 0.0f;

    sx::SetSafeAreaInsets(
        static_cast<float>(g_insetLeft.load(std::memory_order_relaxed)),
        static_cast<float>(g_insetTop.load(std::memory_order_relaxed)),
        static_cast<float>(g_insetRight.load(std::memory_order_relaxed)),
        static_cast<float>(g_insetBottom.load(std::memory_order_relaxed)));
    ImGui::GetIO().FontGlobalScale = e.panelState.fontScale;

    // Feed anything the IME committed since the last frame, before NewFrame
    // latches the input queue.
    pollUnicodeChars();

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplAndroid_NewFrame();
    ImGui::NewFrame();
    sx::DrawMobileUI(e.renderer, e.scene, e.camera, e.renderOptions, e.panelState);
    ImGui::Render();

    // Follow the focused text field. This has to be edge-triggered: calling
    // show every frame makes the IME tear itself down and rebuild, swallowing
    // input as it goes.
    const bool wantText = ImGui::GetIO().WantTextInput;
    if (wantText != e.keyboardShown) {
        e.keyboardShown = wantText;
        callActivityVoid(wantText ? "showSoftInput" : "hideSoftInput");
    }

    eglQuerySurface(e.egl.display, e.egl.surface, EGL_WIDTH, &e.egl.width);
    eglQuerySurface(e.egl.display, e.egl.surface, EGL_HEIGHT, &e.egl.height);
    glViewport(0, 0, e.egl.width, e.egl.height);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    eglSwapBuffers(e.egl.display, e.egl.surface);
}

void shutdownEngine(Engine& e) {
    if (e.contentReady) sx::SaveAppSettings(e.renderOptions, e.panelState);
    if (e.egl.context != EGL_NO_CONTEXT && e.egl.surface == EGL_NO_SURFACE) {
        // Rebind so the GL deletes below land on a live context.
        eglMakeCurrent(e.egl.display, EGL_NO_SURFACE, EGL_NO_SURFACE, e.egl.context);
    }
    if (e.imguiReady) {
        e.renderer.shutdown();
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplAndroid_Shutdown();
        ImGui::DestroyContext();
        e.imguiReady = false;
    }
    destroyEgl(e.egl);
}

} // namespace

void android_main(android_app* app) {
    g_app = app;
    app->onInputEvent = onInputEvent;
    ANativeActivity_setWindowFlags(app->activity, AWINDOW_FLAG_KEEP_SCREEN_ON, 0);

    Engine engine;
    bool fatal = false;          // this device cannot run the app at all
    bool attachRetryPending = false;  // one attach failed; wait for a new window

    while (!app->destroyRequested) {
        const bool drawing = (engine.egl.surface != EGL_NO_SURFACE) && !fatal;
        // A window with no surface yet is work waiting to be done. Treating that
        // as idle is what caused the v1.3.1 startup black screen: the poll below
        // blocked forever right after APP_CMD_INIT_WINDOW handed us the window,
        // so attachWindow() was never reached. Both conditions have to be false
        // before it is genuinely safe to sleep - and when they are, sleeping is
        // what keeps a backgrounded app free.
        const bool pendingAttach = (app->window != nullptr) &&
                                   (engine.egl.surface == EGL_NO_SURFACE) &&
                                   !fatal && !attachRetryPending;
        int timeout = (drawing || pendingAttach) ? 0 : -1;

        int events = 0;
        android_poll_source* source = nullptr;
        while (ALooper_pollOnce(timeout, nullptr, &events,
                                reinterpret_cast<void**>(&source)) >= 0) {
            if (source) source->process(app, source);
            if (app->destroyRequested) break;
            // We went to sleep with nothing to do and an event just arrived; if
            // it handed us a window, stop draining and go attach to it.
            if (timeout < 0 && app->window) break;
            // Past the first blocking return, drain what is queued without
            // blocking so the loop falls through promptly.
            timeout = 0;
        }
        if (app->destroyRequested) break;

        // The window cycling away clears a failed attach: the next one is new
        // hardware state and deserves a fresh attempt.
        if (!app->window) attachRetryPending = false;

        if (app->window && engine.egl.surface == EGL_NO_SURFACE &&
            !fatal && !attachRetryPending) {
            switch (attachWindow(app, engine)) {
            case Attach::Ok:    break;
            case Attach::Retry: attachRetryPending = true; break;
            case Attach::Fatal: fatal = true; break;
            }
        } else if (!app->window && engine.egl.surface != EGL_NO_SURFACE) {
            detachWindow(engine);
        }

        if (app->window && engine.egl.surface != EGL_NO_SURFACE && !fatal)
            drawFrame(app, engine);
    }

    shutdownEngine(engine);
}
