#pragma once
#include "Bc2MagazineEmptyControl.h"
#include <array>
namespace fvr::bc2 {
// Diagnostic only. These facts never authorize a native patch or hand claim.
inline bool ManualEmptyDiagnosticAdmitted(bool combined,bool requestMode,bool requestTarget,bool currentLocal,bool nativeCycleTarget=false)noexcept {
 return (combined||nativeCycleTarget)&&requestMode&&(requestTarget||currentLocal||nativeCycleTarget);
}
struct ManualEmptyDiagnosticContextBoundary {
 bool combined=false,request=false,codeVerified=false,entryAllowed=false,parentPresent=false,parentIsUpdate=false;
 unsigned parentDepth=0,firing=0,parentFiring=0,context=0,parentContext=0,caller=0,expectedCaller=0;
 std::uint64_t parentInvocation=0,stackLow=0,stackHigh=0;
 bool nativeCycleTarget=false;
};
inline bool ManualEmptyDiagnosticContextReadAllowed(const ManualEmptyDiagnosticContextBoundary& b)noexcept {
 return (b.combined||b.nativeCycleTarget)&&b.request&&b.codeVerified&&b.entryAllowed&&b.parentPresent&&b.parentDepth==1&&
  b.parentFiring==b.firing&&b.parentInvocation&&b.parentIsUpdate&&b.parentContext==b.context&&
  b.caller==b.expectedCaller&&b.context>=b.stackLow&&std::uint64_t(b.context)+0x30<=b.stackHigh&&!(b.context&3);
}
enum class ManualEmptyBoundaryDiagnosticReason:unsigned {
 None,StepDeltaNonFinite,StepDeltaNonPositive,StepDeltaTooLarge,ContextDecodeFailed,StepDeltaExceedsContext,
 OwnerEvidenceFailed,BeforeReadFailed,ContextDeltaTooLarge,
 ContextMultiplierNotOne,UnsupportedInput,UnsupportedContextFlags
};
inline ManualEmptyBoundaryDiagnosticReason ManualEmptyBoundaryControlReason(float step,
 const std::optional<ReloadUpdateContext>& context)noexcept {
 if(!std::isfinite(step))return ManualEmptyBoundaryDiagnosticReason::StepDeltaNonFinite;
 if(step<=0)return ManualEmptyBoundaryDiagnosticReason::StepDeltaNonPositive;
 if(!ValidManualReloadDelta(step))return ManualEmptyBoundaryDiagnosticReason::StepDeltaTooLarge;
 if(!context)return ManualEmptyBoundaryDiagnosticReason::ContextDecodeFailed;
 if(step>context->deltaSeconds)return ManualEmptyBoundaryDiagnosticReason::StepDeltaExceedsContext;
 if(!ValidManualReloadDelta(context->deltaSeconds))return ManualEmptyBoundaryDiagnosticReason::ContextDeltaTooLarge;
 if(context->reloadTimeMultiplier!=1)return ManualEmptyBoundaryDiagnosticReason::ContextMultiplierNotOne;
 if(context->orderRequested||(context->inputFlags&~5u))return ManualEmptyBoundaryDiagnosticReason::UnsupportedInput;
 if(!context->flags24Through28[0]||context->flags24Through28[2]||context->flags24Through28[4])return ManualEmptyBoundaryDiagnosticReason::UnsupportedContextFlags;
 return ManualEmptyBoundaryDiagnosticReason::None;
}
struct MagazineInteractionDiagnostic {
 ReloadStateOwner owner{};std::uint64_t sequence=0;
 std::int64_t observedNs=0,deadlineNs=0;
 bool supportHolding=false,ownsLeftHand=false,blocksWeaponActions=false;
 unsigned magazinePhase=0;
};
inline bool MagazineInteractionDiagnosticFresh(const MagazineInteractionDiagnostic& d,
 const ReloadStateOwner& owner,std::uint64_t maximumSequence,std::int64_t now)noexcept {
 return d.owner==owner&&d.sequence&&d.sequence<=maximumSequence&&d.observedNs>0&&
  d.observedNs<=now&&now<d.deadlineNs&&d.deadlineNs-d.observedNs<=150000000;
}
enum class MagazineEmptyDiagnosticStage:unsigned {
 Boundary,OwnerOrContext,OwnerEvidence,FiringRead,Policy,Eligibility,ProfileOrTiming,
 PolicyLock,AdjacentRecheck,Applied,PatchFailed,Count
};
struct MagazineEmptyDiagnosticRecord {
 MagazineEmptyDiagnosticStage stage=MagazineEmptyDiagnosticStage::Boundary;
 MagazineEmptyStep sample{};ReloadUpdateContext context{};
 ReloadFiringObservation after{};bool hasBefore=false,hasContext=false,hasAfter=false,ownerRetained=false;
 bool familyKnown=false;unsigned family=UINT32_MAX,phase=UINT32_MAX;std::int64_t nowNs=0;
 bool requested=false,applied=false,restored=false,hasInteraction=false;
 MagazineInteractionDiagnostic interaction{};
 bool boundaryReasonKnown=false,rawContextKnown=false;
 unsigned boundaryReason=UINT32_MAX,stepDeltaBits=0,contextDeltaBits=0,contextMultiplierBits=0,contextInputFlags=0;
 std::array<unsigned,5> rawContextFlags{};
 bool leaseInvalidLastKnown=false;
};
class MagazineEmptyDiagnosticJournal {
public:
 static constexpr std::size_t Capacity=131;
 void Observe(const MagazineEmptyDiagnosticRecord& incoming)noexcept {
  auto r=incoming;
  if(unsigned(r.stage)>=counts_.size())return;
  ++counts_[unsigned(r.stage)];++observed_;
  // Foreign/boundary calls still count, but cannot evict admitted local evidence.
  const auto& o=r.sample.identity.owner;
  if(r.sample.branch>=3||!o.player||!o.soldier||!o.weak||!o.weapon||
     !o.actorGeneration||!o.equipGeneration||!o.space){
   const MagazineEmptyStep* known=nullptr;
   for(const auto& candidate:lastKnown_)if(candidate&&candidate->before.address==r.sample.before.address){known=&*candidate;break;}
   if(!known){++unownedSkipped_;return;}
   // Attribution only. Never turn an expired owner into current authority.
   const auto address=r.sample.before.address;r.sample=*known;r.sample.before={};r.sample.before.address=address;
   r.hasBefore=r.hasAfter=r.hasContext=r.ownerRetained=r.hasInteraction=false;
   r.requested=r.applied=r.restored=false;
   r.boundaryReasonKnown=r.rawContextKnown=false;r.boundaryReason=UINT32_MAX;
   r.stepDeltaBits=r.contextDeltaBits=r.contextMultiplierBits=r.contextInputFlags=0;r.rawContextFlags={};
   r.leaseInvalidLastKnown=true;++lastKnownAttributed_;
  }else if(r.hasBefore&&r.sample.before.address>=0x10000){
   bool changedOwner=false;for(const auto& old:lastKnown_)if(old&&old->identity.owner!=r.sample.identity.owner)changedOwner=true;
   if(changedOwner){
    // Owner change retires all last-known aliases; cross-owner address reuse must not join.
    for(auto& old:lastKnown_)old.reset();
   }
   lastKnown_[r.sample.branch]=r.sample;
  }
  const unsigned branch=r.sample.branch<3?r.sample.branch:3;
  if(previous_[branch]&&Same(*previous_[branch],r))return;
  previous_[branch]=r;
  // Three dedicated first-Arming paired receipts cannot be evicted by later
  // cancelled idle traffic or foreign/transition banks. Evidence only.
  if(r.phase==2&&r.familyKnown&&r.hasBefore&&r.hasAfter&&r.hasContext&&
     r.requested&&r.applied&&r.restored&&r.ownerRetained&&!r.leaseInvalidLastKnown&&
     r.sample.before.loaded==0&&r.after.loaded==0&&!arming_[branch])arming_[branch]=r;
  // First salient paired transitions survive subsequent ordinary local traffic.
  // This is evidence retention only, not native authority or causality.
  if(Salient(r)){
   if(r.leaseInvalidLastKnown&&boundarySize_>=BoundaryCapacity){++boundaryDropped_;++salientDropped_;return;}
   if(salientSize_<SalientCapacity){salient_[salientSize_++]=r;if(r.leaseInvalidLastKnown)++boundarySize_;}
   else {++salientDropped_;if(r.leaseInvalidLastKnown)++boundaryDropped_;else ++transitionDropped_;}
   return;
  }
  rows_[next_]=r;next_=(next_+1)%RollingCapacity;
  if(size_<RollingCapacity)++size_;else ++overwritten_;
 }
 std::size_t Size()const noexcept{return size_+salientSize_+ArmingSize();}
 std::uint64_t Observed()const noexcept{return observed_;}
 std::uint64_t Overwritten()const noexcept{return overwritten_;}
 std::uint64_t BoundaryDropped()const noexcept{return boundaryDropped_;}
 std::uint64_t TransitionDropped()const noexcept{return transitionDropped_;}
 std::uint64_t LastKnownAttributed()const noexcept{return lastKnownAttributed_;}
 std::uint64_t UnownedSkipped()const noexcept{return unownedSkipped_;}
 std::uint64_t SalientDropped()const noexcept{return salientDropped_;}
 std::size_t SalientSize()const noexcept{return salientSize_;}
 const auto& Counts()const noexcept{return counts_;}
 std::size_t ArmingSize()const noexcept{std::size_t n=0;for(const auto& r:arming_)if(r)++n;return n;}
 const MagazineEmptyDiagnosticRecord& Row(std::size_t n)const noexcept{
  for(const auto& r:arming_)if(r){if(!n)return *r;--n;}
  return n<salientSize_?salient_[n]:rows_[(next_+RollingCapacity-size_+n-salientSize_)%RollingCapacity];}
private:
 static constexpr std::size_t SalientCapacity=32,BoundaryCapacity=16,RollingCapacity=Capacity-SalientCapacity-3;
 static bool Salient(const MagazineEmptyDiagnosticRecord& r)noexcept {
  if(r.leaseInvalidLastKnown||(r.applied&&!r.restored))return true;
  if(!r.hasBefore||!r.hasAfter||r.sample.before.loaded>1)return false;
  const auto reload=[](unsigned state){return state==10||state==11||state==12;};
  const auto& before=r.sample.before;
  return !reload(before.currentState)&&!reload(before.nextState)&&
    (reload(r.after.currentState)||reload(r.after.nextState));
 }
 static bool Same(const MagazineEmptyDiagnosticRecord& a,const MagazineEmptyDiagnosticRecord& b)noexcept {
  const auto state=[](const ReloadFiringObservation& x,const ReloadFiringObservation& y){return
   x.address==y.address&&x.currentState==y.currentState&&x.nextState==y.nextState&&
   x.loaded==y.loaded&&x.reserve==y.reserve&&x.flagsA8==y.flagsA8;};
  return a.boundaryReasonKnown==b.boundaryReasonKnown&&a.boundaryReason==b.boundaryReason&&
   a.rawContextKnown==b.rawContextKnown&&a.contextInputFlags==b.contextInputFlags&&
   a.rawContextFlags==b.rawContextFlags&&a.contextMultiplierBits==b.contextMultiplierBits&&
   a.leaseInvalidLastKnown==b.leaseInvalidLastKnown&&a.stage==b.stage&&a.sample.identity==b.sample.identity&&a.sample.profile==b.sample.profile&&
   a.familyKnown==b.familyKnown&&a.family==b.family&&a.phase==b.phase&&a.hasBefore==b.hasBefore&&a.hasContext==b.hasContext&&a.hasAfter==b.hasAfter&&
   a.ownerRetained==b.ownerRetained&&a.applied==b.applied&&a.restored==b.restored&&
   a.requested==b.requested&&state(a.sample.before,b.sample.before)&&state(a.after,b.after)&&
   a.context.inputFlags==b.context.inputFlags&&a.context.flags24Through28==b.context.flags24Through28&&
   a.context.reloadTimeMultiplier==b.context.reloadTimeMultiplier&&a.hasInteraction==b.hasInteraction&&
   (!a.hasInteraction||(a.interaction.supportHolding==b.interaction.supportHolding&&
     a.interaction.ownsLeftHand==b.interaction.ownsLeftHand&&
     a.interaction.blocksWeaponActions==b.interaction.blocksWeaponActions&&
     a.interaction.magazinePhase==b.interaction.magazinePhase));
 }
 std::array<std::optional<MagazineEmptyDiagnosticRecord>,4> previous_{};
 std::array<std::optional<MagazineEmptyStep>,3> lastKnown_{};
 std::array<MagazineEmptyDiagnosticRecord,RollingCapacity> rows_{};
 std::array<MagazineEmptyDiagnosticRecord,SalientCapacity> salient_{};
 std::array<std::optional<MagazineEmptyDiagnosticRecord>,3> arming_{};
 std::array<std::uint64_t,unsigned(MagazineEmptyDiagnosticStage::Count)> counts_{};
 std::size_t next_=0,size_=0,salientSize_=0,boundarySize_=0;std::uint64_t observed_=0,overwritten_=0,unownedSkipped_=0,salientDropped_=0,lastKnownAttributed_=0,boundaryDropped_=0,transitionDropped_=0;
};
} // namespace fvr::bc2

