#pragma once
#include "KnifeGeometry.h"
#include "fvr/interaction/RigPose.h"
#include "fvr/interaction/MeleeStroke.h"
#include "fvr/interaction/ControllerInput.h"
namespace fvr::bc2::private_knife {
struct KnifeMotionGeometry {interaction::MeleePoint tip{},direction{};};
// Original OpenXR LOCAL grip only, in metres. No rendered hand, current head,
// chest anchor, native camera, locomotion or game-world transform participates.
inline std::optional<math::Matrix4> KnifeWristCalibration(const interaction::InputFrame& input,
    const math::Matrix4& body,const math::Matrix4& trackedRight)noexcept {
    if(!input.hands[1].gripTracked)return {};
    const auto relative=math::MakeRelativePose(input.referenceHead,input.hands[1].grip);
    if(!relative)return {};const auto view=math::MakeLhViewFromOpenXRPose(*relative);
    auto inverseBody=interaction::InverseRigid(body);auto wrist=trackedRight;
    if(!view||!inverseBody||!interaction::InverseRigid(wrist))return {};
    // TrackedRig retains wrist orientation A and uses absolute controller
    // translation. Recover only A: wrist * inverse(body) * inverse(grip).
    // Remove translations BEFORE composition, so native/camera movement and
    // large world coordinates cannot enter this calibration measurement.
    auto inverseGrip=*view;wrist.values[3]=inverseBody->values[3]=inverseGrip.values[3]={0,0,0,1};
    const auto calibration=interaction::Multiply(interaction::Multiply(wrist,*inverseBody),inverseGrip);
    return interaction::InverseRigid(calibration)?std::optional(calibration):std::nullopt;
}
inline std::optional<KnifeMotionGeometry> KnifeMotionFromGrip(const math::Pose& originalGrip,
    const math::Matrix4& measuredWristInGrip)noexcept {
    const auto view=math::MakeLhViewFromOpenXRPose(originalGrip);
    const auto inverseWrist=interaction::InverseRigid(RightHandInKnife);
    if(!view||!inverseWrist||!interaction::InverseRigid(measuredWristInGrip)||
       measuredWristInGrip.values[3]!=std::array<float,4>{0,0,0,1})return {};
    const auto grip=interaction::InverseRigid(*view);if(!grip)return {};
    const auto knife=interaction::Multiply(interaction::Multiply(*inverseWrist,measuredWristInGrip),*grip);
    const auto transform=[&](math::Vec3 p,float w){interaction::MeleePoint out;
        out.x=p.x*knife.values[0][0]+p.y*knife.values[1][0]+p.z*knife.values[2][0]+w*knife.values[3][0];
        out.y=p.x*knife.values[0][1]+p.y*knife.values[1][1]+p.z*knife.values[2][1]+w*knife.values[3][1];
        out.z=p.x*knife.values[0][2]+p.y*knife.values[1][2]+p.z*knife.values[2][2]+w*knife.values[3][2];return out;};
    auto tip=transform(BladeTipInKnife,1),direction=transform(BladeAxisInKnife,0);
    const float length=std::hypot(direction.x,direction.y,direction.z);
    if(!std::isfinite(tip.x)||!std::isfinite(tip.y)||!std::isfinite(tip.z)||
       !std::isfinite(length)||length<.999f||length>1.001f)return {};
    direction.x/=length;direction.y/=length;direction.z/=length;
    return KnifeMotionGeometry{tip,direction};
}
}
