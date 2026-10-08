#pragma once
#include "Bc2AmmoReserve.h"
#include <optional>
#include <ostream>
namespace fvr::bc2 {
// An input-only diagnostic. No reload request, native call or state write.
class Bc2EmptyFireProbe {
public:
 enum class Phase:unsigned {Preflight,Firing,Observe,Done,Failed};
 enum class Failure:unsigned {None,Timeout,Input,Owner,Evidence,Counts,ShotBound,Clock};
 static constexpr std::int64_t MaxNs=28000000000LL,ObserveNs=3000000000LL,GapNs=200000000LL;
 float Tick(const ReloadStateOwner& owner,bool supported,bool inputSafe,
            const std::optional<Bc2AmmoReserveLease>& lease,std::int64_t now)noexcept {
  if(phase_==Phase::Done||phase_==Phase::Failed)return 0;
  if(now<=0||(lastNow_&&now<lastNow_)){Fail(Failure::Clock);return 0;} lastNow_=now;
  if(!first_)first_=now;
  if(now-first_>=MaxNs){Fail(Failure::Timeout);return 0;}
  if(!inputSafe){if(phase_!=Phase::Preflight)Fail(Failure::Input);return 0;}
  const bool fresh=lease&&lease->verified&&lease->identity.owner==owner&&lease->sequence&&
   lease->observedNs>0&&lease->observedNs<=now&&now<lease->deadlineNs&&
   lease->deadlineNs-lease->observedNs<=250000000&&
   lease->capacity>0&&lease->capacity<=100&&lease->loaded>=0&&lease->loaded<=lease->capacity&&
   lease->reserve>0&&lease->reserve<=1000000;
  if(phase_!=Phase::Preflight&&owner!=identity_.owner){Fail(Failure::Owner);return 0;}
  if(phase_!=Phase::Preflight){
   if(!supported){Fail(Failure::Evidence);return 0;}
   if(lease&&lease->identity!=identity_){Fail(Failure::Owner);return 0;}
   if(lease&&(!lease->verified||!lease->sequence||lease->observedNs<=0||lease->observedNs>now||lease->deadlineNs-lease->observedNs>250000000)){Fail(Failure::Evidence);return 0;}
   if(lease&&(lease->capacity!=capacity_||lease->loaded<0||lease->loaded>lastLoaded_||lease->reserve!=reserve_)){
    autoReloadObserved_=lease->loaded>lastLoaded_;Fail(Failure::Counts);return 0;
   }
   if(!fresh){
    if(!missingAt_)missingAt_=now;
    if(phase_==Phase::Observe)emptyAt_=0;
    ++missingSamples_;
    if(now-missingAt_>=GapNs)Fail(Failure::Evidence);
    return 0; // Never use retained evidence to generate trigger input.
   }
   if(missingAt_){
    if(now-missingAt_>=GapNs){Fail(Failure::Evidence);return 0;}
    missingAt_=0; ++recoveredGaps_;
   }
  }else if(!supported||!fresh){baseline_.reset();return 0;}
  const auto& s=*lease;
  if(phase_==Phase::Preflight){
   if(!s.allThreeIdle||s.loaded==0){baseline_.reset();return 0;}
   if(!baseline_||baseline_->identity!=s.identity||baseline_->loaded!=s.loaded||baseline_->reserve!=s.reserve||baseline_->capacity!=s.capacity){baseline_=s;return 0;}
   if(s.sequence<=baseline_->sequence||s.observedNs<=baseline_->observedNs)return 0;
   identity_=s.identity;starting_=lastLoaded_=s.loaded;reserve_=s.reserve;capacity_=s.capacity;
   phase_=Phase::Firing;nextPulse_=now;
  }
  if(s.identity!=identity_){Fail(Failure::Owner);return 0;}
  if(s.capacity!=capacity_||s.loaded>lastLoaded_||s.reserve!=reserve_){autoReloadObserved_=s.loaded>lastLoaded_;Fail(Failure::Counts);return 0;}
  if(s.loaded<lastLoaded_){consumed_+=unsigned(lastLoaded_-s.loaded);lastLoaded_=s.loaded;}
  if(phase_==Phase::Firing&&s.loaded==0){phase_=Phase::Observe;emptyAt_=now;return 0;}
  if(phase_==Phase::Observe){if(!emptyAt_)emptyAt_=now;if(now-emptyAt_>=ObserveNs)phase_=Phase::Done;return 0;}
  if(now>=nextPulse_){
   // A pump or bolt can take longer than the pulse interval. Only spend a
   // trigger attempt once all three native copies are idle again.
   if(!s.allThreeIdle)return 0;
   if(pulses_>=unsigned(starting_)*2u){Fail(Failure::ShotBound);return 0;}
   ++pulses_;pulseEnd_=now+80000000;nextPulse_=now+350000000;
  }
  return now<pulseEnd_?1.f:0.f;
 }
 void Cancel()noexcept {if(phase_==Phase::Firing||phase_==Phase::Observe)Fail(Failure::Input);}
 Phase Current()const noexcept{return phase_;}
 Failure Reason()const noexcept{return failure_;}
 unsigned Pulses()const noexcept{return pulses_;}
 unsigned Consumed()const noexcept{return consumed_;}
 bool AutoReloadObserved()const noexcept{return autoReloadObserved_;}
 void Report(std::ostream& o)const {o<<"{\"phase\":"<<unsigned(phase_)<<",\"failure\":"<<unsigned(failure_)<<",\"starting_loaded\":"<<starting_<<",\"last_loaded\":"<<lastLoaded_<<",\"consumed\":"<<consumed_<<",\"trigger_pulses\":"<<pulses_<<",\"empty_at_ns\":"<<emptyAt_<<",\"verified_zero_dwell_ns\":"<<(emptyAt_?lastNow_-emptyAt_:0)<<",\"missing_samples\":"<<missingSamples_<<",\"recovered_gaps\":"<<recoveredGaps_<<",\"selected_owner\":["<<identity_.owner.player<<','<<identity_.owner.soldier<<','<<identity_.owner.weak<<','<<identity_.owner.weapon<<','<<identity_.owner.actorGeneration<<','<<identity_.owner.equipGeneration<<','<<identity_.owner.space<<']'<<",\"auto_reload_count_increase\":"<<(autoReloadObserved_?"true":"false")<<",\"native_state_writes\":false,\"reload_input\":false}";}
private:
 void Fail(Failure f)noexcept{failure_=f;phase_=Phase::Failed;}
 Phase phase_=Phase::Preflight;Failure failure_=Failure::None;
 std::optional<Bc2AmmoReserveLease> baseline_;ReloadHoldIdentity identity_{};
 std::int64_t first_=0,lastNow_=0,nextPulse_=0,pulseEnd_=0,emptyAt_=0,missingAt_=0;
 int starting_=0,lastLoaded_=0,reserve_=0,capacity_=0;unsigned consumed_=0,pulses_=0,missingSamples_=0,recoveredGaps_=0;bool autoReloadObserved_=false;
};
}
