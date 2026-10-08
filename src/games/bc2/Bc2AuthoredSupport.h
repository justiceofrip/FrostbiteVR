#pragma once
#include "Bc2AuthoredGrip.h"
namespace fvr::bc2 {
// Support-only data cannot override the gun/right-hand transform or acquire a claim.
struct AuthoredSupportProfile {
 std::string_view assetName,meshPath,bindingDigest,clipSha256,skeletonSha256;
 std::uint64_t rigFingerprint=0;math::Matrix4 leftInWeapon{};
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
     !authored_grip_detail::Matrix(p.leftInWeapon))return nullptr;
  if(selected||p.rigFingerprint!=rigFingerprint)return nullptr;
  selected=&p;
 }
 return selected;
}
}
