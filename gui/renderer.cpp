#include "renderer.h"

#include "gles/gl_compat.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <string>

#include "stb_image.h"
#ifdef __EMSCRIPTEN__
#include "texture_loader.h"
#ifndef SXWNL_WEB_ASSET_PREFIX
// Relative HTTP prefix. A leading slash would miss /sxwnl-cpp-3d/.
#define SXWNL_WEB_ASSET_PREFIX "resources/v1/"
#endif
#endif

#ifndef GL_MAX_TEXTURE_SIZE
#define GL_MAX_TEXTURE_SIZE 0x0D33
#endif

namespace sx {

// ============================================================================
//  Shader source strings
// ============================================================================

// Vertex shader shared by all sphere-like bodies (pos + nrm + uv)
static const char* kVS_sphere =
    SXWNL_GLSL_VERSION
    "layout(location=0) in vec3 aPos;\n"
    "layout(location=1) in vec3 aNrm;\n"
    "layout(location=2) in vec2 aUV;\n"
    "uniform mat4 uModel;\n"
    "uniform mat4 uViewProj;\n"
    "out vec3 vWorld; out vec3 vNrm; out vec2 vUV;\n"
    "void main(){\n"
    "  vec4 w = uModel * vec4(aPos, 1.0);\n"
    "  vWorld = w.xyz;\n"
    "  vNrm   = mat3(uModel) * aNrm;\n"
    "  vUV    = aUV;\n"
    "  gl_Position = uViewProj * w;\n"
    "}\n";

// Blinn-Phong lit fragment shader with optional texture and specular.
static const char* kFS_lit =
    SXWNL_GLSL_VERSION
    "in vec3 vWorld; in vec3 vNrm; in vec2 vUV;\n"
    "uniform vec3      uColor;\n"
    "uniform vec3      uLightPos;\n"
    "uniform vec3      uEyePos;\n"
    "uniform sampler2D uTex;\n"
    "uniform int       uUseTex;\n"  // int avoids bool-uniform driver quirks
    "uniform float     uTexMix;\n"
    "uniform float     uSpecStrength;\n"
    "out vec4 frag;\n"
    "void main(){\n"
    "  vec3 N  = normalize(vNrm);\n"
    "  if (!gl_FrontFacing) N = -N;\n"
    "  vec3 L  = normalize(uLightPos - vWorld);\n"
    "  vec3 V  = normalize(uEyePos   - vWorld);\n"
    "  vec3 H  = normalize(L + V);\n"
    "  float diff = max(dot(N, L), 0.0);\n"
    "  diff = pow(diff, 1.18);\n"
    "  float spec = pow(max(dot(N, H), 0.0), 56.0) * smoothstep(0.02, 0.28, diff);\n"
    "  vec3 texc  = texture(uTex, vUV).rgb;\n"
    "  vec3 base  = (uUseTex != 0) ? mix(uColor, texc, clamp(uTexMix, 0.0, 1.0)) : uColor;\n"
    "  frag = vec4(base * (0.12 + 0.98 * diff) + vec3(uSpecStrength) * spec, 1.0);\n"
    "}\n";

// Emissive sun fragment (no lighting, just texture/color)
static const char* kFS_sun =
    SXWNL_GLSL_VERSION
    "in vec2 vUV;\n"
    "uniform vec3      uColor;\n"
    "uniform float     uAlpha;\n"
    "uniform sampler2D uTex;\n"
    "uniform int       uUseTex;\n"  // int avoids bool-uniform driver quirks
    "out vec4 frag;\n"
    "void main(){\n"
    "  vec3 texc = texture(uTex, vUV).rgb;\n"
    "  vec3 c = (uUseTex != 0) ? max(texc, uColor * 0.35) : uColor;\n"
    "  frag = vec4(c, uAlpha);\n"
    "}\n";

// Two-sided ring fragment: alpha comes from the RGBA texture.
static const char* kFS_ring =
    SXWNL_GLSL_VERSION
    "in vec3 vWorld; in vec3 vNrm; in vec2 vUV;\n"
    "uniform vec3      uColor;\n"
    "uniform vec3      uLightPos;\n"
    "uniform sampler2D uTex;\n"
    "uniform int       uUseTex;\n"
    "out vec4 frag;\n"
    "void main(){\n"
    "  vec4 t = (uUseTex != 0) ? texture(uTex, vUV) : vec4(uColor, 0.8);\n"
    "  if (t.a < 0.02) discard;\n"
    "  vec3 N = normalize(vNrm);\n"
    "  vec3 L = normalize(uLightPos - vWorld);\n"
    // Two-sided lighting: sun illuminates both ring faces.
    "  float d = max(dot(N, L), 0.0) + max(dot(-N, L), 0.0);\n"
    "  frag = vec4(t.rgb * (0.18 + 0.82 * d), t.a);\n"
    "}\n";

// Fresnel atmosphere: brighter at the limb, transparent in the centre.
// Uses the same sphere VS (kVS_sphere).
static const char* kFS_atm =
    SXWNL_GLSL_VERSION
    "in vec3 vWorld; in vec3 vNrm;\n"
    "uniform vec3  uColor;\n"
    "uniform vec3  uEyePos;\n"
    "uniform float uAlpha;\n"
    "out vec4 frag;\n"
    "void main(){\n"
    "  vec3  N   = normalize(vNrm);\n"
    "  vec3  V   = normalize(uEyePos - vWorld);\n"
    "  float rim = 1.0 - max(dot(N, V), 0.0);\n"
    "  rim = pow(rim, 2.5);\n"
    "  frag = vec4(uColor, rim * uAlpha);\n"
    "}\n";

// Star-field sprite quad: center(3), corner offset(2), size(1), brightness(1)
static const char* kVS_star =
    SXWNL_GLSL_VERSION
    "layout(location=0) in vec3  aCenter;\n"
    "layout(location=1) in vec2  aOffset;\n"
    "layout(location=2) in float aSize;\n"
    "layout(location=3) in float aBright;\n"
    "uniform mat4 uViewProj;\n"
    "uniform vec3 uBillboardRight;\n"
    "uniform vec3 uBillboardUp;\n"
    "uniform float uWorldScale;\n"
    "out vec2 vUV; out float vBright;\n"
    "void main(){\n"
    "  vec3 p = aCenter + (uBillboardRight * aOffset.x + uBillboardUp * aOffset.y) * aSize * uWorldScale;\n"
    "  gl_Position = uViewProj * vec4(p, 1.0);\n"
    "  vUV = aOffset * 0.5 + 0.5;\n"
    "  vBright = aBright;\n"
    "}\n";

// Soft circular star point
static const char* kFS_star =
    SXWNL_GLSL_VERSION
    "in vec2 vUV;\n"
    "in float vBright;\n"
    "uniform vec3 uColor;\n"
    "out vec4 frag;\n"
    "void main(){\n"
    "  vec2  c = vUV - 0.5;\n"
    "  float d = dot(c, c) * 4.0;\n"
    "  if (d > 1.0) discard;\n"
    "  float a = (1.0 - d) * vBright;\n"
    "  frag = vec4(uColor, a);\n"
    "}\n";

// Solar flame particles: spherical emitter, turbulent outward drift, additive.
static const char* kVS_flame =
    SXWNL_GLSL_VERSION
    "layout(location=0) in vec3  aCenter;\n"
    "layout(location=1) in vec2  aOffset;\n"
    "layout(location=2) in float aSize;\n"
    "layout(location=3) in float aHeat;\n"
    "layout(location=4) in float aAlpha;\n"
    "uniform mat4 uViewProj;\n"
    "uniform vec3 uBillboardRight;\n"
    "uniform vec3 uBillboardUp;\n"
    "uniform float uWorldScale;\n"
    "out vec2 vUV; out float vHeat; out float vAlpha;\n"
    "void main(){\n"
    "  vec3 p = aCenter + (uBillboardRight * aOffset.x + uBillboardUp * aOffset.y) * aSize * uWorldScale;\n"
    "  gl_Position = uViewProj * vec4(p, 1.0);\n"
    "  vUV = aOffset * 0.5 + 0.5;\n"
    "  vHeat = aHeat;\n"
    "  vAlpha = aAlpha;\n"
    "}\n";

static const char* kFS_flame =
    SXWNL_GLSL_VERSION
    "in vec2 vUV;\n"
    "in float vHeat; in float vAlpha;\n"
    "out vec4 frag;\n"
    "void main(){\n"
    "  vec2 p = vUV * 2.0 - 1.0;\n"
    "  float r2 = dot(p, p);\n"
    "  if (r2 > 1.0) discard;\n"
    "  float core = exp(-3.2 * r2);\n"
    "  vec3 hot  = vec3(1.00, 0.92, 0.42);\n"
    "  vec3 warm = vec3(1.00, 0.34, 0.04);\n"
    "  vec3 deep = vec3(0.78, 0.06, 0.00);\n"
    "  vec3 c = mix(deep, warm, smoothstep(0.0, 0.55, vHeat));\n"
    "  c = mix(c, hot, smoothstep(0.58, 1.0, vHeat) * core);\n"
    "  float a = vAlpha * core * (1.0 - smoothstep(0.72, 1.0, r2));\n"
    "  frag = vec4(c, a);\n"
    "}\n";

// Orbit line vertex (position only)
static const char* kVS_line =
    SXWNL_GLSL_VERSION
    "layout(location=0) in vec3 aPos;\n"
    "uniform mat4 uModel;\n"
    "uniform mat4 uViewProj;\n"
    "void main(){ gl_Position = uViewProj * uModel * vec4(aPos, 1.0); }\n";

    // Orbit lines.
static const char* kFS_flat =
    SXWNL_GLSL_VERSION
    "uniform vec3  uColor;\n"
    "uniform float uAlpha;\n"
    "out vec4 frag;\n"
    "void main(){ frag = vec4(uColor, uAlpha); }\n";

// ============================================================================
//  Shader compilation helpers
// ============================================================================
static unsigned int compileShader(unsigned int type, const char* src) {
    unsigned int s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    int ok = 0; glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024]; glGetShaderInfoLog(s, 1024, nullptr, log);
        std::fprintf(stderr, "[shader] %s\n", log);
    }
    return s;
}

static unsigned int linkProg(const char* vs, const char* fs) {
    unsigned int v = compileShader(GL_VERTEX_SHADER,   vs);
    unsigned int f = compileShader(GL_FRAGMENT_SHADER, fs);
    unsigned int p = glCreateProgram();
    glAttachShader(p, v); glAttachShader(p, f); glLinkProgram(p);
    int ok = 0; glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024]; glGetProgramInfoLog(p, 1024, nullptr, log);
        std::fprintf(stderr, "[program] %s\n", log);
    }
    glDeleteShader(v); glDeleteShader(f);
    return p;
}

// ============================================================================
//  Procedural UV sphere  (pos+nrm+uv = 8 floats/vertex)
// v=0 maps to the north pole of an equirectangular texture.
// ============================================================================
static void buildSphere(std::vector<float>& verts,
                        std::vector<unsigned int>& idx,
                        int stacks, int sectors) {
    const float PI = 3.14159265f;
    for (int i = 0; i <= stacks; ++i) {
        float phi = PI * (float)i / stacks - PI * 0.5f;
        float cp  = std::cos(phi), sp = std::sin(phi);
        float v   = 1.0f - (float)i / stacks;
        for (int j = 0; j <= sectors; ++j) {
            float th = 2.0f * PI * (float)j / sectors;
            float x  = cp * std::cos(th), y = sp, z = cp * std::sin(th);
            float u  = (float)j / sectors;
            verts.push_back(x); verts.push_back(y); verts.push_back(z);
            verts.push_back(x); verts.push_back(y); verts.push_back(z); // nrm = pos
            verts.push_back(u); verts.push_back(v);
        }
    }
    int row = sectors + 1;
    for (int i = 0; i < stacks; ++i)
        for (int j = 0; j < sectors; ++j) {
            unsigned int a = i*row+j, b = a+row;
            idx.push_back(a);   idx.push_back(b);   idx.push_back(a+1);
            idx.push_back(a+1); idx.push_back(b);   idx.push_back(b+1);
        }
}

