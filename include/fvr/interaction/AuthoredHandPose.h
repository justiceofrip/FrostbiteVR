#pragma once
#include "fvr/interaction/HandPose.h"

namespace fvr::interaction {
// Presentation-only authored shape. No engine joint names, weapon identifiers,
// input claims or native action authority belong in this reusable path.
struct AuthoredHandShape {
    std::array<std::uint8_t,HandFingerCount> counts{};
    std::array<std::array<math::Matrix4,HandJointsPerFinger>,HandFingerCount> wristFromJoint{};
};

[[nodiscard]] inline std::optional<HandPose> GenerateAuthoredHandPose(
    std::span<const std::int32_t> parents,std::span<const math::Matrix4> currentWorld,
    const HandPoseBinding& binding,const math::Matrix4& targetWrist,
    const AuthoredHandShape& shape,float unitsPerMeter) {
    if(parents.empty()||parents.size()>1024||currentWorld.size()!=parents.size()||
       binding.wrist>=parents.size()||!std::isfinite(unitsPerMeter)||unitsPerMeter<=0)return {};
    auto reference=std::vector<math::Matrix4>(currentWorld.begin(),currentWorld.end());
    auto authoredBinding=binding;
    reference[binding.wrist]={};for(unsigned i=0;i<4;++i)reference[binding.wrist].values[i][i]=1;
    bool any=false;
    for(unsigned finger=0;finger<HandFingerCount;++finger){
        auto& chain=authoredBinding.fingers[finger];
        if(shape.counts[finger]!=chain.count||chain.count>HandJointsPerFinger)return {};
        for(unsigned joint=0;joint<chain.count;++joint){
            auto& target=chain.joints[joint];
            if(target.index>=parents.size()||target.index==binding.wrist)return {};
            const auto& authored=shape.wristFromJoint[finger][joint];
            if(!hand_pose_detail::Proper(authored))return {};
            auto scaled=authored;for(unsigned axis=0;axis<3;++axis)scaled.values[3][axis]*=unitsPerMeter;
            if(!hand_pose_detail::Proper(scaled))return {};
            reference[target.index]=scaled;
            target.openRadians=target.closedRadians=target.pinchRadians=0;
            any=true;
        }
    }
    if(!any)return {};
    HandPoseTargets targets;targets.role=HandPoseRole::MechanismGrip;
    // Existing topology validation and descendant handling preserve unbound
    // attachments relative to their posed parent. Source matrices stay untouched.
    return GenerateHandPose(parents,reference,currentWorld,authoredBinding,targetWrist,targets);
}
} // namespace fvr::interaction
