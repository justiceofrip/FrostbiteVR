#pragma once
#include "Bc2EquipmentIdentity.h"
#include "Bc2ReloadState.h"
#include "Bc2Rig.h"
#include "fvr/interaction/HandInteraction.h"

namespace fvr::bc2 {
// Read-only retirement observation, NOT permission to hide an unsupported gun.
// It certifies the existing ordinary tracked palette at the two native Pack
// boundaries, not material submission, pixel visibility or projectile origin.
struct OrdinaryEquipmentRequest {
    std::uint64_t request=0;
    ReloadStateOwner owner{};
    WeaponEquipmentIdentity equipment{};
    interaction::HandInteractionSample input{};
    interaction::HandInteractionKey gun{};
};
struct OrdinaryEquipmentPair {
    OrdinaryEquipmentRequest source{};
    RigIdentity rig{};
    std::uint64_t drawSerial=0;
    std::int64_t observedNs=0,deadlineNs=0;
    unsigned copyMask=0;
};
inline bool OrdinaryEquipmentFresh(const OrdinaryEquipmentRequest& r,std::int64_t now)noexcept {
    const auto& i=r.input;const auto& n=r.owner;
    return r.request&&n.player>=0x10000&&n.soldier>=0x10000&&n.weak>=0x10000&&n.weapon>=0x10000&&
        n.actorGeneration&&n.equipGeneration&&n.space&&r.equipment.weapon==n.weapon&&r.equipment.data>=0x10000&&
        !r.equipment.Asset().empty()&&r.gun.id==n.weapon&&r.gun.generation==i.owner.equipGeneration&&
        i.owner.actor==((std::uint64_t(n.weak)<<32)|n.soldier)&&i.owner.actorGeneration==n.actorGeneration&&
        i.owner.space==n.space&&i.owner.equipGeneration&&i.sequence&&i.focused&&i.tracked[0]&&i.tracked[1]&&
        i.observedNs>0&&i.observedNs<=now&&i.deadlineNs>now&&i.deadlineNs-i.observedNs<=150000000;
}
inline bool SameOrdinaryEquipment(const OrdinaryEquipmentRequest& a,const OrdinaryEquipmentRequest& b)noexcept {
    return a.request==b.request&&a.owner==b.owner&&a.equipment==b.equipment&&a.input.owner==b.input.owner&&a.gun==b.gun;
}
inline bool OrdinaryEquipmentCurrent(const OrdinaryEquipmentRequest& old,const OrdinaryEquipmentRequest& current,std::int64_t now)noexcept {
    return OrdinaryEquipmentFresh(old,now)&&OrdinaryEquipmentFresh(current,now)&&SameOrdinaryEquipment(old,current)&&
        old.input.sequence<=current.input.sequence&&old.input.observedNs<=current.input.observedNs;
}
inline bool OrdinaryEquipmentPairCurrent(const OrdinaryEquipmentPair& p,const OrdinaryEquipmentRequest& current,std::int64_t now)noexcept {
    return OrdinaryEquipmentCurrent(p.source,current,now)&&p.drawSerial&&p.copyMask==3&&
        p.rig.soldier==current.owner.soldier&&p.rig.weak==current.owner.weak&&p.rig.pose>=0x10000&&p.rig.count>=6&&
        p.observedNs>=p.source.input.observedNs&&p.observedNs<=now&&p.deadlineNs>now&&
        p.observedNs<p.deadlineNs&&p.deadlineNs<=p.source.input.deadlineNs;
}
}
