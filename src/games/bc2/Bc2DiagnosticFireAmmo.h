#pragma once
#include "Bc2AmmoReserve.h"
#include <ostream>
namespace fvr::bc2 {
// Opt-in diagnostic preflight only. It never writes ammunition or requests a
// reload. Two exact fresh idle observations precede the single bounded pulse.
class DiagnosticFireAmmo {
public:
 bool Tick(const std::optional<Bc2AmmoReserveLease>& ammo,const ReloadStateOwner& owner,
           std::int64_t now,bool pulseStarted)noexcept{
  if(failed_)return false;
  if(now<=0||(lastNow_&&now<lastNow_)){failed_=true;return false;}lastNow_=now;
  const bool fresh=ammo&&ammo->verified&&ammo->identity.owner==owner&&ammo->sequence&&
   ammo->observedNs>0&&ammo->observedNs<=now&&ammo->deadlineNs>now&&ammo->deadlineNs-ammo->observedNs<=250000000&&
   ammo->capacity>=3&&ammo->capacity<=100&&ammo->loaded>=0&&ammo->loaded<=ammo->capacity&&ammo->reserve>=0&&ammo->reserve<=1000000&&
   ammo->identity.serverPlayer>=0x10000&&ammo->identity.serverSoldier>=0x10000&&ammo->identity.serverItem>=0x10000&&
   ammo->identity.firing[0]>=0x10000&&ammo->identity.firing[1]>=0x10000&&ammo->identity.firing[2]>=0x10000;
  if(baseline_&&owner!=baseline_->identity.owner){failed_=true;return false;}
  if(ammo&&baseline_&&ammo->identity!=baseline_->identity){failed_=true;return false;}
  if(!fresh){if(!missingAt_)missingAt_=now;if(now-missingAt_>=200000000)failed_=true;return false;}
  if(missingAt_&&now-missingAt_>=200000000){failed_=true;return false;}missingAt_=0;
  const auto& a=*ammo;
  if(lastSequence_&&(a.sequence<lastSequence_||a.observedNs<lastObserved_||
     (a.sequence==lastSequence_&&(a.observedNs!=lastObserved_||a.deadlineNs!=lastDeadline_)))){failed_=true;return false;}
  lastSequence_=a.sequence;lastObserved_=a.observedNs;lastDeadline_=a.deadlineNs;
  if(!baseline_){if(!a.allThreeIdle||a.loaded<3)return false;baseline_=a;lastLoaded_=starting_=a.loaded;return false;}
  if(a.capacity!=baseline_->capacity||a.reserve!=baseline_->reserve||a.loaded>lastLoaded_||(!pulseStarted&&a.loaded!=starting_)){
   failed_=true;return false;
  }
  if(a.sequence<baseline_->sequence||a.observedNs<baseline_->observedNs){failed_=true;return false;}
  lastLoaded_=a.loaded;
  if(!armed_){if(!a.allThreeIdle||a.loaded<3)return false;
   if(a.sequence<=baseline_->sequence||a.observedNs<=baseline_->observedNs)return false;armed_=true;}
  return pulseStarted||a.allThreeIdle;
 }
 bool Failed()const noexcept{return failed_;}
 void Report(std::ostream& out)const{
  out<<"{\"armed\":"<<(armed_?"true":"false")<<",\"failed\":"<<(failed_?"true":"false")
     <<",\"starting_loaded\":"<<starting_<<",\"last_loaded\":"<<lastLoaded_<<",\"native_state_writes\":false";
  if(baseline_){const auto& a=*baseline_;const auto& o=a.identity.owner;
   out<<",\"sequence\":"<<a.sequence<<",\"observed_ns\":"<<a.observedNs<<",\"deadline_ns\":"<<a.deadlineNs
      <<",\"capacity\":"<<a.capacity<<",\"reserve\":"<<a.reserve<<",\"owner\":["<<o.player<<','<<o.soldier<<','<<o.weak<<','<<o.weapon<<','<<o.actorGeneration<<','<<o.equipGeneration<<','<<o.space<<']'
      <<",\"firing\":["<<a.identity.firing[0]<<','<<a.identity.firing[1]<<','<<a.identity.firing[2]<<']'
      <<",\"server_player\":"<<a.identity.serverPlayer<<",\"server_soldier\":"<<a.identity.serverSoldier<<",\"server_item\":"<<a.identity.serverItem;
  }out<<'}';
 }
private:
 std::optional<Bc2AmmoReserveLease> baseline_;bool armed_=false,failed_=false;
 std::uint64_t lastSequence_=0;
 std::int64_t lastNow_=0,missingAt_=0,lastObserved_=0,lastDeadline_=0;std::int32_t starting_=0,lastLoaded_=0;
};
}
