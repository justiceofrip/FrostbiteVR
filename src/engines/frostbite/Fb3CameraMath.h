#pragma once
// OpenXR <-> Frostbite camera conventions.
// OpenXR: right-handed, +X right, +Y up, -Z forward.
// Frostbite: left-handed LinearTransform rows {right, up, forward, trans}, +Z forward
// (BC2 research: ShotConfigData InitialDirection (0,0,1) is "forward"; confirm for BF4
// in RE task R3 before enabling the camera feature).
#include "fvr/math/Vec.h"

#include <array>

namespace fvr::frostbite {

struct LinearTransform {
    math::Vec3 right{1, 0, 0};
    math::Vec3 up{0, 1, 0};
    math::Vec3 forward{0, 0, 1};
    math::Vec3 trans{0, 0, 0};
};

// Row-major 4x4 for row-vector math (D3D convention): p' = p * M.
using Mat4 = std::array<float, 16>;

// Mirror Z between handedness conventions.
inline math::Vec3 XrToFb(math::Vec3 v) { return {v.x, v.y, -v.z}; }

LinearTransform XrPoseToFrostbite(const math::Pose& pose, float worldScale = 1.0f);

// Compose a tracking-space transform onto a world anchor (e.g. the soldier's eye):
// world = local applied in parent's frame.
LinearTransform Compose(const LinearTransform& parent, const LinearTransform& local);

// Inverse of an orthonormal transform: the view matrix for a camera transform.
Mat4 ViewMatrixFromCamera(const LinearTransform& camera);

// OpenXR XrFovf angles are radians; left/down are negative.
struct FovAngles {
    float left = -0.8f, right = 0.8f, up = 0.8f, down = -0.8f;
};

// Left-handed off-axis perspective, depth 0..1 (or 1..0 when reversedZ).
Mat4 ProjectionOffAxisLh(const FovAngles& fov, float nearZ, float farZ, bool reversedZ);

// Transform a point by a row-major matrix (w divide applied).
math::Vec3 TransformPoint(const Mat4& m, math::Vec3 p, float* outW = nullptr);

} // namespace fvr::frostbite
