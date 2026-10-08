#pragma once
#include "Bc2BodyHolster.h"
#include "Bc2BodyAmmoRenderer.h"
#include "Bc2BodyEquipmentProfiles.h"
#include "fvr/graphics/BodyPropFrame.h"
#include "fvr/interaction/BodyAnchors.h"
#include <algorithm>

namespace fvr::bc2 {
// Visual copy of the CURRENT selected, actually hidden weapon only. The slot
// remains owned by BodyInventory; this source grants no item, draw or ammo.
struct BodyHolsteredRenderSource {
    graphics::BodyPropInstance prop{};
    std::shared_ptr<const BodyFreeRightEvidence> freeRight;
    interaction::BodySlotAssignment slot{};
    WeaponEquipmentIdentity equipment{};
    std::uint64_t spaceGeneration=0;
};
namespace body_equipment_detail {
template<std::size_t N> inline std::string_view Text(const std::array<char,N>& a)noexcept {
    const auto end=std::find(a.begin(),a.end(),'\0');
    return end==a.end()?std::string_view{}:std::string_view(a.data(),std::size_t(end-a.begin()));
}
// Exact data rows bind the complete configured mesh set; geometry eligibility
// alone grants no ownership, native hide, or draw receipt.
template<class Configuration> inline bool ProfileMatchesSelected(const BodyEquipmentProfile& p,const Configuration& s)noexcept {
    if(!p.asset||!p.mesh||!p.part||!p.rig||Text(s.weaponName)!=p.asset||s.stateCount!=1||s.states[0].count>8)return false;
    if(p.configurationPath){
        if(!*p.configurationPath||!s.configurationPathVerified||Text(s.configurationPath)!=p.configurationPath||
           p.configuredMeshes.empty()||p.configuredMeshes.size()>8||s.states[0].count!=p.configuredMeshes.size()||
           s.stateTypeInfo<0x10000||s.meshTypeInfo<0x10000||s.soleConfiguredArray<0x10000||s.states[0].array!=s.soleConfiguredArray)return false;
        for(const auto& wanted:p.configuredMeshes){
            if(wanted.empty()||std::count(p.configuredMeshes.begin(),p.configuredMeshes.end(),wanted)!=1)return false;
            unsigned matches=0;for(unsigned n=0;n<s.states[0].count;++n){const auto& m=s.states[0].meshes[n];
                if(m.address<0x10000||m.namePointer<0x10000||m.typeInfo!=s.meshTypeInfo)return false;
                for(unsigned k=0;k<n;++k)if(m.address==s.states[0].meshes[k].address)return false;
                if(Text(m.assetPath)==wanted)++matches;
            }if(matches!=1)return false;
        }
    }else if(!p.configuredMeshes.empty())return false;
    unsigned base=0;for(unsigned n=0;n<s.states[0].count;++n)if(Text(s.states[0].meshes[n].assetPath)==p.mesh)++base;
    return base==1;
}
inline const BodyEquipmentProfile* Profile(const BodyFreeRightEvidence& p,const WeaponEquipmentIdentity& e,
    std::int64_t now)noexcept {
    const auto& selected=p.visibility.selected;
    if(!selected||selected->owner!=p.visibility.nativeOwner||selected->weaponData!=e.data||e.weapon!=selected->owner.weapon||
       Text(selected->weaponName)!=e.Asset()||selected->observedNs<=0||selected->observedNs>now||selected->deadlineNs<=now||
       selected->stateCount!=1||selected->states[0].count>8)return nullptr;
    const BodyEquipmentProfile* found=nullptr;
    for(const auto& profile:BodyEquipmentProfiles){
        if(!profile.asset||!profile.mesh||!profile.part||!profile.rig||e.Asset()!=profile.asset||
           !interaction::reload_insertion_detail::Rigid(profile.modelToAnchor))continue;
        if(!ProfileMatchesSelected(profile,*selected))continue;
        if(found)return nullptr;
        found=&profile;
    }
    return found;
}
inline std::optional<std::pair<std::int64_t,std::int64_t>> Lease(const BodyFreeRightEvidence& p,std::int64_t now)noexcept {
    if(!BodyFreeRightRenderCurrent(p,&p,&p.visibility,now)||!p.visibility.selected||!p.receipt.evidence||
       !p.receipt.evidence->selected)return {};
    const auto& v=p.visibility;const auto& r=p.receipt;const auto& plan=*r.evidence;
    const auto observed=std::max({p.input.observedNs,v.input.observedNs,v.selected->observedNs,r.observedNs,
        plan.observedNs,plan.selected->observedNs,p.suppression.input.observedNs});
    auto deadline=std::min({p.input.deadlineNs,v.input.deadlineNs,v.selected->deadlineNs,r.deadlineNs,
        plan.deadlineNs,plan.inputDeadlineNs,plan.selected->deadlineNs,p.suppression.input.deadlineNs});
    if(p.authorizationDeadlineNs)deadline=std::min(deadline,p.authorizationDeadlineNs);
    if(v.authorizationDeadlineNs)deadline=std::min(deadline,v.authorizationDeadlineNs);
    if(observed<=0||observed>now||deadline<=now||deadline<=observed||deadline-observed>100000000)return {};
    return std::pair{observed,deadline};
}
}
// Same upright recentered-body transform as BodyAnchorHandPose and the body
// ammo anchor. Looking around changes neither shoulder; recenter uses only the
// NEW input reference. Both closed mesh vertices and translations are metres.
inline std::optional<math::Matrix4> BodyEquipmentPose(const interaction::InputFrame& input,
    const math::Matrix4& eyeBase,const interaction::BodyAnchor& shoulder,const math::Matrix4& modelToAnchor)noexcept {
    using namespace interaction;
    if(!ValidInput(input)||!input.focused||!input.headValid||!ValidBodyAnchor(shoulder)||
       !reload_insertion_detail::Rigid(eyeBase)||!reload_insertion_detail::Rigid(modelToAnchor))return {};
    auto body=UprightReference(input.referenceHead);if(!body)return {};body->position=input.head.position;
    const auto relative=math::MakeRelativePose(input.referenceHead,*body);if(!relative)return {};
    auto scaled=*relative;scaled.position.x*=input.worldUnitsPerMeter;scaled.position.y*=input.worldUnitsPerMeter;scaled.position.z*=input.worldUnitsPerMeter;
    const auto view=math::MakeLhViewFromOpenXRPose(scaled);const auto frame=view?InverseRigid(*view):std::nullopt;if(!frame)return {};
    auto anchor=reload_insertion_detail::Identity();anchor.values[3]={shoulder.center.x*input.worldUnitsPerMeter,
        shoulder.center.y*input.worldUnitsPerMeter,shoulder.center.z*input.worldUnitsPerMeter,1};
    auto model=modelToAnchor;
    for(unsigned row=0;row<4;++row)for(unsigned col=0;col<3;++col)model.values[row][col]*=input.worldUnitsPerMeter;
    const auto world=Multiply(Multiply(Multiply(model,anchor),*frame),eyeBase);
    return graphics::RigidPropWorldValid(world)?std::optional{world}:std::nullopt;
}
inline bool BodyHolsteredSourceFresh(const BodyHolsteredRenderSource& s,std::int64_t now)noexcept {
    if(!s.freeRight||!s.slot.item.generation||s.slot.item.id!=s.equipment.weapon||!s.slot.slot||
       s.spaceGeneration!=s.freeRight->input.owner.space||s.prop.spaceGeneration!=s.spaceGeneration||
       s.prop.actorGeneration!=s.freeRight->input.owner.actorGeneration||
       s.prop.equipmentGeneration!=graphics::BodyPropWireEpoch(graphics::BodyPropSourceKind::SelectedHolster,s.freeRight->input.owner.equipGeneration)||!graphics::BodyPropFresh(s.prop,now))return false;
    const auto lease=body_equipment_detail::Lease(*s.freeRight,now);
    const auto* profile=body_equipment_detail::Profile(*s.freeRight,s.equipment,now);
    return lease&&profile&&s.prop.observedNs==lease->first&&s.prop.deadlineNs<=lease->second&&
        s.prop.geometry==MakeBodyAmmoGeometryKey(profile->asset,profile->mesh,profile->part,profile->rig);
}
inline std::optional<BodyHolsteredRenderSource> BuildBodyHolsteredSource(std::shared_ptr<const BodyFreeRightEvidence> free,
    const interaction::BodySlotAssignment& slot,const WeaponEquipmentIdentity& equipment,const interaction::InputFrame& input,
    const math::Matrix4& eyeBase,const interaction::BodyAnchorConfig& anchors,std::int64_t now)noexcept {
    if(!free||!interaction::ValidBodyAnchors(anchors)||input.generation!=free->input.sequence||
       input.spaceGeneration!=free->input.owner.space)return {};
    const auto* profile=body_equipment_detail::Profile(*free,equipment,now);
    const auto lease=body_equipment_detail::Lease(*free,now);if(!profile||!lease)return {};
    const auto anchor=std::find_if(anchors.shoulders.begin(),anchors.shoulders.end(),[&](const auto& a){return a.slot==slot.slot;});
    if(anchor==anchors.shoulders.end())return {};
    const auto pose=BodyEquipmentPose(input,eyeBase,*anchor,profile->modelToAnchor);if(!pose)return {};
    BodyHolsteredRenderSource out{{MakeBodyAmmoGeometryKey(profile->asset,profile->mesh,profile->part,profile->rig),
        *pose,lease->first,lease->second},std::move(free),slot,equipment,input.spaceGeneration};
    out.prop.actorGeneration=out.freeRight->input.owner.actorGeneration;
    out.prop.equipmentGeneration=graphics::BodyPropWireEpoch(graphics::BodyPropSourceKind::SelectedHolster,out.freeRight->input.owner.equipGeneration);
    out.prop.spaceGeneration=out.spaceGeneration;
    return BodyHolsteredSourceFresh(out,now)?std::optional{out}:std::nullopt;
}
inline std::optional<graphics::BodyPropInstance> BodyHolsteredHostInstance(const BodyHolsteredRenderSource& before,
    const BodyHolsteredRenderSource& current,std::int64_t now)noexcept {
    if(!BodyHolsteredSourceFresh(before,now)||!BodyHolsteredSourceFresh(current,now)||
       before.slot!=current.slot||before.equipment!=current.equipment||before.spaceGeneration!=current.spaceGeneration||
       before.prop.geometry!=current.prop.geometry||
       !BodyFreeRightRenderCurrent(*before.freeRight,current.freeRight.get(),&current.freeRight->visibility,now))return {};
    auto out=before.prop;out.deadlineNs=std::min(out.deadlineNs,current.prop.deadlineNs);
    out.observedNs=std::max(out.observedNs,current.prop.observedNs);
    return graphics::BodyPropFresh(out,now)?std::optional{out}:std::nullopt;
}
} // namespace fvr::bc2
