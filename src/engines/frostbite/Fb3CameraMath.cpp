#include "Fb3CameraMath.h"

#include <cmath>

namespace fvr::frostbite {

using math::Vec3;

LinearTransform XrPoseToFrostbite(const math::Pose& pose, float worldScale) {
    LinearTransform t;
    // Rotate the XR basis, then mirror Z. XR forward is -Z, which mirrors to FB +Z.
    t.right = XrToFb(math::Rotate(pose.orientation, {1, 0, 0}));
    t.up = XrToFb(math::Rotate(pose.orientation, {0, 1, 0}));
    t.forward = XrToFb(math::Rotate(pose.orientation, {0, 0, -1}));
    t.trans = XrToFb(pose.position) * worldScale;
    return t;
}

namespace {
Vec3 ApplyBasis(const LinearTransform& m, Vec3 v) {
    return m.right * v.x + m.up * v.y + m.forward * v.z;
}
} // namespace

LinearTransform Compose(const LinearTransform& parent, const LinearTransform& local) {
    LinearTransform out;
    out.right = ApplyBasis(parent, local.right);
    out.up = ApplyBasis(parent, local.up);
    out.forward = ApplyBasis(parent, local.forward);
    out.trans = ApplyBasis(parent, local.trans) + parent.trans;
    return out;
}

Mat4 ViewMatrixFromCamera(const LinearTransform& c) {
    // Rows of the camera basis become columns of the view rotation.
    return {
        c.right.x, c.up.x, c.forward.x, 0.0f,
        c.right.y, c.up.y, c.forward.y, 0.0f,
        c.right.z, c.up.z, c.forward.z, 0.0f,
        -math::Dot(c.trans, c.right), -math::Dot(c.trans, c.up), -math::Dot(c.trans, c.forward), 1.0f,
    };
}

Mat4 ProjectionOffAxisLh(const FovAngles& fov, float n, float f, bool reversedZ) {
    const float l = n * std::tan(fov.left);
    const float r = n * std::tan(fov.right);
    const float b = n * std::tan(fov.down);
    const float t = n * std::tan(fov.up);
    const float a = reversedZ ? n / (n - f) : f / (f - n);
    const float bz = reversedZ ? -n * f / (n - f) : -n * f / (f - n);
    return {
        2.0f * n / (r - l), 0.0f, 0.0f, 0.0f,
        0.0f, 2.0f * n / (t - b), 0.0f, 0.0f,
        (l + r) / (l - r), (t + b) / (b - t), a, 1.0f,
        0.0f, 0.0f, bz, 0.0f,
    };
}

Vec3 TransformPoint(const Mat4& m, Vec3 p, float* outW) {
    const float x = p.x * m[0] + p.y * m[4] + p.z * m[8] + m[12];
    const float y = p.x * m[1] + p.y * m[5] + p.z * m[9] + m[13];
    const float z = p.x * m[2] + p.y * m[6] + p.z * m[10] + m[14];
    const float w = p.x * m[3] + p.y * m[7] + p.z * m[11] + m[15];
    if (outW) *outW = w;
    const float inv = std::fabs(w) > 1e-12f ? 1.0f / w : 0.0f;
    return {x * inv, y * inv, z * inv};
}

} // namespace fvr::frostbite
