#pragma once
#include "fvr/math/StereoMath.h"
#include <cmath>
namespace fvr::bc2::sight_fixture {
// STATIC identity-orientation receiver calibration only; no runtime policy.
// Exact primary generation494: trace20261007-152221-254.
// Exact secondary generation311: trace20261007-153559-462 (settled40mmgl).
// Derivation receipts pin captured matrices, bind-derived palm and source hashes.
inline constexpr math::Vec3 PrimaryWristOffset{-.0219681859f,-.071896688f,-.210772157f};
inline constexpr math::Vec3 SecondaryWristOffset{-.0321350232f,-.0743480187f,-.2127902585f};
inline constexpr math::Vec3 PalmOffset{-.0545238749f,-.0964722299f,-.0276299967f};
inline constexpr math::Vec3 Pivot{.032074f,.0537033f,-.585449f};
inline constexpr math::Vec3 LauncherSupportWrist{.0525216879f,-.1251581868f,-.52158355f};
inline math::Vec3 InputForLocal(math::Vec3 goal,math::Vec3 offset,float rightY=-.4f)noexcept {
 return {-(goal.x-offset.x),rightY+goal.y-offset.y,goal.z-offset.z};
}
inline math::Vec3 SightGrip(bool secondary,float angle)noexcept {
 const auto w=secondary?SecondaryWristOffset:PrimaryWristOffset;
 const math::Vec3 goal{Pivot.x,Pivot.y+.09f*std::sin(angle),Pivot.z-.09f*std::cos(angle)};
 return InputForLocal(goal,{w.x+PalmOffset.x,w.y+PalmOffset.y,w.z+PalmOffset.z});
}
inline math::Vec3 LauncherSupportGrip()noexcept {
 auto input=InputForLocal(LauncherSupportWrist,SecondaryWristOffset);
 input.x+=.01f; // Preserve original intentional1cm approach before support seats.
 return input;
}
} // namespace fvr::bc2::sight_fixture
