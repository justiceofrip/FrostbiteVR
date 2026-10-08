#pragma once
#include "Bc2ReloadHold.h"
#include <limits>
#include <algorithm>
#include <cmath>
namespace fvr::bc2 {
// Optional production cleanup adapter consumer. Ledger itself grants no native dispatch authority.
// This receipt records an entered operation; it never claims allThreeHeld.
enum class PreholdCleanupPhase:unsigned {Empty,Prepared,Entered,CleanupPending,Reconciled,Rejected,Expired};
struct PreholdCleanupEvidence {
 ReloadHoldInput source{};std::uint64_t revision=0,operation=0,sequence=0;
 std::int64_t observedNs=0,deadlineNs=0;
 bool configVerified=false,coherent=false,callbacksExcluded=false;
};
struct PreholdCleanupCandidate {
 ReloadHoldIdentity identity{};ReloadObservedConfig config{};
 std::uint64_t revision=0,operation=0;std::int64_t deadlineNs=0;
 int loaded=0,reserve=0,capacity=0;
 unsigned enteredMask=0;
 static constexpr bool dispatchEnabled=false;
};
class PreholdCleanupLedger {
public:
 static constexpr std::int64_t ObservationNs=200000000,OperationNs=2000000000;
 bool Prepare(const PreholdCleanupEvidence& e,std::int64_t now)noexcept {
  if(phase_!=PreholdCleanupPhase::Empty&&phase_!=PreholdCleanupPhase::Reconciled&&phase_!=PreholdCleanupPhase::Rejected)return false;
  if(!Common(e,now)||!e.callbacksExcluded||e.operation<=watermark_||now>INT64_MAX-OperationNs)return false;
  for(const auto& b:e.source.branches)if((b.currentState!=1&&b.currentState!=2)||b.nextState!=b.currentState)return false;
  original_=e;watermark_=e.operation;began_=now;deadline_=now+OperationNs;entered_=0;phase_=PreholdCleanupPhase::Prepared;return true;
 }
 // Called only after exact same-invocation original entry observation.
 bool ObserveEntry(const PreholdCleanupEvidence& e,unsigned branch,std::int64_t now)noexcept {
  if((phase_!=PreholdCleanupPhase::Prepared&&phase_!=PreholdCleanupPhase::Entered)||branch>=3||!Matches(e,now))return false;
  const auto& b=e.source.branches[branch];
  // Actual trace1804/1805/1811: Update can consume next10 and expose
  // current10/next11 before the following owned Update observer runs.
  const bool request=(b.currentState==1||b.currentState==2)&&b.nextState==10;
  // XM8-01 Update1802/1807/1809 consumes both nested entry phases in
  // one OriginalUpdate: idle2/2 -> current11/next12, counts unchanged.
  const bool entered=(b.currentState==10&&(b.nextState==10||b.nextState==11))||
      (b.currentState==11&&b.nextState==12);
  if(!request&&!entered)return false;
  entered_|=1u<<branch;phase_=PreholdCleanupPhase::Entered;return true;
 }
 bool Cancel(std::uint64_t operation)noexcept {
  if(operation!=original_.operation||phase_!=PreholdCleanupPhase::Entered)return false;
  phase_=PreholdCleanupPhase::CleanupPending;return true;
 }
 // CPU-only admission before optional expensive native observation. Does not
 // reconcile early idle copies or change any source/receipt/deadline.
 bool CleanupObservationOpen(std::int64_t now)noexcept {
  if(phase_!=PreholdCleanupPhase::CleanupPending)return false;
  if(now<began_||now>=deadline_){phase_=PreholdCleanupPhase::Expired;return false;}
  return true;
 }
 std::optional<PreholdCleanupCandidate> ObserveCleanup(const PreholdCleanupEvidence& e,std::int64_t now,bool callbacksDrained=false)noexcept {
  if(!CleanupObservationOpen(now))return {};
  if(!Matches(e,now)){phase_=PreholdCleanupPhase::Rejected;return {};}
  bool idle=true;
  for(const auto& b:e.source.branches){
   const bool i=(b.currentState==1||b.currentState==2)&&(b.nextState==1||b.nextState==2);
   const bool reload=(b.currentState>=10&&b.currentState<=12)||(b.nextState==10);
   if(!i&&!reload){phase_=PreholdCleanupPhase::Rejected;return {};}
   idle&=i;
  }
  // Early all-idle is observational: an unprocessed sibling may still enter.
  // Only an external drained reconciliation may terminally settle the ledger.
  if(idle){if(callbacksDrained)phase_=PreholdCleanupPhase::Reconciled;return {};}
  return PreholdCleanupCandidate{original_.source.identity,original_.source.config,original_.revision,
   original_.operation,std::min(deadline_,e.deadlineNs),original_.source.branches[0].loaded,
   original_.source.branches[0].reserve,original_.source.capacities[0],entered_};
 }
 bool ObserveCancelledEntry(const PreholdCleanupEvidence& e,unsigned branch,std::int64_t now)noexcept {
  if(phase_!=PreholdCleanupPhase::CleanupPending||branch>=3||!Matches(e,now))return false;
  const auto& b=e.source.branches[branch];
  if(((b.currentState<10||b.currentState>12)&&b.nextState!=10))return false;
  entered_|=1u<<branch;return true;
 }
 const PreholdCleanupEvidence& Original()const noexcept{return original_;}
 std::int64_t OperationDeadlineNs()const noexcept{return deadline_;}
 unsigned EnteredMask()const noexcept{return entered_;}
 PreholdCleanupPhase Phase()const noexcept{return phase_;}
private:
 static bool Common(const PreholdCleanupEvidence& e,std::int64_t now)noexcept {
  if(!e.revision||!e.operation||!e.sequence||!e.configVerified||!e.coherent||!e.source.verified||
   e.observedNs<=0||e.observedNs>now||now>=e.deadlineNs||e.deadlineNs-e.observedNs>ObservationNs||
   e.source.nowNs!=e.observedNs||e.source.leaseDeadlineNs!=e.deadlineNs)return false;
  const auto& id=e.source.identity;const auto& o=id.owner;
  if(!o.player||!o.soldier||!o.weak||!o.weapon||!o.actorGeneration||!o.equipGeneration||!o.space||!id.serverPlayer||!id.serverSoldier||!id.serverItem)return false;
  for(unsigned n=0;n<3;++n){const auto& b=e.source.branches[n];
   if(!id.firing[n]||b.address!=id.firing[n]||b.loaded<0||b.reserve<0||e.source.capacities[n]<=0||b.loaded>e.source.capacities[n]||
    (b.flagsA8&(8|16))||!std::isfinite(b.phaseTimer)||b.phaseTimer<0||
    b.loaded!=e.source.branches[0].loaded||b.reserve!=e.source.branches[0].reserve||e.source.capacities[n]!=e.source.capacities[0])return false;
   for(unsigned k=0;k<n;++k)if(id.firing[n]==id.firing[k])return false;
  }return true;
 }
 bool Matches(const PreholdCleanupEvidence& e,std::int64_t now)const noexcept {
  return Common(e,now)&&now>=began_&&now<deadline_&&e.sequence>=original_.sequence&&e.observedNs>=original_.observedNs&&
   e.operation==original_.operation&&e.revision==original_.revision&&e.source.identity==original_.source.identity&&
   e.source.config==original_.source.config&&e.source.capacities==original_.source.capacities&&
   e.source.branches[0].loaded==original_.source.branches[0].loaded&&e.source.branches[0].reserve==original_.source.branches[0].reserve;
 }
 PreholdCleanupPhase phase_=PreholdCleanupPhase::Empty;PreholdCleanupEvidence original_{};
 std::uint64_t watermark_=0;std::int64_t began_=0,deadline_=0;unsigned entered_=0;
};
}



