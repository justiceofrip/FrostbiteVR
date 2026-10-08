#pragma once
#include "fvr/runtime/FrameCoordinator.h"
#include <optional>
namespace fvr::engine {
// Observed BC2 native camera uses RH row vectors and four padded Vec3 rows.
// Padding is not homogeneous W; it must not be submitted as a Matrix4 unchanged.
struct FrostbiteCameraInput {math::Matrix4 transform{};float nearPlane=0,farPlane=0,worldUnitsPerMeter=0;};
struct FrostbiteEye {math::Matrix4 transform{},view{},projection{};};
std::optional<runtime::WorldFrame> CanonicalCamera(const FrostbiteCameraInput&) noexcept;
std::optional<FrostbiteEye> NativeEye(const runtime::EyeView&) noexcept;
}