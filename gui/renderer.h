// OpenGL renderer: draws the solar system to an off-screen FBO.
// Planets use real OBJ meshes loaded from resources/; falls back to a
// procedural UV sphere when a mesh is not available (e.g. Pluto).
// A secondary FBO renders a 3-D moon-phase view.
#ifndef SXWNL_GUI_RENDERER_H
#define SXWNL_GUI_RENDERER_H

#include "camera.h"
#include "scene.h"
#include "objloader.h"

#include <map>
#include <string>

namespace sx {

struct RenderOptions {
    bool showOrbits  = true;
    bool showLabels  = true;
    bool showMoon    = true;
    bool showEarthAxis = true;   // draw Earth's rotation axis
    bool showGravityGrid = true; // relativistic gravity-well visualization
    bool showAsteroids = true;
    bool showSolarFlames = true;
    float gravityGridDensity = 2.0f;
    float gravityGridCurvature = 1.2f;
    float bg[3]      = {0.02f, 0.02f, 0.05f};
};

class Renderer {
public:
    bool init();
    void shutdown();

    // Load planet OBJ (resources/planet/8k-solar-system.obj),
    // moon OBJ (resources/moon/Moon2K.obj), and all textures.
    void loadModels(const std::string& resourceDir);

    // Resize the main off-screen render target. Safe to call every frame.
    void resize(int w, int h);

    // Render the solar system scene to the main FBO.
    void render(const Scene& scene, const gx::OrbitCamera& cam,
                const RenderOptions& opt);

    // Render the moon to the moon-phase FBO. elongDeg is the moon-sun
    // elongation in [0, 360).
    void renderMoonPhase(float elongDeg, bool waxing, float yawDeg, float pitchDeg);

    // Main FBO color texture (for ImGui::Image in the 3-D viewport).
    unsigned int colorTexture() const { return colorTex_; }

    // Moon-phase FBO color texture, y-flipped like main FBO.
    unsigned int moonPhaseTex() const { return moonPhaseTex_; }

    int width()  const { return w_; }
    int height() const { return h_; }

    // VP matrix used for the last render() call (for label projection).
    const gx::Mat4& viewProj() const { return viewProj_; }

    // Resource loading status (for debug overlay).
    int loadedMeshes()   const { return loadedMeshes_; }
    int loadedTextures() const { return loadedTextures_; }

private:
    // FBO helpers.
    void ensureFBO();
    void ensureMoonPhaseFBO();

    // Texture loading. rgba=false loads as RGB; rgba=true loads as RGBA.
    unsigned int loadTexFile(const char* path, bool rgba = false);

    // Main scene FBO.
    unsigned int fbo_ = 0, colorTex_ = 0, depthRbo_ = 0;
    int w_ = 16, h_ = 16;
    bool fboDirty_ = true;

    // Moon-phase FBO (fixed square render target).
    unsigned int moonPhaseFBO_ = 0, moonPhaseTex_ = 0, moonPhaseDepth_ = 0;
    static const int kMoonPhaseSize = 512;

    // Fallback UV sphere (pos+nrm+uv, 8 floats/vertex).
    unsigned int sphereVAO_ = 0, sphereVBO_ = 0, sphereEBO_ = 0;
    int sphereIndexCount_ = 0;

    // Orbit line VAO (pos only, 3 floats/vertex).
    unsigned int lineVAO_ = 0, lineVBO_ = 0;

    // Star-field VAO: pos(3)+size(1)+brightness(1) = 5 floats/star.
    unsigned int starVAO_ = 0, starVBO_ = 0;
    int starCount_ = 0;

    // Asteroid belt VAO: pos(3)+size(1)+brightness(1) = 5 floats/particle.
    unsigned int asteroidVAO_ = 0, asteroidVBO_ = 0;

    // Solar flame particles: pos(3)+size(1)+heat(1)+alpha(1).
    unsigned int flameVAO_ = 0, flameVBO_ = 0;

    // Shader programs.
    unsigned int litProg_  = 0;   // Blinn-Phong planet (optional texture)
    unsigned int sunProg_  = 0;   // emissive sun (optional texture)
    unsigned int ringProg_ = 0;   // two-sided, alpha from texture
    unsigned int lineProg_ = 0;   // flat-color orbit lines
    unsigned int starProg_ = 0;   // GL_POINTS star field
    unsigned int flameProg_ = 0;  // additive solar flame particles
    unsigned int atmProg_  = 0;   // Fresnel atmosphere rim (Earth / Venus)

    // OBJ meshes keyed by material name.
    std::map<std::string, GpuMesh> objMeshes_; // planet OBJ groups
    GpuMesh moonMesh_;                         // Moon2K.obj

    // Textures keyed by material name.
    std::map<std::string, unsigned int> materialTextures_;

    // 1x1 white dummy texture, bound to unit 0 when no real texture exists.
    unsigned int dummyTex_ = 0;

    // Cached VP matrix.
    gx::Mat4 viewProj_;

    // Loading status (for debug overlay).
    int loadedMeshes_   = 0;
    int loadedTextures_ = 0;
};

} // namespace sx
#endif
