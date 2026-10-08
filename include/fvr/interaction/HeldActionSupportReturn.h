#pragma once
#include "fvr/interaction/SupportGrip.h"
#include <limits>
namespace fvr::interaction {
// Presentation custody only. The adapter arms this after an accepted physical
// request releases its item, and settles it after verifying that SAME outcome.
// It cannot grant ammunition, native readiness, geometry or a renewed lease.
class HeldActionSupportReturn {
public:
 void Reset()noexcept {request_=0;readyNs_=0;}
 void CancelUnlessSettled()noexcept {if(!readyNs_)Reset();}
 void Arm(std::uint64_t request,const HandInteractionSample& current,const HandClaim& gun)noexcept {
  Reset();if(!request||!Safe(current)||gun.token.kind!=HandClaimKind::GunHold||gun.token.hand!=InteractionHand::Right||
   gun.token.owner!=current.owner||gun.inputSequence!=current.sequence||gun.deadlineNs<=current.nowNs)return;
  request_=request;gun_=gun.token;last_=current;submittedNs_=current.nowNs;submittedSequence_=current.sequence;
 }
 void Observe(const HandInteractionSample& current,const std::optional<HandClaim>& gun,bool invalid,bool occupied)noexcept {
  if(!request_)return;
  if(invalid||occupied||!Safe(current)||!gun||gun->token!=gun_||gun->inputSequence!=current.sequence||gun->deadlineNs<=current.nowNs||
   current.owner!=last_.owner||current.sequence<last_.sequence||current.observedNs<last_.observedNs||current.nowNs>=last_.deadlineNs||
   (readyNs_&&current.nowNs-readyNs_>1500000000)){Reset();return;}
  last_=current;
 }
 void Complete(std::uint64_t request,std::int64_t observedNs)noexcept {
  if(request_&&request==request_&&observedNs>=submittedNs_&&observedNs<=last_.nowNs)readyNs_=observedNs;
 }
 std::optional<SupportGripResult> Continue(const SupportGripOwner& owner,const InputFrame& input,
  const SupportGripContact& contact,const HandInteractionSample& original,HandInteractionKey contactKey,
  HandInteraction& hands,SupportGrip& support,std::uint64_t& intent,bool cancel)noexcept {
  if(!request_)return {};
  if(cancel){Reset();return {};}
  if(!readyNs_||!Safe(last_)||last_.nowNs<readyNs_||last_.nowNs-readyNs_>1500000000)return {};
  if(owner.equipped!=gun_.item.id||contactKey!=HandInteractionKey{2,gun_.item.id}||input.generation!=last_.sequence||
   original.owner!=last_.owner||original.sequence<=submittedSequence_||original.sequence>last_.sequence||
   original.observedNs<readyNs_||original.observedNs>last_.observedNs||!SafeAt(original,last_.nowNs)||
   !contact.valid||!std::isfinite(contact.distanceMeters)||contact.distanceMeters<0||contact.distanceMeters>.16f)return {};
  const auto gun=hands.Current(InteractionHand::Right);
  if(!gun||gun->token!=gun_||hands.Current(InteractionHand::Left))return {};
  Reset();if(intent==std::numeric_limits<std::uint64_t>::max())return {};
  const HandClaimRequest request{last_.owner,InteractionHand::Left,HandClaimKind::WeaponSupport,gun_.item,
   {contactKey,original.sequence,original.deadlineNs,true},++intent,gun_.id};
  const auto acquired=hands.AcquireFrom(last_,original,request);if(!acquired.claim)return {};
  auto proposed=support;const auto result=proposed.AdoptHeld(owner,input,contact,last_,original,*acquired.claim,*gun);
  if(!result.holding||!result.engaged){hands.Release(last_,acquired.claim->token);return {};}
  support=proposed;return result;
 }
private:
 static bool SafeAt(const HandInteractionSample& s,std::int64_t now)noexcept {
  return s.sequence&&s.focused&&s.tracked[0]&&s.tracked[1]&&!s.released[0]&&!s.released[1]&&
   s.observedNs>0&&s.observedNs<=now&&now<s.deadlineNs&&s.deadlineNs-s.observedNs<=200000000;
 }
 static bool Safe(const HandInteractionSample& s)noexcept{return SafeAt(s,s.nowNs);}
 std::uint64_t request_=0,submittedSequence_=0;
 std::int64_t submittedNs_=0,readyNs_=0;
 HandClaimToken gun_{};HandInteractionSample last_{};
};
}