// ============================================================================
//  Renderer::init
// ============================================================================
bool Renderer::init() {
    litProg_  = linkProg(kVS_sphere, kFS_lit);
    sunProg_  = linkProg(kVS_sphere, kFS_sun);
    ringProg_ = linkProg(kVS_sphere, kFS_ring);
    lineProg_ = linkProg(kVS_line,   kFS_flat);
    starProg_ = linkProg(kVS_star,   kFS_star);
    flameProg_ = linkProg(kVS_flame, kFS_flame);
    atmProg_  = linkProg(kVS_sphere, kFS_atm);

    // Fallback sphere VAO
    std::vector<float>        verts;
    std::vector<unsigned int> idx;
    buildSphere(verts, idx, 36, 54);
    std::vector<float> drawVerts;
    drawVerts.reserve(idx.size() * 8);
    for (unsigned int vi : idx) {
        const size_t base = (size_t)vi * 8;
        for (int k = 0; k < 8; ++k) drawVerts.push_back(verts[base + k]);
    }
    sphereIndexCount_ = (int)(drawVerts.size() / 8);

    glGenVertexArrays(1, &sphereVAO_);
    glGenBuffers(1, &sphereVBO_);
    glGenBuffers(1, &sphereEBO_);
    glBindVertexArray(sphereVAO_);
    glBindBuffer(GL_ARRAY_BUFFER, sphereVBO_);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(drawVerts.size()*sizeof(float)),
                 drawVerts.data(), GL_STATIC_DRAW);
    {
        const int s = 8*(int)sizeof(float);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, s, (void*)0);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, s, (void*)(3*sizeof(float)));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, s, (void*)(6*sizeof(float)));
        glEnableVertexAttribArray(2);
    }
    glBindVertexArray(0);

    // Line VAO (pos only)
    glGenVertexArrays(1, &lineVAO_);
    glGenBuffers(1, &lineVBO_);
    glBindVertexArray(lineVAO_);
    glBindBuffer(GL_ARRAY_BUFFER, lineVBO_);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3*sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glBindVertexArray(0);

    // Asteroid belt VAO: center(3)+corner offset(2)+size(1)+brightness(1).
    {
        glGenVertexArrays(1, &asteroidVAO_);
        glGenBuffers(1, &asteroidVBO_);
        glBindVertexArray(asteroidVAO_);
        glBindBuffer(GL_ARRAY_BUFFER, asteroidVBO_);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 7 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 7 * sizeof(float), (void*)(3 * sizeof(float)));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, 7 * sizeof(float), (void*)(5 * sizeof(float)));
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, 7 * sizeof(float), (void*)(6 * sizeof(float)));
        glEnableVertexAttribArray(3);
        glBindVertexArray(0);
    }

    // Solar flame particle VAO: center(3)+corner offset(2)+size(1)+heat(1)+alpha(1).
    {
        glGenVertexArrays(1, &flameVAO_);
        glGenBuffers(1, &flameVBO_);
        glBindVertexArray(flameVAO_);
        glBindBuffer(GL_ARRAY_BUFFER, flameVBO_);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(3 * sizeof(float)));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(5 * sizeof(float)));
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(6 * sizeof(float)));
        glEnableVertexAttribArray(3);
        glVertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(7 * sizeof(float)));
        glEnableVertexAttribArray(4);
        glBindVertexArray(0);
    }

    // Procedural star field.
    // 2000 stars randomly distributed on a sphere of radius 800 world-units.
    // Six vertices per star, each: center(3)+corner offset(2)+size(1)+brightness(1).
    {
        const int   N = 2000;
        const float R = 800.0f;
        const float PI = 3.14159265f;
        std::vector<float> sv;
        sv.reserve(N * 6 * 7);

        // Simple LCG for a deterministic but "random" distribution
        unsigned int rng = 0x12345678u;
        auto nextf = [&]() -> float {
            rng = rng * 1664525u + 1013904223u;
            return (rng >> 8) / (float)(1 << 24);
        };

        for (int i = 0; i < N; ++i) {
            // Uniform point on sphere
            float cosT  = 2.0f * nextf() - 1.0f;
            float sinT  = std::sqrt(std::max(0.0f, 1.0f - cosT*cosT));
            float phi   = 2.0f * PI * nextf();
            float x     = R * sinT * std::cos(phi);
            float y     = R * cosT;
            float z     = R * sinT * std::sin(phi);
            float sz    = 1.2f + nextf() * 2.0f;     // point size [1.2, 3.2]
            float bright = 0.4f + nextf() * 0.6f;    // brightness [0.4, 1.0]
            static const float kCorner[12] = {
                -1.0f, -1.0f,  1.0f, -1.0f,  1.0f,  1.0f,
                -1.0f, -1.0f,  1.0f,  1.0f, -1.0f,  1.0f,
            };
            for (int c = 0; c < 6; ++c) {
                sv.push_back(x); sv.push_back(y); sv.push_back(z);
                sv.push_back(kCorner[c * 2]); sv.push_back(kCorner[c * 2 + 1]);
                sv.push_back(sz);
                sv.push_back(bright);
            }
        }
        starCount_ = N * 6;
    #ifdef __APPLE__
        starCount_ = 0;
    #endif

        glGenVertexArrays(1, &starVAO_);
        glGenBuffers(1, &starVBO_);
        glBindVertexArray(starVAO_);
        glBindBuffer(GL_ARRAY_BUFFER, starVBO_);
        glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(sv.size()*sizeof(float)),
                     sv.data(), GL_STATIC_DRAW);
        const int ss = 7 * (int)sizeof(float);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, ss, (void*)0);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, ss, (void*)(3*sizeof(float)));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, ss, (void*)(5*sizeof(float)));
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, ss, (void*)(6*sizeof(float)));
        glEnableVertexAttribArray(3);
        glBindVertexArray(0);
    }

    // 1x1 white dummy texture – must be a proper generated object (not name 0)
    // because macOS Metal GL validates every sampler binding at draw time.
    {
        unsigned char white[4] = {255, 255, 255, 255};
        glGenTextures(1, &dummyTex_);          // ← was missing; left dummyTex_=0
        glBindTexture(GL_TEXTURE_2D, dummyTex_);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, white);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    return true;
}

// ============================================================================
//  Texture loading
// ============================================================================
unsigned int Renderer::loadTexFile(const char* path, bool rgba) {
    stbi_set_flip_vertically_on_load(1);
    int w = 0, h = 0, ch = 0;
    int desired = rgba ? 4 : 3;
    unsigned char* data = stbi_load(path, &w, &h, &ch, desired);
    if (!data) {
        std::fprintf(stderr, "[tex] not found: %s\n", path);
        return 0;
    }
    bool freeWithStd = false;
    GLint maxTexSize = 0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTexSize);
#ifdef __ANDROID__
    constexpr GLint kAndroidTextureLimit = 2048;
    if (maxTexSize <= 0 || maxTexSize > kAndroidTextureLimit)
        maxTexSize = kAndroidTextureLimit;
#endif
    if (maxTexSize > 0 && (w > maxTexSize || h > maxTexSize)) {
        int nw = w, nh = h;
        while (nw > maxTexSize || nh > maxTexSize) {
            nw = (nw > 1) ? (nw / 2) : 1;
            nh = (nh > 1) ? (nh / 2) : 1;
        }
        int comps = desired;
        unsigned char* scaled = (unsigned char*)std::malloc((size_t)nw * (size_t)nh * (size_t)comps);
        if (scaled) {
            for (int y = 0; y < nh; ++y) {
                int sy = y * h / nh;
                for (int x = 0; x < nw; ++x) {
                    int sx = x * w / nw;
                    const unsigned char* src = data + ((size_t)sy * (size_t)w + (size_t)sx) * comps;
                    unsigned char* dst = scaled + ((size_t)y * (size_t)nw + (size_t)x) * comps;
                    for (int c = 0; c < comps; ++c) dst[c] = src[c];
                }
            }
            stbi_image_free(data);
            data = scaled;
            freeWithStd = true;
            std::fprintf(stderr, "[tex] scaled %s from %dx%d to %dx%d (max %d)\n",
                         path, w, h, nw, nh, maxTexSize);
            w = nw;
            h = nh;
        }
    }

    unsigned int id = 0;
    glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_2D, id);
    GLenum internalFmt = rgba ? GL_RGBA8   : GL_RGB8;
    GLenum fmt         = rgba ? GL_RGBA    : GL_RGB;
    while (glGetError() != GL_NO_ERROR) {}
    glTexImage2D(GL_TEXTURE_2D, 0, internalFmt, w, h, 0,
                 fmt, GL_UNSIGNED_BYTE, data);
    GLenum err = glGetError();
    if (err != GL_NO_ERROR) {
        std::fprintf(stderr, "[tex] upload failed 0x%x: %s (%dx%d)\n", err, path, w, h);
        if (id) glDeleteTextures(1, &id);
        if (freeWithStd) std::free(data);
        else stbi_image_free(data);
        return 0;
    }
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S,     GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T,     GL_CLAMP_TO_EDGE);
    if (freeWithStd) std::free(data);
    else stbi_image_free(data);
    std::fprintf(stderr, "[tex] loaded %s (%dx%d %s)\n",
                 path, w, h, rgba ? "RGBA" : "RGB");
    return id;
}

#ifdef __EMSCRIPTEN__
void Renderer::wasmOnPlanet(bool ok, void* user) {
    auto* self = static_cast<Renderer*>(user);
    if (!ok) {
        std::fprintf(stderr, "[obj] planet fetch failed\n");
        return;
    }
    self->objMeshes_ = loadOBJ("/resources/planet/8k-solar-system.obj");
    self->loadedMeshes_ += (int)self->objMeshes_.size();
    std::fprintf(stderr, "[obj] planet meshes=%d\n", (int)self->objMeshes_.size());
}

void Renderer::wasmOnMoon(bool ok, void* user) {
    auto* self = static_cast<Renderer*>(user);
    if (!ok) {
        std::fprintf(stderr, "[obj] moon fetch failed\n");
        return;
    }
    auto moonMeshes = loadOBJ("/resources/moon/Moon2K.obj");
    for (auto& kv : moonMeshes) {
        if (kv.second.valid) {
            self->moonMesh_ = kv.second;
            kv.second = GpuMesh{};
            ++self->loadedMeshes_;
            break;
        }
    }
    freeOBJMeshes(moonMeshes);
}

void Renderer::wasmOnBounds(bool ok, void* user) {
    auto* self = static_cast<Renderer*>(user);
    if (!ok) {
        std::fprintf(stderr, "[bounds] world_b.bin fetch failed\n");
        return;
    }
    self->loadWorldBoundaries("/resources");
}
#endif

// ============================================================================
//  loadModels: OBJ meshes + all textures
// ============================================================================
void Renderer::loadModels(const std::string& resourceDir, const ProgressFn& onProgress) {
    std::fprintf(stderr, "[renderer] loadModels from: %s\n", resourceDir.c_str());
    auto report = [&](float f, const char* what) { if (onProgress) onProgress(f, what); };

#ifdef __EMSCRIPTEN__
    // Textures: one HTTP URL each, decoded by the browser. First paint does
    // not wait for them — TexRequest binds a 1x1 placeholder on the same id.
    (void)resourceDir;
    static const struct { const char* mtl; const char* rel; } kWebMap[] = {
        {"Sun",              "planet/tex/8k_sun.jpg"},
        {"Mercury",          "planet/tex/8k_mercury.jpg"},
        {"Venus",            "planet/tex/4k_venus_atmosphere.jpg"},
        {"Venus._Atmosphere","planet/tex/8k_venus_surface.jpg"},
        {"Earth",            "planet/tex/8k_earth_daymap.jpg"},
        {"Earth_Atmosphere", "planet/tex/8k_earth_clouds.jpg"},
        {"Mars",             "planet/tex/8k_mars.jpg"},
        {"Jupiter",          "planet/tex/8k_jupiter.jpg"},
        {"Saturn",           "planet/tex/8k_saturn.jpg"},
        {"Saturn_Rings",     "planet/tex/8k_saturn_ring_UV-mapped.png"},
        {"Uranus",           "planet/tex/2k_uranus.jpg"},
        {"Neptune",          "planet/tex/2k_neptune.jpg"},
        {"Moon",             "moon/Textures/Diffuse_2K.jpg"},
    };
    report(0.2f, "textures");
    for (const auto& kv : kWebMap) {
        Texture* t = TexRequest(std::string(SXWNL_WEB_ASSET_PREFIX) + kv.rel);
        if (t && t->id) {
            materialTextures_[kv.mtl] = t->id;
            ++loadedTextures_;
        }
    }
    report(0.4f, "meshes");
    FetchToMemfs(std::string(SXWNL_WEB_ASSET_PREFIX) + "planet/8k-solar-system.obj",
                 "/resources/planet/8k-solar-system.obj",
                 &Renderer::wasmOnPlanet, this);
    FetchToMemfs(std::string(SXWNL_WEB_ASSET_PREFIX) + "moon/Moon2K.obj",
                 "/resources/moon/Moon2K.obj",
                 &Renderer::wasmOnMoon, this);
    FetchToMemfs(std::string(SXWNL_WEB_ASSET_PREFIX) + "world_b.bin",
                 "/resources/world_b.bin",
                 &Renderer::wasmOnBounds, this);
    report(1.0f, "pending");
    std::fprintf(stderr, "[renderer] wasm: %d texture requests; meshes fetch in background\n",
                 loadedTextures_);
#else
    // Planet OBJ.
    report(0.0f, "planets");
    std::string planetOBJ = resourceDir + "/planet/8k-solar-system.obj";
    objMeshes_ = loadOBJ(planetOBJ.c_str());
    loadedMeshes_ = (int)objMeshes_.size();

    // Moon OBJ.
    report(0.15f, "moon");
    {
        std::string moonOBJ = resourceDir + "/moon/Moon2K.obj";
        auto moonMeshes = loadOBJ(moonOBJ.c_str());
        for (auto& kv : moonMeshes) {
            if (kv.second.valid) {
                moonMesh_ = kv.second; // take ownership
                kv.second = GpuMesh{}; // zero out so freeOBJMeshes won't double-free
                ++loadedMeshes_;
                break;
            }
        }
        freeOBJMeshes(moonMeshes); // release any remaining groups
    }

    // Textures.
    static const struct { const char* mtl; const char* rel; bool rgba; } kMap[] = {
        {"Sun",              "planet/tex/8k_sun.jpg",                   false},
        {"Mercury",          "planet/tex/8k_mercury.jpg",               false},
        {"Venus",            "planet/tex/4k_venus_atmosphere.jpg",      false},
        {"Venus._Atmosphere","planet/tex/8k_venus_surface.jpg",         false},
        {"Earth",            "planet/tex/8k_earth_daymap.jpg",          false},
        {"Earth_Atmosphere", "planet/tex/8k_earth_clouds.jpg",          false},
        {"Mars",             "planet/tex/8k_mars.jpg",                  false},
        {"Jupiter",          "planet/tex/8k_jupiter.jpg",               false},
        {"Saturn",           "planet/tex/8k_saturn.jpg",                false},
        {"Saturn_Rings",     "planet/tex/8k_saturn_ring_UV-mapped.png", true },
        {"Uranus",           "planet/tex/2k_uranus.jpg",                false},
        {"Neptune",          "planet/tex/2k_neptune.jpg",               false},
        {"Moon",             "moon/Textures/Diffuse_2K.png",            false},
    };
    const int texCount = (int)(sizeof(kMap) / sizeof(kMap[0]));
    int texIndex = 0;
    for (const auto& kv : kMap) {
        report(0.25f + 0.70f * (float)texIndex / (float)texCount, kv.mtl);
        ++texIndex;
        std::string path = resourceDir + "/" + kv.rel;
        unsigned int id = loadTexFile(path.c_str(), kv.rgba);
        if (id) { materialTextures_[kv.mtl] = id; ++loadedTextures_; }
    }
    std::fprintf(stderr, "[renderer] loaded %d meshes, %d textures\n",
                 loadedMeshes_, loadedTextures_);

    // World administrative boundaries (optional)
    report(0.95f, "boundaries");
    loadWorldBoundaries(resourceDir);
    report(1.0f, "done");
#endif
}

