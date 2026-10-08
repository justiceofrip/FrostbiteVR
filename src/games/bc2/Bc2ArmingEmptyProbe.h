#pragma once
#include "Bc2EmptyFireProbe.h"
#include "Bc2ReloadRequestCycle.h"
#include "Bc2PhysicalReloadProbe.h"
namespace fvr::bc2 {
// Opt-in INPUT-ONLY orchestration. Native Step journal is the sole evidence of
// inhibition/restoration. This class NEVER reports native_verified=true.
class Bc2ArmingEmptyProbe {
public:
 enum class Phase:unsigned {Emptying,Prepare,NeutralArming,CancelWait,Done,Failed};
 float Fire(const ReloadStateOwner& owner,bool supported,bool safe,const std::optional<Bc2AmmoReserveLease>& reserve,std::int64_t now)noexcept {
  if(phase_!=Phase::Emptying)return 0;
  const auto result=fire_.Tick(owner,supported,safe,reserve,now);
  if(fire_.Current()==Bc2EmptyFireProbe::Phase::Failed){Fail(1);return 0;}
  if(fire_.Current()==Bc2EmptyFireProbe::Phase::Observe&&reserve&&reserve->loaded==0){
   owner_=owner;reserveBefore_=reserve->reserve;capacity_=reserve->capacity;phase_=Phase::Prepare;}
  return result;
 }
 void Prepare(interaction::InputFrame& input,const ReloadStateOwner& owner,std::string_view asset,const ReloadRawContact& raw,
 const PhysicalReloadProbeState& state,std::int64_t observed,std::int64_t deadline,std::int64_t now)noexcept {
  if(phase_==Phase::Done||phase_==Phase::Failed||phase_==Phase::Emptying){input.hands[0].squeeze=0;return;}
  if(owner!=owner_){Fail(2);input.hands[0].squeeze=0;return;}
  if(phase_==Phase::CancelWait){input.hands[0].squeeze=0;return;}
  motion_.Prepare(input,owner,asset,raw,state,observed,deadline,now);
  if(motion_.CancelConsumer()){Fail(3);input.hands[0].squeeze=0;}
 }
 // Call immediately after ACTUAL physical consumer Tick, before mapping its
 // original reloadHeld into native action input. The current packet timestamp
 // belongs to that actual consumer; cached evidence never renews its lease.
 bool After(const PhysicalReloadProbeState& state,bool reloadHeld,unsigned policyPhase,
 std::uint64_t policyCycle,std::uint64_t input,std::int64_t now)noexcept {
  if(phase_==Phase::Done||phase_==Phase::Failed)return false;
  if(phase_==Phase::Emptying)return reloadHeld;
  const bool fresh=state.reserve&&state.reserve->verified&&state.reserve->identity.owner==owner_&&
   state.reserve->observedNs>0&&state.reserve->observedNs<=now&&state.reserve->deadlineNs>now&&
   state.reserve->loaded==0&&state.reserve->reserve==reserveBefore_&&state.reserve->capacity==capacity_;
  if(!fresh){if(phase_==Phase::NeutralArming)Fail(4);return false;}
  if(phase_==Phase::CancelWait){
   if(now-stopAt_>=2200000000ll&&!state.active&&!state.held&&!state.pending&&state.reserve->allThreeIdle)phase_=Phase::Done;
   else if(now-stopAt_>=6000000000ll)Fail(5);return false;
  }
  if(phase_==Phase::Prepare&&reloadHeld&&state.active&&state.held&&state.cycle){
   identity_=state.identity;cycle_=state.cycle;startAt_=now;phase_=Phase::NeutralArming;
  }
  if(phase_==Phase::NeutralArming){
   if(state.identity!=identity_||state.cycle!=cycle_||!state.active||!state.held||state.pending||state.nativeHolding){Fail(6);return false;}
   if(policyCycle==cycle_&&policyPhase==unsigned(ReloadRequestCyclePhase::Arming))++armingSamples_;
   else if(policyPhase!=~0u){Fail(7);return false;}
   if(reloadHeld){++withheld_;if(!firstInput_)firstInput_=input;lastInput_=input;}
   if(now-startAt_>=300000000ll){if(!armingSamples_){Fail(8);return false;}
    stopAt_=now;phase_=Phase::CancelWait;cancel_=true;}
   return false; // NEVER replay the swallowed 100ms edge later.
  }
  return reloadHeld;
 }
 void Cancel()noexcept {if(phase_==Phase::Done||phase_==Phase::Failed)return;if(phase_==Phase::Emptying){fire_.Cancel();if(fire_.Current()==Bc2EmptyFireProbe::Phase::Failed)Fail(9);}else if(phase_!=Phase::Done)Fail(9);}
 bool CancelConsumer()const noexcept{return cancel_||phase_==Phase::Done||phase_==Phase::Failed;}
 Phase Current()const noexcept{return phase_;}
 void Report(std::ostream& o)const {o<<"{\"phase\":"<<unsigned(phase_)<<",\"failure\":"<<failure_
  <<",\"cycle\":"<<cycle_<<",\"start_ns\":"<<startAt_<<",\"cancel_ns\":"<<stopAt_
  <<",\"withheld_reload_samples\":"<<withheld_<<",\"first_input\":"<<firstInput_<<",\"last_input\":"<<lastInput_
  <<",\"arming_policy_samples\":"<<armingSamples_<<",\"original_loaded\":0,\"original_reserve\":"<<reserveBefore_
  <<",\"owner\":["<<owner_.player<<','<<owner_.soldier<<','<<owner_.weak<<','<<owner_.weapon<<','<<owner_.actorGeneration<<','<<owner_.equipGeneration<<','<<owner_.space<<']'
  <<",\"empty_fire\":";fire_.Report(o);o<<",\"scope\":\"existing_tube_consumer\",\"magazine_arming_covered\":false,\"native_verified\":false,\"native_state_writes\":false,\"paired_step_evidence_required\":true}";}
private:
 void Fail(unsigned why)noexcept{if(phase_==Phase::Failed)return;phase_=Phase::Failed;failure_=why;cancel_=true;}
 Bc2EmptyFireProbe fire_;Bc2PhysicalReloadProbe motion_{true,1};Phase phase_=Phase::Emptying;
 ReloadStateOwner owner_{};ReloadHoldIdentity identity_{};std::uint64_t cycle_=0,firstInput_=0,lastInput_=0;
 std::int64_t startAt_=0,stopAt_=0;int reserveBefore_=-1,capacity_=0;unsigned failure_=0,withheld_=0,armingSamples_=0;bool cancel_=false;
};
}
