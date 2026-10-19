#pragma once
// Math helpers for the BF4 adapter's platform-neutral code, over the framework's own value
// types (fvr/math/StereoMath.h: Vec3, Quaternion, Pose), so BF4 code and the framework's
// IPC/XR headers share one definition in a translation unit. Still Windows-free.
#include "fvr/math/StereoMath.h"

#include <cmath>

namespace fvr::math {

using Quat = Quaternion;

inline Vec3 operator+(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline Vec3 operator-(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline Vec3 operator*(Vec3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
inline float Dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec3 Cross(Vec3 a, Vec3 b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline float Length(Vec3 a) { return std::sqrt(Dot(a, a)); }
inline Vec3 Normalize(Vec3 a) {
    const float len = Length(a);
    return len > 1e-8f ? a * (1.0f / len) : Vec3{};
}


inline Vec3 Rotate(Quat q, Vec3 v) {
    // v' = v + 2w(u x v) + 2(u x (u x v)), u = (x, y, z)
    const Vec3 u{q.x, q.y, q.z};
    const Vec3 t = Cross(u, v) * 2.0f;
    return v + t * q.w + Cross(u, t);
}

inline Quat Conjugate(Quat q) { return {-q.x, -q.y, -q.z, q.w}; }

// Hamilton product: (a * b) rotates by b first, then a.
inline Quat operator*(Quat a, Quat b) {
    return {a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
            a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
            a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
            a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z};
}

inline Quat FromAxisAngle(Vec3 axis, float radians) {
    const Vec3 n = Normalize(axis);
    const float s = std::sin(radians * 0.5f);
    return {n.x * s, n.y * s, n.z * s, std::cos(radians * 0.5f)};
}

// Pose (StereoMath.h) is {position, orientation}: OpenXR convention, right-handed,
// +X right, +Y up, -Z forward, metres. Use member names, not brace order.

} // namespace fvr::math
