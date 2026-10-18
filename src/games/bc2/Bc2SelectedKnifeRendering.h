#pragma once
#include "Bc2AuthoredGrip.h"
#include "Bc2InputBinding.h"
#include "Bc2KnifeMotionGeometry.h"
#include "Bc2Rig.h"
#include "fvr/interaction/HandInteraction.h"

namespace fvr::bc2 {
// Selected campaign Knife_Equipable presentation. No chest operation/token,
// attack grant, native selection, state write or damage authority is invented.
struct SelectedKnifeRenderSource {
    ReloadStateSnapshot native{};
    WeaponEquipmentIdentity equipment{};
    interaction::HandInteractionSample input{}; // physical generation, not native equip epoch
    bool gameplayOwned=false; // current GameplayOwnsInput result from original input
};
inline const std::array<AuthoredGripProfile,1>& SelectedKnifeGripProfiles()noexcept {
    // Independent profile address: ChestKnifeGripProfiles is guarded by chest
    // custody. Reuse only measured static mesh/wrist geometry, not that custody.
    static const std::array<AuthoredGripProfile,1> profiles{{{"KNV-1",private_knife::KnifeMesh,
        private_knife::ReferenceBindingDigest,"503047dd626a723e39e3b2c325863b6b049e0ed5985bd7d811dee4eb646d04c7",private_knife::SkeletonSha256,
        0xa7f219a1426216abull,private_knife::RightHandInKnife,[] {math::Matrix4 m;m.values={{
            {0.744314902066f,0.446674005868f,-0.49646516398f,0},
            {0.653793949468f,-0.638975081088f,0.405295345875f,0},
            {-0.13619397271f,-0.626253286004f,-0.767631437323f,0},
            {-0.253332671452f,-0.0234942306453f,0.247122105309f,1}}};return m;}(),false}}};
    return profiles; // No left support or model-axis/aim capability.
}
namespace selected_knife_detail {
inline bool Source(const SelectedKnifeRenderSource& s,std::int64_t now)noexcept {
    const auto& n=s.native;const auto& h=s.input;const auto& o=n.owner;
    return s.gameplayOwned&&h.sequence&&h.observedNs>0&&h.nowNs>=h.observedNs&&h.nowNs<=now&&
        h.deadlineNs>now&&h.deadlineNs-h.observedNs<=100000000&&h.focused&&h.tracked[1]&&!h.released[1]&&
        h.owner.actor==((std::uint64_t(o.weak)<<32)|o.soldier)&&h.owner.actorGeneration==o.actorGeneration&&
        h.owner.space==o.space&&h.owner.equipGeneration&&s.equipment.weapon==o.weapon&&
        s.equipment.data==n.config.weaponData&&s.equipment.persistence&&s.equipment.Asset()=="KNV-1"&&
        n.observedNs>=h.observedNs&&
        VerifiedSelectedFireAction(n,o,h.sequence,now)==EntryAction::MeleeAttack&&
        n.branches[0].wrapperOffset==0x3c&&n.branches[1].wrapperOffset==0x40&&
        n.branches[0].address&&n.branches[1].address&&n.branches[0].address!=n.branches[1].address;
}
inline AuthoredGripOwner Owner(const SelectedKnifeRenderSource& s)noexcept {
    return {s.native.owner.soldier,s.native.owner.weak,s.native.owner.actorGeneration,
        s.input.owner.equipGeneration,s.native.owner.space,s.input.sequence,s.input.deadlineNs};
}
inline bool Mesh(const SelectedMeshesSnapshot& m,const SelectedKnifeRenderSource& s)noexcept {
    return m.owner==s.native.owner&&m.weaponData==s.native.config.weaponData&&m.inventory==s.native.inventory&&
        m.selectedSlot==s.native.selectedSlot&&m.configurationPathVerified&&m.configurationPathPointer&&
        authored_grip_detail::Text(m.configurationPath)==authored_grip_detail::Text(s.native.config.assetPath);
}
inline bool Rig(const RigIdentity& r,const SelectedKnifeRenderSource& s,std::uint64_t fingerprint)noexcept {
    return r.soldier==s.native.owner.soldier&&r.weak==s.native.owner.weak&&r.animation&&r.skeleton&&r.pose&&
        r.worldHeader&&r.worldMatrices&&r.skinMatrices&&r.evaluatedMatrices&&r.count==147&&
        fingerprint==SelectedKnifeGripProfiles()[0].rigFingerprint;
}
}
inline bool SelectedKnifeSourceCurrent(const SelectedKnifeRenderSource& captured,
    const SelectedKnifeRenderSource& current,std::int64_t now)noexcept {
    if(!selected_knife_detail::Source(captured,now)||!selected_knife_detail::Source(current,now))return false;
    const auto& a=captured.native;const auto& b=current.native;
    return a.owner==b.owner&&a.inventory==b.inventory&&a.selectedSlot==b.selectedSlot&&a.config==b.config&&
        a.sequence<=b.sequence&&a.observedNs<=b.observedNs&&captured.equipment==current.equipment&&
        captured.input.owner==current.input.owner&&captured.input.sequence<=current.input.sequence&&
        captured.input.observedNs<=current.input.observedNs&&
        a.branches[0].address==b.branches[0].address&&a.branches[1].address==b.branches[1].address;
}
struct SelectedKnifeRenderBinding {
    SelectedKnifeRenderSource source{};
    AuthoredGripBinding grip{};
    RigIdentity rig{};
    std::uint64_t rigFingerprint=0;
    static constexpr bool attackCapability=false,damageCapability=false,selectionCapability=false;
};
inline std::optional<SelectedKnifeRenderBinding> BindSelectedKnifeGrip(const SelectedKnifeRenderSource& source,
    std::shared_ptr<const SelectedMeshesSnapshot> mesh,const RigIdentity& rig,std::uint64_t fingerprint,
    std::int64_t now)noexcept {
    if(!selected_knife_detail::Source(source,now)||!mesh||!selected_knife_detail::Mesh(*mesh,source)||
       !selected_knife_detail::Rig(rig,source,fingerprint))return {};
    const auto bound=BindAuthoredGrip(SelectedKnifeGripProfiles(),std::move(mesh),source.equipment,
        selected_knife_detail::Owner(source),fingerprint,now,false);
    if(!bound.binding)return {};
    return SelectedKnifeRenderBinding{source,*bound.binding,rig,fingerprint};
}
inline bool SelectedKnifeBindingCurrent(const SelectedKnifeRenderBinding& binding,
    const SelectedKnifeRenderSource& current,const SelectedMeshesSnapshot* mesh,const RigIdentity& rig,
    std::uint64_t fingerprint,std::int64_t now)noexcept {
    return binding.grip.profile==&SelectedKnifeGripProfiles()[0]&&binding.rig==rig&&binding.rigFingerprint==fingerprint&&
        SelectedKnifeSourceCurrent(binding.source,current,now)&&selected_knife_detail::Rig(rig,current,fingerprint)&&
        mesh&&selected_knife_detail::Mesh(*mesh,current)&&
        AuthoredGripCurrent(binding.grip,mesh,current.equipment,selected_knife_detail::Owner(current),now);
}
struct SelectedKnifeRenderReceipt {
    SelectedKnifeRenderBinding binding{};
    math::Matrix4 wristInGrip{}; // Actual current TrackedRig rotation, not authored static wrist.
};
inline std::optional<SelectedKnifeRenderReceipt> MakeSelectedKnifeRenderReceipt(const SelectedKnifeRenderBinding& binding,
    const math::Matrix4& wristInGrip)noexcept {
    if(binding.grip.profile!=&SelectedKnifeGripProfiles()[0]||
       wristInGrip.values[3]!=std::array<float,4>{0,0,0,1}||!interaction::InverseRigid(wristInGrip))return {};
    return SelectedKnifeRenderReceipt{binding,wristInGrip};
}
inline bool SelectedKnifeRenderCurrent(const SelectedKnifeRenderReceipt& receipt,
    const SelectedKnifeRenderSource& current,const SelectedMeshesSnapshot* mesh,const RigIdentity& rig,
    std::uint64_t fingerprint,std::int64_t now)noexcept {
    return receipt.wristInGrip.values[3]==std::array<float,4>{0,0,0,1}&&interaction::InverseRigid(receipt.wristInGrip)&&
        SelectedKnifeBindingCurrent(receipt.binding,current,mesh,rig,fingerprint,now);
}
}
