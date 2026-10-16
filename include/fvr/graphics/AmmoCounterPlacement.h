#pragma once
#include "fvr/math/StereoMath.h"
#include <cmath>
#include <optional>
namespace fvr::graphics {
// Metres in the right controller's aim frame. Position follows the held gun;
// text faces the viewer and does not roll into the receiver during a reload.
// This is presentation only, independent of game units or weapon calibration.
inline constexpr math::Vec3 AmmoCounterGunOffset{-.15f,.035f,-.12f};
inline constexpr float AmmoCounterWidthMeters=.20f;
inline constexpr float AmmoCounterHeightMeters=.0375f;
inline std::optional<math::Pose> AmmoCounterBesideGun(const math::Pose& head,
    const math::Pose& grip,const math::Pose& aim)noexcept {
    const auto normalizedHead=math::MakeRelativePose(math::Pose{},head);
    const auto normalizedGrip=math::MakeRelativePose(math::Pose{},grip);
    const auto normalizedAim=math::MakeRelativePose(math::Pose{},aim);
    if(!normalizedHead||!normalizedGrip||!normalizedAim)return {};
    const auto& q=normalizedAim->orientation;const auto& p=AmmoCounterGunOffset;
    // Unit quaternion rotation of the local offset; OpenXR remains RH/-Z forward.
    const math::Vec3 t{2*(q.y*p.z-q.z*p.y),2*(q.z*p.x-q.x*p.z),2*(q.x*p.y-q.y*p.x)};
    const math::Vec3 offset{p.x+q.w*t.x+q.y*t.z-q.z*t.y,
        p.y+q.w*t.y+q.z*t.x-q.x*t.z,p.z+q.w*t.z+q.x*t.y-q.y*t.x};
    math::Pose result=*normalizedHead;
    result.position={grip.position.x+offset.x,grip.position.y+offset.y,grip.position.z+offset.z};
    const auto relative=math::MakeRelativePose(head,result);if(!relative)return {};
    const auto& v=relative->position;const float distance=std::hypot(v.x,v.y,v.z);
    // Never pull a near/behind-head label onto the user's face to keep it visible.
    if(!std::isfinite(distance)||distance<.20f||distance>1.8f||v.z>=-.08f)return {};
    return result;
}
}
