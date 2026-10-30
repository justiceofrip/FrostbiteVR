#pragma once
#include "Bc2AuthoredGrip.h"

namespace fvr::bc2 {
// Explicit reviewed visual axes, independent of either hand's attachment.
// No firing, recoil, spread, muzzle-origin or interaction capability is granted.
struct AuthoredAimProfile {
    std::string_view assetName,configurationPath,meshPath,bindingDigest,
        meshSha256,skeletonSha256,authoredAxisEvidence;
    std::uint64_t rigFingerprint=0;
    math::Vec3 modelForward{},modelUp{};
};
using AuthoredAimOwner=AuthoredGripOwner;
enum class AuthoredAimStatus : unsigned {
    NoProfile,MissingSnapshot,InvalidProfile,IdentityMismatch,Expired,
    AmbiguousConfiguration,RigMismatch,Bound,Count
};
struct AuthoredAimBinding {
    const AuthoredAimProfile* profile=nullptr; // immutable compiled registry
    std::shared_ptr<const SelectedMeshesSnapshot> selected;
    WeaponEquipmentIdentity equipment{};
    AuthoredAimOwner owner{};
    static constexpr bool nativeFireCapability=false,muzzleCapability=false,
        gripCapability=false,supportCapability=false,headsetAccepted=false;
};
struct AuthoredAimResult {
    AuthoredAimStatus status=AuthoredAimStatus::NoProfile;
    std::optional<AuthoredAimBinding> binding;
};
namespace authored_aim_detail {
inline bool Profile(const AuthoredAimProfile& p)noexcept {
    using namespace authored_grip_detail;
    return !p.assetName.empty()&&!p.configurationPath.empty()&&!p.meshPath.empty()&&
        p.rigFingerprint&&Digest(p.bindingDigest)&&Digest(p.meshSha256)&&
        Digest(p.skeletonSha256)&&Digest(p.authoredAxisEvidence)&&
        interaction::ValidModelAxes(p.modelForward,p.modelUp);
}
inline bool Configuration(const AuthoredAimProfile& p,const SelectedMeshesSnapshot& s)noexcept {
    if(!s.configurationPathVerified||!s.configurationPathPointer||
       authored_grip_detail::Text(s.configurationPath)!=p.configurationPath)return false;
    AuthoredGripProfile geometry;geometry.meshPath=p.meshPath;
    return authored_grip_detail::Configuration(geometry,s);
}
}
inline AuthoredAimResult BindAuthoredAim(std::span<const AuthoredAimProfile> profiles,
    std::shared_ptr<const SelectedMeshesSnapshot> snapshot,const WeaponEquipmentIdentity& equipment,
    const AuthoredAimOwner& owner,std::uint64_t rigFingerprint,std::int64_t now)noexcept {
    const AuthoredAimProfile* p=nullptr;bool foundAsset=false;
    for(const auto& entry:profiles)if(entry.assetName==equipment.Asset()){
        foundAsset=true;
        if(!authored_aim_detail::Profile(entry))return {AuthoredAimStatus::InvalidProfile,{}};
        if(snapshot&&authored_aim_detail::Configuration(entry,*snapshot)){
            if(p)return {AuthoredAimStatus::AmbiguousConfiguration,{}};
            p=&entry;
        }
    }
    if(!foundAsset)return {};
    if(!snapshot)return {AuthoredAimStatus::MissingSnapshot,{}};
    const auto& s=*snapshot;const auto& o=s.owner;
    if(!equipment.weapon||!equipment.data||!owner.soldier||!owner.weak||
       !owner.actorGeneration||!owner.equipmentGeneration||!owner.space||!owner.inputSequence||
       !o.player||!o.equipGeneration||o.soldier!=owner.soldier||o.weak!=owner.weak||
       o.weapon!=equipment.weapon||o.actorGeneration!=owner.actorGeneration||
       o.space!=owner.space||s.weaponData!=equipment.data||
       authored_grip_detail::Text(s.weaponName)!=equipment.Asset())return {AuthoredAimStatus::IdentityMismatch,{}};
    if(!authored_grip_detail::Fresh(s,now)||owner.inputDeadlineNs<=now||
       s.sequence>owner.inputSequence)return {AuthoredAimStatus::Expired,{}};
    if(!p){
        const auto configuration=authored_grip_detail::Text(s.configurationPath);
        const bool registered=std::any_of(profiles.begin(),profiles.end(),[&](const auto& entry){
            return entry.assetName==equipment.Asset()&&entry.configurationPath==configuration;
        });
        // An exact, fresh but unenrolled variant keeps its existing visual path.
        // Missing config proof or a known config with the wrong mesh still fails.
        if(s.configurationPathVerified&&s.configurationPathPointer&&!configuration.empty()&&!registered)return {};
        return {AuthoredAimStatus::AmbiguousConfiguration,{}};
    }
    if(rigFingerprint!=p->rigFingerprint)return {AuthoredAimStatus::RigMismatch,{}};
    return {AuthoredAimStatus::Bound,AuthoredAimBinding{p,std::move(snapshot),equipment,owner}};
}
// Recheck at publication and every consumer/Pack. A refreshed snapshot confirms
// identity only; it cannot renew either original lease or the original rig.
inline bool AuthoredAimCurrent(const AuthoredAimBinding& b,const SelectedMeshesSnapshot* current,
    const WeaponEquipmentIdentity& equipment,const AuthoredAimOwner& owner,
    std::uint64_t rigFingerprint,std::int64_t now)noexcept {
    if(!b.profile||!b.selected||!current||!authored_aim_detail::Profile(*b.profile)||
       b.profile->rigFingerprint!=rigFingerprint||b.equipment!=equipment||
       b.owner.soldier!=owner.soldier||b.owner.weak!=owner.weak||
       b.owner.actorGeneration!=owner.actorGeneration||b.owner.equipmentGeneration!=owner.equipmentGeneration||
       b.owner.space!=owner.space||b.owner.inputSequence>owner.inputSequence||
       b.owner.inputDeadlineNs<=now||owner.inputDeadlineNs<=now||current->sequence>owner.inputSequence)return false;
    const auto& old=*b.selected;
    return BindAuthoredAim(std::span(b.profile,1),b.selected,b.equipment,b.owner,rigFingerprint,now).binding.has_value()&&
        authored_grip_detail::Fresh(*current,now)&&authored_aim_detail::Configuration(*b.profile,*current)&&
        current->owner==old.owner&&current->sequence>=old.sequence&&current->observedNs>=old.observedNs&&
        current->weaponData==old.weaponData&&current->inventory==old.inventory&&current->selectedSlot==old.selectedSlot&&
        current->stateTypeInfo==old.stateTypeInfo&&current->meshTypeInfo==old.meshTypeInfo&&
        current->weaponName==old.weaponName&&current->stateCount==old.stateCount&&
        current->soleConfiguredArray==old.soleConfiguredArray&&current->states==old.states&&
        current->configurationPath==old.configurationPath&&
        current->configurationPathPointer==old.configurationPathPointer;
}
// Caller retains and validates the binding. Pure orientation output composes
// with the existing tracked rig, authored grip and support-corrected XR input.
inline std::optional<math::Matrix4> AuthoredWeaponAimFrame(const AuthoredAimBinding& b,
    const math::Matrix4& aim)noexcept {
    if(!b.profile||!b.selected||!authored_aim_detail::Profile(*b.profile))return {};
    return interaction::ModelAimFrame(b.profile->modelForward,b.profile->modelUp,aim);
}
} // namespace fvr::bc2
