#include "Bc2PhysicalReloadLifecycleProbe.h"
#include <ostream>
#include <cmath>
namespace fvr::bc2 {
PhysicalReloadProbeState Bc2PhysicalReloadLifecycleProbe::Local(const PhysicalReloadProbeState& s)const noexcept {
 auto r=s;r.acquired-=baseAcquired_;r.submitted-=baseSubmitted_;r.completed-=baseCompleted_;return r;
}
void Bc2PhysicalReloadLifecycleProbe::Stop(const PhysicalReloadProbeState& s,std::int64_t now,bool normal)noexcept {
 stopping_=true;stopAt_=now;stoppedCycle_=s.cycle;normalCompletion_=normal;
 if(!normal)++cancelCount_;
 if(rowCount_<rows_.size())rows_[rowCount_++]={s.cycle,now,unsigned(entry_?entry_->phase:ReloadRequestCyclePhase::Idle),entry_?entry_->enteredMask:0,s.reserve?s.reserve->loaded:-1,s.reserve?s.reserve->reserve:-1,normal};
}
void Bc2PhysicalReloadLifecycleProbe::Prepare(interaction::InputFrame& input,const ReloadStateOwner& owner,std::string_view asset,const ReloadRawContact& raw,const PhysicalReloadProbeState& s,std::int64_t observed,std::int64_t deadline,std::int64_t now)noexcept {
 if(!enabled_)return;
 // Once final conservation was accepted, stop issuing fixture input. Later
 // diagnostic-source expiry must not retroactively invalidate that receipt.
 if(done_||failed_){input.hands[0].squeeze=0;return;}
 if(!startupCommitted_){
  // Private synthetic input only: remain neutral, allowing actual current
  // source/rig publications to settle rather than latching CancelConsumer.
  input.hands[0].grip.position={-.23f,-.55f,-.02f};input.hands[0].aim=input.hands[0].grip;input.hands[0].squeeze=0;
  ++startupWaits_;
  if(now<=0||(startupLastNow_&&now<startupLastNow_)){failed_=true;startupReason_=1;return;}
  if(!startupFirstNs_)startupFirstNs_=now;startupLastNow_=now;
  if(now-startupFirstNs_>=3000000000ll){failed_=true;startupReason_=2;return;}
  const auto nearIdentity=[](const math::Pose& pose){const auto& q=pose.orientation;
   return std::hypot(pose.position.x,pose.position.y,pose.position.z)<.0001f&&
    std::abs(q.x)<.001f&&std::abs(q.y)<.001f&&std::abs(q.z)<.001f&&std::abs(std::abs(q.w)-1)<.001f;};
  const bool currentInput=interaction::ValidInput(input)&&input.generation&&input.focused&&input.headValid&&
   input.spaceGeneration==owner.space&&input.hands[0].gripTracked&&input.hands[1].gripTracked&&input.hands[1].aimTracked&&
   nearIdentity(input.referenceHead)&&nearIdentity(input.head)&&observed>0&&observed<=now&&deadline>now&&deadline-observed<=100000000;
  const bool currentReserve=s.reserve&&s.reserve->verified&&s.reserve->identity.owner==owner&&s.reserve->sequence&&
   s.reserve->observedNs>0&&s.reserve->observedNs<=now&&s.reserve->deadlineNs>now&&
   s.reserve->deadlineNs-s.reserve->observedNs<=200000000&&s.reserve->reloadInputReady;
  const bool currentRaw=raw.valid&&raw.owner==owner&&raw.rigFingerprint==SpasReloadRig&&raw.inputEvidence.sequence&&
   raw.inputEvidence.sequence<=input.generation&&raw.inputEvidence.observedNs>0&&raw.inputEvidence.observedNs<=now&&raw.inputEvidence.deadlineNs>now;
  const bool ready=currentInput&&currentReserve&&currentRaw&&asset==SpasReloadAsset&&!s.active&&!s.held&&!s.pending;
  startupReason_=!currentInput?3:!currentReserve?4:!currentRaw?5:!ready?6:0;
  if(!ready){startupCandidateNs_=0;startupFirstInput_=startupLastInput_=0;return;}
  if(!startupCandidateNs_||startupOwner_!=owner){if(startupCandidateNs_&&startupOwner_!=owner)++startupOwnerChanges_;
   startupOwner_=owner;startupCandidateNs_=now;startupFirstInput_=startupLastInput_=input.generation;return;}
  if(input.generation<startupLastInput_){failed_=true;startupReason_=7;return;}
  startupLastInput_=input.generation;
  if(now-startupCandidateNs_<500000000||startupLastInput_<=startupFirstInput_)return;
  startupCommitted_=true;
 }
 if(owner!=startupOwner_){failed_=true;startupReason_=8;input.hands[0].squeeze=0;return;}
 if(s.acquired<baseAcquired_||s.submitted<baseSubmitted_||s.completed<baseCompleted_){failed_=true;return;}
 if(stopping_){
  input.hands[0].squeeze=0;
  // No new interaction until original2s cleanup window passed, actual consumer
  // retired, and fresh coherent counts match conservation/normal receipt.
  const bool fresh=s.reserve&&s.reserve->verified&&s.reserve->identity.owner==owner&&s.reserve->observedNs>0&&s.reserve->observedNs<=now&&s.reserve->deadlineNs>now;
  const int added=normalCompletion_?1:0;
  const bool counts=fresh&&s.reserve->loaded==originalLoaded_+added&&s.reserve->reserve==originalReserve_-added;
  if(now-stopAt_>=2200000000ll&&!s.active&&!s.held&&!s.pending&&counts){
   const bool next=(scenario_==PhysicalReloadLifecycleScenario::RepeatArmingCancel&&cancelCount_<2)||
    (scenario_==PhysicalReloadLifecycleScenario::CompleteThenArmingCancel&&normalCompletion_);
   if(!next){done_=true;return;}
   if(s.cycle!=stoppedCycle_){failed_=true;return;}
   baseAcquired_=s.acquired;baseSubmitted_=s.submitted;baseCompleted_=s.completed;
   originalLoaded_=s.reserve->loaded;originalReserve_=s.reserve->reserve;
   delegate_=Bc2PhysicalReloadProbe(true,1);stopping_=false;normalCompletion_=false;entry_.reset();++attempt_;
  }else {if(now-stopAt_>6000000000ll)failed_=true;return;}
 }
 if(done_||failed_)return;
 const auto local=Local(s);
 if(originalLoaded_<0&&s.reserve&&s.reserve->verified){originalLoaded_=s.reserve->loaded;originalReserve_=s.reserve->reserve;}
 if(scenario_==PhysicalReloadLifecycleScenario::CompleteThenArmingCancel&&attempt_==0&&s.reserve&&s.reserve->capacity-s.reserve->loaded<2){failed_=true;return;}
 delegate_.Prepare(input,owner,asset,raw,local,observed,deadline,now);
 const bool holding=scenario_==PhysicalReloadLifecycleScenario::HoldingCancel;
 const bool cancelArming=scenario_==PhysicalReloadLifecycleScenario::ArmingCancel||scenario_==PhysicalReloadLifecycleScenario::RepeatArmingCancel||
  (scenario_==PhysicalReloadLifecycleScenario::CompleteThenArmingCancel&&attempt_>0);
 // Actual cached owned Update entry + exact current policy phase, never
 // infer Arming from absence of a Held lease.
 // Cancel-only scenarios never drift into the normal insertion stroke when
 // the entry observation/window was missed. Safety-cancel publicly, report
 // failed rather than claiming an Arming case succeeded.
 if(s.active&&s.nativeHolding&&cancelArming){failed_=true;input.hands[0].squeeze=0;return;}
 if(s.active&&s.nativeHolding&&holding&&(!entry_||!FreshPreholdEntry(*entry_,s.identity,s.cycle,now))){failed_=true;input.hands[0].squeeze=0;return;}
 if(s.active&&entry_&&FreshPreholdEntry(*entry_,s.identity,s.cycle,now)&&
    ((cancelArming&&entry_->phase==ReloadRequestCyclePhase::Arming)||
     (holding&&s.nativeHolding&&entry_->phase==ReloadRequestCyclePhase::Holding))){
  if(s.cycle<=stoppedCycle_){failed_=true;return;}Stop(s,now,false);input.hands[0].squeeze=0;
 }
}
void Bc2PhysicalReloadLifecycleProbe::Observe(const PhysicalReloadProbeState& s,std::int64_t now)noexcept {
 if(!enabled_||!startupCommitted_||stopping_||done_||failed_)return;
 if(s.acquired<baseAcquired_||s.submitted<baseSubmitted_||s.completed<baseCompleted_){failed_=true;return;}
 delegate_.Observe(Local(s),now);
 if(scenario_==PhysicalReloadLifecycleScenario::CompleteThenArmingCancel&&attempt_==0&&s.completed==baseCompleted_+1)Stop(s,now,true);
 else if(scenario_==PhysicalReloadLifecycleScenario::Normal&&delegate_.CancelConsumer())done_=true;
}
void Bc2PhysicalReloadLifecycleProbe::Report(std::ostream& out)const {
 out<<"{\"scenario\":"<<unsigned(scenario_)<<",\"done\":"<<(done_?"true":"false")<<",\"failed\":"<<(failed_?"true":"false")<<",\"attempt\":"<<attempt_<<",\"cancel_count\":"<<cancelCount_<<",\"native_verified\":false,\"startup\":{\"committed\":"<<(startupCommitted_?"true":"false")
    <<",\"first_ns\":"<<startupFirstNs_<<",\"candidate_ns\":"<<startupCandidateNs_<<",\"first_input\":"<<startupFirstInput_
    <<",\"last_input\":"<<startupLastInput_<<",\"waits\":"<<startupWaits_<<",\"owner_changes\":"<<startupOwnerChanges_
    <<",\"reason\":"<<startupReason_<<"},\"events\":[";
 for(unsigned n=0;n<rowCount_;++n){if(n)out<<',';const auto& r=rows_[n];out<<"{\"cycle\":"<<r.cycle<<",\"now_ns\":"<<r.now<<",\"prior_phase\":"<<r.prior<<",\"entered_mask\":"<<r.entered<<",\"loaded\":"<<r.loaded<<",\"reserve\":"<<r.reserve<<",\"normal_completed\":"<<(r.normal?"true":"false")<<'}';}
 out<<"],\"motion_fixture\":";delegate_.Report(out);out<<'}';
}
}
