#pragma once
#include "Bc2BodyEquipment.h"
#include "Bc2BodyAmmo.h"
#include "Bc2MagazineInteraction.h"

namespace fvr::bc2 {
// Availability display for the native-selected, physically holstered item.
// Deliberately has no HandClaim, AmmoSupplySource, reservation or native command.
// Empty hands therefore cannot turn this render sample into a reload interaction.
struct StowedAmmoRenderSource {
    BodyHolsteredRenderSource holstered;
    Bc2AmmoReserveLease reserve{};
    const MagazineEquipmentProfile* magazine=nullptr;
    graphics::BodyPropInstance prop{};
    interaction::AmmoSupplyContact contact{};
};
namespace stowed_ammo_detail {
inline bool ReserveCurrent(const BodyHolsteredRenderSource& h,const Bc2AmmoReserveLease& r,
    std::int64_t now)noexcept {
    if(!BodyHolsteredSourceFresh(h,now)||!r.verified||!r.sequence||!r.allThreeIdle||
       r.observedNs<=0||r.observedNs>now||r.deadlineNs<=now||r.deadlineNs-r.observedNs>200000000||
       r.identity.owner!=h.freeRight->visibility.nativeOwner||r.identity.serverPlayer<0x10000||
       r.identity.serverSoldier<0x10000||r.identity.serverItem<0x10000||
       r.loaded<0||r.capacity<=0||r.loaded>r.capacity||r.reserve<=0)return false;
    const auto& f=r.identity.firing;
    return f[0]>=0x10000&&f[1]>=0x10000&&f[2]>=0x10000&&f[0]!=f[1]&&f[0]!=f[2]&&f[1]!=f[2];
}
struct Geometry {graphics::RigidPropGeometryKey key{};math::Matrix4 partFromContact{};};
inline std::optional<Geometry> ResolveGeometry(const BodyHolsteredRenderSource& h,
    const MagazineEquipmentProfile* magazine,std::int64_t now)noexcept {
    if(!h.freeRight||!h.freeRight->visibility.selected)return {};
    const auto& selected=*h.freeRight->visibility.selected;
    if(magazine){
        if(!magazine->Ready()||!MagazineSelected(selected,h.freeRight->visibility.nativeOwner,
            h.equipment.Asset(),*magazine,now))return {};
        const auto& g=*magazine->geometry;
        const auto contact=interaction::InverseRigid(g.interaction.insertion.itemFromHand);
        if(!contact)return {};
        return Geometry{MakeBodyAmmoGeometryKey(g.asset,g.mesh,g.bones.magazine,g.rigFingerprint),*contact};
    }
    // The existing holstered-source validator has already checked the exact
    // configured SPAS mesh. Other tube/launcher assets are not inferred by class.
    if(h.equipment.Asset()!=SpasReloadAsset)return {};
    const auto contact=SpasShellBoneAtCenter(interaction::reload_insertion_detail::Identity(),1.f);
    if(!contact)return {};
    return Geometry{MakeBodyAmmoGeometryKey(SpasReloadAsset,SpasReloadMesh,"jntWpn_7",SpasReloadRig),*contact};
}
}
inline bool StowedAmmoFresh(const StowedAmmoRenderSource& s,std::int64_t now)noexcept {
    if(!stowed_ammo_detail::ReserveCurrent(s.holstered,s.reserve,now)||!graphics::BodyPropFresh(s.prop,now))return false;
    if(!interaction::ValidBodyAnchor({1,{s.contact.centerMeters[0],s.contact.centerMeters[1],s.contact.centerMeters[2]},s.contact.radiusMeters}))return false;
    const auto geometry=stowed_ammo_detail::ResolveGeometry(s.holstered,s.magazine,now);
    const auto& h=s.holstered;const auto& owner=h.freeRight->input.owner;
    return geometry&&s.prop.geometry==geometry->key&&s.prop.actorGeneration==owner.actorGeneration&&
        s.prop.spaceGeneration==owner.space&&s.prop.equipmentGeneration==graphics::BodyPropWireEpoch(graphics::BodyPropSourceKind::Ammo,owner.equipGeneration)&&
        s.prop.observedNs==std::max(h.prop.observedNs,s.reserve.observedNs)&&
        s.prop.deadlineNs<=std::min(h.prop.deadlineNs,s.reserve.deadlineNs);
}
inline std::optional<StowedAmmoRenderSource> BuildStowedAmmoSource(const BodyHolsteredRenderSource& h,
    const Bc2AmmoReserveLease& reserve,const MagazineEquipmentProfile* magazine,
    const interaction::InputFrame& input,const math::Matrix4& eyeBase,
    const interaction::AmmoSupplyContact& contact,std::int64_t now)noexcept {
    if(!stowed_ammo_detail::ReserveCurrent(h,reserve,now)||
       input.generation!=h.freeRight->input.sequence||input.spaceGeneration!=h.spaceGeneration)return {};
    const auto geometry=stowed_ammo_detail::ResolveGeometry(h,magazine,now);if(!geometry)return {};
    const interaction::BodyAnchor anchor{1,{contact.centerMeters[0],contact.centerMeters[1],contact.centerMeters[2]},contact.radiusMeters};
    const auto world=BodyEquipmentPose(input,eyeBase,anchor,geometry->partFromContact);if(!world)return {};
    const auto& owner=h.freeRight->input.owner;
    StowedAmmoRenderSource result{h,reserve,magazine,{geometry->key,*world,
        std::max(h.prop.observedNs,reserve.observedNs),std::min(h.prop.deadlineNs,reserve.deadlineNs),
        owner.actorGeneration,graphics::BodyPropWireEpoch(graphics::BodyPropSourceKind::Ammo,owner.equipGeneration),owner.space},contact};
    return StowedAmmoFresh(result,now)?std::optional{result}:std::nullopt;
}
inline std::optional<graphics::BodyPropInstance> StowedAmmoHostInstance(const StowedAmmoRenderSource& before,
    const StowedAmmoRenderSource& current,std::int64_t now)noexcept {
    if(!StowedAmmoFresh(before,now)||!StowedAmmoFresh(current,now)||before.magazine!=current.magazine||
       before.contact.centerMeters!=current.contact.centerMeters||before.contact.radiusMeters!=current.contact.radiusMeters||
       !BodyHolsteredHostInstance(before.holstered,current.holstered,now))return {};
    const auto& a=before.reserve;const auto& b=current.reserve;
    if(a.identity!=b.identity||a.loaded!=b.loaded||a.reserve!=b.reserve||a.capacity!=b.capacity||
       a.sequence>b.sequence||a.observedNs>b.observedNs||
       (a.sequence==b.sequence&&(a.observedNs!=b.observedNs||a.deadlineNs!=b.deadlineNs)))return {};
    auto result=before.prop;result.observedNs=std::max(result.observedNs,current.prop.observedNs);
    result.deadlineNs=std::min(result.deadlineNs,current.prop.deadlineNs);
    return graphics::BodyPropFresh(result,now)?std::optional{result}:std::nullopt;
}
} // namespace fvr::bc2
