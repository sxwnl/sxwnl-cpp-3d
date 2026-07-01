#include "objloader.h"

#include <glad/glad.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace sx {

struct Vertex {
    float px, py, pz;
    float nx, ny, nz;
    float u, v;
};

struct Vec2 { float x = 0.0f, y = 0.0f; };
struct Vec3 { float x = 0.0f, y = 0.0f, z = 0.0f; };

static Vec3 operator-(const Vec3& a, const Vec3& b) {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

static Vec3 cross(const Vec3& a, const Vec3& b) {
    return {
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x
    };
}

static float length(const Vec3& a) {
    return std::sqrt(a.x * a.x + a.y * a.y + a.z * a.z);
}

static Vec3 normalize(const Vec3& a) {
    float l = length(a);
    return (l > 1e-12f) ? Vec3{a.x / l, a.y / l, a.z / l} : Vec3{0.0f, 1.0f, 0.0f};
}

static std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return {};
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

static int fixIndex(int idx, size_t n) {
    if (idx > 0) return idx - 1;
    if (idx < 0) return (int)n + idx;
    return -1;
}

static bool parseCorner(const std::string& tok,
                        const std::vector<Vec3>& pos,
                        const std::vector<Vec2>& uv,
                        const std::vector<Vec3>& nrm,
                        Vertex& out,
                        bool& hasNormal) {
    int vi = 0, ti = 0, ni = 0;
    const char* s = tok.c_str();
    char* end = nullptr;

    vi = (int)std::strtol(s, &end, 10);
    if (end == s) return false;
    s = end;

    if (*s == '/') {
        ++s;
        if (*s != '/') {
            ti = (int)std::strtol(s, &end, 10);
            s = end;
        }
        if (*s == '/') {
            ++s;
            ni = (int)std::strtol(s, &end, 10);
        }
    }

    int pi = fixIndex(vi, pos.size());
    if (pi < 0 || pi >= (int)pos.size()) return false;
    out.px = pos[pi].x;
    out.py = pos[pi].y;
    out.pz = pos[pi].z;

    int tiFixed = fixIndex(ti, uv.size());
    if (ti != 0 && tiFixed >= 0 && tiFixed < (int)uv.size()) {
        out.u = uv[tiFixed].x;
        out.v = uv[tiFixed].y;
    } else {
        out.u = 0.0f;
        out.v = 0.0f;
    }

    int niFixed = fixIndex(ni, nrm.size());
    if (ni != 0 && niFixed >= 0 && niFixed < (int)nrm.size()) {
        out.nx = nrm[niFixed].x;
        out.ny = nrm[niFixed].y;
        out.nz = nrm[niFixed].z;
        hasNormal = true;
    } else {
        out.nx = out.ny = out.nz = 0.0f;
        hasNormal = false;
    }
    return true;
}

static void setNormal(Vertex& v, const Vec3& n) {
    v.nx = n.x;
    v.ny = n.y;
    v.nz = n.z;
}

struct PendingMesh {
    std::string objectName = "default";
    std::string materialName;
    std::vector<Vertex> vertices;
};

static std::string meshKey(const PendingMesh& p) {
    if (!p.materialName.empty()) return p.materialName;
    if (!p.objectName.empty()) return p.objectName;
    return "__default__";
}

static GpuMesh uploadMesh(const PendingMesh& src) {
    GpuMesh out;
    if (src.vertices.empty()) return out;

    std::vector<float> verts;
    verts.reserve(src.vertices.size() * 8);

    Vec3 mn{
        std::numeric_limits<float>::max(),
        std::numeric_limits<float>::max(),
        std::numeric_limits<float>::max()
    };
    Vec3 mx{
        -std::numeric_limits<float>::max(),
        -std::numeric_limits<float>::max(),
        -std::numeric_limits<float>::max()
    };
    for (const Vertex& v : src.vertices) {
        mn.x = std::min(mn.x, v.px); mn.y = std::min(mn.y, v.py); mn.z = std::min(mn.z, v.pz);
        mx.x = std::max(mx.x, v.px); mx.y = std::max(mx.y, v.py); mx.z = std::max(mx.z, v.pz);
    }

    Vec3 center{
        (mn.x + mx.x) * 0.5f,
        (mn.y + mx.y) * 0.5f,
        (mn.z + mx.z) * 0.5f
    };
    float radius = 1e-6f;
    for (const Vertex& v : src.vertices) {
        radius = std::max(radius, length(Vec3{v.px - center.x, v.py - center.y, v.pz - center.z}));
    }

    for (size_t i = 0; i < src.vertices.size(); ++i) {
        const Vertex& v = src.vertices[i];
        verts.push_back((v.px - center.x) / radius);
        verts.push_back((v.py - center.y) / radius);
        verts.push_back((v.pz - center.z) / radius);
        verts.push_back(v.nx);
        verts.push_back(v.ny);
        verts.push_back(v.nz);
        verts.push_back(v.u);
        verts.push_back(v.v);
    }

    glGenVertexArrays(1, &out.vao);
    glGenBuffers(1, &out.vbo);
    while (glGetError() != GL_NO_ERROR) {}
    glBindVertexArray(out.vao);

    glBindBuffer(GL_ARRAY_BUFFER, out.vbo);
    glBufferData(GL_ARRAY_BUFFER,
                 (GLsizeiptr)(verts.size() * sizeof(float)),
                 verts.data(), GL_DYNAMIC_DRAW);

    const int stride = 8 * (int)sizeof(float);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride, (void*)(6 * sizeof(float)));
    glEnableVertexAttribArray(2);
    glBindVertexArray(0);

    out.indexCount = (int)src.vertices.size();
    out.valid = true;
    out.objectName = src.objectName;
    out.materialName = src.materialName;
    out.sourceCenter[0] = center.x;
    out.sourceCenter[1] = center.y;
    out.sourceCenter[2] = center.z;
    out.sourceRadius = radius;
    GLenum err = glGetError();
    if (err != GL_NO_ERROR) {
        std::fprintf(stderr,
                     "[obj] upload '%s': vertices=%d bytes=%d vao=%u vbo=%u err=0x%x\n",
                     src.materialName.c_str(),
                     out.indexCount,
                     (int)(verts.size() * sizeof(float)),
                     out.vao,
                     out.vbo,
                     err);
    }
    return out;
}

