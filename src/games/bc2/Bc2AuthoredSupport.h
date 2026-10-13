#pragma once
#include "Bc2AuthoredGrip.h"
#include "fvr/interaction/SupportGrip.h"
namespace fvr::bc2 {
// Support-only data cannot override the gun/right-hand transform or acquire a claim.
struct AuthoredSupportProfile {
 std::string_view assetName,meshPath,bindingDigest,clipSha256,skeletonSha256;
 std::uint64_t rigFingerprint=0;math::Matrix4 leftInWeapon{};
 // Exact compiled contact reference only; no right-hand, aim or ammo authority.
 bool contactReference=false;
 interaction::SupportAimMode aimMode=interaction::SupportAimMode::TwoPoint;
};
struct AuthoredSupportBinding {
 const AuthoredSupportProfile* profile=nullptr;
 std::shared_ptr<const SelectedMeshesSnapshot> selected;
 WeaponEquipmentIdentity equipment{};AuthoredGripOwner owner{};
};
inline const AuthoredSupportProfile* BindAuthoredSupport(std::span<const AuthoredSupportProfile> profiles,
 const SelectedMeshesSnapshot& snapshot,const WeaponEquipmentIdentity& equipment,
 const AuthoredGripOwner& owner,std::uint64_t rigFingerprint,std::int64_t now)noexcept {
 if(!equipment.weapon||!equipment.data||!equipment.persistence||!owner.soldier||!owner.weak||
    !owner.actorGeneration||!owner.equipmentGeneration||!owner.space||!owner.inputSequence||
    !snapshot.owner.player||!snapshot.owner.equipGeneration||
    !authored_grip_detail::Fresh(snapshot,now)||owner.inputDeadlineNs<=now||snapshot.deadlineNs<owner.inputDeadlineNs||snapshot.sequence>owner.inputSequence||
    snapshot.owner.weapon!=equipment.weapon||snapshot.weaponData!=equipment.data||snapshot.owner.soldier!=owner.soldier||
    snapshot.owner.weak!=owner.weak||snapshot.owner.actorGeneration!=owner.actorGeneration||
    snapshot.owner.space!=owner.space||
    equipment.Asset().empty()||authored_grip_detail::Text(snapshot.weaponName)!=equipment.Asset())return nullptr;
 const AuthoredSupportProfile* selected=nullptr;
 for(const auto& p:profiles){
  if(p.assetName!=equipment.Asset())continue;
  AuthoredGripProfile geometry;geometry.meshPath=p.meshPath;
  if(!authored_grip_detail::Configuration(geometry,snapshot))continue;
  if(!authored_grip_detail::Digest(p.bindingDigest)||
     !authored_grip_detail::Digest(p.clipSha256)||!authored_grip_detail::Digest(p.skeletonSha256)||
     !authored_grip_detail::Matrix(p.leftInWeapon)||
     (p.aimMode!=interaction::SupportAimMode::TwoPoint&&p.aimMode!=interaction::SupportAimMode::PrimaryHand))return nullptr;
  if(selected||!p.rigFingerprint||p.rigFingerprint!=rigFingerprint)return nullptr;
  selected=&p;
 }
 return selected;
}
inline std::optional<AuthoredSupportBinding> BindAuthoredSupportReference(
 std::span<const AuthoredSupportProfile> profiles,std::shared_ptr<const SelectedMeshesSnapshot> snapshot,
 const WeaponEquipmentIdentity& equipment,const AuthoredGripOwner& owner,
 std::uint64_t rigFingerprint,std::int64_t now)noexcept {
 if(!snapshot)return {};
 const auto* p=BindAuthoredSupport(profiles,*snapshot,equipment,owner,rigFingerprint,now);
 if(!p)return {};
 return AuthoredSupportBinding{p,std::move(snapshot),equipment,owner};
}
// An independently refreshed selected-mesh view may confirm the source but
// cannot extend the original rendered pose's input or configuration deadline.
inline bool AuthoredSupportCurrent(const AuthoredSupportBinding& b,const SelectedMeshesSnapshot* current,
 const WeaponEquipmentIdentity& equipment,const AuthoredGripOwner& owner,std::int64_t now)noexcept {
 if(!b.profile||!b.selected||!current||b.equipment!=equipment||
    b.owner.soldier!=owner.soldier||b.owner.weak!=owner.weak||
    b.owner.actorGeneration!=owner.actorGeneration||b.owner.equipmentGeneration!=owner.equipmentGeneration||
    b.owner.space!=owner.space||b.owner.inputSequence>owner.inputSequence||
    b.owner.inputDeadlineNs<=now||owner.inputDeadlineNs<=now)return false;
 const auto& old=*b.selected;
 return authored_grip_detail::Fresh(old,now)&&authored_grip_detail::Fresh(*current,now)&&
    BindAuthoredSupport(std::span(b.profile,1),old,b.equipment,b.owner,b.profile->rigFingerprint,now)==b.profile&&
    current->owner==old.owner&&current->sequence>=old.sequence&&current->observedNs>=old.observedNs&&
    current->weaponData==old.weaponData&&current->inventory==old.inventory&&current->selectedSlot==old.selectedSlot&&
    current->stateTypeInfo==old.stateTypeInfo&&current->meshTypeInfo==old.meshTypeInfo&&
    current->weaponName==old.weaponName&&current->stateCount==old.stateCount&&
    current->soleConfiguredArray==old.soleConfiguredArray&&current->states==old.states;
}
// The caller first validates the immutable binding at its output boundary.
inline bool AuthoredSupportContact(const std::optional<AuthoredSupportBinding>& b)noexcept {
 return b&&b->profile&&b->selected&&b->profile->contactReference;
}
}
