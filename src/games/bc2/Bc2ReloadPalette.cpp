#include "Bc2ReloadPalette.h"
#include "fvr/interaction/ArmIk.h"
#include <algorithm>
namespace fvr::bc2 {
Bc2ReloadPalettePlan BuildBc2ReloadPalette(const RigSnapshot& rig,
    const Bc2ReloadPresentationBinding& binding,const Bc2ReloadPaletteSample& sample,
    const Bc2ReloadTargets& targets) {
    Bc2ReloadPalettePlan out;
    if(!sample.enabled)return out;
    const auto presentation=BuildBc2ReloadPresentation(rig,binding,sample.presentation,targets);
    if(!presentation.palette){out.reason=Bc2ReloadPaletteReason::PresentationRejected;out.presentationReason=presentation.reason;return out;}
    // Validate all existing writes before merging. Duplicate indices would make
    // overlay precedence depend on caller ordering; hidden leaves remain banned.
    if(sample.armNativeWorld.size()!=rig.names.size()||sample.baseWrites.empty()||
       !BuildRigPosePlan(rig,sample.baseWrites)){
        out.reason=Bc2ReloadPaletteReason::InvalidBasePose;return out;
    }
    const auto wrist=std::find_if(presentation.writes.begin(),presentation.writes.end(),
        [&](const auto& w){return w.index==binding.wrist;});
    if(wrist==presentation.writes.end()||binding.wrist!=rig.left.wrist){out.reason=Bc2ReloadPaletteReason::ArmRejected;return out;}
    // Right arm remains the accepted ordinary pose; solving it again here can
    // disturb the firing hand. Only the shell hand is redirected.
    const auto arm=interaction::SolveTrackedArms(rig.parents,sample.armNativeWorld,rig.left,rig.right,
        {wrist->transform,sample.leftPoleDirection,sample.leftShoulder,true},{{},{},{},false});
    if(!arm){out.reason=Bc2ReloadPaletteReason::ArmRejected;return out;}
    out.leftWristError=arm->targetError[0];
    // Reject reach clamping instead of independently moving the wrist away from
    // its measured shell grasp. 0.1mm is numerical tolerance, not a grip offset.
    if(arm->reachClamped[0]||out.leftWristError>.0001f*sample.presentation.unitsPerMetre){
        out.reason=Bc2ReloadPaletteReason::WristUnreachable;return out;
    }
    auto writes=std::vector<interaction::BoneWrite>(sample.baseWrites.begin(),sample.baseWrites.end());
    const auto merge=[&](const auto& added){for(const auto& w:added){
        const auto prior=std::find_if(writes.begin(),writes.end(),[&](const auto& p){return p.index==w.index;});
        if(prior==writes.end())writes.push_back(w);else *prior=w;
    }};
    merge(arm->writes);
    // Captured finger pose, wrist and independent shell override follow the
    // ordinary weapon subtree, so that subtree cannot drag the shell back.
    merge(presentation.writes);
    auto palette=bc2_reload_detail::BuildShellVisibilityPalette(rig,binding,writes,presentation.ownedShellVisibility);
    if(!palette){out.reason=Bc2ReloadPaletteReason::InvalidPalette;return out;}
    out.reason=Bc2ReloadPaletteReason::None;out.writes=std::move(writes);out.palette=std::move(palette);
    out.ownedShellVisibility=presentation.ownedShellVisibility;return out;
}
}
