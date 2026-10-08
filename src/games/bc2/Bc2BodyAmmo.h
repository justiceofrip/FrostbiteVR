#pragma once
#include "Bc2BeltAmmo.h"
#include "Bc2MagazinePresentation.h"
#include "fvr/interaction/AmmoSupplyProp.h"

namespace fvr::bc2 {
// Render-only copy of the SAME source/contact used by the reload consumer.
// Exactly one native family must be present. This cannot mint or reserve ammo.
struct BodyAmmoTracking {
    interaction::AmmoSupplyVisualSample visual{};
    std::optional<MagazineTracking> magazine;
    std::optional<BeltAmmoTracking> shell;
};
inline bool BodyAmmoFresh(const BodyAmmoTracking& t,std::int64_t now)noexcept {
    using namespace interaction;
    if(bool(t.magazine)==bool(t.shell)||!AmmoSupplyVisualFresh(t.visual,now))return false;
    if(t.shell)return SpasBeltAmmoFresh(*t.shell,now)&&
        AmmoSupplyVisualRetained(t.shell->visual,t.visual,now)&&
        AmmoSupplyVisualRetained(t.visual,t.shell->visual,now);
    const auto& m=*t.magazine;const auto& in=t.visual.input;
    if(!MagazineTrackingFresh(m,now)||m.detach||m.inputEvidence.owner!=in.owner||
       m.inputEvidence.sequence!=in.sequence||m.inputEvidence.observedNs!=in.observedNs||
       m.inputEvidence.deadlineNs!=in.deadlineNs||
       (m.target&&m.target->role!=MagazinePropRole::Attached&&m.target->role!=MagazinePropRole::Hidden))return false;
    const auto owners=BindMagazineOwners(in.owner,m.family.binding.weapon,m.reserve,m.cycle,now,m.family);
    const auto source=owners?MagazineBodySupply(*owners,m.reserve,in.owner.space,now):std::nullopt;
    return source&&*source==t.visual.source;
}
inline bool BodyAmmoRetained(const BodyAmmoTracking& old,const BodyAmmoTracking& current,std::int64_t now)noexcept {
    if(!BodyAmmoFresh(old,now)||!BodyAmmoFresh(current,now)||bool(old.magazine)!=bool(current.magazine)||
       !interaction::AmmoSupplyVisualRetained(old.visual,current.visual,now))return false;
    if(old.shell)return SpasBeltAmmoRetained(*old.shell,*current.shell,now);
    const auto& a=*old.magazine;const auto& b=*current.magazine;
    return a.owner==b.owner&&a.family.binding==b.family.binding&&a.cycle==b.cycle&&
        a.reserve.identity==b.reserve.identity&&a.reserve.loaded==b.reserve.loaded&&a.reserve.capacity==b.reserve.capacity&&
        a.selected->weaponData==b.selected->weaponData&&a.selected->soleConfiguredArray==b.selected->soleConfiguredArray&&
        a.selected->states==b.selected->states&&a.selected->sequence<=b.selected->sequence&&
        a.selected->observedNs<=b.selected->observedNs&&
        (a.selected->sequence!=b.selected->sequence||
         (a.selected->observedNs==b.selected->observedNs&&a.selected->deadlineNs==b.selected->deadlineNs));
}
struct BodyAmmoPose {
    BodyAmmoTracking source{};
    interaction::InputFrame input{};
    math::Matrix4 eyeBase{},partWorld{};
    std::string_view asset,mesh,part;
    std::uint64_t rigFingerprint=0;
};
inline std::optional<BodyAmmoPose> BuildBodyAmmoPose(const BodyAmmoTracking& source,
    const interaction::InputFrame& input,const math::Matrix4& eyeBase,std::int64_t now)noexcept {
    using namespace interaction;
    if(!BodyAmmoFresh(source,now))return {};
    std::optional<math::Matrix4> partFromContact;
    BodyAmmoPose pose;pose.source=source;pose.input=input;pose.eyeBase=eyeBase;
    if(source.magazine){
        const auto& g=*source.magazine->family.binding.profile->geometry;
        // The pickup handle is the authored grasp, not the weapon root. Part
        // geometry is extracted into the named magazine bone's local space.
        partFromContact=InverseRigid(g.interaction.insertion.itemFromHand);
        pose.asset=g.asset;pose.mesh=g.mesh;pose.part=g.bones.magazine;pose.rigFingerprint=g.rigFingerprint;
    }else{
        partFromContact=SpasShellBoneAtCenter(reload_insertion_detail::Identity(),1.f);
        pose.asset=SpasReloadAsset;pose.mesh=SpasReloadMesh;pose.part="jntWpn_7";pose.rigFingerprint=SpasReloadRig;
    }
    const auto placed=partFromContact?BuildAmmoSupplyPropPose(source.visual,input,eyeBase,*partFromContact,now):std::nullopt;
    if(!placed)return {};
    pose.partWorld=placed->partWorld;return pose;
}
} // namespace fvr::bc2
