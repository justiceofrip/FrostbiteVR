#pragma once
#include "Bc2ReloadPreview.h"
#include "fvr/interaction/ControllerInput.h"
#include <cmath>
namespace fvr::bc2 {
// Observation only. The shared raw-contact carrier supplies the diagnostic
// inverse, but this path has no magazine, shell, reservation or ammo authority.
// Capture the original hand evidence directly; unavailable reload geometry must
// not prevent emulating ordinary support on another weapon family.
inline ReloadRawContact BuildSupportControllerMapping(const ReloadStateOwner& owner,
    const interaction::HandInteractionSample& evidence,const interaction::InputFrame& input,
    const RigIdentity& rig,std::uint64_t fingerprint,const math::Matrix4& wrist,
    const math::Matrix4& weapon,const math::Matrix4& body,float units,std::int64_t now)noexcept {
    using interaction::reload_insertion_detail::Rigid;
    if(!owner.player||!owner.soldier||!owner.weak||!owner.weapon||!owner.actorGeneration||
       !owner.equipGeneration||!owner.space||rig.soldier!=owner.soldier||rig.weak!=owner.weak||
       !rig.pose||!rig.skeleton||!rig.count||rig.count>1024||!fingerprint||
       evidence.owner.actor!=((std::uint64_t(owner.weak)<<32)|owner.soldier)||
       evidence.owner.actorGeneration!=owner.actorGeneration||!evidence.owner.equipGeneration||
       evidence.owner.space!=owner.space||input.spaceGeneration!=owner.space||
       !evidence.sequence||evidence.sequence!=input.generation||!evidence.focused||
       !evidence.tracked[0]||!evidence.tracked[1]||evidence.observedNs<=0||
       evidence.observedNs>now||evidence.deadlineNs<=now||
       !interaction::ValidInput(input)||!input.focused||!input.headValid||
       !input.hands[0].gripTracked||!input.hands[1].gripTracked||!input.hands[1].aimTracked||
       !std::isfinite(units)||units<=0||!Rigid(wrist)||!Rigid(weapon)||!Rigid(body))return {};
    ReloadRawContact out;out.valid=true;out.owner=owner;out.rig=rig;
    out.rigFingerprint=fingerprint;out.inputEvidence=evidence;
    out.rawLeftWristWorldMeters=wrist;out.weaponWorldMeters=weapon;out.trackingBodyWorldMeters=body;
    for(auto* matrix:{&out.rawLeftWristWorldMeters,&out.weaponWorldMeters,&out.trackingBodyWorldMeters})
        for(unsigned axis=0;axis<3;++axis)matrix->values[3][axis]/=units;
    return out;
}
}
