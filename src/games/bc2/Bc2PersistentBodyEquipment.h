#pragma once
#include "Bc2BodyEquipment.h"
#include "Bc2BodyInventory.h"
#include "Bc2ChestKnifeRendering.h"

namespace fvr::bc2 {
// This typed batch represents assigned, native-owned, unselected items only.
// Selected committed hide remains the separate BodyHolsteredRenderSource path.
struct BodyCarriedRenderBatch {
    std::shared_ptr<const BodyInventoryDisplay> inventory;
    std::array<std::shared_ptr<const CarriedMeshesSnapshot>,8> configured{};
    std::array<graphics::BodyPropInstance,8> instances{};
    std::array<unsigned,8> slotIndices{};unsigned count=0;
};
namespace body_carried_detail {
// Separate render-only row: never enroll a knife in ordinary weapon slots or
// selected-hide capability. Review316/323 puts jntWpn_1 at the handle, with
// blade toward local -Z and thickness along X. Keep that handle at the exact
// existing grab center; map thickness to body depth and the blade downward.
inline const BodyEquipmentProfile& ChestKnifeProfile()noexcept {
    static constexpr std::array<std::string_view,1> meshes{private_knife::KnifeMesh};
    static const BodyEquipmentProfile profile{"KNV-1",private_knife::KnifeMesh.data(),"jntWpn_1",
        ChestKnifeGripProfiles()[0].rigFingerprint,{{{{0,0,1,0},{1,0,0,0},{0,1,0,0},{0,0,0,1}}}},
        "Objects/Weapons/Handheld/Knife/Knife",meshes};
    return profile;
}
inline std::optional<BodyInventoryDisplaySlot> RenderSlot(const BodyInventoryDisplay& d,unsigned index)noexcept {
    if(index<d.count&&index<d.slots.size())return d.slots[index];
    if(index!=d.count||index>=d.slots.size()||!BodyKnifeCandidateCurrent(d))return {};
    return BodyInventoryDisplaySlot{{private_knife::KnifeChest.slot,d.knifeCandidate},d.carried.items[BodyKnifeNativeSlot]};
}
inline std::optional<BodyInventoryDisplaySlot> StowedKnifeSlot(const BodyInventoryDisplay& d,
    const WeaponEquipmentIdentity& selected,bool temporaryTool)noexcept {
    const auto knife=RenderSlot(d,d.count);
    if(!knife||temporaryTool||selected.weapon!=d.selectedOwner.weapon||selected.Asset().empty()||
       selected.Asset()==ChestKnifeProfile().asset)return {};
    const auto end=d.carried.items.begin()+d.carried.count;
    const auto active=std::find_if(d.carried.items.begin(),end,[&](const auto& v){return v.weapon==selected.weapon;});
    return active!=end&&active->data==selected.data&&active->persistence==selected.persistence?knife:std::nullopt;
}
inline std::optional<WeaponEquipmentIdentity> Equipment(const CarriedMeshesSnapshot& c)noexcept {
    WeaponEquipmentIdentity out;out.weapon=c.weapon;out.data=c.configured.weaponData;out.persistence=c.persistence;
    const auto name=body_equipment_detail::Text(c.configured.weaponName);
    if(name.empty()||name.size()>=out.asset.size())return {};
    std::copy(name.begin(),name.end(),out.asset.begin());return out;
}
inline const BodyEquipmentProfile* Profile(const BodyInventoryDisplay& d,const BodyInventoryDisplaySlot& slot,
    const CarriedMeshesSnapshot& c,std::int64_t now)noexcept {
    const auto& s=c.configured;
    if(c.weapon!=slot.native.weapon||c.nativeSlot!=slot.native.slot||c.persistence!=slot.native.persistence||
       s.owner!=d.selectedOwner||s.weaponData!=slot.native.data||s.inventory!=d.carried.inventory||
       s.selectedSlot>=d.carried.count||d.carried.items[s.selectedSlot].weapon!=d.selectedOwner.weapon||
       s.observedNs<=0||s.observedNs>now||s.deadlineNs<=now||s.deadlineNs-s.observedNs>200000000||!s.sequence||
       s.stateCount!=1||!s.soleConfiguredArray||s.states[0].array!=s.soleConfiguredArray||s.states[0].count>8||
       c.weapon==d.selectedOwner.weapon||slot.assignment.item==d.physicalSelected)return nullptr;
    const auto asset=body_equipment_detail::Text(s.weaponName);const BodyEquipmentProfile* found=nullptr;
    if(slot.native.carryAsset[0]&&asset!=body_equipment_detail::Text(slot.native.carryAsset))return nullptr;
    if(slot.native.category==11){
        const auto knife=RenderSlot(d,d.count);
        const auto& p=ChestKnifeProfile();
        return knife&&*knife==slot&&body_equipment_detail::ProfileMatchesSelected(p,s)?&p:nullptr;
    }
    for(const auto& p:BodyEquipmentProfiles)if(asset==p.asset&&body_equipment_detail::ProfileMatchesSelected(p,s)){
        if(found||!interaction::reload_insertion_detail::Rigid(p.modelToAnchor))return nullptr;found=&p;
    }return found;
}
inline bool SameConfig(const CarriedMeshesSnapshot& a,const CarriedMeshesSnapshot& b)noexcept {
    const auto& x=a.configured;const auto& y=b.configured;
    return a.weapon==b.weapon&&a.nativeSlot==b.nativeSlot&&a.persistence==b.persistence&&x.owner==y.owner&&
        x.weaponData==y.weaponData&&x.inventory==y.inventory&&x.selectedSlot==y.selectedSlot&&x.weaponName==y.weaponName&&
        x.stateTypeInfo==y.stateTypeInfo&&x.meshTypeInfo==y.meshTypeInfo&&x.stateCount==y.stateCount&&
        x.soleConfiguredArray==y.soleConfiguredArray&&x.states==y.states&&
        x.configurationPathVerified==y.configurationPathVerified&&x.configurationPath==y.configurationPath;
}
}
inline bool BodyCarriedBatchFresh(const BodyCarriedRenderBatch& b,std::int64_t now)noexcept {
    if(!b.inventory||!BodyInventoryRenderDisplayFresh(*b.inventory,now)||b.count>b.instances.size())return false;
    for(unsigned n=0;n<b.count;++n){const auto idx=b.slotIndices[n];
        const auto slot=body_carried_detail::RenderSlot(*b.inventory,idx);
        if(!slot||!b.configured[n])return false;
        const auto* p=body_carried_detail::Profile(*b.inventory,*slot,*b.configured[n],now);
        const auto& v=b.instances[n];const auto& c=b.configured[n]->configured;
        if(!p||v.geometry!=MakeBodyAmmoGeometryKey(p->asset,p->mesh,p->part,p->rig)||!graphics::BodyPropFresh(v,now)||
           v.actorGeneration!=b.inventory->physicalOwner.actorGeneration||v.equipmentGeneration!=graphics::BodyPropWireEpoch(graphics::BodyPropSourceKind::Carried,b.inventory->cohort,idx)||
           v.spaceGeneration!=b.inventory->physicalOwner.space||v.observedNs<std::max(b.inventory->observedNs,c.observedNs)||
           v.deadlineNs>std::min(b.inventory->deadlineNs,c.deadlineNs))return false;
        for(unsigned k=0;k<n;++k)if(b.slotIndices[k]==idx)return false;
    }return true;
}
inline std::optional<BodyCarriedRenderBatch> BuildBodyCarriedBatch(std::shared_ptr<const BodyInventoryDisplay> d,
    const std::array<std::shared_ptr<const CarriedMeshesSnapshot>,8>& configs,const interaction::InputFrame& input,
    const math::Matrix4& eyeBase,const interaction::BodyAnchorConfig& anchors,std::int64_t now)noexcept {
    if(!d||!BodyInventoryRenderDisplayFresh(*d,now)||input.generation!=d->sequence||input.spaceGeneration!=d->physicalOwner.space||
       !interaction::ValidInput(input)||!input.focused||!input.headValid||!interaction::ValidBodyAnchors(anchors))return {};
    BodyCarriedRenderBatch out;out.inventory=d;
    for(unsigned i=0;i<configs.size();++i){
        const auto entry=body_carried_detail::RenderSlot(*d,i);if(!entry)break;
        const auto& slot=*entry;
        if(slot.native.weapon==d->selectedOwner.weapon||slot.assignment.item==d->physicalSelected)continue;
        // Unknown/unbound geometry is omitted; a known bound row with stale
        // source rejects this complete batch, never a per-eye partial fallback.
        if(!configs[i])continue;
        const auto* p=body_carried_detail::Profile(*d,slot,*configs[i],now);if(!p)return {};
        const auto* anchor=i==d->count?&private_knife::KnifeChest:interaction::FindBodyWeaponAnchor(anchors,slot.assignment.slot);
        if(!anchor||out.count==out.instances.size())return {};
        const auto pose=BodyEquipmentPose(input,eyeBase,*anchor,p->modelToAnchor);if(!pose)return {};
        const auto& c=configs[i]->configured;auto& v=out.instances[out.count];
        v.geometry=MakeBodyAmmoGeometryKey(p->asset,p->mesh,p->part,p->rig);v.world=*pose;
        v.observedNs=std::max(d->observedNs,c.observedNs);v.deadlineNs=std::min(d->deadlineNs,c.deadlineNs);
        v.actorGeneration=d->physicalOwner.actorGeneration;v.equipmentGeneration=graphics::BodyPropWireEpoch(graphics::BodyPropSourceKind::Carried,d->cohort,i);v.spaceGeneration=d->physicalOwner.space;
        out.configured[out.count]=configs[i];out.slotIndices[out.count++]=i;
    }
    return BodyCarriedBatchFresh(out,now)?std::optional(out):std::nullopt;
}
// Whole new batch is all-or-none. Existing ammo/selected-hide props are not
// affected. Intersect originals; a newer source never extends the first eye.
inline bool AppendBodyCarriedHostInstances(const BodyCarriedRenderBatch& before,const BodyCarriedRenderBatch& current,
    std::int64_t now,std::uint64_t space,graphics::BodyPropEye& eye)noexcept {
    if(!BodyCarriedBatchFresh(before,now)||!BodyCarriedBatchFresh(current,now)||before.count!=current.count||
       before.inventory->cohort!=current.inventory->cohort||!SameBodyInventoryDisplayCohort(*before.inventory,*current.inventory)||
       space!=before.inventory->physicalOwner.space||eye.count>graphics::MaxBodyProps||before.count>graphics::MaxBodyProps-eye.count)return false;
    auto next=eye;
    for(unsigned n=0;n<before.count;++n){
        if(before.slotIndices[n]!=current.slotIndices[n]||!body_carried_detail::SameConfig(*before.configured[n],*current.configured[n])||
           before.instances[n].geometry!=current.instances[n].geometry)return false;
        auto v=before.instances[n];v.observedNs=std::max(v.observedNs,current.instances[n].observedNs);
        v.deadlineNs=std::min(v.deadlineNs,current.instances[n].deadlineNs);
        if(!graphics::BodyPropFresh(v,now))return false;next.instances[next.count++]=v;
    }
    eye=next;return true;
}
}
