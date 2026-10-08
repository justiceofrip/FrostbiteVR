#pragma once
#include "fvr/math/StereoMath.h"
#include <algorithm>
#include <cmath>
namespace fvr::interaction {
// Engine-independent controller direction in the upright tracking reference.
// Positive yaw turns right; positive pitch points up. No current HMD pose enters.
struct AimAngles {float yaw=0,pitch=0;};
inline std::optional<AimAngles> ControllerAim(const math::Pose& reference,const math::Pose& aim,float previousYaw=0) noexcept {
    const auto p=math::MakeRelativePose(reference,aim);if(!p||!std::isfinite(previousYaw))return {};
    const auto& q=p->orientation;
    const float x=-2*(q.x*q.z+q.w*q.y),y=2*(q.w*q.x-q.y*q.z),z=1-2*(q.x*q.x+q.y*q.y);
    return AimAngles{std::hypot(x,z)>.01f?std::atan2(x,z):previousYaw,std::asin(std::clamp(y,-1.f,1.f))};
}
// Translate head-relative locomotion into a different native input heading.
inline void RotateMovement(float radians,float& forward,float& strafe) noexcept {
    const float f=forward,s=strafe,c=std::cos(radians),sn=std::sin(radians);
    forward=f*c-s*sn;strafe=f*sn+s*c;
}
}
