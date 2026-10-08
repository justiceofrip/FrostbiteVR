#include "Bc2MagazinePhysicalProbe.h"
#include "Bc2PhysicalReloadProbe.h"
#include "fvr/interaction/BodyAnchors.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <sstream>
namespace fvr::bc2 {
namespace {
using namespace interaction;using namespace reload_insertion_detail;
std::optional<math::Matrix4> Controller(const math::Pose& p)noexcept {const auto v=math::MakeLhViewFromOpenXRPose(p);return v?InverseRigid(*v):std::nullopt;}
bool Static(const InputFrame& s)noexcept {const auto a=Controller(s.referenceHead),b=Controller(s.head);return a&&b&&
 Distance(*a,Identity())<.0001f&&Angle(*a,Identity())<.001f&&Distance(*b,Identity())<.0001f&&Angle(*b,Identity())<.001f;}
math::Pose Step(const math::Pose& from,const math::Pose& target)noexcept {
 auto out=target;const float d=std::hypot(target.position.x-from.position.x,target.position.y-from.position.y,target.position.z-from.position.z);
 const float t=d>.01f?.01f/d:1;out.position={from.position.x+(target.position.x-from.position.x)*t,
 from.position.y+(target.position.y-from.position.y)*t,from.position.z+(target.position.z-from.position.z)*t};
 auto q=target.orientation;const auto a=from.orientation;float dot=a.x*q.x+a.y*q.y+a.z*q.z+a.w*q.w;
 if(dot<0){q={-q.x,-q.y,-q.z,-q.w};dot=-dot;}
 const float angle=2*std::acos(std::clamp(dot,0.f,1.f)),u=angle>.05f?.05f/angle:1;
 q={a.x+(q.x-a.x)*u,a.y+(q.y-a.y)*u,a.z+(q.z-a.z)*u,a.w+(q.w-a.w)*u};
 const float n=std::sqrt(q.x*q.x+q.y*q.y+q.z*q.z+q.w*q.w);out.orientation={q.x/n,q.y/n,q.z/n,q.w/n};return out;
}
bool Fresh(const Bc2AmmoReserveLease& r,const ReloadStateOwner& owner,std::int64_t now)noexcept {
 return r.verified&&r.identity.owner==owner&&r.observedNs>0&&r.observedNs<=now&&r.deadlineNs>now&&r.deadlineNs-r.observedNs<=200000000;
}
}
void Bc2MagazinePhysicalProbe::PhaseTo(Phase p,std::int64_t now)noexcept {
 phase_=p;phaseAt_=now;alignedAt_=0;
 if(rowCount_<rows_.size())rows_[rowCount_++]={unsigned(p),failure_,now};else ++dropped_;
}
void Bc2MagazinePhysicalProbe::Fail(unsigned why,std::int64_t now)noexcept {if(CancelConsumer())return;failure_=why;PhaseTo(Phase::Failed,now);}
std::optional<Bc2MagazinePhysicalProbe::Counts> Bc2MagazinePhysicalProbe::EpisodeCounts(
 const MagazinePhysicalProbeState& s,std::int64_t now,bool mayBegin)noexcept {
 const Counts current{s.acquired,s.started,s.submitted,s.completed,s.originalReturns};
 if(s.completed>s.submitted||s.submitted>s.acquired||std::uint64_t(s.submitted)+s.originalReturns>s.started){Fail(4,now);return {};}
 if(!counterBaseline_){
  // Starting another script is allowed only after the real consumer has
  // retired its previous work. A busy consumer is never a new zero baseline.
  if(!mayBegin||s.active||s.nativeHolding||s.retiring||s.pending||s.ownsHand||s.blocksEquipment||
   s.phase!=DetachableMagazinePhase::Attached){Fail(21,now);return {};}
  counterBaseline_=counterLatest_=current;
 }
 if(current.acquired<counterLatest_.acquired||current.started<counterLatest_.started||
  current.submitted<counterLatest_.submitted||current.completed<counterLatest_.completed||current.returned<counterLatest_.returned){Fail(4,now);return {};}
 counterLatest_=current;const auto& b=*counterBaseline_;
 const Counts delta{current.acquired-b.acquired,current.started-b.started,current.submitted-b.submitted,
  current.completed-b.completed,current.returned-b.returned};
 if(delta.started>1||delta.submitted>1||delta.completed>delta.submitted||delta.submitted>delta.acquired||delta.acquired>1||
  (originalReturn_?(delta.submitted||delta.completed||delta.acquired||delta.returned>1):delta.returned!=0)){Fail(4,now);return {};}
 return delta;
}
void Bc2MagazinePhysicalProbe::Prepare(InputFrame& in,const ReloadStateOwner& owner,std::string_view asset,const MagazineRawContact& raw,
 const MagazinePhysicalProbeState& state,std::int64_t observed,std::int64_t deadline,std::int64_t now)noexcept {
 if(!enabled_)return;
 if(CancelConsumer()){in.hands[0].grip=command_;in.hands[0].aim=command_;in.hands[0].squeeze=0;return;}
 if(!first_){first_=phaseAt_=now;owner_=owner;command_=in.hands[0].grip;baseline_=packs_;}
 const auto counts=EpisodeCounts(state,now,true);
 if(!counts){in.hands[0].squeeze=0;return;}
 if(now<lastNow_||now-first_>=30000000000ll)Fail(1,now);lastNow_=now;
 if(owner!=owner_||asset!=Xm8MagazineAsset||!ValidInput(in)||!in.focused||!in.headValid||!Static(in)||
 !in.hands[0].gripTracked||!in.hands[1].gripTracked||!in.hands[1].aimTracked||observed<=0||observed>now||
 deadline<=now||deadline-observed>100000000)Fail(2,now);
 if(lastInput_&&in.generation<=lastInput_->generation){
  if(in.generation<lastInput_->generation)Fail(3,now);
  in.hands[0].grip=lastInput_->hands[0].grip;in.hands[0].aim=lastInput_->hands[0].aim;
  in.hands[0].squeeze=CancelConsumer()?0:lastInput_->hands[0].squeeze;return;
 }
 if(state.phase==DetachableMagazinePhase::Cancelled&&phase_!=Phase::Warmup)Fail(5,now);
 const Input* original=nullptr;
 if(raw.valid&&raw.owner==owner&&raw.rigFingerprint==Xm8MagazineRig)
  for(const auto& h:history_)if(h&&h->owner==owner&&h->frame.generation==raw.inputEvidence.sequence&&h->observed==raw.inputEvidence.observedNs&&
   h->deadline==raw.inputEvidence.deadlineNs&&h->deadline>now){original=&*h;break;}
 const bool newRaw=original&&raw.inputEvidence.sequence>lastRaw_;
 if(phase_==Phase::Warmup&&now-first_>=6000000000ll){
  if(state.reserve&&Fresh(*state.reserve,owner,now)&&state.reserve->loaded>0&&state.reserve->loaded<state.reserve->capacity&&
   state.reserve->reserve>0&&state.reserve->reloadInputReady&&newRaw&&raw.nativeMagazineAttached){
   loadedBefore_=state.reserve->loaded;reserveBefore_=state.reserve->reserve;capacityBefore_=state.reserve->capacity;
   expectedUnits_=originalReturn_?0:std::min(capacityBefore_-loadedBefore_,reserveBefore_);PhaseTo(Phase::ApproachMagazine,now);
  }else if(now-first_>=10000000000ll)Fail(6,now);
 }
 float along=.1f;bool solve=false;
 if(phase_==Phase::ApproachMagazine||phase_==Phase::Grip){along=.1f;solve=true;}
 if(phase_==Phase::Pull){along=.1f-.12f*std::clamp(float(now-phaseAt_)/900000000.f,0.f,1.f);solve=true;}
 if(phase_==Phase::Release){along=-.02f;solve=true;}
 if(phase_==Phase::ApproachRail){along=-.11f;solve=true;}
 if(phase_==Phase::Enter){along=-.025f;solve=true;}
 if(phase_==Phase::Stroke){along=-.025f+.125f*std::clamp(float(now-phaseAt_)/1200000000.f,0.f,1.f);solve=true;}
 if(phase_==Phase::WaitAck){along=.1f;solve=true;}
 if(solve&&newRaw&&!CancelConsumer()){
  const auto p=Xm8MagazineConfig().insertion;
  const auto desired=Multiply(p.itemFromHand,Multiply(*InverseRigid(p.itemFromInsertion),Multiply(TravelPose(p,along),Multiply(p.weaponFromEntry,raw.weaponWorldMeters))));
  if(const auto control=PhysicalReloadProbeController(raw,original->frame,desired)){
   command_=Step(command_,*control);lastRaw_=raw.inputEvidence.sequence;
   const float position=Distance(raw.rawLeftWristWorldMeters,desired),angle=Angle(raw.rawLeftWristWorldMeters,desired);
   if(position<.003f&&angle<.05f){if(!alignedAt_)alignedAt_=now;}else alignedAt_=0;
   const bool settled=alignedAt_&&now-alignedAt_>=100000000;
   if(phase_==Phase::ApproachMagazine&&settled&&original->frame.hands[0].squeeze<=.35f)PhaseTo(Phase::Grip,now);
   else if(phase_==Phase::Grip&&state.nativeHolding&&state.phase==DetachableMagazinePhase::Pulling)PhaseTo(Phase::Pull,now);
   else if(phase_==Phase::Pull&&state.phase==DetachableMagazinePhase::RemovedHeld&&settled&&removedPair_)PhaseTo(originalReturn_?Phase::ApproachRail:(carryChallenge_?Phase::Carry:Phase::Release),now);
   else if(phase_==Phase::Release&&state.phase==DetachableMagazinePhase::WellEmpty&&!state.ownsHand&&hiddenPair_&&
    original->frame.hands[0].squeeze<=.35f&&now-phaseAt_>=700000000)PhaseTo(Phase::Pouch,now);
   else if(phase_==Phase::ApproachRail&&settled)PhaseTo(Phase::Enter,now);
   else if(phase_==Phase::Enter&&settled&&(originalReturn_?(state.phase==DetachableMagazinePhase::RemovedHeld&&removedPair_):(state.phase==DetachableMagazinePhase::Guided&&replacementPair_)))PhaseTo(Phase::Stroke,now);
   if(now-rowAt_>=100000000){rowAt_=now;if(rowCount_<rows_.size())rows_[rowCount_++]={unsigned(phase_),0,now,in.generation,
    raw.inputEvidence.sequence,state.cycle,position,angle,along,state.reserve?state.reserve->loaded:-1,state.reserve?state.reserve->reserve:-1,packs_.pairs};else ++dropped_;}
  }else Fail(7,now);
 }
 if(phase_==Phase::Carry&&!CancelConsumer()){
  const unsigned missing=(!state.active?1u:0u)|(!state.ownsHand?2u:0u)|
   (state.phase!=DetachableMagazinePhase::RemovedHeld?4u:0u)|(!removedPair_?8u:0u)|(!state.nativeHolding?16u:0u);
  if(state.nativeHolding)carryWaitAt_=0;
  if(missing&15u){carryFailureFlags_=missing;carryFailureAt_=now;Fail(17,now);}
  else if(!state.nativeHolding){
   // Prepare precedes consumer Tick: its previous positive lease may expire
   // between packets. Keep only the existing command while real Tick rereads.
   if(!carryWaitAt_)carryWaitAt_=now;++carryWaits_;
   if(now-carryWaitAt_>=50000000ll){carryFailureFlags_=missing|32u;carryFailureAt_=now;Fail(20,now);}
  }else if(newRaw){
   carryWaitAt_=0;
   if(!carryStart_)carryStart_=raw.rawLeftWristWorldMeters;
   auto desired=*carryStart_;
   if(!carryReturning_){
    auto rotation=Identity();const float c=std::cos(.65f),s=std::sin(.65f);
    rotation.values[0][0]=rotation.values[2][2]=c;rotation.values[0][2]=-s;rotation.values[2][0]=s;
    desired=Multiply(rotation,*carryStart_);
    desired.values[3]=carryStart_->values[3];desired.values[3][0]+=.12f;desired.values[3][1]+=.04f;
   }
   if(const auto control=PhysicalReloadProbeController(raw,original->frame,desired)){
    // Deliberate fresh off-rail command, rather than the rail's 1cm smoother.
    command_=*control;lastRaw_=raw.inputEvidence.sequence;++carrySamples_;
    if(carryPrevious_){carryStepTranslation_=std::max(carryStepTranslation_,Distance(raw.rawLeftWristWorldMeters,*carryPrevious_));
     carryStepAngle_=std::max(carryStepAngle_,Angle(raw.rawLeftWristWorldMeters,*carryPrevious_));}
    carryPrevious_=raw.rawLeftWristWorldMeters;
    carryDisplacement_=std::max(carryDisplacement_,Distance(raw.rawLeftWristWorldMeters,*carryStart_));
    carryAngle_=std::max(carryAngle_,Angle(raw.rawLeftWristWorldMeters,*carryStart_));
    const float position=Distance(raw.rawLeftWristWorldMeters,desired),angle=Angle(raw.rawLeftWristWorldMeters,desired);
    if(position<.003f&&angle<.05f){if(!alignedAt_)alignedAt_=now;}else alignedAt_=0;
    if(alignedAt_&&now-alignedAt_>=100000000){
     if(!carryReturning_&&carryDisplacement_>=.10f&&carryAngle_>=.5f){carryExercised_=true;carryReturning_=true;alignedAt_=0;}
     else if(carryReturning_)PhaseTo(Phase::Release,now);
    }
    if(now-rowAt_>=100000000){rowAt_=now;if(rowCount_<rows_.size())rows_[rowCount_++]={unsigned(phase_),0,now,in.generation,
     raw.inputEvidence.sequence,state.cycle,position,angle,0,state.reserve?state.reserve->loaded:-1,state.reserve?state.reserve->reserve:-1,packs_.pairs};else ++dropped_;}
   }else Fail(7,now);
  }
 }
 if(phase_==Phase::Pouch||phase_==Phase::GrabReplacement){
  math::Pose pouch;const auto c=chestSupply_?ChestAmmoSupply():Bc2MagazinePhysicalReload::DefaultPouch();pouch.position={c.pouchCenterMeters[0],c.pouchCenterMeters[1],-c.pouchCenterMeters[2]};
  command_=Step(command_,pouch);
  if(phase_==Phase::Pouch&&newRaw&&original->frame.hands[0].squeeze<=.35f){
   const auto actual=chestSupply_?BodyAnchorHandPose(original->frame,InteractionHand::Left):PhysicalReloadPouchPose(original->frame),target=Controller(pouch);
   if(actual&&target&&Distance(*actual,*target)<.01f&&Angle(*actual,*target)<.05f){
    if(!alignedAt_)alignedAt_=now;if(now-alignedAt_>=100000000)PhaseTo(Phase::GrabReplacement,now);
   }else alignedAt_=0;
  }
  if(phase_==Phase::GrabReplacement&&counts->acquired==1&&state.ownsHand&&state.phase==DetachableMagazinePhase::ReplacementHeld)PhaseTo(Phase::ApproachRail,now);
 }
 if((originalReturn_?state.originalReturning:counts->submitted==1)&&phase_>=Phase::ApproachRail&&phase_<=Phase::Stroke)PhaseTo(Phase::WaitAck,now);
 if(phase_==Phase::WaitBaseline&&receiptVerified_&&!state.active&&!state.retiring&&!state.pending&&!state.blocksEquipment&&
  state.phase==DetachableMagazinePhase::Attached&&removedPair_&&(originalReturn_?originalReturns_==1:(hiddenPair_&&replacementPair_))){
  PhaseTo(Phase::Done,now);
  if(returnThenReplace_&&!secondCycle_){
   // Restart only the synthetic driver. Never reset consumer/native counters,
   // ownership, ammo, or renderer receipts. The same exact owner and original
   // 30-second outer deadline cover both actual interactions.
   try {
    std::ostringstream evidence;Report(evidence);
    Bc2MagazinePhysicalProbe next(true,false,true,false,chestSupply_);
    next.returnThenReplace_=true;next.secondCycle_=true;next.firstCycleReport_=evidence.str();
    next.first_=first_;next.phaseAt_=now;next.lastNow_=now;next.owner_=owner_;
    next.command_=command_;next.packs_=packs_;next.baseline_=packs_;
    if(!next.EpisodeCounts(state,now,true)){phase_=Phase::WaitBaseline;Fail(21,now);return;}
    *this=std::move(next);
   }catch(...){phase_=Phase::WaitBaseline;Fail(19,now);}
  }
 }
 const auto elapsed=now-phaseAt_;
 if((phase_==Phase::ApproachMagazine||phase_==Phase::Pouch||phase_==Phase::ApproachRail)&&elapsed>4500000000ll)Fail(8,now);
 if((phase_==Phase::Grip||phase_==Phase::WaitAck||phase_==Phase::WaitBaseline)&&elapsed>4000000000ll)Fail(9,now);
 if((phase_==Phase::Pull||phase_==Phase::Enter||phase_==Phase::Stroke)&&elapsed>3500000000ll)Fail(10,now);
 if((phase_==Phase::Release||phase_==Phase::GrabReplacement)&&elapsed>1500000000ll)Fail(11,now);
 if(phase_==Phase::Carry&&elapsed>1500000000ll)Fail(18,now);
 const bool squeezing=phase_==Phase::Carry||(phase_>=Phase::Grip&&phase_<=Phase::Pull)||(phase_>=Phase::GrabReplacement&&phase_<=Phase::WaitAck);
 in.hands[0].grip=command_;in.hands[0].aim=command_;in.hands[0].squeeze=squeezing?(carryChallenge_&&(phase_==Phase::Pull||phase_==Phase::Carry)?.6f:1.f):0.f;
 lastInput_=in;history_[historyNext_]=Input{in,owner,observed,deadline};historyNext_=(historyNext_+1)%history_.size();
}
void Bc2MagazinePhysicalProbe::Observe(const MagazinePhysicalProbeState& s,const MagazinePackCounters& packs,std::int64_t now)noexcept {
 if(!enabled_||CancelConsumer())return;
 const auto counts=EpisodeCounts(s,now);if(!counts)return;
 if(packs.copies<packs_.copies||packs.pairs<packs_.pairs||packs.fallbacks<packs_.fallbacks){Fail(12,now);return;}
 for(unsigned n=0;n<4;++n)if(packs.roleCopies[n]<packs_.roleCopies[n]||packs.rolePairs[n]<packs_.rolePairs[n]){Fail(12,now);return;}
 packs_=packs;submitted_=counts->submitted;acquired_=counts->acquired;
 if(originalReturn_){
  if(s.original){
   if(original_&&*original_!=*s.original){Fail(14,now);return;}
   if(!original_){original_=s.original;originalCycle_=s.cycle;}
  }
  if(s.reserve&&Fresh(*s.reserve,owner_,now)&&loadedBefore_>=0&&
    (s.reserve->loaded!=loadedBefore_||s.reserve->reserve!=reserveBefore_||s.reserve->capacity!=capacityBefore_)){Fail(15,now);return;}
  if(counts->returned>originalReturns_){
   const auto& receipt=s.originalReceipt;
   if(!original_||!receipt||!receipt->verified||receipt->original!=*original_||receipt->cycle!=originalCycle_||
     !receipt->seat||!receipt->retirement||receipt->observedNs<=0||receipt->observedNs>now||receipt->deadlineNs<=now||
     receipt->deadlineNs-receipt->observedNs>200000000||!removedPair_||(phase_!=Phase::WaitAck&&phase_!=Phase::Stroke)||
     !s.reserve||!Fresh(*s.reserve,owner_,now)||!s.reserve->reloadInputReady||
     s.reserve->loaded!=loadedBefore_||s.reserve->reserve!=reserveBefore_||s.reserve->capacity!=capacityBefore_){Fail(16,now);return;}
   originalReceipt_=receipt;originalReturns_=counts->returned;receiptVerified_=true;PhaseTo(Phase::WaitBaseline,now);
  }
 }
 const auto newPair=[&](unsigned role){if(!phasePairs_[role])phasePairs_[role]=packs.rolePairs[role];
  return packs.rolePairs[role]>*phasePairs_[role]&&packs.rolePairs[role]>baseline_.rolePairs[role];};
 if(s.phase==DetachableMagazinePhase::RemovedHeld&&newPair(1))removedPair_=true;
 if(s.phase==DetachableMagazinePhase::WellEmpty&&newPair(3))hiddenPair_=true;
 if((s.phase==DetachableMagazinePhase::Guided||s.phase==DetachableMagazinePhase::AwaitingSeat)&&newPair(2))replacementPair_=true;
 if(counts->completed>completed_){
  if(counts->completed!=1||counts->submitted!=1||!s.reserve||!Fresh(*s.reserve,owner_,now)||expectedUnits_<=0||
   s.reserve->capacity!=capacityBefore_||s.reserve->loaded!=loadedBefore_+expectedUnits_||s.reserve->reserve!=reserveBefore_-expectedUnits_){Fail(13,now);return;}
  completed_=counts->completed;receiptVerified_=true;PhaseTo(Phase::WaitBaseline,now);
 }
}
void Bc2MagazinePhysicalProbe::Report(std::ostream& o)const {
 const auto precision=o.precision();o.precision(std::numeric_limits<float>::max_digits10);
 const auto counters=[&](const Counts& c){o<<"{\"acquired\":"<<c.acquired<<",\"started\":"<<c.started
  <<",\"submitted\":"<<c.submitted<<",\"completed\":"<<c.completed<<",\"original_returns\":"<<c.returned<<'}';};
 o<<"{\"enabled\":"<<(enabled_?"true":"false")<<",\"synthetic_input\":true,\"headset_verified\":false,\"eye_textures_verified\":false"
  <<",\"counter_baseline\":";if(counterBaseline_)counters(*counterBaseline_);else o<<"null";
 o<<",\"counter_latest\":";counters(counterLatest_);
 o<<",\"chest_supply\":"<<(chestSupply_?"true":"false")
  <<",\"return_then_replace\":"<<(returnThenReplace_?"true":"false")<<",\"second_cycle\":"<<(secondCycle_?"true":"false")
  <<",\"first_cycle\":"<<(firstCycleReport_.empty()?"null":firstCycleReport_)
  <<",\"original_return_fixture\":"<<(originalReturn_?"true":"false")
  <<",\"carry_challenge\":"<<(carryChallenge_?"true":"false")<<",\"carry_exercised\":"<<(carryExercised_?"true":"false")
  <<",\"carry_displacement_m\":"<<carryDisplacement_<<",\"carry_angle_rad\":"<<carryAngle_<<",\"carry_raw_samples\":"<<carrySamples_
  <<",\"carry_wait_packets\":"<<carryWaits_<<",\"carry_wait_started_ns\":"<<carryWaitAt_
  <<",\"carry_failure_flags\":"<<carryFailureFlags_<<",\"carry_failure_ns\":"<<carryFailureAt_
  <<",\"carry_max_raw_step_m\":"<<carryStepTranslation_<<",\"carry_max_raw_step_rad\":"<<carryStepAngle_
  <<",\"original_returns\":"<<originalReturns_<<",\"acquired\":"<<acquired_
  <<",\"original_item\":"<<(original_?original_->item.id:0)<<",\"original_generation\":"<<(original_?original_->item.generation:0)
  <<",\"original_rounds\":"<<(original_?original_->rounds:0)<<",\"original_cycle\":"<<originalCycle_
  <<",\"original_return_seat\":"<<(originalReceipt_?originalReceipt_->seat:0)<<",\"original_return_retirement\":"<<(originalReceipt_?originalReceipt_->retirement:0)
  <<",\"phase\":"<<unsigned(phase_)<<",\"failure\":"<<failure_<<",\"actual_consumer_completed\":"<<(phase_==Phase::Done?"true":"false")
  <<",\"native_receipt\":"<<(receiptVerified_?"true":"false")<<",\"submitted\":"<<submitted_<<",\"completed\":"<<completed_
  <<",\"loaded_before\":"<<loadedBefore_<<",\"reserve_before\":"<<reserveBefore_<<",\"expected_units\":"<<expectedUnits_
  <<",\"removed_pair\":"<<removedPair_<<",\"hidden_pair\":"<<hiddenPair_<<",\"replacement_pair\":"<<replacementPair_
  <<",\"copies\":"<<packs_.copies<<",\"pairs\":"<<packs_.pairs<<",\"fallbacks\":"<<packs_.fallbacks<<",\"row_dropped\":"<<dropped_<<",\"rows\":[";
 for(unsigned n=0;n<rowCount_;++n){const auto& r=rows_[n];if(n)o<<',';o<<"{\"phase\":"<<r.phase<<",\"reason\":"<<r.reason<<",\"now_ns\":"<<r.now
  <<",\"input\":"<<r.input<<",\"raw\":"<<r.raw<<",\"cycle\":"<<r.cycle<<",\"position_error_m\":"<<r.positionError
  <<",\"angle_error_rad\":"<<r.angleError<<",\"rail_m\":"<<r.rail<<",\"loaded\":"<<r.loaded<<",\"reserve\":"<<r.reserve<<",\"pairs\":"<<r.pairs<<'}';}
 o<<"]}";o.precision(precision);
}
} // namespace fvr::bc2
