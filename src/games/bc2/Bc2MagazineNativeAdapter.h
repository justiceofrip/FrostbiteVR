#pragma once
#include "Bc2MagazineNativeProfile.h"
#include "Bc2MagazineInteraction.h"
#include "Bc2SelectedCarriedWeapon.h"
#include <variant>

namespace fvr::bc2 {
using MagazineNativeIdentityEvidence=std::variant<MagazineFamilyEvidence,SelectedCarriedWeaponEvidence>;
enum class MagazineAdapterStatus:std::uint8_t {
 IdentityMismatch,EquipmentMismatch,NativeObservationUnavailable,CandidateDisabled,ConfigurationMismatch,Ready
};
struct MagazineAdapterAssessment {
 MagazineAdapterStatus status=MagazineAdapterStatus::IdentityMismatch;
 bool identityMatched=false;
 // This is configuration/identity eligibility, never native hold/seat/transfer
 // permission. Those remain the request policy's three-branch receipts.
 bool ConfigurationReady()const noexcept{return status==MagazineAdapterStatus::Ready;}
};
inline MagazineAdapterAssessment AssessMagazineNativeAdapter(const MagazineNativeProfile& profile,
 const MagazineNativeIdentityEvidence& evidence,const ReloadHoldInput& native,
 const interaction::HandInteractionOwner& physical,interaction::HandInteractionKey weapon,
 const WeaponEquipmentIdentity& currentEquipment,std::int64_t now)noexcept {
 const auto& owner=native.identity.owner;
 bool identity=false;
 if(weapon.id!=owner.weapon){
  const auto* linked=std::get_if<MagazineFamilyEvidence>(&evidence);
  identity=profile.identityRoute==MagazineIdentityRoute::LinkedLauncherAlias&&linked&&!linked->carried&&
   MagazineFamilyFresh(*linked,owner,physical,weapon,now);
  if(identity&&currentEquipment.persistence!=weapon.id)return {MagazineAdapterStatus::EquipmentMismatch,false};
 }else{
  const auto* carried=std::get_if<SelectedCarriedWeaponEvidence>(&evidence);
  if(const auto* family=std::get_if<MagazineFamilyEvidence>(&evidence)){
   if(family->carried&&MagazineFamilyFresh(*family,owner,physical,weapon,now))carried=&*family->carried;
  }
  identity=carried&&SelectedCarriedWeaponFresh(*carried,owner,physical,weapon,now);
  if(identity&&carried->equipment!=currentEquipment)return {MagazineAdapterStatus::EquipmentMismatch,false};
 }
 if(!identity)return {};
 if(currentEquipment.weapon!=owner.weapon||currentEquipment.data!=native.config.weaponData||
    currentEquipment.Asset()!=profile.configuration.assetName||
    !ReloadDescriptorText(native.config.assetName,currentEquipment.Asset()))
  return {MagazineAdapterStatus::EquipmentMismatch,true};
 if(!native.verified||native.nowNs<=0||native.nowNs>now||native.leaseDeadlineNs<=now||
    native.leaseDeadlineNs-native.nowNs>200000000)
  return {MagazineAdapterStatus::NativeObservationUnavailable,true};
 if(!profile.Reviewed())return {MagazineAdapterStatus::CandidateDisabled,true};
 if(!profile.Matches(native.config))return {MagazineAdapterStatus::ConfigurationMismatch,true};
 return {MagazineAdapterStatus::Ready,true};
}
} // namespace fvr::bc2
