#pragma once
#include "Bc2MagazinePhysicalProbe.h"
namespace fvr::bc2 {
// Explicit diagnostic input fault only. This object never calls the native API,
// changes a consumer, or manufactures a retirement or ammunition receipt.
class Bc2ReloadInterruptionProbe {
public:
 enum class Phase:unsigned {Idle,Watching,TrackingLost,Retiring,Recovered,Failed};
 bool Begin(const MagazinePhysicalProbeState& s,const MagazinePackCounters& packs,std::int64_t now)noexcept {
  if(phase_!=Phase::Idle)return false;
  if(now<=0||s.active||s.retiring||s.pending||s.ownsHand||s.blocksEquipment||s.phase!=interaction::DetachableMagazinePhase::Attached){Fail(1);return false;}
  baseline_=s;packBaseline_=packs.rolePairs[1];phase_=Phase::Watching;started_=lastNow_=now;return true;
 }
 // Apply to the private action-policy copy before ControllerActions::Update.
 void ActionInput(interaction::InputFrame& in)const noexcept {if(phase_==Phase::TrackingLost)Drop(in);}
 void Prepare(interaction::InputFrame& in,std::int64_t now)noexcept {
  if(phase_!=Phase::TrackingLost&&phase_!=Phase::Retiring)return;
  if(!Clock(now)||!interaction::ValidInput(in)||!in.focused||!in.headValid||!in.hands[0].gripTracked||
     !in.hands[1].gripTracked||!in.hands[1].aimTracked||in.generation<lastLossInput_){Fail(2);return;}
  if(phase_==Phase::TrackingLost){
   if(now-lossAt_>=350000000&&lossPackets_&&cancelObserved_){phase_=Phase::Retiring;restoredAt_=now;}
   else {Drop(in);if(in.generation>lastLossInput_){lastLossInput_=in.generation;++lossPackets_;}}
  }
 }
 void Observe(const MagazinePhysicalProbeState& s,const MagazinePackCounters& packs,const MagazineRawContact& raw,std::int64_t now)noexcept {
  if(phase_==Phase::Idle||phase_==Phase::Failed||phase_==Phase::Recovered)return;
  if(!Clock(now))return;
  if(phase_==Phase::Watching){
   if(s.cancelled!=baseline_.cancelled||s.reconciled!=baseline_.reconciled||s.acquired!=baseline_.acquired||
      s.submitted!=baseline_.submitted||s.completed!=baseline_.completed||s.originalReturns!=baseline_.originalReturns){Fail(3);return;}
   if(s.phase!=interaction::DetachableMagazinePhase::RemovedHeld||!s.active||!s.nativeHolding||!s.ownsHand||
      !s.cycle||s.started!=baseline_.started+1||packs.rolePairs[1]<=packBaseline_)return;
   if(!s.reserve||!Fresh(*s.reserve,now)){Fail(4);return;}
   held_=s;lossAt_=now;phase_=Phase::TrackingLost;return;
  }
  if(s.started!=held_.started||s.acquired!=held_.acquired||s.submitted!=held_.submitted||s.completed!=held_.completed||
     s.originalReturns!=held_.originalReturns||s.cancelled<held_.cancelled||s.cancelled>held_.cancelled+1||
     s.reconciled<held_.reconciled||s.reconciled>held_.reconciled+1){Fail(5);return;}
  if(s.reserve&&Fresh(*s.reserve,now)&&(s.reserve->identity!=held_.reserve->identity||s.reserve->loaded!=held_.reserve->loaded||
     s.reserve->reserve!=held_.reserve->reserve||s.reserve->capacity!=held_.reserve->capacity)){Fail(6);return;}
  cancelObserved_=s.cancelled==held_.cancelled+1;
  if(phase_==Phase::Retiring&&cancelObserved_&&s.reconciled==held_.reconciled+1&&
     !s.active&&!s.nativeHolding&&!s.retiring&&!s.pending&&!s.ownsHand&&!s.blocksEquipment&&
     s.phase==interaction::DetachableMagazinePhase::Attached&&s.reserve&&Fresh(*s.reserve,now)&&s.reserve->reloadInputReady&&
     raw.valid&&raw.nativeMagazineAttached&&raw.owner==held_.reserve->identity.owner&&raw.rigFingerprint==Xm8MagazineRig&&
     raw.inputEvidence.sequence>lastLossInput_&&raw.inputEvidence.observedNs>=restoredAt_&&raw.inputEvidence.observedNs<=now&&
     raw.inputEvidence.deadlineNs>now&&raw.inputEvidence.focused&&raw.inputEvidence.tracked[0]&&raw.inputEvidence.tracked[1]){
   retired_=s;recoveredAt_=now;phase_=Phase::Recovered;
  }
 }
 Phase State()const noexcept{return phase_;}
 bool Recovered()const noexcept{return phase_==Phase::Recovered;}
 bool Failed()const noexcept{return phase_==Phase::Failed;}
 bool LossStarted()const noexcept{return lossAt_!=0;}
 void Report(std::ostream& o)const {
  o<<"{\"phase\":"<<unsigned(phase_)<<",\"failure\":"<<failure_<<",\"cycle\":"<<held_.cycle
   <<",\"loss_started_ns\":"<<lossAt_<<",\"restored_ns\":"<<restoredAt_<<",\"recovered_ns\":"<<recoveredAt_
   <<",\"loss_packets\":"<<lossPackets_<<",\"last_loss_input\":"<<lastLossInput_<<",\"cancel_observed\":"<<(cancelObserved_?"true":"false")
   <<",\"cancelled_before\":"<<held_.cancelled<<",\"cancelled_after\":"<<retired_.cancelled
   <<",\"reconciled_before\":"<<held_.reconciled<<",\"reconciled_after\":"<<retired_.reconciled
   <<",\"loaded_before\":"<<(held_.reserve?held_.reserve->loaded:-1)<<",\"reserve_before\":"<<(held_.reserve?held_.reserve->reserve:-1)
   <<",\"loaded_after\":"<<(retired_.reserve?retired_.reserve->loaded:-1)<<",\"reserve_after\":"<<(retired_.reserve?retired_.reserve->reserve:-1)<<'}';
 }
private:
 static bool Fresh(const Bc2AmmoReserveLease& r,std::int64_t now)noexcept {
  return r.verified&&r.observedNs>0&&r.observedNs<=now&&r.deadlineNs>now&&r.deadlineNs-r.observedNs<=200000000;
 }
 static void Drop(interaction::InputFrame& in)noexcept {in.hands[0].gripTracked=in.hands[0].aimTracked=false;in.hands[0].squeeze=0;}
 bool Clock(std::int64_t now)noexcept {
  if(now<=0||now<lastNow_||now-started_>=30000000000ll||(lossAt_&&now-lossAt_>=6000000000ll)){Fail(7);return false;}
  lastNow_=now;return true;
 }
 void Fail(unsigned why)noexcept {if(phase_!=Phase::Failed){failure_=why;phase_=Phase::Failed;}}
 Phase phase_=Phase::Idle;unsigned failure_=0,packBaseline_=0,lossPackets_=0;
 bool cancelObserved_=false;std::uint64_t lastLossInput_=0;
 std::int64_t started_=0,lastNow_=0,lossAt_=0,restoredAt_=0,recoveredAt_=0;
 MagazinePhysicalProbeState baseline_{},held_{},retired_{};
};
}
