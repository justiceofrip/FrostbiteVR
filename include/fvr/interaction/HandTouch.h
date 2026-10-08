#pragma once
#include "HandPose.h"
#include "ControllerInput.h"
namespace fvr::interaction {
// Sensor presence is independent of touch state: unsupported controllers keep
// the accepted analog pose. Capacitive input never creates a gameplay action.
inline HandPoseTargets ApplyHandTouch(HandPoseTargets targets,const ControllerState& hand)noexcept {
    if(targets.role!=HandPoseRole::Free)return targets;
    if(hand.touchActive&ThumbTouch)
        targets.curl[std::size_t(HandFinger::Thumb)]=(hand.touched&ThumbTouch)?(.35f+.55f*std::clamp(hand.squeeze,0.f,1.f)):0.f;
    if((hand.touchActive&IndexTouch)&&(hand.touched&IndexTouch))
        targets.curl[std::size_t(HandFinger::Index)]=std::max(targets.curl[std::size_t(HandFinger::Index)],.15f);
    return targets;
}
}
