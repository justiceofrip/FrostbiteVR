#include "Bc2MagazineDetachGate.h"
#include <limits>
namespace fvr::bc2 {
using namespace interaction;
bool Bc2MagazineDetachGate::Sample(const MagazineDetachGateSample& s)const noexcept {
 const auto& r=s.reserve;const auto& i=s.input;const auto& g=s.gun;
 return BindMagazineOwners(i.owner,s.family.binding.weapon,r,0,i.nowNs,s.family).has_value()&&r.allThreeIdle&&
  r.loaded>=0&&r.loaded<=r.capacity&&r.reserve>=0&&s.nativeTick&&s.cache>=0x10000&&i.focused&&i.tracked[0]&&i.tracked[1]&&!i.released[1]&&
  i.sequence&&i.observedNs>0&&i.observedNs<=i.nowNs&&i.deadlineNs>i.nowNs&&i.deadlineNs-i.observedNs<=100000000&&
  s.selected&&MagazineSelected(*s.selected,r.identity.owner,s.family.binding.profile->geometry->asset,*s.family.binding.profile,i.nowNs)&&
  (!s.family.carried||
   s.selected->weaponData==s.family.binding.equipment.data)&&
  g.token.id&&g.token.owner==i.owner&&g.token.item==s.family.binding.weapon&&g.token.hand==InteractionHand::Right&&
  g.token.kind==HandClaimKind::GunHold&&!g.token.prerequisiteClaim&&g.inputSequence==i.sequence&&g.deadlineNs>i.nowNs;
}
bool Bc2MagazineDetachGate::Current(const MagazineDetachGateSample& s,bool counts)const noexcept {
 return Sample(s)&&s.family.binding==binding_&&original_&&s.input.owner==original_->owner&&s.family.binding.weapon==original_->weapon&&
  s.reserve.identity==baseline_.identity&&s.reserve.sequence>=baseline_.sequence&&s.reserve.observedNs>=baseline_.observedNs&&
  (!counts||(s.reserve.loaded==baseline_.loaded&&s.reserve.reserve==baseline_.reserve&&s.reserve.capacity==baseline_.capacity));
}
bool Bc2MagazineDetachGate::Begin(const MagazineDetachGateSample& s)noexcept {
 if(BlocksActions()||next_==std::numeric_limits<std::uint64_t>::max()||!Sample(s)||!s.nativeMagazineAttached||s.reserve.loaded<=0||
    (s.reserve.loaded<s.reserve.capacity&&s.reserve.reserve>0))return false;
 request_=++next_;began_=s.input.nowNs;baseline_=s.reserve;binding_=s.family.binding;const auto p=binding_.profile->geometry->interaction.insertion;
 original_=OriginalMagazine{s.input.owner,s.family.binding.weapon,{0x424332434d4147ull,request_},{p.id,p.revision},
  {s.reserve.identity.serverItem,s.reserve.identity.owner.equipGeneration},s.input.owner.space,s.reserve.sequence,
  s.reserve.observedNs,s.reserve.deadlineNs,unsigned(s.reserve.loaded),unsigned(s.reserve.capacity)};
 recoveryInput_=s.input;authorization_.reset();phase_=MagazineDetachPhase::Suppressing;return true;
}
std::optional<HolsterSuppressionRequest> Bc2MagazineDetachGate::Demand(const MagazineDetachGateSample& s)const noexcept {
 if(!BlocksActions()||!Current(s,phase_!=MagazineDetachPhase::Recovering))return {};
 return HolsterSuppressionRequest{s.reserve.identity.owner,s.input,request_,s.nativeTick,s.cache};
}
bool Bc2MagazineDetachGate::Commit(const MagazineDetachGateSample& s,const HolsterSuppressionReceipt& receipt,std::int64_t committedNs)noexcept {
 const auto request=Demand(s);
 if(!request||!HolsterSuppressionCurrent(receipt,*request)||s.input.nowNs<began_||
   (phase_!=MagazineDetachPhase::Recovering&&s.input.nowNs-began_>=30000000000ll)){Invalidate();return false;}
 MagazineDetachAuthorization candidate{*original_,baseline_,s.reserve,receipt,request_,committedNs,
  phase_==MagazineDetachPhase::Returning||phase_==MagazineDetachPhase::Recovering};
 if(!MagazineDetachAuthorizationFresh(candidate,s.input.nowNs)){Invalidate();return false;}
 recoveryInput_=s.input;authorization_=candidate;if(phase_==MagazineDetachPhase::Suppressing)phase_=MagazineDetachPhase::Detached;return true;
}
bool Bc2MagazineDetachGate::Return(const MagazineDetachGateSample& s,const ReloadInsertionSeat& seat)noexcept {
 if(phase_!=MagazineDetachPhase::Detached||!Current(s,true)||!authorization_||
  !MagazineDetachAuthorizationFresh(*authorization_,s.input.nowNs)||!seat.id||!seat.inputSequence||seat.inputSequence>s.input.sequence||
  seat.identity!=ReloadInsertionIdentity{original_->owner,original_->weapon,original_->item,original_->trackingEpoch}||
  seat.profile!=original_->profile||seat.operation!=ReloadOperation::SeatMagazine||
  seat.weaponClaim!=s.gun.token||seat.itemClaim.kind!=HandClaimKind::Mechanism||seat.itemClaim.owner!=s.input.owner||
  seat.itemClaim.item!=original_->weapon||seat.itemClaim.hand!=InteractionHand::Left||seat.itemClaim.prerequisiteClaim!=s.gun.token.id)return false;
 returnNs_=s.input.nowNs;phase_=MagazineDetachPhase::Returning;authorization_.reset();return true;
}
bool Bc2MagazineDetachGate::Finish(const MagazineDetachGateSample& s,const MagazineDetachPairReceipt& pair)noexcept {
 if((phase_!=MagazineDetachPhase::Returning&&phase_!=MagazineDetachPhase::Recovering)||!Current(s,phase_==MagazineDetachPhase::Returning)||
  !s.nativeMagazineAttached||!authorization_||!MagazineDetachPairCurrent(pair,*authorization_,true,s.input.nowNs)||
  pair.observedNs<returnNs_||s.reserve.observedNs<pair.observedNs)return false;
 if(phase_==MagazineDetachPhase::Returning)++returned_;else ++recovered_;
 phase_=MagazineDetachPhase::Idle;authorization_.reset();original_.reset();return true;
}
void Bc2MagazineDetachGate::Invalidate()noexcept {
 if(!BlocksActions())return;phase_=MagazineDetachPhase::Recovering;authorization_.reset();returnNs_=began_;
}
std::optional<HolsterSuppressionRequest> Bc2MagazineDetachGate::RecoveryDemand(const ReloadStateOwner& owner,
 const HandInteractionSample& current,std::uint64_t nativeTick,std::uint32_t cache)const noexcept {
 if(phase_!=MagazineDetachPhase::Recovering||!original_||owner!=baseline_.identity.owner||current.owner!=original_->owner||
  !nativeTick||cache<0x10000||current.nowNs<recoveryInput_.nowNs)return {};
 auto safety=recoveryInput_;safety.nowNs=current.nowNs;safety.focused=current.focused;safety.tracked=current.tracked;
 // Deliberately retain sequence, observed and deadline from the last genuine
 // input evidence. HolsterSuppressionCurrent rejects expired/untracked receipts.
 return HolsterSuppressionRequest{owner,safety,request_,nativeTick,cache};
}
std::optional<HolsterSuppressionRequest> Bc2MagazineDetachGate::ReplacementDemand(const ReloadStateOwner& owner,
 const HandInteractionSample& input,std::uint64_t nativeTick,std::uint32_t cache)const noexcept {
 if(!BlocksActions()||owner==baseline_.identity.owner||!nativeTick||cache<0x10000||owner.player<0x10000||owner.soldier<0x10000||
  owner.weak<0x10000||owner.weapon<0x10000||!owner.actorGeneration||!owner.equipGeneration||!owner.space||
  input.owner.actor!=((std::uint64_t(owner.weak)<<32)|owner.soldier)||input.owner.actorGeneration!=owner.actorGeneration||
  input.owner.space!=owner.space||!input.owner.equipGeneration||!input.sequence||!input.focused||!input.tracked[1]||
  input.observedNs<=0||input.observedNs>input.nowNs||input.deadlineNs<=input.nowNs||input.deadlineNs-input.observedNs>100000000)return {};
 return HolsterSuppressionRequest{owner,input,request_,nativeTick,cache};
}
bool Bc2MagazineDetachGate::RetireReplacedOwner(const HolsterSuppressionRequest& current,const HolsterSuppressionReceipt& receipt)noexcept {
 // No native operation was started. A fresh different exact owner cannot use
 // the old private pose (all draw guards require that owner); no old counts
 // are restored or copied to it. Release only this old presentation capability.
 const auto expected=ReplacementDemand(current.owner,current.input,current.nativeTick,current.cache);
 if(!expected||current.request!=request_||!HolsterSuppressionCurrent(receipt,*expected))return false;
 phase_=MagazineDetachPhase::Idle;authorization_.reset();original_.reset();++recovered_;return true;
}
}
