#pragma once
#include "fvr/interaction/SupportGrip.h"
#include <limits>
#include <ostream>
namespace fvr::interaction {
// Presentation custody only. The adapter arms this after an accepted physical
// request releases its item, and settles it after verifying that SAME outcome.
// It cannot grant ammunition, native readiness, geometry or a renewed lease.
class HeldActionSupportReturn {
public:
 enum class Diagnostic:unsigned {Armed,Settled,Cancelled,RetainedSettled,Unsafe,Occupied,ChangedGun,ChangedOwner,ClockOrLease,Expired,ContinueCancelled,WaitingReceipt,WaitingContact,HandsUnavailable,AcquireRejected,AdoptRejected,Returned,ReceiptRejected};
 void Reset(Diagnostic why=Diagnostic::Cancelled,unsigned adapterReason=~0u,unsigned sourceFlags=0)noexcept {
  if(request_)Record(why,adapterReason,sourceFlags);request_=0;readyNs_=0;
 }
 void CancelUnlessSettled(unsigned adapterReason=~0u,unsigned sourceFlags=0)noexcept {
  if(!readyNs_)Reset(Diagnostic::Cancelled,adapterReason,sourceFlags);
  else Record(Diagnostic::RetainedSettled,adapterReason,sourceFlags);
 }
 void Arm(std::uint64_t request,const HandInteractionSample& current,const HandClaim& gun)noexcept {
  Reset();if(!request||!Safe(current)||gun.token.kind!=HandClaimKind::GunHold||gun.token.hand!=InteractionHand::Right||
   gun.token.owner!=current.owner||gun.inputSequence!=current.sequence||gun.deadlineNs<=current.nowNs)return;
  request_=request;gun_=gun.token;last_=current;submittedNs_=current.nowNs;submittedSequence_=current.sequence;Record(Diagnostic::Armed);
 }
 void Observe(const HandInteractionSample& current,const std::optional<HandClaim>& gun,bool invalid,bool occupied,unsigned sourceFlags=0)noexcept {
  if(!request_)return;
  if(invalid||occupied||!Safe(current)||!gun||gun->token!=gun_||gun->inputSequence!=current.sequence||gun->deadlineNs<=current.nowNs||
   current.owner!=last_.owner||current.sequence<last_.sequence||current.observedNs<last_.observedNs||current.nowNs>=last_.deadlineNs||
   (readyNs_&&current.nowNs-readyNs_>1500000000)){
   const auto why=invalid||!Safe(current)?Diagnostic::Unsafe:occupied?Diagnostic::Occupied:
    !gun||gun->token!=gun_?Diagnostic::ChangedGun:current.owner!=last_.owner?Diagnostic::ChangedOwner:
    readyNs_&&current.nowNs-readyNs_>1500000000?Diagnostic::Expired:Diagnostic::ClockOrLease;
   last_=current;Reset(why,~0u,sourceFlags);return;}
  last_=current;
 }
 void Complete(std::uint64_t request,std::int64_t observedNs)noexcept {
  if(request_&&request==request_&&observedNs>=submittedNs_&&observedNs<=last_.nowNs){readyNs_=observedNs;Record(Diagnostic::Settled);}
  else if(request_)Record(Diagnostic::ReceiptRejected);
 }
 std::optional<SupportGripResult> Continue(const SupportGripOwner& owner,const InputFrame& input,
  const SupportGripContact& contact,const HandInteractionSample& original,HandInteractionKey contactKey,
  HandInteraction& hands,SupportGrip& support,std::uint64_t& intent,bool cancel,const HandInteractionSample& current)noexcept {
  if(!request_)return {};
  if(cancel){Reset(Diagnostic::ContinueCancelled);return {};}
  // Another serialized consumer may have advanced the arbiter after Observe.
  // Refresh processing time only; never restamp the original packet or renew
  // its deadline. A changed packet must first pass the ordinary Observe path.
  if(current.owner!=last_.owner||current.sequence!=last_.sequence||current.observedNs!=last_.observedNs||
     current.deadlineNs!=last_.deadlineNs||current.nowNs<last_.nowNs||!Safe(current)){
   Reset(Diagnostic::ClockOrLease);return {};}
  last_.nowNs=current.nowNs;
  if(!readyNs_||!Safe(last_)||last_.nowNs<readyNs_||last_.nowNs-readyNs_>1500000000){Record(Diagnostic::WaitingReceipt);return {};}
  if(owner.equipped!=gun_.item.id||contactKey!=HandInteractionKey{2,gun_.item.id}||input.generation!=last_.sequence||
   original.owner!=last_.owner||original.sequence<=submittedSequence_||original.sequence>last_.sequence||
   original.observedNs<readyNs_||original.observedNs>last_.observedNs||!SafeAt(original,last_.nowNs)||
   !contact.valid||!std::isfinite(contact.distanceMeters)||contact.distanceMeters<0||contact.distanceMeters>.16f){Record(Diagnostic::WaitingContact);return {};}
  const auto gun=hands.Current(InteractionHand::Right);
  if(!gun||gun->token!=gun_||hands.Current(InteractionHand::Left)){Record(Diagnostic::HandsUnavailable);return {};}
  if(intent==std::numeric_limits<std::uint64_t>::max()){Reset();return {};}
  const HandClaimRequest request{last_.owner,InteractionHand::Left,HandClaimKind::WeaponSupport,gun_.item,
   {contactKey,original.sequence,original.deadlineNs,true},++intent,gun_.id};
  const auto acquired=hands.AcquireFrom(last_,original,request);if(!acquired.claim){Reset(Diagnostic::AcquireRejected,unsigned(acquired.reason));return {};}
  auto proposed=support;const auto result=proposed.AdoptHeld(owner,input,contact,last_,original,*acquired.claim,*gun);
  if(!result.holding||!result.engaged){hands.Release(last_,acquired.claim->token);Reset(Diagnostic::AdoptRejected,unsigned(result.reason));return {};}
  support=proposed;Reset(Diagnostic::Returned);return result;
 }
 void Report(std::ostream& out)const {
  static constexpr const char* names[]={"armed","settled","cancelled","retained_settled","unsafe","occupied","changed_gun","changed_owner","clock_or_lease","expired","continue_cancelled","waiting_receipt","waiting_contact","hands_unavailable","acquire_rejected","adopt_rejected","returned","receipt_rejected"};
  out<<"{\"request\":"<<request_<<",\"settled_ns\":"<<readyNs_<<",\"capacity\":"<<rows_.size()<<",\"dropped\":"<<dropped_<<",\"rows\":[";
  for(unsigned n=0;n<count_;++n){if(n)out<<',';const auto& r=rows_[(count_==rows_.size()?next_+n:n)%rows_.size()];
   out<<"{\"reason\":\""<<names[unsigned(r.reason)]<<"\",\"request\":"<<r.request<<",\"input\":"<<r.input<<",\"now_ns\":"<<r.now
    <<",\"settled_ns\":"<<r.ready<<",\"gun_claim\":"<<r.gun<<",\"adapter_reason\":"<<r.adapterReason<<",\"source_flags\":"<<r.sourceFlags<<'}';}
  out<<"]}";
 }
private:
 struct Row {Diagnostic reason{};std::uint64_t request=0,input=0,gun=0;std::int64_t now=0,ready=0;unsigned adapterReason=~0u,sourceFlags=0;};
 std::array<Row,32> rows_{};unsigned count_=0,next_=0;std::uint64_t dropped_=0;
 void Record(Diagnostic reason,unsigned adapterReason=~0u,unsigned sourceFlags=0)noexcept {
  if(count_){const auto& p=rows_[(next_+rows_.size()-1)%rows_.size()];
   if(p.reason==reason&&p.request==request_&&p.ready==readyNs_&&p.adapterReason==adapterReason&&p.sourceFlags==sourceFlags)return;}
  rows_[next_]={reason,request_,last_.sequence,gun_.id,last_.nowNs,readyNs_,adapterReason,sourceFlags};next_=(next_+1)%unsigned(rows_.size());
  if(count_<rows_.size())++count_;else ++dropped_;
 }
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
