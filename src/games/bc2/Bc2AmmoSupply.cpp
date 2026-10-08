#include "Bc2AmmoSupply.h"
namespace fvr::bc2 {
using namespace interaction;
std::optional<AmmoSupplySource> SpasAmmoSupplySource(const Bc2ReloadInteractionSample& sample,
    const Bc2AmmoReserveLease& native)noexcept {
    const auto& s=sample.insertion;
    const auto& p=s.identity.owner;const auto& n=native.identity.owner;const auto& f=native.identity.firing;
    if(!native.verified||!native.sequence||native.observedNs<=0||native.observedNs>s.nowNs||
        native.deadlineNs<=s.nowNs||native.deadlineNs-native.observedNs>200000000||
        n.player<0x10000||n.soldier<0x10000||n.weak<0x10000||n.weapon<0x10000||
        !n.actorGeneration||!n.equipGeneration||!n.space||native.identity.serverPlayer<0x10000||
        native.identity.serverSoldier<0x10000||native.identity.serverItem<0x10000||
        f[0]<0x10000||f[1]<0x10000||f[2]<0x10000||f[0]==f[1]||f[0]==f[2]||f[1]==f[2]||
        p.actor!=((std::uint64_t(n.weak)<<32)|n.soldier)||p.actorGeneration!=n.actorGeneration||
        !p.equipGeneration||p.space!=n.space||s.identity.weapon.id!=n.weapon||s.identity.weapon.generation!=p.equipGeneration||
        !s.identity.trackingEpoch||native.reserve<0||
        native.loaded<0||native.capacity<=0||native.loaded>native.capacity||
        sample.assetName!=SpasReloadAsset||sample.meshPath!=SpasReloadMesh||sample.rigFingerprint!=SpasReloadRig||
        !sample.selectedMeshIdentityVerified)return {};
    const auto profile=SpasReloadInsertionProfile();
    const HandInteractionKey pool{native.identity.serverItem,native.identity.owner.equipGeneration};
    return AmmoSupplySource{{p,s.identity.weapon,{profile.id,profile.revision},pool,s.identity.trackingEpoch},
        ReloadInsertionFamily::SingleShell,static_cast<std::uint32_t>(native.reserve),1,native.sequence,
        native.observedNs,native.deadlineNs,true};
}
std::optional<AmmoSupplySource> SpasAmmoSupplySource(const Bc2ReloadInteractionSample& sample,
    const ReloadRoundLease& native)noexcept {
    return SpasAmmoSupplySource(sample,Bc2AmmoReserveLease{native.identity,native.sequence,native.observedNs,
        native.deadlineNs,native.loaded,native.reserve,native.capacity,native.nativeBindingVerified});
}
bool SameAmmoReservation(const AmmoSupplyReservation& supply,const Bc2ReloadReservedItem& native)noexcept {
    return supply.item==native.item&&supply.claim==native.claim&&supply.seat==native.seat&&
        supply.request==native.request&&supply.cycle==native.cycle;
}
std::optional<AmmoSupplyReceipt> SpasAmmoSupplyReceipt(const AmmoSupplyReservation& supply,
    const Bc2ReloadOwnerMap& original,const Bc2ReloadBridgeResult& bridge,const Bc2ReloadAckEvidence& evidence,
    const ReloadRoundLease& native,const Bc2AmmoReserveLease& reserve,std::int64_t now)noexcept {
    const auto p=SpasReloadInsertionProfile();
    const auto& ack=evidence.acknowledgement;
    const auto current=BindBc2ReloadOwners(original.physical,original.weapon,native,now);
    if(!current||*current!=original||original.physical!=supply.identity.owner||original.weapon!=supply.identity.weapon||
        supply.identity.pool!=HandInteractionKey{original.native.serverItem,original.native.owner.equipGeneration}||
        original.cycle!=supply.cycle||supply.units!=1||supply.identity.profile!=HandInteractionKey{p.id,p.revision}||
        supply.operation!=ReloadOperation::InsertRound||bridge.phase!=Bc2ReloadBridgePhase::Complete||bridge.unresolvedNative||
        !bridge.consumed||!SameAmmoReservation(supply,*bridge.consumed)||!bridge.acknowledged||
        !evidence.verified||!ack.serverInvocation||ack.identity!=original.native||ack.cycle!=original.cycle||
        !ack.sampleSequence||native.sequence<ack.sampleSequence||
        evidence.observedNs<supply.startedNs||evidence.observedNs>now||evidence.deadlineNs<=now||
        evidence.deadlineNs-evidence.observedNs>200000000||native.observedNs<evidence.observedNs||native.reserve<0||
        !reserve.verified||reserve.identity!=original.native||reserve.sequence<=supply.sourceSequence||
        reserve.observedNs<evidence.observedNs||reserve.observedNs>now||reserve.deadlineNs<=now||
        reserve.deadlineNs-reserve.observedNs>200000000||reserve.loaded!=native.loaded||
        reserve.reserve!=native.reserve||reserve.capacity!=native.capacity)return {};
    const ManualReloadOwner physical{supply.identity.owner.actor,supply.identity.owner.actorGeneration,
        supply.identity.weapon.id,supply.identity.owner.equipGeneration,supply.identity.owner.space};
    const auto& n=original.native.owner;
    const ManualReloadOwner nativeOwner{n.soldier,n.actorGeneration,n.weapon,n.equipGeneration,n.space};
    if(bridge.acknowledged->request!=supply.request||bridge.acknowledged->owner!=physical||
        bridge.acknowledged->operation!=supply.operation||bridge.acknowledged->status!=ReloadAcknowledgement::Applied||
        ack.semantic.request!=supply.request||ack.semantic.owner!=nativeOwner||ack.semantic.operation!=supply.operation||
        ack.semantic.status!=ReloadAcknowledgement::Applied)return {};
    return AmmoSupplyReceipt{supply,*bridge.acknowledged,
        {supply.identity,ReloadInsertionFamily::SingleShell,static_cast<std::uint32_t>(reserve.reserve),1,reserve.sequence,
            reserve.observedNs,reserve.deadlineNs,true},ack.serverInvocation,evidence.observedNs,evidence.deadlineNs,true};
}
std::optional<AmmoSupplyReceipt> SpasAmmoSupplyReceipt(const AmmoSupplyReservation& supply,
    const Bc2ReloadOwnerMap& original,const Bc2ReloadBridgeResult& bridge,const Bc2ReloadAckEvidence& evidence,
    const ReloadRoundLease& native,std::int64_t now)noexcept {
    return SpasAmmoSupplyReceipt(supply,original,bridge,evidence,native,
        {native.identity,native.sequence,native.observedNs,native.deadlineNs,native.loaded,native.reserve,native.capacity,native.nativeBindingVerified},now);
}
} // namespace fvr::bc2
