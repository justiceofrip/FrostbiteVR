#pragma once
#include "Bc2AmmoReserve.h"
#include "fvr/interaction/ControllerInput.h"
#include <ostream>
namespace fvr::bc2 {
// Finite ordinary controller trigger only. It cannot invoke a native method,
// hold a state, acknowledge a cycle, fabricate contact, reload or select a gun.
class Bc2M95StockShotProbe {
public:
 enum class Phase:unsigned {Preflight,Fire,Released,Done,Failed};
 enum class Failure:unsigned {None,Clock,Deadline,Input,Owner,Counts,Evidence,Sequence};
 float Tick(const interaction::InputFrame& input,const ReloadStateOwner& owner,bool safe,bool exactAsset,
   const std::optional<Bc2AmmoReserveLease>& evidence,std::int64_t now)noexcept {
  if(Completed()||Failed())return 0;
  if(now<=0||(lastNow_&&now<lastNow_))return Fail(Failure::Clock);
  lastNow_=now;if(!first_)first_=now;
  if(now-first_>=12000000000ll)return Fail(Failure::Deadline);
  bool neutral=true;for(const auto& h:input.hands)neutral=neutral&&!h.held&&h.trigger==0&&h.squeeze==0&&h.stickX==0&&h.stickY==0;
  if(!safe||!neutral||!interaction::ValidInput(input)||!input.focused||!input.headValid||
     !input.hands[1].aimTracked||!input.hands[0].gripTracked||!input.hands[1].gripTracked||
     !(input.hands[1].active&interaction::Trigger)||!exactAsset){
   if(phase_!=Phase::Preflight)return Fail(Failure::Input);baseline_.reset();return 0;}
  if(phase_!=Phase::Preflight&&owner!=identity_.owner)return Fail(Failure::Owner);
  if(lastInput_&&input.generation<lastInput_)return Fail(Failure::Sequence);
  const bool fresh=evidence&&evidence->verified&&evidence->identity.owner==owner&&evidence->sequence&&
   evidence->observedNs>0&&evidence->observedNs<=now&&now<evidence->deadlineNs&&
   evidence->deadlineNs-evidence->observedNs<=250000000&&evidence->capacity==5&&evidence->loaded>=0&&
   evidence->loaded<=5&&evidence->reserve>=0&&evidence->reserve<=1000000;
  if(!fresh){
   if(phase_==Phase::Preflight){baseline_.reset();return 0;}
   if(phase_==Phase::Fire){phase_=Phase::Released;releaseAt_=now;releaseSequence_=input.generation;}
   if(!missingAt_)missingAt_=now;++missing_;
   if(now-missingAt_>=200000000)return Fail(Failure::Evidence);
   return 0; // A gap can truncate this single pulse; it never retries.
  }
  if(missingAt_){if(now-missingAt_>=200000000)return Fail(Failure::Evidence);missingAt_=0;}
  const auto& s=*evidence;
  if(phase_!=Phase::Preflight){
   if(s.identity!=identity_)return Fail(Failure::Owner);
   if(s.reserve!=reserve_||s.loaded>loaded_||s.loaded<loaded_-1)return Fail(Failure::Counts);
   if(s.loaded==loaded_-1){consumed_=1;if(!shotAt_)shotAt_=now;}
   if(consumed_&&s.loaded!=loaded_-1)return Fail(Failure::Counts);
  }
  if(lastInput_==input.generation)return phase_==Phase::Fire&&now-fireAt_<100000000?1.f:0.f;
  lastInput_=input.generation;
  if(phase_==Phase::Preflight){
   if(!s.allThreeIdle||s.loaded<2){baseline_.reset();return 0;}
   if(!baseline_||baseline_->identity!=s.identity||baseline_->loaded!=s.loaded||baseline_->reserve!=s.reserve){baseline_=s;return 0;}
   if(s.sequence<=baseline_->sequence||s.observedNs<=baseline_->observedNs||now-first_<2000000000ll)return 0;
   identity_=s.identity;loaded_=s.loaded;reserve_=s.reserve;phase_=Phase::Fire;fireAt_=now;pulseSequence_=input.generation;return 1;
  }
  if(phase_==Phase::Fire&&now-fireAt_>=100000000){phase_=Phase::Released;releaseAt_=now;releaseSequence_=input.generation;}
  if(phase_==Phase::Released&&consumed_&&s.allThreeIdle&&now-releaseAt_>=3000000000ll){phase_=Phase::Done;doneAt_=now;}
  return phase_==Phase::Fire?1.f:0.f;
 }
 bool Failed()const noexcept{return phase_==Phase::Failed;}
 bool Completed()const noexcept{return phase_==Phase::Done;}
 Phase Current()const noexcept{return phase_;}
 Failure Reason()const noexcept{return failure_;}
 void Report(std::ostream& o)const {
  o<<"{\"phase\":"<<unsigned(phase_)<<",\"failure\":"<<unsigned(failure_)<<",\"trigger_pulses\":"<<(fireAt_?1:0)
   <<",\"consumed\":"<<consumed_<<",\"starting_loaded\":"<<loaded_<<",\"reserve\":"<<reserve_
   <<",\"fire_ns\":"<<fireAt_<<",\"release_ns\":"<<releaseAt_<<",\"shot_count_observed_ns\":"<<shotAt_<<",\"done_ns\":"<<doneAt_
   <<",\"fire_sequence\":"<<pulseSequence_<<",\"release_sequence\":"<<releaseSequence_<<",\"missing_samples\":"<<missing_
   <<",\"native_hold_enabled\":false,\"native_state_writes\":false,\"reload_input\":false,\"physical_cycle_admitted\":false,\"firing\":["
   <<identity_.firing[0]<<','<<identity_.firing[1]<<','<<identity_.firing[2]<<"]}";
 }
private:
 float Fail(Failure why)noexcept {failure_=why;phase_=Phase::Failed;return 0;}
 Phase phase_=Phase::Preflight;Failure failure_=Failure::None;std::optional<Bc2AmmoReserveLease> baseline_;
 ReloadHoldIdentity identity_{};std::int64_t first_=0,lastNow_=0,fireAt_=0,releaseAt_=0,shotAt_=0,doneAt_=0,missingAt_=0;
 std::uint64_t lastInput_=0,pulseSequence_=0,releaseSequence_=0;int loaded_=0,reserve_=0;unsigned consumed_=0,missing_=0;
};
}
