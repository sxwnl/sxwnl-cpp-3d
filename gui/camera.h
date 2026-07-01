// Orbit camera around the Sun: azimuth/elevation rotation, zoom, pan.
#ifndef SXWNL_GUI_CAMERA_H
#define SXWNL_GUI_CAMERA_H

#include <algorithm>

#include "mathx.h"

namespace gx {

struct OrbitCamera {
    Vec3 target{0, 0, 0};   // look-at point (world units)
    float distance = 60.0f; // distance from target
    float yaw = 0.6f;       // azimuth (radians)
    float pitch = 0.9f;     // elevation (radians)
    float fovy = 45.0f * 3.14159265f / 180.0f;
    float znear = 0.05f;
    float zfar = 5000.0f;

    Vec3 focusTarget{0, 0, 0};
    float focusDistance = 60.0f;
    bool focusing = false;

    Vec3 eye() const {
        float cp = std::cos(pitch), sp = std::sin(pitch);
        float cy = std::cos(yaw), sy = std::sin(yaw);
        return target + Vec3{distance * cp * cy, distance * sp, distance * cp * sy};
    }

    Mat4 view() const { return lookAt(eye(), target, Vec3{0, 1, 0}); }
    Mat4 proj(float aspect) const { return perspective(fovy, aspect, znear, zfar); }

    void rotate(float dyaw, float dpitch) {
        focusing = false;
        yaw += dyaw;
        pitch += dpitch;
        const float lim = 1.55334f; // ~89 degrees
        if (pitch > lim) pitch = lim;
        if (pitch < -lim) pitch = -lim;
    }
    void zoom(float factor) {
        focusing = false;
        distance *= factor;
        if (distance < 0.02f) distance = 0.02f;
        if (distance > 4000.0f) distance = 4000.0f;
    }
    // Pan in the camera's screen plane, scaled by distance for natural feel.
    void pan(float dx, float dy) {
        focusing = false;
        Vec3 fwd = normalize(target - eye());
        Vec3 right = normalize(cross(fwd, Vec3{0, 1, 0}));
        Vec3 up = cross(right, fwd);
        float k = distance * 0.0015f;
        target = target + right * (-dx * k) + up * (dy * k);
    }

    void focusOn(const Vec3& p, float radius, bool immediate = false) {
        focusTarget = p;
        focusDistance = std::clamp(radius * 2.6f, 0.08f, 52.0f);
        focusing = !immediate;
        if (immediate) {
            target = focusTarget;
            distance = focusDistance;
        }
    }

    void updateFocus(float dtSec) {
        if (!focusing) return;
        float a = 1.0f - std::exp(-std::max(0.0f, dtSec) * 8.5f);
        target = target + (focusTarget - target) * a;
        distance += (focusDistance - distance) * a;
        if (length(focusTarget - target) < 0.002f &&
            std::fabs(focusDistance - distance) < 0.002f) {
            target = focusTarget;
            distance = focusDistance;
            focusing = false;
        }
    }
};

} // namespace gx

#endif
