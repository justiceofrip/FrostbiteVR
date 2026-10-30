#pragma once
#include "Bc2BeltAmmo.h"
#include "Bc2ShotgunEquipment.h"
#include "Bc2MagazinePresentation.h"
#include "fvr/interaction/AmmoSupplyProp.h"
#include "Bc2FixedContainerEquipment.h"

namespace fvr::bc2 {
// Render-only copy of the SAME source/contact used by the reload consumer.
// Exactly one native family must be present. This cannot mint or reserve ammo.
struct BodyAmmoTracking {
    interaction::AmmoSupplyVisualSample visual{};
    std::optional<MagazineTracking> magazine;
    std::optional<BeltAmmoTracking> shell;
    std::optional<FixedContainerBodyAmmo> fixed;
};
inline bool BodyAmmoFresh(const BodyAmmoTracking& t,std::int64_t now)noexcept {
    using namespace interaction;
    if(unsigned(bool(t.magazine))+unsigned(bool(t.shell))+unsigned(bool(t.fixed))!=1||!AmmoSupplyVisualFresh(t.visual,now))return false;
    if(t.fixed){const auto source=FixedContainerBodySource(*t.fixed,t.visual.input,now);return source&&*source==t.visual.source;}
    if(t.shell)return ShotgunBeltAmmoFresh(*t.shell,now)&&
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
    if(!BodyAmmoFresh(old,now)||!BodyAmmoFresh(current,now)||bool(old.magazine)!=bool(current.magazine)||bool(old.fixed)!=bool(current.fixed))return false;
    if(old.fixed){const auto& a=*old.fixed;const auto& b=*current.fixed;
        return a.equipment==b.equipment&&a.binding.owner==b.binding.owner&&a.binding.context==b.binding.context&&
            a.binding.inventory==b.binding.inventory&&a.binding.switching==b.binding.switching&&a.binding.data==b.binding.data&&
            a.binding.persistence==b.binding.persistence&&a.ammunition.sequence<=b.ammunition.sequence&&
            (a.ammunition.sequence!=b.ammunition.sequence||a.ammunition.counts==b.ammunition.counts)&&
            AmmoSupplyVisualRetained(old.visual,current.visual,now);}
    if(old.shell)return interaction::AmmoSupplyVisualRetained(old.visual,current.visual,now)&&
        ShotgunBeltAmmoRetained(*old.shell,*current.shell,now);
    const auto& a=*old.magazine;const auto& b=*current.magazine;
    const auto& v=old.visual;const auto& w=current.visual;
    // Both exact typed publications were validated above. A newer loaded count
    // can change refill cost without changing this reserve prop's geometry or
    // availability. Only this render comparison permits that change; pickup
    // source/reservation comparisons remain strict, and no lease is renewed.
    // Reusing a reserve sequence with different counts/content is corruption,
    // even when the refill cost happens to stay equal (for example low reserve).
    return v.source.identity==w.source.identity&&v.gun.token==w.gun.token&&v.frame==w.frame&&
        v.contact.centerMeters==w.contact.centerMeters&&v.contact.radiusMeters==w.contact.radiusMeters&&
        v.source.family==w.source.family&&v.source.reserveUnits==w.source.reserveUnits&&
        v.source.sequence<=w.source.sequence&&v.source.observedNs<=w.source.observedNs&&
        v.input.sequence<=w.input.sequence&&v.input.observedNs<=w.input.observedNs&&
        (v.input.sequence!=w.input.sequence||
         (v.input.observedNs==w.input.observedNs&&v.input.deadlineNs==w.input.deadlineNs))&&
        (v.source.sequence!=w.source.sequence||
         (v.source==w.source&&a.reserve.loaded==b.reserve.loaded&&
          a.reserve.reloadInputReady==b.reserve.reloadInputReady&&a.reserve.allThreeIdle==b.reserve.allThreeIdle&&
          a.reserve.emptyReloadControlled==b.reserve.emptyReloadControlled))&&
        a.owner==b.owner&&a.family.binding==b.family.binding&&a.cycle==b.cycle&&
        a.reserve.identity==b.reserve.identity&&a.reserve.reserve==b.reserve.reserve&&a.reserve.capacity==b.reserve.capacity&&
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
    }else if(source.fixed){
        const auto* e=KnownFixedContainerEquipment(source.fixed->equipment);if(!e)return {};
        partFromContact=InverseRigid(e->interaction.geometry[2].motion.placement.sourceFromGrip);
        pose.asset=e->asset;pose.mesh=e->mesh;pose.part=e->supplyRoot;pose.rigFingerprint=e->rigFingerprint;
    }else{
        const auto* equipment=KnownShotgunEquipment(source.shell->equipment);const auto geometry=ShotgunShellGeometry(equipment);
        if(!equipment||!geometry)return {};
        partFromContact=ShotgunShellBoneAtCenter(equipment,reload_insertion_detail::Identity(),1.f);
        pose.asset=equipment->native->config->assetName;pose.mesh=equipment->meshPath;pose.part=geometry->bone;pose.rigFingerprint=equipment->rigFingerprint;
    }
    const auto placed=partFromContact?BuildAmmoSupplyPropPose(source.visual,input,eyeBase,*partFromContact,now):std::nullopt;
    if(!placed)return {};
    pose.partWorld=placed->partWorld;return pose;
}
} // namespace fvr::bc2
