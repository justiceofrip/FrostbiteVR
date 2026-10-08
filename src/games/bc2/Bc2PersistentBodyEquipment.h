#pragma once
#include "Bc2BodyEquipment.h"
#include "Bc2BodyInventory.h"

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
    for(const auto& p:BodyEquipmentProfiles)if(asset==p.asset){
        unsigned matches=0;
        for(unsigned n=0;n<s.states[0].count;++n)if(body_equipment_detail::Text(s.states[0].meshes[n].assetPath)==p.mesh)++matches;
        if(matches!=1||found||!interaction::reload_insertion_detail::Rigid(p.modelToAnchor))return nullptr;found=&p;
    }return found;
}
inline bool SameConfig(const CarriedMeshesSnapshot& a,const CarriedMeshesSnapshot& b)noexcept {
    const auto& x=a.configured;const auto& y=b.configured;
    return a.weapon==b.weapon&&a.nativeSlot==b.nativeSlot&&a.persistence==b.persistence&&x.owner==y.owner&&
        x.weaponData==y.weaponData&&x.inventory==y.inventory&&x.selectedSlot==y.selectedSlot&&x.weaponName==y.weaponName&&
        x.stateTypeInfo==y.stateTypeInfo&&x.meshTypeInfo==y.meshTypeInfo&&x.stateCount==y.stateCount&&
        x.soleConfiguredArray==y.soleConfiguredArray&&x.states==y.states;
}
}
inline bool BodyCarriedBatchFresh(const BodyCarriedRenderBatch& b,std::int64_t now)noexcept {
    if(!b.inventory||!BodyInventoryDisplayFresh(*b.inventory,now)||b.count>b.instances.size())return false;
    for(unsigned n=0;n<b.count;++n){const auto idx=b.slotIndices[n];
        if(idx>=b.inventory->count||!b.configured[n])return false;
        const auto* p=body_carried_detail::Profile(*b.inventory,b.inventory->slots[idx],*b.configured[n],now);
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
    if(!d||!BodyInventoryDisplayFresh(*d,now)||input.generation!=d->sequence||input.spaceGeneration!=d->physicalOwner.space||
       !interaction::ValidInput(input)||!input.focused||!input.headValid||!interaction::ValidBodyAnchors(anchors))return {};
    BodyCarriedRenderBatch out;out.inventory=d;
    for(unsigned i=0;i<d->count;++i){const auto& slot=d->slots[i];
        if(slot.native.weapon==d->selectedOwner.weapon||slot.assignment.item==d->physicalSelected)continue;
        // Unknown/unbound geometry is omitted; a known bound row with stale
        // source rejects this complete batch, never a per-eye partial fallback.
        if(!configs[i])continue;
        const auto* p=body_carried_detail::Profile(*d,slot,*configs[i],now);if(!p)return {};
        const auto anchor=std::find_if(anchors.shoulders.begin(),anchors.shoulders.end(),[&](const auto& a){return a.slot==slot.assignment.slot;});
        if(anchor==anchors.shoulders.end()||out.count==out.instances.size())return {};
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