std::map<std::string, GpuMesh> loadOBJ(const char* path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        std::fprintf(stderr, "[obj] cannot open: %s\n", path);
        return {};
    }

    std::vector<Vec3> positions;
    std::vector<Vec2> texcoords;
    std::vector<Vec3> normals;
    std::vector<PendingMesh> meshes;
    PendingMesh cur;

    auto flush = [&]() {
        if (!cur.vertices.empty()) {
            meshes.push_back(std::move(cur));
            cur = PendingMesh{};
        }
    };

    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::string s = trim(line);
        if (s.empty() || s[0] == '#') continue;

        std::istringstream ss(s);
        std::string key;
        ss >> key;

        if (key == "v") {
            Vec3 p;
            ss >> p.x >> p.y >> p.z;
            positions.push_back(p);
        } else if (key == "vt") {
            Vec2 t;
            ss >> t.x >> t.y;
            texcoords.push_back(t);
        } else if (key == "vn") {
            Vec3 n;
            ss >> n.x >> n.y >> n.z;
            normals.push_back(n);
        } else if (key == "o" || key == "g") {
            std::string name = trim(s.substr(1));
            flush();
            cur.objectName = name.empty() ? key : name;
        } else if (key == "usemtl") {
            std::string material;
            ss >> material;
            if (!cur.vertices.empty() && cur.materialName != material) {
                std::string keepName = cur.objectName;
                flush();
                cur.objectName = keepName.empty() ? "default" : keepName;
            }
            cur.materialName = material;
        } else if (key == "f") {
            std::vector<std::string> toks;
            std::string tok;
            while (ss >> tok) toks.push_back(tok);
            if (toks.size() < 3) continue;

            for (size_t k = 2; k < toks.size(); ++k) {
                Vertex a{}, b{}, c{};
                bool na = false, nb = false, nc = false;
                if (!parseCorner(toks[0], positions, texcoords, normals, a, na)) continue;
                if (!parseCorner(toks[k - 1], positions, texcoords, normals, b, nb)) continue;
                if (!parseCorner(toks[k], positions, texcoords, normals, c, nc)) continue;

                if (!na || !nb || !nc) {
                    Vec3 pa{a.px, a.py, a.pz};
                    Vec3 pb{b.px, b.py, b.pz};
                    Vec3 pc{c.px, c.py, c.pz};
                    Vec3 fn = normalize(cross(pb - pa, pc - pa));
                    if (!na) setNormal(a, fn);
                    if (!nb) setNormal(b, fn);
                    if (!nc) setNormal(c, fn);
                }

                cur.vertices.push_back(a);
                cur.vertices.push_back(b);
                cur.vertices.push_back(c);
            }
        }
    }
    flush();

    std::map<std::string, GpuMesh> result;
    for (PendingMesh& p : meshes) {
        std::string key = meshKey(p);
        GpuMesh mesh = uploadMesh(p);
        if (!mesh.valid) continue;

        if (result.find(key) != result.end()) {
            int suffix = 2;
            std::string base = key;
            do {
                key = base + "#" + std::to_string(suffix++);
            } while (result.find(key) != result.end());
        }

        std::fprintf(stderr,
                     "[obj]   '%s' object='%s' material='%s': %d tris\n",
                     key.c_str(),
                     mesh.objectName.c_str(),
                     mesh.materialName.c_str(),
                     mesh.indexCount / 3);
        result[key] = std::move(mesh);
    }

    std::fprintf(stderr, "[obj] %s: v=%zu vt=%zu vn=%zu meshes=%zu\n",
                 path,
                 positions.size(),
                 texcoords.size(),
                 normals.size(),
                 result.size());
    return result;
}

void freeGpuMesh(GpuMesh& m) {
    if (m.vao) { glDeleteVertexArrays(1, &m.vao); m.vao = 0; }
    if (m.vbo) { glDeleteBuffers(1, &m.vbo); m.vbo = 0; }
    if (m.ebo) { glDeleteBuffers(1, &m.ebo); m.ebo = 0; }
    m.indexCount = 0;
    m.valid = false;
}

void freeOBJMeshes(std::map<std::string, GpuMesh>& meshes) {
    for (auto& kv : meshes) freeGpuMesh(kv.second);
    meshes.clear();
}

} // namespace sx
