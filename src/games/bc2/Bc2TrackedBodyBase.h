#pragma once
#include "fvr/interaction/ComfortCamera.h"
#include "fvr/interaction/RigPose.h"
#include <cmath>
namespace fvr::bc2 {
// Shared renderer/player frame: native stance height, current collision origin,
// current body yaw and actual consumed roomscale displacement. Metres are
// converted only by the caller after this native-units matrix is constructed.
inline std::optional<math::Matrix4> BuildTrackedBodyBase(const math::Matrix4& nativeEye,
    float yaw,float units,math::Vec3 actor,math::Vec3 consumed)noexcept {
    const auto finite=[](math::Vec3 p){return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z);};
    if(!std::isfinite(yaw)||!std::isfinite(units)||units<=0||units>1000||!finite(actor)||!finite(consumed)||
       !interaction::InverseAnimatedTransform(nativeEye))return {};
    auto base=interaction::MakeComfortCamera(nativeEye,(yaw-3.141592653589793f)*57.29577951308232f);
    if(!base)return {};
    base->values[3][0]=actor.x;base->values[3][2]=actor.z;
    for(unsigned axis=0;axis<3;++axis)base->values[3][axis]-=units*(consumed.x*base->values[0][axis]-consumed.z*base->values[2][axis]);
    return base;
}
}
