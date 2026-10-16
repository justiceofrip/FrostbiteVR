#pragma once
#include "Bc2PhysicalPump.h"
#include "fvr/interaction/ArmIk.h"
#include <algorithm>
#include <utility>

namespace fvr::bc2 {
struct Bc2ActivePumpPosePlan {
    std::vector<interaction::BoneWrite> mechanicalWrites;
    RigPosePlan mechanicalPlan;
    math::Matrix4 wrist{};
    std::optional<interaction::ArmPose> hand;
};

// Pure private-palette preparation. Fresh physical/native receipts still gate
// the moving part; inability to reach its wrist does not restore stock animation.
// The publication/packer must retain its existing current-owner/source checks.
inline std::optional<Bc2ActivePumpPosePlan> BuildActivePumpPosePlan(
    const Bc2PumpTracking& tracking,const RigSnapshot& rig,const math::Matrix4& placed,
    float units,std::int64_t now,std::span<const interaction::BoneWrite> ordinaryWrites,
    std::span<const math::Matrix4> armNative,const interaction::ArmAnchor& leftAnchor,
    interaction::ArmReachPolicy reach=interaction::ArmReachPolicy::ClampToAnatomy) {
    if(!tracking.target&&!tracking.recoveryTarget)return {};
    const auto presentation=BuildPumpPresentation(tracking,rig,placed,units,now);
    if(!presentation.part||!presentation.wrist)return {};
    Bc2ActivePumpPosePlan result;result.mechanicalWrites.assign(ordinaryWrites.begin(),ordinaryWrites.end());
    const auto merge=[&](const interaction::BoneWrite& write){
        const auto found=std::find_if(result.mechanicalWrites.begin(),result.mechanicalWrites.end(),
            [&](const auto& prior){return prior.index==write.index;});
        if(found==result.mechanicalWrites.end())result.mechanicalWrites.push_back(write);else *found=write;
    };
    merge(*presentation.part);for(const auto& part:presentation.fixedAssembly)merge(part);
    for(const auto& part:presentation.cycleAssembly)merge(part);
    auto plan=BuildRigPosePlan(rig,result.mechanicalWrites);if(!plan)return {};
    result.mechanicalPlan=std::move(*plan);result.wrist=*presentation.wrist;
    auto hand=interaction::SolveTrackedArms(rig.parents,armNative,rig.left,rig.right,
        {result.wrist,leftAnchor.poleDirection,leftAnchor.shoulder,true,reach},{{},{},{},false});
    if(hand&&!hand->reachClamped[0]&&hand->targetError[0]<=.001f*units)result.hand=std::move(hand);
    return result;
}
}
