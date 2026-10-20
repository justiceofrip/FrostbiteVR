#include "Bf4Mat4.h"

#include <cmath>

namespace fvr::bf4 {

Mat4 Mul(const Mat4& a, const Mat4& b) {
    Mat4 r{};
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j) {
            float s = 0;
            for (int k = 0; k < 4; ++k) s += a[i * 4 + k] * b[k * 4 + j];
            r[i * 4 + j] = s;
        }
    return r;
}

Mat4 Identity4() { return {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}; }

Mat4 FromPose(const math::Pose& pose) {
    const math::Vec3 x = math::Rotate(pose.orientation, {1, 0, 0}), y = math::Rotate(pose.orientation, {0, 1, 0}),
                     z = math::Rotate(pose.orientation, {0, 0, 1});
    return {x.x, y.x, z.x, pose.position.x, x.y, y.y, z.y, pose.position.y, x.z, y.z, z.z, pose.position.z, 0, 0, 0, 1};
}

Mat4 InverseRigid(const Mat4& m) {
    Mat4 r = Identity4();
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) r[i * 4 + j] = m[j * 4 + i];
    for (int i = 0; i < 3; ++i) r[i * 4 + 3] = -(r[i * 4 + 0] * m[3] + r[i * 4 + 1] * m[7] + r[i * 4 + 2] * m[11]);
    return r;
}

Mat4 Translation(math::Vec3 t) {
    Mat4 m = Identity4();
    m[3] = t.x;
    m[7] = t.y;
    m[11] = t.z;
    return m;
}

math::Vec3 TransformPoint(const Mat4& m, math::Vec3 p) {
    return {m[0] * p.x + m[1] * p.y + m[2] * p.z + m[3], m[4] * p.x + m[5] * p.y + m[6] * p.z + m[7],
            m[8] * p.x + m[9] * p.y + m[10] * p.z + m[11]};
}

math::Vec3 TransformVector(const Mat4& m, math::Vec3 v) {
    return {m[0] * v.x + m[1] * v.y + m[2] * v.z, m[4] * v.x + m[5] * v.y + m[6] * v.z,
            m[8] * v.x + m[9] * v.y + m[10] * v.z};
}

Mat4 RotationBetween(math::Vec3 from, math::Vec3 to) {
    const math::Vec3 a = math::Normalize(from), b = math::Normalize(to);
    const float c = math::Dot(a, b);
    math::Vec3 axis = math::Cross(a, b);
    float s = math::Length(axis);
    if (s < 1e-6f) {
        if (c > 0) return Identity4();
        // Opposite: half a turn about any axis perpendicular to a.
        axis = math::Cross(a, std::fabs(a.x) < 0.9f ? math::Vec3{1, 0, 0} : math::Vec3{0, 1, 0});
        axis = math::Normalize(axis);
        s = 0;
    } else {
        axis = axis * (1.0f / s);
    }
    // Rodrigues: R = I c + (1 - c) k k^T + s [k]x
    const float t = 1.0f - c, x = axis.x, y = axis.y, z = axis.z;
    return {c + t * x * x,     t * x * y - s * z, t * x * z + s * y, 0,
            t * x * y + s * z, c + t * y * y,     t * y * z - s * x, 0,
            t * x * z - s * y, t * y * z + s * x, c + t * z * z,     0,
            0,                 0,                 0,                 1};
}

bool AllFinite(const Mat4& m) {
    for (float x : m)
        if (!std::isfinite(x)) return false;
    return true;
}

} // namespace fvr::bf4