// ============================================================================
//  FBO management
// ============================================================================
void Renderer::ensureFBO() {
    if (!fboDirty_) return;
    if (!fbo_)      glGenFramebuffers(1,  &fbo_);
    if (!colorTex_) glGenTextures(1,      &colorTex_);
    if (!depthRbo_) glGenRenderbuffers(1, &depthRbo_);

    glBindTexture(GL_TEXTURE_2D, colorTex_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w_, h_, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    glBindRenderbuffer(GL_RENDERBUFFER, depthRbo_);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, w_, h_);

    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, colorTex_, 0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                              GL_RENDERBUFFER, depthRbo_);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    fboDirty_ = false;
}

void Renderer::ensureMoonPhaseFBO() {
    if (moonPhaseFBO_) return; // already created
    const int S = kMoonPhaseSize;

    glGenFramebuffers(1,  &moonPhaseFBO_);
    glGenTextures(1,      &moonPhaseTex_);
    glGenRenderbuffers(1, &moonPhaseDepth_);

    glBindTexture(GL_TEXTURE_2D, moonPhaseTex_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, S, S, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    glBindRenderbuffer(GL_RENDERBUFFER, moonPhaseDepth_);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, S, S);

    glBindFramebuffer(GL_FRAMEBUFFER, moonPhaseFBO_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, moonPhaseTex_, 0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                              GL_RENDERBUFFER, moonPhaseDepth_);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

// Forward declaration – defined later alongside other material helpers.
static void bindMaterialTex(unsigned int prog,
                             const std::map<std::string, unsigned int>& textures,
                             const std::string& matName,
                             unsigned int dummyTex);

void Renderer::ensureEclipseGlobeFBO() {
    if (eclipseGlobeFBO_) return;
    const int S = kEclipseGlobeSize;

    glGenFramebuffers(1,  &eclipseGlobeFBO_);
    glGenTextures(1,      &eclipseGlobeTex_);
    glGenRenderbuffers(1, &eclipseGlobeDepth_);

    glBindTexture(GL_TEXTURE_2D, eclipseGlobeTex_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, S, S, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    glBindRenderbuffer(GL_RENDERBUFFER, eclipseGlobeDepth_);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, S, S);

    glBindFramebuffer(GL_FRAMEBUFFER, eclipseGlobeFBO_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, eclipseGlobeTex_, 0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                              GL_RENDERBUFFER, eclipseGlobeDepth_);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

// ============================================================================
//  Per-mesh axis calibration.
//
//  Each bundled mesh sits in its own arbitrary local frame, so before any
//  astronomical spin/tilt is applied the mesh has to be carried into the
//  canonical geographic frame: +Y = north pole, +Z = prime meridian,
//  +X = 90 deg east. These matrices were measured from the meshes' own
//  vertex/UV data (pole axis from where the texture's latitude parameter is
//  stationary, meridian phase from a least-squares fit over every vertex).
//
//  Spinning a body about the wrong axis makes its texture wobble once per
//  rotation instead of turning in place, which is what these correct. The
//  offenders are Earth (54 deg out), the Moon (79 deg) and Saturn (26.5 deg);
//  Jupiter is 4.5 deg out, enough to make its bands visibly drift up and down.
//  The remaining meshes already have their pole on local +Y and only need the
//  meridian phase.
//
//  Jupiter and the Moon do not use an equirectangular UV layout (their fits
//  leave a ~7-10 deg median residual), so for those two only the pole is
//  trustworthy; the meridian phase is a best effort and merely shifts which
//  longitude faces the camera.
// ============================================================================
gx::Mat4 meshAxisFixFor(const std::string& pinyin) {
    struct Entry { const char* name; gx::Mat4 (*make)(); };
    if (pinyin == "earth")
        return gx::fromColumns({-0.237068f,-0.809406f,-0.537272f},
                               {+0.342213f,-0.587165f,+0.733571f},
                               {-0.909225f,-0.009956f,+0.416187f});
    if (pinyin == "jupiter")
        return gx::fromColumns({-0.048441f,-0.078187f,-0.995761f},
                               {+0.002369f,+0.996920f,-0.078393f},
                               {+0.998823f,-0.006156f,-0.048106f});
    if (pinyin == "saturn")
        return gx::fromColumns({+0.108912f,+0.217127f,+0.970049f},
                               {-0.419315f,+0.894819f,-0.153209f},
                               {-0.901284f,-0.390070f,+0.188501f});
    if (pinyin == "moon")
        return gx::fromColumns({-0.347833f,+0.485555f,-0.802027f},
                               {+0.937447f,+0.193201f,-0.289598f},
                               {+0.014337f,-0.852590f,-0.522384f});
    // Mercury, Venus, Mars, Uranus, Neptune and the Sun share one frame:
    // pole already on +Y, prime meridian rotated 65.9 deg away.
    if (pinyin == "mercury" || pinyin == "venus" || pinyin == "mars" ||
        pinyin == "uranus"  || pinyin == "neptune" || pinyin == "sun")
        return gx::fromColumns({+0.408181f, 0.0f, +0.912901f},
                               { 0.0f,      1.0f,  0.0f},
                               {-0.912901f, 0.0f, +0.408181f});
    return gx::Mat4::identity();
}

// ============================================================================
//  Load world boundary polylines from resources/world_b.bin.
//  Binary format:
//    magic[4]     = "WBD1"
//    num_segs     = uint32 (little-endian)
//    for each seg: num_pts = uint32, then num_pts × (float32 lon, float32 lat)
//  Builds a GL_LINES VAO with 3D Cartesian unit-sphere positions.
// ============================================================================
void Renderer::loadWorldBoundaries(const std::string& resDir) {
    std::string path = resDir + "/world_b.bin";
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) {
        std::fprintf(stderr, "[bounds] world_b.bin not found; boundary layer disabled.\n");
        return;
    }

    char magic[4] = {};
    if (std::fread(magic, 1, 4, f) != 4 ||
        magic[0]!='W'||magic[1]!='B'||magic[2]!='D'||magic[3]!='1') {
        std::fprintf(stderr, "[bounds] invalid magic in world_b.bin\n");
        std::fclose(f); return;
    }

    unsigned int numSeg = 0;
    if (std::fread(&numSeg, 4, 1, f) != 1) { std::fclose(f); return; }

    const float PI = 3.14159265f;
    std::vector<float> lineVerts;   // 3 floats per vertex, GL_LINES pairs
    boundaryLonLat_.clear();

    // Read the polylines up front: the CPU copy below needs a decimation stride
    // derived from the total point count, which is not known while streaming.
    std::vector<std::vector<float>> polys;   // lon/lat pairs, one entry per line
    polys.reserve(numSeg);
    size_t totalPts = 0;
    {
        std::vector<float> pts;
        for (unsigned int s = 0; s < numSeg; ++s) {
            unsigned int n = 0;
            if (std::fread(&n, 4, 1, f) != 1) break;
            if (n < 2) { std::fseek(f, (long)(n * 8), SEEK_CUR); continue; }
            pts.resize(n * 2);
            if (std::fread(pts.data(), 4, n * 2, f) != n * 2) break;
            polys.push_back(pts);
            totalPts += n;
        }
    }
    std::fclose(f);

    // GPU copy: full detail. This goes through our own shader as a plain vertex
    // buffer, so the point count costs nothing but memory.
    for (const std::vector<float>& pts : polys) {
        const size_t n = pts.size() / 2;
        for (size_t i = 0; i + 1 < n; ++i) {
            float dlon = pts[(i+1)*2] - pts[i*2];
            if (dlon > 180.f || dlon < -180.f) continue;   // antimeridian jump

            for (int k = 0; k < 2; ++k) {
                float lon  = pts[(i+k)*2+0];
                float lat  = pts[(i+k)*2+1];
                float lonR = lon * PI / 180.0f;
                float latR = lat * PI / 180.0f;
                float cl   = std::cos(latR);
                lineVerts.push_back(cl * std::sin(lonR));
                lineVerts.push_back(std::sin(latR));
                lineVerts.push_back(cl * std::cos(lonR));
            }
        }
    }

    // CPU copy: heavily decimated, and deliberately so.
    //
    // This one is drawn through ImDrawList, one AddLine per segment. ImGui's
    // index type is 16 bit, and the OpenGL ES backend has no
    // glDrawElementsBaseVertex to split draw commands on, so a draw list is
    // capped at 65536 vertices - four per line. The full dataset is ~200k
    // segments, which silently wrapped the index range and turned the whole
    // eclipse page into garbled geometry. Keeping every k-th point of each
    // polyline stays inside the budget and still draws continuous (if coarser)
    // outlines, rather than the dashes that dropping whole segments would give.
    const size_t kMaxCpuSegments = 8000;
    size_t stride = 1;
    if (totalPts > kMaxCpuSegments)
        stride = (totalPts + kMaxCpuSegments - 1) / kMaxCpuSegments;
    boundaryLonLat_.reserve(kMaxCpuSegments * 4);
    for (const std::vector<float>& pts : polys) {
        const size_t n = pts.size() / 2;
        if (n < 2) continue;
        const size_t step = std::min(stride, n - 1);
        for (size_t i = 0; i + step < n; i += step) {
            const size_t j = i + step;
            float lon0 = pts[i*2 + 0], lat0 = pts[i*2 + 1];
            float lon1 = pts[j*2 + 0], lat1 = pts[j*2 + 1];
            float dlon = lon1 - lon0;
            if (dlon > 180.f || dlon < -180.f) continue;
            boundaryLonLat_.push_back(lon0);
            boundaryLonLat_.push_back(lat0);
            boundaryLonLat_.push_back(lon1);
            boundaryLonLat_.push_back(lat1);
        }
    }

    if (lineVerts.empty()) return;

    glGenVertexArrays(1, &boundaryVAO_);
    glGenBuffers(1, &boundaryVBO_);
    glBindVertexArray(boundaryVAO_);
    glBindBuffer(GL_ARRAY_BUFFER, boundaryVBO_);
    glBufferData(GL_ARRAY_BUFFER,
                 (GLsizeiptr)(lineVerts.size() * sizeof(float)),
                 lineVerts.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glBindVertexArray(0);

    boundaryVertCount_ = (int)(lineVerts.size() / 3);
    boundaryAvail_     = true;
    std::fprintf(stderr, "[bounds] loaded %u segs → %d gpu verts, %d cpu segs\n",
                 numSeg, boundaryVertCount_, (int)(boundaryLonLat_.size() / 4));
}

// ============================================================================
//  Render textured Earth globe + eclipse path to the eclipse-globe FBO.
// ============================================================================
void Renderer::renderEclipseGlobe(float yawDeg, float pitchDeg,
                                   const std::vector<EclipsePathSample>& path,
                                   double jdTd, bool showBoundaries,
                                   const EclipseLimits* limits) {
    ensureEclipseGlobeFBO();

    const float PI = 3.14159265f;
    const int   S  = kEclipseGlobeSize;

    // --- Camera / projection (same setup as renderMoonPhase) ---
    gx::Vec3 camEye{0.f, 0.f, 3.05f};
    gx::Mat4 mv  = gx::lookAt(camEye, {0,0,0}, {0,1,0});
    gx::Mat4 pr  = gx::perspective(42.f * PI / 180.f, 1.f, 0.1f, 200.f);
    gx::Mat4 vp  = pr * mv;

    // The bundled Earth OBJ's own vertex/UV space is not aligned to any of
    // its principal axes: measured directly from resources/planet/
    // 8k-solar-system.obj, the mesh's Greenwich meridian, +90 deg-east
    // meridian, and true north pole (accounting for stbi's vertical flip on
    // load, which makes the mesh's raw v=0 sample the *south* pole of the
    // daymap) sit at oblique local directions that no single-axis rotation
    // can reconcile with our lon/lat frame. This is the full 3x3 rotation
    // that carries mesh space into that frame: Greenwich -> +Z, 90E -> +X,
    // north pole -> +Y.
    //
    // It applies to the *mesh only*. Boundary and eclipse-path vertices are
    // built straight from lon/lat by toXYZ(), so they are already in the
    // lon/lat frame; putting the fix on them too would rotate both alike and
    // leave the texture-vs-path mismatch exactly as it was. Keep lines only
    // 0.5% above the surface to avoid z-fighting without making them visibly
    // float off the globe.
    gx::Mat4 meshAxisFix = gx::fromColumns(
        {-0.237642f, -0.809675f, -0.537455f},
        { 0.341882f, -0.586805f,  0.733293f},
        {-0.909199f, -0.009275f,  0.416442f});
    gx::Mat4 view      = gx::rotateX(pitchDeg * PI / 180.0f)
                       * gx::rotateY(yawDeg   * PI / 180.0f);
    gx::Mat4 sphModel  = view * meshAxisFix * gx::scale(0.96f);
    gx::Mat4 lineModel = view * gx::scale(0.965f);

    // Front-upper light (world space, independent of globe rotation)
    gx::Vec3 lightPos{1.0f, 2.5f, 5.0f};

    // --- Render ---
    glBindFramebuffer(GL_FRAMEBUFFER, eclipseGlobeFBO_);
    glViewport(0, 0, S, S);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDisable(GL_BLEND);
    glClearColor(0.04f, 0.07f, 0.14f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // -- Earth sphere --
    glUseProgram(litProg_);
    glUniformMatrix4fv(glGetUniformLocation(litProg_, "uViewProj"), 1, GL_FALSE, vp.data());
    glUniformMatrix4fv(glGetUniformLocation(litProg_, "uModel"),    1, GL_FALSE, sphModel.data());
    glUniform3f(glGetUniformLocation(litProg_, "uColor"),        0.30f, 0.55f, 0.90f);
    glUniform1f(glGetUniformLocation(litProg_, "uTexMix"),       0.92f);
    glUniform1f(glGetUniformLocation(litProg_, "uSpecStrength"), 0.22f);
    glUniform3f(glGetUniformLocation(litProg_, "uLightPos"),
                lightPos.x, lightPos.y, lightPos.z);
    glUniform3f(glGetUniformLocation(litProg_, "uEyePos"),
                camEye.x, camEye.y, camEye.z);
    bindMaterialTex(litProg_, materialTextures_, "Earth", dummyTex_);

    {
        auto it = objMeshes_.find("Earth");
        if (it != objMeshes_.end() && it->second.valid) {
            glBindVertexArray(it->second.vao);
            glDrawArrays(GL_TRIANGLES, 0, it->second.indexCount);
        } else {
            glBindVertexArray(sphereVAO_);
            glDrawArrays(GL_TRIANGLES, 0, sphereIndexCount_);
        }
    }

    // -- Admin boundaries --
    if (showBoundaries && boundaryAvail_ && boundaryVertCount_ > 0) {
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDepthMask(GL_FALSE);
        glUseProgram(lineProg_);
        glUniformMatrix4fv(glGetUniformLocation(lineProg_, "uViewProj"), 1, GL_FALSE, vp.data());
        glUniformMatrix4fv(glGetUniformLocation(lineProg_, "uModel"),    1, GL_FALSE, lineModel.data());
        glUniform3f(glGetUniformLocation(lineProg_, "uColor"),  0.90f, 0.90f, 0.85f);
        glUniform1f(glGetUniformLocation(lineProg_, "uAlpha"),  0.55f);
        glBindVertexArray(boundaryVAO_);
        glDrawArrays(GL_LINES, 0, boundaryVertCount_);
        glDepthMask(GL_TRUE);
        glDisable(GL_BLEND);
    }

    // lon/lat -> unit-sphere position, in the same geographic frame the
    // boundary layer uses. Shared by the limit curves and the path lines.
    auto toXYZ = [&](double lon, double lat, float rad) {
        float lonR=(float)(lon*PI/180.0), latR=(float)(lat*PI/180.0);
        float cl=std::cos(latR);
        return std::array<float,3>{rad*cl*std::sin(lonR), rad*std::sin(latR), rad*cl*std::cos(lonR)};
    };

    // -- Limit curves (jieX): north/south limits, sunrise-sunset maximum lines
    //    and the closed first/last-contact rings. Same lon/lat frame as the
    //    path, so they share lineModel.
    if (limits && limits->valid && !limits->curves.empty()) {
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDepthMask(GL_FALSE);
        glUseProgram(lineProg_);
        glUniformMatrix4fv(glGetUniformLocation(lineProg_, "uViewProj"), 1, GL_FALSE, vp.data());
        glUniformMatrix4fv(glGetUniformLocation(lineProg_, "uModel"),    1, GL_FALSE, lineModel.data());

        std::vector<float> verts;
        for (const EclipseLimitCurve& c : limits->curves) {
            // Colours follow the original vml.js map: red for the limits and
            // contact rings, green for the 0.5-magnitude penumbra lines.
            float r = 1.00f, g = 0.47f, b = 0.47f, a = 0.80f, w = 1.4f;
            if (c.kind == EclipseLimitCurve::HalfPenumbraLimit) {
                r = 0.50f; g = 0.94f; b = 0.50f; a = 0.80f;
            } else if (c.kind == EclipseLimitCurve::CenterLine) {
                a = 0.90f; w = 1.8f;
            }
            verts.clear();
            verts.reserve(c.points.size() * 6);
            for (size_t i = 0; i + 1 < c.points.size(); ++i) {
                const EclipseGeoPoint& pa = c.points[i];
                const EclipseGeoPoint& pb = c.points[i+1];
                if (!pa.valid || !pb.valid) continue;
                if (std::fabs(pa.longitudeDeg - pb.longitudeDeg) > 180.0) continue;
                auto va = toXYZ(pa.longitudeDeg, pa.latitudeDeg, 1.0035f);
                auto vb = toXYZ(pb.longitudeDeg, pb.latitudeDeg, 1.0035f);
                for (float v : va) verts.push_back(v);
                for (float v : vb) verts.push_back(v);
            }
            if (verts.empty()) continue;
            glBindVertexArray(lineVAO_);
            glBindBuffer(GL_ARRAY_BUFFER, lineVBO_);
            glBufferData(GL_ARRAY_BUFFER,
                         (GLsizeiptr)(verts.size()*sizeof(float)),
                         verts.data(), GL_STREAM_DRAW);
            glUniform3f(glGetUniformLocation(lineProg_, "uColor"), r, g, b);
            glUniform1f(glGetUniformLocation(lineProg_, "uAlpha"), a);
            glLineWidth(w);
            glDrawArrays(GL_LINES, 0, (GLsizei)(verts.size()/3));
        }
        glLineWidth(1.0f);
        glDepthMask(GL_TRUE);
        glDisable(GL_BLEND);
    }

    // -- Eclipse path lines --
    if (!path.empty()) {
        struct LineSpec { int idx; float r,g,b,a,w,rad; };
        static const LineSpec kSpecs[5] = {
            {0, 1.00f,0.82f,0.29f, 0.96f, 2.5f, 1.004f}, // center  (gold)
            {1, 0.39f,0.75f,1.00f, 0.70f, 1.5f, 1.002f}, // penumbra N (blue)
            {2, 0.39f,0.75f,1.00f, 0.70f, 1.5f, 1.002f}, // penumbra S
            {3, 1.00f,0.41f,0.33f, 0.88f, 2.0f, 1.003f}, // umbra N (red)
            {4, 1.00f,0.41f,0.33f, 0.88f, 2.0f, 1.003f}, // umbra S
        };

        auto getGP = [](const EclipsePathSample& s, int i) -> const EclipseGeoPoint& {
            if (i==1) return s.penumbraNorth;
            if (i==2) return s.penumbraSouth;
            if (i==3) return s.umbraNorth;
            if (i==4) return s.umbraSouth;
            return s.center;
        };
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDepthMask(GL_FALSE);
        glUseProgram(lineProg_);
        glUniformMatrix4fv(glGetUniformLocation(lineProg_, "uViewProj"), 1, GL_FALSE, vp.data());
        glUniformMatrix4fv(glGetUniformLocation(lineProg_, "uModel"),    1, GL_FALSE, lineModel.data());

        std::vector<float> verts;
        for (const auto& sp : kSpecs) {
            verts.clear();
            verts.reserve(path.size() * 6);
            for (size_t i = 0; i+1 < path.size(); ++i) {
                const EclipseGeoPoint& a = getGP(path[i],   sp.idx);
                const EclipseGeoPoint& b = getGP(path[i+1], sp.idx);
                if (!a.valid || !b.valid) continue;
                if (std::fabs(a.longitudeDeg - b.longitudeDeg) > 180.0) continue;
                auto pa = toXYZ(a.longitudeDeg, a.latitudeDeg, sp.rad);
                auto pb = toXYZ(b.longitudeDeg, b.latitudeDeg, sp.rad);
                for (float v : pa) verts.push_back(v);
                for (float v : pb) verts.push_back(v);
            }
            if (verts.empty()) continue;
            glBindVertexArray(lineVAO_);
            glBindBuffer(GL_ARRAY_BUFFER, lineVBO_);
            glBufferData(GL_ARRAY_BUFFER,
                         (GLsizeiptr)(verts.size()*sizeof(float)),
                         verts.data(), GL_STREAM_DRAW);
            glUniform3f(glGetUniformLocation(lineProg_, "uColor"), sp.r, sp.g, sp.b);
            glUniform1f(glGetUniformLocation(lineProg_, "uAlpha"), sp.a);
            glLineWidth(sp.w);
            glDrawArrays(GL_LINES, 0, (GLsizei)(verts.size()/3));
        }
        glLineWidth(1.0f);

        // -- Current-time marker: a ring on the centre line at the sample
        //    nearest jdTd, so the textured view also has something that
        //    tracks the clock during a playback, like the 2-D view's dot.
        const EclipsePathSample* now = nullptr;
        for (const EclipsePathSample& s : path) {
            if (!s.center.valid) continue;
            if (!now || std::fabs(s.jdTd - jdTd) < std::fabs(now->jdTd - jdTd)) now = &s;
        }
        if (now) {
            // Small circle around the shadow centre, built in the tangent
            // plane so it lies flat on the globe.
            auto c = toXYZ(now->center.longitudeDeg, now->center.latitudeDeg, 1.006f);
            gx::Vec3 n = gx::normalize(gx::Vec3{c[0], c[1], c[2]});
            gx::Vec3 up{0.f, 1.f, 0.f};
            if (std::fabs(n.y) > 0.95f) up = gx::Vec3{1.f, 0.f, 0.f};
            gx::Vec3 e0 = gx::normalize(gx::cross(up, n));
            gx::Vec3 e1 = gx::cross(n, e0);

            const int kSeg = 48;
            const float ringR = 0.055f;
            verts.clear();
            verts.reserve(kSeg * 6);
            for (int i = 0; i < kSeg; ++i) {
                float a0 = (float)(i)     / kSeg * 2.0f * PI;
                float a1 = (float)(i + 1) / kSeg * 2.0f * PI;
                auto pt = [&](float a) {
                    gx::Vec3 p{ n.x + (e0.x*std::cos(a) + e1.x*std::sin(a)) * ringR,
                                n.y + (e0.y*std::cos(a) + e1.y*std::sin(a)) * ringR,
                                n.z + (e0.z*std::cos(a) + e1.z*std::sin(a)) * ringR };
                    p = gx::normalize(p);
                    return gx::Vec3{p.x * 1.006f, p.y * 1.006f, p.z * 1.006f};
                };
                gx::Vec3 pa = pt(a0), pb = pt(a1);
                verts.push_back(pa.x); verts.push_back(pa.y); verts.push_back(pa.z);
                verts.push_back(pb.x); verts.push_back(pb.y); verts.push_back(pb.z);
            }
            glBindVertexArray(lineVAO_);
            glBindBuffer(GL_ARRAY_BUFFER, lineVBO_);
            glBufferData(GL_ARRAY_BUFFER,
                         (GLsizeiptr)(verts.size()*sizeof(float)),
                         verts.data(), GL_STREAM_DRAW);
            glUniform3f(glGetUniformLocation(lineProg_, "uColor"), 1.00f, 0.29f, 0.22f);
            glUniform1f(glGetUniformLocation(lineProg_, "uAlpha"), 1.00f);
            glLineWidth(2.6f);
            glDrawArrays(GL_LINES, 0, (GLsizei)(verts.size()/3));
            glLineWidth(1.0f);
        }

        glDepthMask(GL_TRUE);
        glDisable(GL_BLEND);
    }

    glBindVertexArray(0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void Renderer::resize(int w, int h) {
    if (w < 16) w = 16;
    if (h < 16) h = 16;
    if (w == w_ && h == h_) return;
    w_ = w; h_ = h; fboDirty_ = true;
}

// ============================================================================
//  Helper: bind texture for a given material name.
//  Always binds *something* to unit 0 so the sampler is never unbound.
// ============================================================================
static void bindMaterialTex(
    unsigned int prog,
    const std::map<std::string, unsigned int>& textures,
    const std::string& matName,
    unsigned int dummyTex)
{
    auto it  = textures.find(matName);
    bool has = (it != textures.end() && it->second != 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, has ? it->second : dummyTex);
    glUniform1i(glGetUniformLocation(prog, "uTex"),    0);
    glUniform1i(glGetUniformLocation(prog, "uUseTex"), has ? 1 : 0);
}

// Map body pinyin to OBJ material name (nullptr if no mesh).
static const char* pinyinToMaterial(const std::string& pinyin) {
    if (pinyin == "sun")     return "Sun";
    if (pinyin == "mercury") return "Mercury";
    if (pinyin == "venus")   return "Venus";
    if (pinyin == "earth")   return "Earth";
    if (pinyin == "mars")    return "Mars";
    if (pinyin == "jupiter") return "Jupiter";
    if (pinyin == "saturn")  return "Saturn";
    if (pinyin == "uranus")  return "Uranus";
    if (pinyin == "neptune") return "Neptune";
    return nullptr; // Pluto or unknown bodies use the sphere fallback.
}

// Draw one mesh (OBJ or sphere fallback).
// prog must already be in use and common uniforms are set.
static void drawMesh(unsigned int prog,
                     const std::map<std::string, GpuMesh>& objMeshes,
                     const std::map<std::string, unsigned int>& materialTextures,
                     const std::string& matName,
                     unsigned int sphereVAO, int sphereIndexCount,
                     unsigned int dummyTex)
{
    bindMaterialTex(prog, materialTextures, matName, dummyTex);

    auto it = objMeshes.find(matName);
    if (it != objMeshes.end() && it->second.valid) {
        if (std::getenv("SXWNL_TRACE_DRAWS")) {
            std::fprintf(stderr, "[draw] mesh %s vao=%u count=%d\n",
                         matName.c_str(), it->second.vao, it->second.indexCount);
            std::fflush(stderr);
        }
        glBindVertexArray(it->second.vao);
        glDrawArrays(GL_TRIANGLES, 0, it->second.indexCount);
    } else {
#ifdef __APPLE__
        const GpuMesh* proxy = nullptr;
        for (const char* key : {"Mercury", "Earth", "Sun"}) {
            auto pit = objMeshes.find(key);
            if (pit != objMeshes.end() && pit->second.valid) {
                proxy = &pit->second;
                break;
            }
        }
        if (proxy) {
            if (std::getenv("SXWNL_TRACE_DRAWS")) {
                std::fprintf(stderr, "[draw] proxy %s vao=%u count=%d\n",
                             matName.c_str(), proxy->vao, proxy->indexCount);
                std::fflush(stderr);
            }
            glBindVertexArray(proxy->vao);
            glDrawArrays(GL_TRIANGLES, 0, proxy->indexCount);
            return;
        }
        if (std::getenv("SXWNL_TRACE_DRAWS")) {
            std::fprintf(stderr, "[draw] skip fallback %s\n", matName.c_str());
            std::fflush(stderr);
        }
#else
        if (std::getenv("SXWNL_TRACE_DRAWS")) {
            std::fprintf(stderr, "[draw] fallback %s vao=%u count=%d\n",
                         matName.c_str(), sphereVAO, sphereIndexCount);
            std::fflush(stderr);
        }
        glBindVertexArray(sphereVAO);
        glDrawArrays(GL_TRIANGLES, 0, sphereIndexCount);
#endif
    }
}

static void drawFlatMesh(const std::map<std::string, GpuMesh>& objMeshes,
                         const std::string& matName,
                         unsigned int sphereVAO, unsigned int sphereEBO,
                         int sphereIndexCount,
                         bool forceSphere)
{
    auto it = objMeshes.find(matName);
    if (!forceSphere && it != objMeshes.end() && it->second.valid) {
        glBindVertexArray(it->second.vao);
        glDrawArrays(GL_TRIANGLES, 0, it->second.indexCount);
    } else {
        glBindVertexArray(sphereVAO);
        glDrawArrays(GL_TRIANGLES, 0, sphereIndexCount);
    }
}

static float hash01(int n) {
    unsigned int x = (unsigned int)n * 747796405u + 2891336453u;
    x = ((x >> ((x >> 28u) + 4u)) ^ x) * 277803737u;
    x = (x >> 22u) ^ x;
    return (x & 0x00ffffffu) / 16777215.0f;
}

static float smoothNoise(float x) {
    int i = (int)std::floor(x);
    float f = x - (float)i;
    float u = f * f * (3.0f - 2.0f * f);
    return hash01(i) * (1.0f - u) + hash01(i + 1) * u;
}

static float smoothstepf(float edge0, float edge1, float x) {
    float t = std::clamp((x - edge0) / std::max(edge1 - edge0, 1e-6f), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

static void setBillboardUniforms(unsigned int prog, const gx::Vec3& right,
                                 const gx::Vec3& up, float worldScale) {
    glUniform3f(glGetUniformLocation(prog, "uBillboardRight"), right.x, right.y, right.z);
    glUniform3f(glGetUniformLocation(prog, "uBillboardUp"), up.x, up.y, up.z);
    glUniform1f(glGetUniformLocation(prog, "uWorldScale"), worldScale);
}

static void appendSpriteVertex(std::vector<float>& data, const gx::Vec3& center,
                               float ox, float oy, float size, float v0) {
    data.push_back(center.x);
    data.push_back(center.y);
    data.push_back(center.z);
    data.push_back(ox);
    data.push_back(oy);
    data.push_back(size);
    data.push_back(v0);
}

static void appendSpriteVertex(std::vector<float>& data, const gx::Vec3& center,
                               float ox, float oy, float size, float v0, float v1) {
    data.push_back(center.x);
    data.push_back(center.y);
    data.push_back(center.z);
    data.push_back(ox);
    data.push_back(oy);
    data.push_back(size);
    data.push_back(v0);
    data.push_back(v1);
}

static void appendSpriteQuad(std::vector<float>& data, const gx::Vec3& center,
                             float size, float v0) {
    static const float kCorner[12] = {
        -1.0f, -1.0f,  1.0f, -1.0f,  1.0f,  1.0f,
        -1.0f, -1.0f,  1.0f,  1.0f, -1.0f,  1.0f,
    };
    for (int c = 0; c < 6; ++c)
        appendSpriteVertex(data, center, kCorner[c * 2], kCorner[c * 2 + 1], size, v0);
}

static void appendSpriteQuad(std::vector<float>& data, const gx::Vec3& center,
                             float size, float v0, float v1) {
    static const float kCorner[12] = {
        -1.0f, -1.0f,  1.0f, -1.0f,  1.0f,  1.0f,
        -1.0f, -1.0f,  1.0f,  1.0f, -1.0f,  1.0f,
    };
    for (int c = 0; c < 6; ++c)
        appendSpriteVertex(data, center, kCorner[c * 2], kCorner[c * 2 + 1], size, v0, v1);
}

static gx::Vec3 gravityGridPoint(float x, float z, float curvature, float extent) {
    float r = std::sqrt(x*x + z*z);
    float core = std::clamp(2.2f + curvature * 1.15f, 1.4f, 5.8f);
    float broad = std::clamp(12.0f + curvature * 6.0f, 9.0f, 28.0f);
    float rimFade = 1.0f - smoothstepf(extent * 0.62f, extent * 0.98f, r);
    float funnel = 1.0f / (1.0f + (r * r) / (core * core));
    float cloth = std::exp(-(r * r) / (2.0f * broad * broad));
    float well = -(5.6f * curvature * funnel + 3.2f * curvature * cloth) * rimFade;
    return {x, well, z};
}

static void drawGravityGrid(unsigned int lineProg, unsigned int lineVAO,
                            unsigned int lineVBO, const gx::Mat4& vp,
                            const gx::Mat4& id, float density,
                            float curvature, float cameraDistance)
{
    const float extent = 52.0f;
    density = std::clamp(density, 0.6f, 4.0f);
    curvature = std::clamp(curvature, 0.1f, 3.0f);
    float zoomDensity = density * std::clamp(55.0f / std::max(cameraDistance, 1.0f), 0.75f, 3.2f);
    const int lines = std::clamp((int)std::round(30.0f + zoomDensity * 18.0f), 36, 112);
    const int segs = std::clamp((int)std::round(190.0f + zoomDensity * 82.0f), 220, 520);
    const int ringSegs = std::clamp((int)std::round(180.0f + zoomDensity * 72.0f), 220, 520);
    const float step = (extent * 2.0f) / (lines - 1);
    std::vector<gx::Vec3> line;
    line.reserve(std::max(segs, ringSegs) + 1);

    sxwnlSetLineSmoothing(true);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    glUseProgram(lineProg);
    glUniformMatrix4fv(glGetUniformLocation(lineProg, "uViewProj"),
                       1, GL_FALSE, vp.data());
    glUniformMatrix4fv(glGetUniformLocation(lineProg, "uModel"),
                       1, GL_FALSE, id.data());
    glUniform3f(glGetUniformLocation(lineProg, "uColor"), 0.33f, 0.48f, 0.78f);
    glUniform1f(glGetUniformLocation(lineProg, "uAlpha"), 0.19f);
    glBindVertexArray(lineVAO);
    glBindBuffer(GL_ARRAY_BUFFER, lineVBO);

    for (int axis = 0; axis < 2; ++axis) {
        for (int i = 0; i < lines; ++i) {
            float fixed = -extent + step * i;
            line.clear();
            for (int s = 0; s <= segs; ++s) {
                float v = -extent + (extent * 2.0f) * (float)s / (float)segs;
                line.push_back(axis == 0 ? gravityGridPoint(v, fixed, curvature, extent)
                                         : gravityGridPoint(fixed, v, curvature, extent));
            }
            glBufferData(GL_ARRAY_BUFFER,
                         (GLsizeiptr)(line.size() * sizeof(gx::Vec3)),
                         line.data(), GL_DYNAMIC_DRAW);
            glDrawArrays(GL_LINE_STRIP, 0, (int)line.size());
        }
    }

    // Concentric rings make the curvature legible from oblique camera angles.
    glUniform3f(glGetUniformLocation(lineProg, "uColor"), 0.42f, 0.58f, 0.92f);
    glUniform1f(glGetUniformLocation(lineProg, "uAlpha"), 0.15f);
    int ringCount = std::clamp((int)std::round(10.0f + zoomDensity * 7.0f), 14, 42);
    for (int rIdx = 0; rIdx < ringCount; ++rIdx) {
        float u = (float)rIdx / (float)(ringCount - 1);
        float rr = 2.2f + std::pow(u, 1.28f) * 43.5f;
        line.clear();
        for (int s = 0; s <= ringSegs; ++s) {
            float a = 6.2831853f * (float)s / (float)ringSegs;
            line.push_back(gravityGridPoint(std::cos(a) * rr, std::sin(a) * rr, curvature, extent));
        }
        glBufferData(GL_ARRAY_BUFFER,
                     (GLsizeiptr)(line.size() * sizeof(gx::Vec3)),
                     line.data(), GL_DYNAMIC_DRAW);
        glDrawArrays(GL_LINE_STRIP, 0, (int)line.size());
    }

    glBindVertexArray(0);
    glDepthMask(GL_TRUE);
    sxwnlSetLineSmoothing(false);
    glDisable(GL_BLEND);
}

static void drawAsteroidBelt(unsigned int starProg, unsigned int asteroidVAO,
                             unsigned int asteroidVBO, const Scene& scene,
                             const gx::Mat4& vp, const gx::Vec3& billboardRight,
                             const gx::Vec3& billboardUp, float worldScale)
{
    const auto& ast = scene.asteroids();
    if (ast.empty()) return;

    std::vector<float> data;
    data.reserve(ast.size() * 6 * 7);
    for (const AsteroidState& a : ast) {
        appendSpriteQuad(data, a.world, a.displaySize, a.brightness);
    }

    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glUseProgram(starProg);
    glUniformMatrix4fv(glGetUniformLocation(starProg, "uViewProj"),
                       1, GL_FALSE, vp.data());
    setBillboardUniforms(starProg, billboardRight, billboardUp, worldScale);
    glUniform3f(glGetUniformLocation(starProg, "uColor"), 0.78f, 0.72f, 0.62f);
    glBindVertexArray(asteroidVAO);
    glBindBuffer(GL_ARRAY_BUFFER, asteroidVBO);
    glBufferData(GL_ARRAY_BUFFER,
                 (GLsizeiptr)(data.size() * sizeof(float)),
                 data.data(), GL_DYNAMIC_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, (int)ast.size() * 6);
    glBindVertexArray(0);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
}

static gx::Vec3 rotateAroundAxis(const gx::Vec3& v, const gx::Vec3& axis, float a) {
    float c = std::cos(a), s = std::sin(a);
    return v * c + gx::cross(axis, v) * s + axis * (gx::dot(axis, v) * (1.0f - c));
}

static void drawSolarFlames(unsigned int flameProg, unsigned int flameVAO,
                            unsigned int flameVBO, const gx::Mat4& vp,
                            const gx::Vec3& sunWorld, float sunRadius,
                            float timeSec, const gx::Vec3& billboardRight,
                            const gx::Vec3& billboardUp, float worldScale)
{
    float relRadius = std::clamp(sunRadius / 2.0f, 0.45f, 2.6f);
    const int N = std::clamp((int)std::round(230.0f * relRadius * relRadius), 90, 1450);
    const float PI = 3.14159265358979323846f;
    std::vector<float> data;
    data.reserve(N * 6 * 8);

    for (int i = 0; i < N; ++i) {
        float u = ((float)i + hash01(i * 17 + 3)) / (float)N;
        float v = hash01(i * 31 + 11);
        float z = 1.0f - 2.0f * v;
        float ring = std::sqrt(std::max(0.0f, 1.0f - z * z));
        float phi = 2.0f * PI * u;
        gx::Vec3 dir{ring * std::cos(phi), z, ring * std::sin(phi)};

        float seed = hash01(i * 73 + 19);
        float life = std::fmod(timeSec * (0.10f + 0.18f * seed) + seed, 1.0f);
        float curlA = (smoothNoise(timeSec * 0.42f + seed * 9.0f) - 0.5f) * 1.1f;
        gx::Vec3 swirlAxis = gx::normalize(gx::cross(dir, gx::Vec3{0.23f, 1.0f, 0.37f}));
        if (gx::length(swirlAxis) < 1e-4f) swirlAxis = {0.0f, 0.0f, 1.0f};
        dir = gx::normalize(rotateAroundAxis(dir, swirlAxis, curlA * life));

        float burst = std::pow(1.0f - life, 0.55f);
        float lift = sunRadius * (1.02f + life * (0.55f + 0.42f * seed));
        float wave = 0.08f * std::sin(timeSec * (2.0f + seed * 2.5f) + seed * 29.0f);
        gx::Vec3 pos = sunWorld + dir * (lift + sunRadius * wave);

        float heat = std::clamp(1.0f - life * 0.85f + 0.18f * seed, 0.0f, 1.0f);
        float alpha = 0.30f * burst * (0.55f + seed * 0.7f) / std::sqrt(relRadius);
        float size = std::clamp(sunRadius * (6.5f + 8.0f * (1.0f - life) + 3.0f * seed),
                                5.0f, 52.0f);
        appendSpriteQuad(data, pos, size, heat, alpha);
    }

    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    glUseProgram(flameProg);
    glUniformMatrix4fv(glGetUniformLocation(flameProg, "uViewProj"),
                       1, GL_FALSE, vp.data());
    setBillboardUniforms(flameProg, billboardRight, billboardUp, worldScale);
    glBindVertexArray(flameVAO);
    glBindBuffer(GL_ARRAY_BUFFER, flameVBO);
    glBufferData(GL_ARRAY_BUFFER,
                 (GLsizeiptr)(data.size() * sizeof(float)),
                 data.data(), GL_DYNAMIC_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, N * 6);
    glBindVertexArray(0);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
}

// ============================================================================
//  Eclipse geometry drawn into the solar-system scene
// ============================================================================
// The scene is a schematic: distances are log-compressed, bodies are inflated
// and the Moon's orbit is stretched so it can be seen at all. A shadow cone
// built from true radii and distances would be a needle a hundred thousand
// units long and read as nothing. What is drawn instead is the shadow's real
// geography - the umbra and penumbra land on the Earth exactly where the
// engine's path says they do, at the size the engine's north/south limits say
// - joined back to the Moon by a cone. So the parts that carry information
// are exact and only the connecting cone is schematic.

// Unit vector for a geographic point, in the frame boundary lines and eclipse
// paths already use: lon 0 -> +Z, lon 90E -> +X, north pole -> +Y.
static gx::Vec3 geoToUnit(double lonDeg, double latDeg) {
    const float d2r = 3.14159265358979323846f / 180.0f;
    float lo = (float)lonDeg * d2r, la = (float)latDeg * d2r;
    float cl = std::cos(la);
    return {cl * std::sin(lo), std::sin(la), cl * std::cos(lo)};
}

// Angular radius of a shadow from the two limit points on opposite sides of
// it: half the angle they subtend at the centre of the Earth.
static float shadowAngRadius(const EclipseGeoPoint& a, const EclipseGeoPoint& b) {
    if (!a.valid || !b.valid) return 0.0f;
    float c = gx::dot(geoToUnit(a.longitudeDeg, a.latitudeDeg),
                      geoToUnit(b.longitudeDeg, b.latitudeDeg));
    return 0.5f * std::acos(std::clamp(c, -1.0f, 1.0f));
}

// Two vectors spanning the plane perpendicular to n.
static void basisFor(const gx::Vec3& n, gx::Vec3& e0, gx::Vec3& e1) {
    gx::Vec3 up = (std::fabs(n.y) > 0.95f) ? gx::Vec3{1, 0, 0} : gx::Vec3{0, 1, 0};
    e0 = gx::normalize(gx::cross(up, n));
    e1 = gx::cross(n, e0);
}

// Side wall of a truncated cone between two circles, as triangles.
static void appendFrustum(std::vector<float>& out,
                          const gx::Vec3& c0, float r0,
                          const gx::Vec3& c1, float r1, int seg) {
    gx::Vec3 axis = gx::normalize(c1 - c0);
    if (gx::length(axis) < 0.5f) return;
    gx::Vec3 e0, e1;
    basisFor(axis, e0, e1);
    const float TAU = 6.28318530718f;
    auto put = [&](const gx::Vec3& v) {
        out.push_back(v.x); out.push_back(v.y); out.push_back(v.z);
    };
    for (int i = 0; i < seg; ++i) {
        float a0 = TAU * i / seg, a1 = TAU * (i + 1) / seg;
        gx::Vec3 d0 = e0 * std::cos(a0) + e1 * std::sin(a0);
        gx::Vec3 d1 = e0 * std::cos(a1) + e1 * std::sin(a1);
        gx::Vec3 p00 = c0 + d0 * r0, p01 = c0 + d1 * r0;
        gx::Vec3 p10 = c1 + d0 * r1, p11 = c1 + d1 * r1;
        put(p00); put(p01); put(p11);
        put(p00); put(p11); put(p10);
    }
}

// A disc lying on the unit sphere around `centre`, as a triangle fan. Used for
// the shadow footprints, which have to hug the surface rather than float over
// it as a flat plate would at these angular sizes.
static void appendSphereCap(std::vector<float>& out, const gx::Vec3& centre,
                            float angRadius, float radius, int seg) {
    gx::Vec3 n = gx::normalize(centre), e0, e1;
    basisFor(n, e0, e1);
    const float TAU = 6.28318530718f;
    float ca = std::cos(angRadius), sa = std::sin(angRadius);
    auto put = [&](const gx::Vec3& v) {
        out.push_back(v.x * radius); out.push_back(v.y * radius); out.push_back(v.z * radius);
    };
    for (int i = 0; i < seg; ++i) {
        float a0 = TAU * i / seg, a1 = TAU * (i + 1) / seg;
        gx::Vec3 p0 = n * ca + (e0 * std::cos(a0) + e1 * std::sin(a0)) * sa;
        gx::Vec3 p1 = n * ca + (e0 * std::cos(a1) + e1 * std::sin(a1)) * sa;
        put(n); put(gx::normalize(p0)); put(gx::normalize(p1));
    }
}

// Sample whose time is closest to jdTd, or null when the path is empty.
static const EclipsePathSample* nearestSample(
        const std::vector<EclipsePathSample>& path, double jdTd) {
    const EclipsePathSample* best = nullptr;
    for (const EclipsePathSample& s : path) {
        if (!s.center.valid && !s.penumbraNorth.valid) continue;
        if (!best || std::fabs(s.jdTd - jdTd) < std::fabs(best->jdTd - jdTd)) best = &s;
    }
    return best;
}

// ============================================================================
// Render the main solar-system scene into the FBO.
// ============================================================================
void Renderer::render(const Scene& scene, const gx::OrbitCamera& cam,
                      const RenderOptions& opt,
                      const EclipseSceneOverlay* eclipse) {
    ensureFBO();
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
    glViewport(0, 0, w_, h_);
    glEnable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);  // planets may have inconsistent winding; keep both faces
    // Background stays black; the star field provides the space atmosphere.
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    float    aspect = (float)w_ / (float)h_;
    gx::Mat4 vp     = cam.proj(aspect) * cam.view();
    viewProj_       = vp;
    gx::Mat4 id     = gx::Mat4::identity();
    gx::Vec3 eyePos = cam.eye();
    gx::Vec3 viewForward = gx::normalize(cam.target - eyePos);
    gx::Vec3 billboardRight = gx::normalize(gx::cross(viewForward, gx::Vec3{0.0f, 1.0f, 0.0f}));
    if (gx::length(billboardRight) < 1e-4f) billboardRight = {1.0f, 0.0f, 0.0f};
    gx::Vec3 billboardUp = gx::normalize(gx::cross(billboardRight, viewForward));
    if (starCount_ > 0) {
        glDepthMask(GL_FALSE);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE); // additive: brightest stars glow
        glUseProgram(starProg_);
        glUniformMatrix4fv(glGetUniformLocation(starProg_, "uViewProj"),
                           1, GL_FALSE, vp.data());
        setBillboardUniforms(starProg_, billboardRight, billboardUp, 0.22f);
        glUniform3f(glGetUniformLocation(starProg_, "uColor"), 0.88f, 0.92f, 1.0f);
        glBindVertexArray(starVAO_);
        glDrawArrays(GL_TRIANGLES, 0, starCount_);
        glBindVertexArray(0);
        glDepthMask(GL_TRUE);
        glDisable(GL_BLEND);
    }

    if (opt.showGravityGrid) {
        drawGravityGrid(lineProg_, lineVAO_, lineVBO_, vp, id,
                        opt.gravityGridDensity, opt.gravityGridCurvature, cam.distance);
    }

    // Orbit lines.
    if (opt.showOrbits) {
        sxwnlSetLineSmoothing(true);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDepthMask(GL_FALSE);
        glUseProgram(lineProg_);
        glUniformMatrix4fv(glGetUniformLocation(lineProg_, "uViewProj"),
                           1, GL_FALSE, vp.data());
        glUniformMatrix4fv(glGetUniformLocation(lineProg_, "uModel"),
                           1, GL_FALSE, id.data());
        glUniform1f(glGetUniformLocation(lineProg_, "uAlpha"), 0.68f);
        glBindVertexArray(lineVAO_);
        glLineWidth(1.35f);

        const auto& orbits = scene.orbits();
        const auto& bodies = scene.bodies();
        for (size_t i = 0; i < orbits.size(); ++i) {
            if (orbits[i].empty()) continue;
            glUniform3f(glGetUniformLocation(lineProg_, "uColor"),
                        std::min(1.0f, bodies[i].color[0]*1.15f + 0.10f),
                        std::min(1.0f, bodies[i].color[1]*1.15f + 0.10f),
                        std::min(1.0f, bodies[i].color[2]*1.15f + 0.10f));
            glBindBuffer(GL_ARRAY_BUFFER, lineVBO_);
            glBufferData(GL_ARRAY_BUFFER,
                         (GLsizeiptr)(orbits[i].size() * sizeof(gx::Vec3)),
                         orbits[i].data(), GL_DYNAMIC_DRAW);
            glDrawArrays(GL_LINE_LOOP, 0, (int)orbits[i].size());
        }
        glLineWidth(1.0f);
        glBindVertexArray(0);
        glDepthMask(GL_TRUE);
        sxwnlSetLineSmoothing(false);
        glDisable(GL_BLEND);
    }

    if (opt.showAsteroids) {
        float asteroidScale = std::clamp(cam.distance * 0.0009f, 0.025f, 0.28f);
        drawAsteroidBelt(starProg_, asteroidVAO_, asteroidVBO_, scene, vp,
                         billboardRight, billboardUp, asteroidScale);
    }

    // Opaque bodies: Sun and planets, excluding Saturn rings.
    const auto& states = scene.states();
    const auto& bodies = scene.bodies();

    // Sun world-space position (for lighting)
    gx::Vec3 sunWorld{0.f, 0.f, 0.f};
    float sunDisplayRadius = 1.0f;
    for (size_t i = 0; i < bodies.size(); ++i) {
        if (bodies[i].isSun) {
            sunWorld = states[i].world;
            sunDisplayRadius = states[i].displayRadius;
            break;
        }
    }

    for (size_t i = 0; i < states.size(); ++i) {
        const BodyState& s = states[i];
        const BodyInfo&  b = bodies[i];
        float radius = std::max(s.displayRadius, b.isSun ? 1.35f : 0.22f);
        // model = T * tilt * spin * S.
        //   spin: rotation about the local polar (Y) axis, the body's day.
        //   tilt/node: swing the polar axis to its true sky direction.
        // Bodies with no rotation elements leave all three at 0, so the
        // rotation factors collapse to identity and they render upright.
        const float kDeg2Rad = 3.14159265358979323846f / 180.0f;
        gx::Mat4 meshAxisFix = meshAxisFixFor(b.pinyin);
        gx::Mat4 model = gx::translate(s.world)
                       * gx::rotateY(s.poleNodeDeg * kDeg2Rad)
                       * gx::rotateX(s.axialTiltDeg * kDeg2Rad)
                       * gx::rotateY(s.spinDeg * kDeg2Rad)
                       * meshAxisFix
                       * gx::scale(radius);

        const char* matName = pinyinToMaterial(b.pinyin);
        std::string mat     = matName ? matName : "";

        unsigned int prog = b.isSun ? sunProg_ : litProg_;
        glUseProgram(prog);
        glUniformMatrix4fv(glGetUniformLocation(prog, "uViewProj"),
                           1, GL_FALSE, vp.data());
        glUniformMatrix4fv(glGetUniformLocation(prog, "uModel"),
                           1, GL_FALSE, model.data());
        float boost = b.isSun ? 1.0f : 1.25f;
        float baseR = std::min(b.color[0] * boost, 1.0f);
        float baseG = std::min(b.color[1] * boost, 1.0f);
        float baseB = std::min(b.color[2] * boost, 1.0f);
        float texMix = 0.82f;
        float specStrength = 0.22f;
        if (b.pinyin == "mercury") {
            baseR = 0.78f; baseG = 0.66f; baseB = 0.52f;
            texMix = 0.55f;
            specStrength = 0.10f;
        }
        glUniform3f(glGetUniformLocation(prog, "uColor"), baseR, baseG, baseB);
        if (b.isSun) {
            glUniform1f(glGetUniformLocation(prog, "uAlpha"), 1.0f);
        } else {
            glUniform1f(glGetUniformLocation(prog, "uTexMix"), texMix);
            glUniform1f(glGetUniformLocation(prog, "uSpecStrength"), specStrength);
            glUniform3f(glGetUniformLocation(prog, "uLightPos"),
                        sunWorld.x, sunWorld.y, sunWorld.z);
            glUniform3f(glGetUniformLocation(prog, "uEyePos"),
                        eyePos.x, eyePos.y, eyePos.z);
        }

        drawMesh(prog, objMeshes_, materialTextures_, mat,
                 sphereVAO_, sphereIndexCount_, dummyTex_);
    }

    // Moon near Earth, with intentionally compressed display scale.
    if (opt.showSolarFlames) {
        using Clock = std::chrono::steady_clock;
        static const auto t0 = Clock::now();
        float tNow = std::chrono::duration<float>(Clock::now() - t0).count();
        float flameScale = std::clamp(cam.distance * 0.00045f, 0.015f, 0.85f);
        drawSolarFlames(flameProg_, flameVAO_, flameVBO_, vp,
                sunWorld, std::max(sunDisplayRadius, 1.35f), tNow,
                billboardRight, billboardUp, flameScale);
    }

    if (opt.showMoon && scene.moon().valid) {
        const MoonData& md = scene.moon();
        const float kDeg2Rad = 3.14159265358979323846f / 180.0f;
        gx::Mat4 model = gx::translate(md.worldPos)
                       * gx::rotateY(md.poleNodeDeg  * kDeg2Rad)
                       * gx::rotateX(md.axialTiltDeg * kDeg2Rad)
                       * gx::rotateY(md.spinDeg      * kDeg2Rad)
                       * meshAxisFixFor("moon")
                       * gx::scale(std::max(md.displayRadius, 0.10f));
        glUseProgram(litProg_);
        glUniformMatrix4fv(glGetUniformLocation(litProg_, "uViewProj"),
                           1, GL_FALSE, vp.data());
        glUniformMatrix4fv(glGetUniformLocation(litProg_, "uModel"),
                           1, GL_FALSE, model.data());
        // Inside Earth's shadow the Moon is not lit by the Sun at all; what is
        // left is the red-bent light refracted through Earth's atmosphere, so
        // it goes dim and coppery rather than simply dark.
        float shade = (eclipse && eclipse->active && !eclipse->solar)
                    ? std::clamp(eclipse->lunarShade, 0.0f, 1.0f) : 0.0f;
        glUniform3f(glGetUniformLocation(litProg_, "uColor"),
                    0.86f - 0.30f * shade, 0.86f - 0.62f * shade, 0.82f - 0.66f * shade);
        glUniform1f(glGetUniformLocation(litProg_, "uTexMix"), 0.92f - 0.55f * shade);
        glUniform1f(glGetUniformLocation(litProg_, "uSpecStrength"), 0.08f);
        glUniform3f(glGetUniformLocation(litProg_, "uLightPos"),
                    sunWorld.x, sunWorld.y, sunWorld.z);
        glUniform3f(glGetUniformLocation(litProg_, "uEyePos"),
                    eyePos.x, eyePos.y, eyePos.z);
        bindMaterialTex(litProg_, materialTextures_, "Moon", dummyTex_);
        if (moonMesh_.valid) {
            glBindVertexArray(moonMesh_.vao);
            glDrawArrays(GL_TRIANGLES, 0, moonMesh_.indexCount);
        } else {
            glBindVertexArray(sphereVAO_);
            glDrawArrays(GL_TRIANGLES, 0, sphereIndexCount_);
        }
    }

    // ---- Eclipse geometry ---------------------------------------------------
    if (eclipse && eclipse->active) {
        const float kDeg2Rad = 3.14159265358979323846f / 180.0f;
        int earthIdx = -1;
        for (size_t i = 0; i < bodies.size(); ++i)
            if (bodies[i].xt == 0) { earthIdx = (int)i; break; }

        if (earthIdx >= 0) {
            const BodyState& es = states[earthIdx];
            const float earthR = std::max(es.displayRadius, 0.22f);
            // The geographic frame, without the mesh's own axis correction:
            // curves and shadows are built from lon/lat directly, so they need
            // the body transform the mesh gets *before* that correction.
            gx::Mat4 geoRot = gx::rotateY(es.poleNodeDeg * kDeg2Rad)
                            * gx::rotateX(es.axialTiltDeg * kDeg2Rad)
                            * gx::rotateY(es.spinDeg * kDeg2Rad);
            gx::Mat4 geoModel = gx::translate(es.world) * geoRot * gx::scale(earthR);
            // World position of a geographic point on (or just above) Earth.
            auto geoWorld = [&](double lon, double lat, float r) {
                gx::Vec3 u = geoToUnit(lon, lat);
                gx::Vec3 rot = gx::transformDir(geoRot, u);
                return es.world + rot * (earthR * r);
            };

            std::vector<float> verts;
            auto flush = [&](unsigned int mode, float r, float g, float b, float a,
                             const gx::Mat4& model) {
                if (verts.empty()) return;
                glUseProgram(lineProg_);
                glUniformMatrix4fv(glGetUniformLocation(lineProg_, "uViewProj"), 1, GL_FALSE, vp.data());
                glUniformMatrix4fv(glGetUniformLocation(lineProg_, "uModel"), 1, GL_FALSE, model.data());
                glUniform3f(glGetUniformLocation(lineProg_, "uColor"), r, g, b);
                glUniform1f(glGetUniformLocation(lineProg_, "uAlpha"), a);
                glBindVertexArray(lineVAO_);
                glBindBuffer(GL_ARRAY_BUFFER, lineVBO_);
                glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(verts.size() * sizeof(float)),
                             verts.data(), GL_STREAM_DRAW);
                glDrawArrays(mode, 0, (GLsizei)(verts.size() / 3));
                verts.clear();
            };

            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glDepthMask(GL_FALSE);

            const EclipsePathSample* now =
                (eclipse->solar && eclipse->path) ? nearestSample(*eclipse->path, eclipse->jdTd)
                                                  : nullptr;
            // Only treat the shadow as present while the clock is actually
            // inside the eclipse; otherwise the nearest sample is hours away
            // and would park a shadow somewhere it never fell.
            const bool shadowNow = now && std::fabs(now->jdTd - eclipse->jdTd) < 6.0 / 1440.0;

            // -- Limit map on the globe (界线图) --
            if (eclipse->showCurves && eclipse->solar) {
                sxwnlSetLineSmoothing(true);
                if (eclipse->limits && eclipse->limits->valid) {
                    for (const EclipseLimitCurve& c : eclipse->limits->curves) {
                        float r = 1.00f, g = 0.42f, b = 0.42f, a = 0.85f, wdt = 1.6f;
                        if (c.kind == EclipseLimitCurve::HalfPenumbraLimit) {
                            r = 0.45f; g = 0.94f; b = 0.52f;
                        } else if (c.kind == EclipseLimitCurve::CenterLine) {
                            r = 1.00f; g = 0.85f; b = 0.25f; a = 0.95f; wdt = 2.2f;
                        }
                        for (size_t i = 0; i + 1 < c.points.size(); ++i) {
                            const EclipseGeoPoint& pa = c.points[i];
                            const EclipseGeoPoint& pb = c.points[i + 1];
                            if (!pa.valid || !pb.valid) continue;
                            if (std::fabs(pa.longitudeDeg - pb.longitudeDeg) > 180.0) continue;
                            gx::Vec3 va = geoToUnit(pa.longitudeDeg, pa.latitudeDeg) * 1.008f;
                            gx::Vec3 vb = geoToUnit(pb.longitudeDeg, pb.latitudeDeg) * 1.008f;
                            verts.push_back(va.x); verts.push_back(va.y); verts.push_back(va.z);
                            verts.push_back(vb.x); verts.push_back(vb.y); verts.push_back(vb.z);
                        }
                        glLineWidth(wdt);
                        flush(GL_LINES, r, g, b, a, geoModel);
                    }
                }
                glLineWidth(1.0f);
                sxwnlSetLineSmoothing(false);
            }

            // -- Shadow footprints and the cone that produces them --
            if (shadowNow) {
                float penAng = shadowAngRadius(now->penumbraNorth, now->penumbraSouth);
                float umbAng = shadowAngRadius(now->umbraNorth, now->umbraSouth);
                const EclipseGeoPoint& c =
                    now->center.valid ? now->center
                                      : (now->penumbraNorth.valid ? now->penumbraNorth
                                                                  : now->penumbraSouth);
                if (c.valid) {
                    gx::Vec3 cu = geoToUnit(c.longitudeDeg, c.latitudeDeg);
                    if (penAng > 1e-4f) {
                        appendSphereCap(verts, cu, penAng, 1.004f, 64);
                        flush(GL_TRIANGLES, 0.02f, 0.03f, 0.07f, 0.45f, geoModel);
                    }
                    if (umbAng > 1e-5f) {
                        // The umbra can be only tens of kilometres across, far
                        // under a pixel here, so it gets a floor wide enough to
                        // stay visible - it marks where totality is, and a mark
                        // too small to see marks nothing.
                        float drawn = std::max(umbAng, 0.012f);
                        appendSphereCap(verts, cu, drawn, 1.006f, 48);
                        flush(GL_TRIANGLES, 0.02f, 0.02f, 0.04f, 0.92f, geoModel);
                    }

                    if (eclipse->showCones && opt.showMoon && scene.moon().valid) {
                        const MoonData& md = scene.moon();
                        float moonR = std::max(md.displayRadius, 0.10f);
                        gx::Vec3 tip = geoWorld(c.longitudeDeg, c.latitudeDeg, 1.0f);
                        float penTipR = earthR * std::sin(std::max(penAng, 0.02f));
                        float umbTipR = earthR * std::sin(std::max(umbAng, 0.012f));
                        gx::Mat4 id2 = gx::Mat4::identity();

                        appendFrustum(verts, md.worldPos, moonR, tip, penTipR, 48);
                        flush(GL_TRIANGLES, 0.55f, 0.62f, 0.80f, 0.10f, id2);
                        appendFrustum(verts, md.worldPos, moonR * 0.999f, tip, umbTipR, 48);
                        flush(GL_TRIANGLES, 0.10f, 0.11f, 0.18f, 0.42f, id2);

                        // Outline: the cone walls read as haze on their own, and
                        // the edges are what show it converging.
                        gx::Vec3 axis = gx::normalize(tip - md.worldPos), e0, e1;
                        basisFor(axis, e0, e1);
                        for (int i = 0; i < 4; ++i) {
                            float a = 1.5707963f * i;
                            gx::Vec3 d = e0 * std::cos(a) + e1 * std::sin(a);
                            gx::Vec3 p0 = md.worldPos + d * moonR;
                            gx::Vec3 p1 = tip + d * umbTipR;
                            verts.push_back(p0.x); verts.push_back(p0.y); verts.push_back(p0.z);
                            verts.push_back(p1.x); verts.push_back(p1.y); verts.push_back(p1.z);
                        }
                        glLineWidth(1.6f);
                        flush(GL_LINES, 1.00f, 0.62f, 0.35f, 0.75f, id2);
                        glLineWidth(1.0f);
                    }
                }
            }

            // -- Lunar eclipse: Earth's own shadow, drawn out past the Moon --
            if (!eclipse->solar && eclipse->showCones && opt.showMoon && scene.moon().valid) {
                const MoonData& md = scene.moon();
                gx::Vec3 antiSun = gx::normalize(es.world - sunWorld);
                float moonDist = gx::dot(md.worldPos - es.world, antiSun);
                if (moonDist > 0.0f) {
                    // Ratios at the Moon's real distance: Earth's umbra is
                    // about 0.72 Earth radii across there and the penumbra
                    // about 1.26, which is what makes a lunar eclipse a slow
                    // crawl through a shadow far bigger than the Moon.
                    gx::Vec3 farC = es.world + antiSun * (moonDist * 1.25f);
                    gx::Mat4 id2 = gx::Mat4::identity();
                    appendFrustum(verts, es.world, earthR * 1.26f, farC, earthR * 1.58f, 48);
                    flush(GL_TRIANGLES, 0.45f, 0.52f, 0.70f, 0.08f, id2);
                    appendFrustum(verts, es.world, earthR, farC, earthR * 0.65f, 48);
                    flush(GL_TRIANGLES, 0.08f, 0.07f, 0.12f, 0.40f, id2);

                    gx::Vec3 e0, e1;
                    basisFor(antiSun, e0, e1);
                    for (int i = 0; i < 4; ++i) {
                        float a = 1.5707963f * i;
                        gx::Vec3 d = e0 * std::cos(a) + e1 * std::sin(a);
                        gx::Vec3 p0 = es.world + d * earthR;
                        gx::Vec3 p1 = farC + d * (earthR * 0.65f);
                        verts.push_back(p0.x); verts.push_back(p0.y); verts.push_back(p0.z);
                        verts.push_back(p1.x); verts.push_back(p1.y); verts.push_back(p1.z);
                    }
                    glLineWidth(1.6f);
                    flush(GL_LINES, 1.00f, 0.55f, 0.30f, 0.70f, id2);
                    glLineWidth(1.0f);
                }
            }

            glDepthMask(GL_TRUE);
            glDisable(GL_BLEND);
            glBindVertexArray(0);
        }
    }

#ifndef __APPLE__
    // Atmosphere glow.
    {
        // { pinyin, rim color R G B, scale, alpha, use additive blend }
        static const struct { const char* p; float r,g,b; float sc; float al; bool add; } kAtm[] = {
            {"earth",  0.30f, 0.55f, 1.00f, 1.10f, 0.55f, false},  // blue atmosphere
            {"venus",  0.90f, 0.80f, 0.50f, 1.08f, 0.35f, false},  // thick haze
        };
        glEnable(GL_BLEND);
        glUseProgram(atmProg_);
        glUniformMatrix4fv(glGetUniformLocation(atmProg_, "uViewProj"),
                           1, GL_FALSE, vp.data());
        for (const auto& a : kAtm) {
            for (size_t i = 0; i < bodies.size(); ++i) {
                if (bodies[i].pinyin != a.p) continue;
                gx::Mat4 m = gx::translate(states[i].world)
                           * gx::scale(states[i].displayRadius * a.sc);
                glUniformMatrix4fv(glGetUniformLocation(atmProg_, "uModel"),
                                   1, GL_FALSE, m.data());
                glUniform3f(glGetUniformLocation(atmProg_, "uColor"), a.r, a.g, a.b);
                glUniform3f(glGetUniformLocation(atmProg_, "uEyePos"),
                            eyePos.x, eyePos.y, eyePos.z);
                glUniform1f(glGetUniformLocation(atmProg_, "uAlpha"), a.al);
                if (a.add) glBlendFunc(GL_SRC_ALPHA, GL_ONE);
                else       glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
                glDepthMask(GL_FALSE);
                glBindVertexArray(sphereVAO_);
                glDrawArrays(GL_TRIANGLES, 0, sphereIndexCount_);
                glDepthMask(GL_TRUE);
                break;
            }
        }
        glDisable(GL_BLEND);
    }
#endif

    // Saturn rings, transparent pass.
    {
        auto ringIt = objMeshes_.find("Saturn_Rings");
        if (ringIt != objMeshes_.end() && ringIt->second.valid) {
            // Find Saturn's world position + display radius
            gx::Vec3 satWorld{0.f, 0.f, 0.f};
            float    satR = 1.f;
            for (size_t i = 0; i < bodies.size(); ++i) {
                if (bodies[i].pinyin == "saturn") {
                    satWorld = states[i].world;
                    satR     = states[i].displayRadius;
                    break;
                }
            }
            // Rings are scaled relative to Saturn's compressed visual radius.
            gx::Mat4 ringModel = gx::translate(satWorld) * gx::scale(satR * 1.75f);

            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glDisable(GL_CULL_FACE);
            glDepthMask(GL_FALSE);

            glUseProgram(ringProg_);
            glUniformMatrix4fv(glGetUniformLocation(ringProg_, "uViewProj"),
                               1, GL_FALSE, vp.data());
            glUniformMatrix4fv(glGetUniformLocation(ringProg_, "uModel"),
                               1, GL_FALSE, ringModel.data());
            glUniform3f(glGetUniformLocation(ringProg_, "uColor"),
                        0.85f, 0.78f, 0.55f);
            glUniform3f(glGetUniformLocation(ringProg_, "uLightPos"),
                        sunWorld.x, sunWorld.y, sunWorld.z);
            bindMaterialTex(ringProg_, materialTextures_, "Saturn_Rings", dummyTex_);

            glBindVertexArray(ringIt->second.vao);
            glDrawArrays(GL_TRIANGLES, 0, ringIt->second.indexCount);

            glDepthMask(GL_TRUE);
            glDisable(GL_BLEND);
        }
    }

    // Rotation axes: a line from south pole to north pole, for every body
    // that has rotation elements (the Sun and all planets).
    if (opt.showEarthAxis) {
        for (size_t i = 0; i < bodies.size(); ++i) {
            if (!bodies[i].rot.valid) continue;
            const BodyState& es = states[i];
            float radius = std::max(es.displayRadius, 0.22f);
            float axisLen = radius * 2.4f;

            // Scene already resolved the true polar axis in world space.
            const gx::Vec3& ax = es.poleDir;
            gx::Vec3 north = { es.world.x + ax.x * axisLen,
                               es.world.y + ax.y * axisLen,
                               es.world.z + ax.z * axisLen };
            gx::Vec3 south = { es.world.x - ax.x * axisLen,
                               es.world.y - ax.y * axisLen,
                               es.world.z - ax.z * axisLen };

            float lineVerts[6] = {
                south.x, south.y, south.z,
                north.x, north.y, north.z
            };

            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glDepthMask(GL_FALSE);
            sxwnlSetLineSmoothing(true);
            glLineWidth(1.6f);

            glUseProgram(lineProg_);
            glUniformMatrix4fv(glGetUniformLocation(lineProg_, "uViewProj"),
                               1, GL_FALSE, vp.data());
            glUniformMatrix4fv(glGetUniformLocation(lineProg_, "uModel"),
                               1, GL_FALSE, id.data());
            glUniform3f(glGetUniformLocation(lineProg_, "uColor"),
                        0.40f, 0.75f, 1.0f);  // pale blue
            glUniform1f(glGetUniformLocation(lineProg_, "uAlpha"), 0.85f);

            glBindVertexArray(lineVAO_);
            glBindBuffer(GL_ARRAY_BUFFER, lineVBO_);
            glBufferData(GL_ARRAY_BUFFER, sizeof(lineVerts), lineVerts, GL_DYNAMIC_DRAW);
            glDrawArrays(GL_LINES, 0, 2);

            glLineWidth(1.0f);
            sxwnlSetLineSmoothing(false);
            glDepthMask(GL_TRUE);
            glDisable(GL_BLEND);
            break;
        }
    }

    glBindVertexArray(0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

// ============================================================================
// Render the Moon mesh to a square FBO.
//  The sun is placed at the direction corresponding to the elongation angle.
//  Camera is at (0,0,3) looking at the origin.
// ============================================================================
void Renderer::renderMoonPhase(float elongDeg, float limbAngleDeg,
                               float yawDeg, float pitchDeg) {
    ensureMoonPhaseFBO();

    const float PI = 3.14159265f;
    float elong = elongDeg * PI / 180.0f;

    // Sun direction. The camera sits at +Z, standing in for the observer on
    // Earth, so: at new moon the Moon is between us and the Sun, putting the
    // Sun beyond it at -Z and the dark face toward us; at full moon the Earth
    // is in between, so the Sun is behind the camera at +Z and the lit face
    // is toward us. elong is 0 at new moon and 180 at full, hence the minus.
    // (The old comment here had it backwards, and the sign matched the
    // comment rather than the geometry, so the phases rendered inverted.)
    float sunX =  std::sin(elong) * 100.f;
    float sunY =  18.0f;
    float sunZ = -std::cos(elong) * 100.f;

    // Roll the light around the view axis so the terminator leans the same way
    // as the 2-D disk. limbAngleDeg is clockwise from screen-up while GL's Y
    // points up, so the rotation runs the other way.
    {
        const float roll = -(limbAngleDeg - 90.0f) * PI / 180.0f;
        const float c = std::cos(roll), s2 = std::sin(roll);
        const float rx = sunX * c - sunY * s2;
        const float ry = sunX * s2 + sunY * c;
        sunX = rx;
        sunY = ry;
    }

    gx::Vec3 camEye{0.f, 0.f, 3.05f};
    gx::Mat4 mv  = gx::lookAt(camEye, {0,0,0}, {0,1,0});
    gx::Mat4 pr  = gx::perspective(42.f * PI / 180.f, 1.f, 0.1f, 200.f);
    gx::Mat4 vp  = pr * mv;
    gx::Mat4 model = gx::rotateX(pitchDeg * PI / 180.0f)
                   * gx::rotateY(yawDeg * PI / 180.0f)
                   * gx::scale(0.96f);

    glBindFramebuffer(GL_FRAMEBUFFER, moonPhaseFBO_);
    glViewport(0, 0, kMoonPhaseSize, kMoonPhaseSize);
    glEnable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glClearColor(0.02f, 0.02f, 0.06f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glUseProgram(litProg_);
    glUniformMatrix4fv(glGetUniformLocation(litProg_, "uViewProj"),
                       1, GL_FALSE, vp.data());
    glUniformMatrix4fv(glGetUniformLocation(litProg_, "uModel"),
                       1, GL_FALSE, model.data());
    glUniform3f(glGetUniformLocation(litProg_, "uColor"), 0.88f, 0.86f, 0.82f);
    glUniform1f(glGetUniformLocation(litProg_, "uTexMix"), 0.94f);
    glUniform1f(glGetUniformLocation(litProg_, "uSpecStrength"), 0.10f);
    glUniform3f(glGetUniformLocation(litProg_, "uLightPos"), sunX, sunY, sunZ);
    glUniform3f(glGetUniformLocation(litProg_, "uEyePos"),
                camEye.x, camEye.y, camEye.z);

    bindMaterialTex(litProg_, materialTextures_, "Moon", dummyTex_);

    if (moonMesh_.valid) {
        glBindVertexArray(moonMesh_.vao);
        glDrawArrays(GL_TRIANGLES, 0, moonMesh_.indexCount);
    } else {
        glBindVertexArray(sphereVAO_);
        glDrawArrays(GL_TRIANGLES, 0, sphereIndexCount_);
    }

    glBindVertexArray(0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

// ============================================================================
//  shutdown
// ============================================================================
void Renderer::shutdown() {
    if (sphereVAO_) glDeleteVertexArrays(1, &sphereVAO_);
    if (sphereVBO_) glDeleteBuffers(1, &sphereVBO_);
    if (sphereEBO_) glDeleteBuffers(1, &sphereEBO_);
    if (lineVAO_)   glDeleteVertexArrays(1, &lineVAO_);
    if (lineVBO_)   glDeleteBuffers(1, &lineVBO_);
    if (starVAO_)   glDeleteVertexArrays(1, &starVAO_);
    if (starVBO_)   glDeleteBuffers(1, &starVBO_);
    if (asteroidVAO_) glDeleteVertexArrays(1, &asteroidVAO_);
    if (asteroidVBO_) glDeleteBuffers(1, &asteroidVBO_);
    if (flameVAO_) glDeleteVertexArrays(1, &flameVAO_);
    if (flameVBO_) glDeleteBuffers(1, &flameVBO_);
    if (litProg_)   glDeleteProgram(litProg_);
    if (sunProg_)   glDeleteProgram(sunProg_);
    if (ringProg_)  glDeleteProgram(ringProg_);
    if (lineProg_)  glDeleteProgram(lineProg_);
    if (starProg_)  glDeleteProgram(starProg_);
    if (flameProg_) glDeleteProgram(flameProg_);
    if (atmProg_)   glDeleteProgram(atmProg_);

    if (dummyTex_) glDeleteTextures(1, &dummyTex_);

    freeOBJMeshes(objMeshes_);
    freeGpuMesh(moonMesh_);

    for (auto& kv : materialTextures_)
        if (kv.second) glDeleteTextures(1, &kv.second);
    materialTextures_.clear();

    if (colorTex_)       glDeleteTextures(1,      &colorTex_);
    if (depthRbo_)       glDeleteRenderbuffers(1, &depthRbo_);
    if (fbo_)            glDeleteFramebuffers(1,  &fbo_);
    if (moonPhaseTex_)   glDeleteTextures(1,      &moonPhaseTex_);
    if (moonPhaseDepth_) glDeleteRenderbuffers(1, &moonPhaseDepth_);
    if (moonPhaseFBO_)   glDeleteFramebuffers(1,  &moonPhaseFBO_);
    if (eclipseGlobeTex_)   glDeleteTextures(1,      &eclipseGlobeTex_);
    if (eclipseGlobeDepth_) glDeleteRenderbuffers(1, &eclipseGlobeDepth_);
    if (eclipseGlobeFBO_)   glDeleteFramebuffers(1,  &eclipseGlobeFBO_);
    if (boundaryVAO_)    glDeleteVertexArrays(1, &boundaryVAO_);
    if (boundaryVBO_)    glDeleteBuffers(1,      &boundaryVBO_);
}

} // namespace sx
