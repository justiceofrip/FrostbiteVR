#pragma once
#include "fvr/interaction/ControllerInput.h"
#include "fvr/interaction/AimFrame.h"

namespace fvr::interaction {
struct TrackedBodyAnchorResult {float yaw=0;bool referenceChanged=false,basisReplaced=false;};
// Native yaw seeds a new actor only: during tracked play it is controller aim,
// not the body's heading. An unknown/replaced tracking basis retains the last
// virtual view heading, without comparing poses from unrelated XR coordinates.
class TrackedBodyAnchor {
public:
    std::optional<TrackedBodyAnchorResult> Update(std::uint64_t actor,const InputFrame& input,
        float nativeYaw,float lastHeadYaw,float turnDegrees)noexcept {
        if(!actor||!ValidInput(input)||!input.focused||!input.headValid||
           !std::isfinite(nativeYaw)||!std::isfinite(lastHeadYaw)||!std::isfinite(turnDegrees))return {};
        const auto reference=UprightReference(input.referenceHead);if(!reference)return {};
        const bool newActor=actor_!=actor;
        const bool restarted=!newActor&&(input.generation<generation_||input.predictedNs<time_);
        const bool changed=newActor||restarted||space_!=input.spaceGeneration||basis_!=input.trackingBasisGeneration;
        auto yaw=yaw_;bool replaced=false;
        if(newActor)yaw=nativeYaw;
        else if(changed){
            const bool comparable=!restarted&&basis_&&basis_==input.trackingBasisGeneration;
            if(comparable){
                const auto delta=ControllerAim(reference_,*reference);if(!delta)return {};
                yaw+=delta->yaw; // Includes position-only/vertical-look recenter: zero yaw.
            }else {yaw+=lastHeadYaw;replaced=true;}
        }
        constexpr float tau=6.283185307179586f,rad=.017453292519943295f;
        yaw=std::fmod(yaw+turnDegrees*rad,tau);if(yaw<0)yaw+=tau;
        actor_=actor;space_=input.spaceGeneration;basis_=input.trackingBasisGeneration;
        generation_=input.generation;time_=input.predictedNs;reference_=*reference;yaw_=yaw;
        return TrackedBodyAnchorResult{yaw,changed,replaced};
    }
private:
    std::uint64_t actor_=0,space_=0,generation_=0;
    std::uint32_t basis_=0;std::int64_t time_=0;
    math::Pose reference_{};float yaw_=0;
};
}
