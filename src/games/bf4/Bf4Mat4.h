#pragma once
// Rigid 4x4 matrices as BF4's constant buffers hold them: row-major, column vectors (p' = M p),
// translation in column 3. Platform-neutral (shared by the first-person view and the arm IK).

#include "fvr/math/Vec.h"

#include <array>

namespace fvr::bf4 {

using Mat4 = std::array<float, 16>;
Mat4 Mul(const Mat4& a, const Mat4& b);
Mat4 Identity4();
Mat4 FromPose(const math::Pose& pose); // OpenXR pose (BF4's view space shares its axes)
Mat4 InverseRigid(const Mat4& m);
Mat4 Translation(math::Vec3 t);
math::Vec3 TransformPoint(const Mat4& m, math::Vec3 p);
math::Vec3 TransformVector(const Mat4& m, math::Vec3 v); // rotation part only
// The smallest rotation taking direction `from` onto direction `to` (any axis when opposite).
Mat4 RotationBetween(math::Vec3 from, math::Vec3 to);
bool AllFinite(const Mat4& m);

} // namespace fvr::bf4
