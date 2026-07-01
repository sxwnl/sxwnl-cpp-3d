// Minimal column-major 3D math (vec3 / mat4) for the solar-system GUI.
// Header-only; avoids pulling in glm as an extra dependency.
#ifndef SXWNL_GUI_MATHX_H
#define SXWNL_GUI_MATHX_H

#include <cmath>

namespace gx {

struct Vec3 {
    float x = 0, y = 0, z = 0;
    Vec3() {}
    Vec3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}
    Vec3 operator+(const Vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    Vec3 operator-(const Vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    Vec3 operator*(float s) const { return {x * s, y * s, z * s}; }
};

inline float dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec3 cross(const Vec3& a, const Vec3& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline float length(const Vec3& a) { return std::sqrt(dot(a, a)); }
inline Vec3 normalize(const Vec3& a) {
    float l = length(a);
    return (l > 1e-8f) ? Vec3{a.x / l, a.y / l, a.z / l} : Vec3{0, 0, 0};
}

// Column-major 4x4 matrix, stored so m[col*4 + row] (OpenGL convention).
struct Mat4 {
    float m[16];
    Mat4() { for (int i = 0; i < 16; ++i) m[i] = 0.0f; }
    static Mat4 identity() {
        Mat4 r;
        r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1.0f;
        return r;
    }
    const float* data() const { return m; }
};

inline Mat4 operator*(const Mat4& a, const Mat4& b) {
    Mat4 r;
    for (int c = 0; c < 4; ++c)
        for (int row = 0; row < 4; ++row) {
            float s = 0;
            for (int k = 0; k < 4; ++k) s += a.m[k * 4 + row] * b.m[c * 4 + k];
            r.m[c * 4 + row] = s;
        }
    return r;
}

inline Mat4 translate(const Vec3& t) {
    Mat4 r = Mat4::identity();
    r.m[12] = t.x; r.m[13] = t.y; r.m[14] = t.z;
    return r;
}

inline Mat4 scale(float s) {
    Mat4 r = Mat4::identity();
    r.m[0] = r.m[5] = r.m[10] = s;
    return r;
}

// Rotation about the world X axis (radians). Tilts +Y toward +Z.
// Used to set a body's polar-axis obliquity relative to the ecliptic.
inline Mat4 rotateX(float a) {
    Mat4 r = Mat4::identity();
    float c = std::cos(a), s = std::sin(a);
    r.m[5] = c;  r.m[9]  = -s;
    r.m[6] = s;  r.m[10] =  c;
    return r;
}

// Rotation about the (local) Y axis (radians). A positive angle spins
// +X toward -Z; with Y as the north pole this is the prograde / eastward
// rotation direction. Used for planetary self-rotation (day/night spin).
inline Mat4 rotateY(float a) {
    Mat4 r = Mat4::identity();
    float c = std::cos(a), s = std::sin(a);
    r.m[0] = c;  r.m[8]  = s;
    r.m[2] = -s; r.m[10] = c;
    return r;
}

inline Mat4 rotateZ(float a) {
    Mat4 r = Mat4::identity();
    float c = std::cos(a), s = std::sin(a);
    r.m[0] = c;  r.m[4] = -s;
    r.m[1] = s;  r.m[5] =  c;
    return r;
}

inline Mat4 perspective(float fovyRad, float aspect, float zn, float zf) {
    Mat4 r;
    float f = 1.0f / std::tan(fovyRad * 0.5f);
    r.m[0] = f / aspect;
    r.m[5] = f;
    r.m[10] = (zf + zn) / (zn - zf);
    r.m[11] = -1.0f;
    r.m[14] = (2.0f * zf * zn) / (zn - zf);
    return r;
}

inline Mat4 lookAt(const Vec3& eye, const Vec3& center, const Vec3& up) {
    Vec3 f = normalize(center - eye);
    Vec3 s = normalize(cross(f, up));
    Vec3 u = cross(s, f);
    Mat4 r = Mat4::identity();
    r.m[0] = s.x; r.m[4] = s.y; r.m[8] = s.z;
    r.m[1] = u.x; r.m[5] = u.y; r.m[9] = u.z;
    r.m[2] = -f.x; r.m[6] = -f.y; r.m[10] = -f.z;
    r.m[12] = -dot(s, eye);
    r.m[13] = -dot(u, eye);
    r.m[14] = dot(f, eye);
    return r;
}

// Project a world point to viewport pixel coords. Returns false if behind camera.
// Output: sx,sy in pixels (origin top-left), relative to a viewport of size (w,h).
inline bool projectToScreen(const Mat4& viewProj, const Vec3& p,
                            float w, float h, float& sx, float& sy) {
    float cx = viewProj.m[0] * p.x + viewProj.m[4] * p.y + viewProj.m[8] * p.z + viewProj.m[12];
    float cy = viewProj.m[1] * p.x + viewProj.m[5] * p.y + viewProj.m[9] * p.z + viewProj.m[13];
    float cw = viewProj.m[3] * p.x + viewProj.m[7] * p.y + viewProj.m[11] * p.z + viewProj.m[15];
    if (cw <= 1e-5f) return false;
    float ndcx = cx / cw, ndcy = cy / cw;
    sx = (ndcx * 0.5f + 0.5f) * w;
    sy = (1.0f - (ndcy * 0.5f + 0.5f)) * h;
    return true;
}

} // namespace gx

#endif
