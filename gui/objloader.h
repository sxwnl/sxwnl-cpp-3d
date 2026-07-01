// OBJ mesh loader -> OpenGL VAO/VBO/EBO.
// Requires an active OpenGL 3.3 core context when called.
//
// The loader intentionally mirrors the verified tmp OBJ path:
// - parse the file directly instead of depending on an external MTL,
// - split meshes on o / g / usemtl boundaries,
// - keep each split independently drawable for per-planet control.
#ifndef SXWNL_GUI_OBJLOADER_H
#define SXWNL_GUI_OBJLOADER_H

#include <map>
#include <string>

namespace sx {

// GPU mesh uploaded from one split OBJ object/material group.
// Vertex layout: pos(3) + nrm(3) + uv(2) = 8 floats, stride 32 bytes.
struct GpuMesh {
    unsigned int vao = 0;
    unsigned int vbo = 0;
    unsigned int ebo = 0;       // OBJ meshes are non-indexed; fallback meshes may use EBO.
    int          indexCount = 0; // OBJ: vertex count for glDrawArrays.
    bool         valid = false;

    std::string objectName;
    std::string materialName;
    float       sourceCenter[3] = {0.0f, 0.0f, 0.0f};
    float       sourceRadius = 1.0f;
};

// Parse an OBJ file. Returns a map keyed by material name when present,
// otherwise by object/group name. Quads and n-gons are fan-triangulated.
std::map<std::string, GpuMesh> loadOBJ(const char* path);

// Free all GPU resources in the map and clear it.
void freeOBJMeshes(std::map<std::string, GpuMesh>& meshes);

// Free a single GpuMesh.
void freeGpuMesh(GpuMesh& m);

} // namespace sx
#endif
