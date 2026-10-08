#include "Bc2MagazineDetachedProbe.h"
#include "Bc2PhysicalReloadProbe.h"
#include <algorithm>
#include <cmath>
#include <limits>
namespace fvr::bc2 {
namespace {
using namespace interaction;using namespace reload_insertion_detail;
std::optional<math::Matrix4> Matrix(const math::Pose& p)noexcept{const auto v=math::MakeLhViewFromOpenXRPose(p);return v?InverseRigid(*v):std::nullopt;}
bool Static(const InputFrame& s)noexcept{const auto a=Matrix(s.referenceHead),b=Matrix(s.head);return a&&b&&Distance(*a,Identity())<.0001f&&Angle(*a,Identity())<.001f&&Distance(*b,Identity())<.0001f&&Angle(*b,Identity())<.001f;}
bool Fresh(const Bc2AmmoReserveLease& r,const ReloadStateOwner& o,std::int64_t now)noexcept{return r.verified&&r.allThreeIdle&&r.identity.owner==o&&r.sequence&&r.observedNs>0&&r.observedNs<=now&&r.deadlineNs>now&&r.deadlineNs-r.observedNs<=200000000;}
bool SameCounts(const Bc2AmmoReserveLease& a,const Bc2AmmoReserveLease& b)noexcept{return a.identity==b.identity&&a.loaded==b.loaded&&a.reserve==b.reserve&&a.capacity==b.capacity;}
math::Pose Step(const math::Pose& from,const math::Pose& target)noexcept{
 auto out=target;const float d=std::hypot(target.position.x-from.position.x,target.position.y-from.position.y,target.position.z-from.position.z),t=d>.01f?.01f/d:1;
 out.position={from.position.x+(target.position.x-from.position.x)*t,from.position.y+(target.position.y-from.position.y)*t,from.position.z+(target.position.z-from.position.z)*t};
 auto q=target.orientation;const auto a=from.orientation;float dot=a.x*q.x+a.y*q.y+a.z*q.z+a.w*q.w;if(dot<0){q={-q.x,-q.y,-q.z,-q.w};dot=-dot;}
 const float angle=2*std::acos(std::clamp(dot,0.f,1.f)),u=angle>.05f?.05f/angle:1;
 q={a.x+(q.x-a.x)*u,a.y+(q.y-a.y)*u,a.z+(q.z-a.z)*u,a.w+(q.w-a.w)*u};
 const float n=std::sqrt(q.x*q.x+q.y*q.y+q.z*q.z+q.w*q.w);out.orientation={q.x/n,q.y/n,q.z/n,q.w/n};return out;
}
}
void Bc2MagazineDetachedProbe::Move(Phase phase,std::int64_t now)noexcept{phase_=phase;phaseAt_=now;alignedAt_=0;if(rowCount_<rows_.size())rows_[rowCount_++]={unsigned(phase),failure_,now};else ++dropped_;}
void Bc2MagazineDetachedProbe::Fail(unsigned reason,std::int64_t now)noexcept{if(Finished())return;failure_=reason;Move(Phase::Failed,now);}
void Bc2MagazineDetachedProbe::Prepare(InputFrame& in,const ReloadStateOwner& owner,std::string_view asset,const MagazineRawContact& raw,
 const std::optional<Bc2AmmoReserveLease>& reserve,MagazineDetachPhase controller,std::int64_t observed,std::int64_t deadline,std::int64_t now)noexcept{
 if(!enabled_)return;
 if(Finished()){in.hands[0].grip=in.hands[0].aim=command_;in.hands[0].squeeze=0;return;}
 if(!first_){first_=phaseAt_=now;owner_=owner;command_=in.hands[0].grip;removedPairBaseline_=packs_.rolePairs[1];}
 if(now<lastNow_||now-first_>=30000000000ll)Fail(1,now);lastNow_=now;
 if(owner!=owner_||asset!=Xm8MagazineAsset||!ValidInput(in)||!in.focused||!in.headValid||!Static(in)||!in.hands[0].gripTracked||!in.hands[1].gripTracked||!in.hands[1].aimTracked||observed<=0||observed>now||deadline<=now||deadline-observed>100000000)Fail(2,now);
 if(lastInput_&&in.generation<=lastInput_->generation){if(in.generation<lastInput_->generation)Fail(3,now);in.hands[0]=lastInput_->hands[0];if(Finished())in.hands[0].squeeze=0;return;}
 if(reserve&&Fresh(*reserve,owner,now)&&baseline_&&!SameCounts(*reserve,*baseline_))Fail(4,now);
 if(controller==MagazineDetachPhase::Recovering)Fail(5,now);
 const Input* original=nullptr;
 if(raw.valid&&raw.owner==owner&&raw.rigFingerprint==Xm8MagazineRig)for(const auto& h:history_)if(h&&h->owner==owner&&h->frame.generation==raw.inputEvidence.sequence&&h->observed==raw.inputEvidence.observedNs&&h->deadline==raw.inputEvidence.deadlineNs&&h->deadline>now){original=&*h;break;}
 const bool newRaw=original&&raw.inputEvidence.sequence>lastRaw_;
 if(phase_==Phase::Warmup){
  ++warmup_.samples;warmup_.now=now;warmup_.input=in.generation;warmup_.raw=raw.inputEvidence.sequence;
  warmup_.reserveObserved=reserve?reserve->observedNs:0;warmup_.reserveDeadline=reserve?reserve->deadlineNs:0;
  if(!reserve)++warmup_.reserveAbsent;
  else{if(reserve->observedNs>now)++warmup_.reserveFuture;
   if(!Fresh(*reserve,owner,now))++warmup_.reserveInvalid;
   if(!reserve->allThreeIdle)++warmup_.reserveNonIdle;
   if(reserve->capacity!=30||reserve->loaded<=0||reserve->loaded>30||reserve->reserve<0||
      (reserve->loaded!=30&&reserve->reserve!=0))++warmup_.reserveIneligible;}
  if(!raw.valid)++warmup_.rawAbsent;
  else if(!original)++warmup_.rawUnmatched;
  else if(!newRaw)++warmup_.rawRepeated;
  if(raw.valid&&!raw.nativeMagazineAttached)++warmup_.rawDetached;
 }
 if(phase_==Phase::Warmup&&now-first_>=6000000000ll){
  if(reserve&&Fresh(*reserve,owner,now)&&reserve->capacity==30&&reserve->loaded>0&&reserve->loaded<=30&&reserve->reserve>=0&&
    (reserve->loaded==30||reserve->reserve==0)&&controller==MagazineDetachPhase::Idle&&newRaw&&raw.nativeMagazineAttached){baseline_=reserve;baselineAccepted_=now;Move(Phase::Approach,now);}
  else if(now-first_>=10000000000ll)Fail(6,now);
 }
 float along=.1f;const bool solve=phase_>=Phase::Approach&&phase_<=Phase::WaitAttached;
 if(phase_==Phase::Pull)along=.1f-.12f*std::clamp(float(now-phaseAt_)/900000000.f,0.f,1.f);
 if(phase_==Phase::Withdraw)along=-.11f;
 if(phase_==Phase::Enter)along=-.025f;
 if(phase_==Phase::Stroke)along=-.025f+.125f*std::clamp(float(now-phaseAt_)/1200000000.f,0.f,1.f);
 if(solve&&newRaw&&!Finished()){
  const auto p=Xm8MagazineConfig().insertion;
  const auto desired=Multiply(p.itemFromHand,Multiply(*InverseRigid(p.itemFromInsertion),Multiply(TravelPose(p,along),Multiply(p.weaponFromEntry,raw.weaponWorldMeters))));
  if(const auto control=PhysicalReloadProbeController(raw,original->frame,desired)){
   command_=Step(command_,*control);lastRaw_=raw.inputEvidence.sequence;
   const float position=Distance(raw.rawLeftWristWorldMeters,desired),angle=Angle(raw.rawLeftWristWorldMeters,desired);
   if(position<.003f&&angle<.05f){if(!alignedAt_)alignedAt_=now;}else alignedAt_=0;
   const bool settled=alignedAt_&&now-alignedAt_>=100000000;
   if(phase_==Phase::Approach&&settled&&original->frame.hands[0].squeeze<=.35f)Move(Phase::Grip,now);
   else if(phase_==Phase::Grip&&controller==MagazineDetachPhase::Detached&&claimed_&&firstCommit_)Move(Phase::Pull,now);
   else if(phase_==Phase::Pull&&settled&&removedPair_)Move(Phase::Withdraw,now);
   else if(phase_==Phase::Withdraw&&settled)Move(Phase::Enter,now);
   else if(phase_==Phase::Enter&&settled&&insertionPhase_==ReloadInsertionPhase::Guided)Move(Phase::Stroke,now);
   if(now-rowAt_>=100000000){rowAt_=now;if(rowCount_<rows_.size())rows_[rowCount_++]={unsigned(phase_),0,now,in.generation,raw.inputEvidence.sequence,lastCommitTick_,position,angle,along,reserve?reserve->loaded:-1,reserve?reserve->reserve:-1};else ++dropped_;}
  }else Fail(7,now);
 }
 const auto elapsed=now-phaseAt_;
 if((phase_==Phase::Approach||phase_==Phase::Withdraw)&&elapsed>4500000000ll)Fail(8,now);
 if((phase_==Phase::Grip||phase_==Phase::WaitAttached||phase_==Phase::WaitAvailable)&&elapsed>4000000000ll)Fail(9,now);
 if((phase_==Phase::Pull||phase_==Phase::Enter||phase_==Phase::Stroke)&&elapsed>3500000000ll)Fail(10,now);
 in.hands[0].grip=in.hands[0].aim=command_;in.hands[0].squeeze=phase_>=Phase::Grip&&phase_<=Phase::WaitAttached?1.f:0.f;
 lastInput_=in;history_[historyNext_]=Input{in,owner,observed,deadline};historyNext_=(historyNext_+1)%history_.size();
}
void Bc2MagazineDetachedProbe::Observe(const Bc2MagazineDetached& controller,const MagazinePhysicalResult& result,
 const std::optional<HolsterSuppressionReceipt>& receipt,const std::optional<MagazineDetachPairReceipt>& pair,
 const std::optional<Bc2AmmoReserveLease>& reserve,const MagazinePackCounters& packs,bool requested,std::int64_t now)noexcept{
 if(!enabled_||Finished())return;
 if(packs.pairs<packs_.pairs||packs.copies<packs_.copies||packs.fallbacks<packs_.fallbacks){Fail(11,now);return;}packs_=packs;
 if(result.acquired||result.submitted||result.completed||result.reloadHeld){Fail(12,now);return;}
 if(controller.Recovered()||controller.Grabs()>1||controller.Seats()>1){Fail(18,now);return;}
 if(controller.Phase()==MagazineDetachPhase::Recovering){Fail(5,now);return;}
 if(reserve&&Fresh(*reserve,owner_,now)&&baseline_&&!SameCounts(*reserve,*baseline_)){Fail(4,now);return;}
 if(receipt){if(!requested||receipt->nativeTick<lastCommitTick_||receipt->owner!=owner_){Fail(13,now);return;}
  ++commits_;lastCommitTick_=receipt->nativeTick;if(!firstCommit_){firstCommit_=now;firstCommitTick_=receipt->nativeTick;}}
 const auto auth=result.tracking.detach;
 if(result.tracking.retainedVisualSuppression){
  // A repeated visual is not this callback's native count authorization,
  // contact or pair receipt. Keep the already observed fixture phase.
  if(reserve||!receipt||!auth||!authorization_||!baseline_||!result.ownsLeftHand||
   !MagazineTargetFresh(result.tracking,now)||auth->original!=authorization_->original||
   !SameCounts(auth->baseline,*baseline_)||result.interaction.insertion.seat||result.interaction.originalSeat||
   !HolsterSuppressionCurrent(*receipt,*result.tracking.retainedVisualSuppression)){Fail(14,now);return;}
  claimed_=true;return;
 }
 if(auth){
  if(!receipt||!baseline_||!MagazineDetachAuthorizationFresh(*auth,now)||auth->suppression.nativeTick!=receipt->nativeTick||
    !HolsterSuppressionCurrent(*receipt,auth->suppression)||!SameCounts(auth->baseline,*baseline_)||
    (authorization_&&authorization_->original!=auth->original)){Fail(14,now);return;}
  if(!authorization_)authorization_=*auth;
  if(auth->restoring)returnAuthorization_=*auth;
 }
 claimed_=result.ownsLeftHand;insertionPhase_=result.interaction.insertion.phase;
 if(result.tracking.target&&result.tracking.target->role==MagazinePropRole::Removed){
  if(!auth||!receipt||auth->restoring||!firstCommit_||!MagazineTargetFresh(result.tracking,now)){Fail(15,now);return;}
  ++removedPublications_;if(!firstRemoved_){firstRemoved_=now;firstRemovedTick_=receipt->nativeTick;}
  if(pair&&MagazineDetachPairCurrent(*pair,*auth,false,now)&&packs.rolePairs[1]>removedPairBaseline_)removedPair_=true;
 }
 if(result.tracking.target&&result.tracking.target->role==MagazinePropRole::Attached&&auth&&auth->restoring)++attachedPublications_;
 if(result.interaction.insertion.seat){
  if(seat_||!authorization_||!removedPair_||phase_!=Phase::Stroke||result.interaction.insertion.seat->identity.item!=authorization_->original.item){Fail(16,now);return;}
  seat_=result.interaction.insertion.seat;seatAt_=now;Move(Phase::WaitAttached,now);
 }
 if(controller.Returned()>returned_){
  if(controller.Returned()!=1||!seat_||!authorization_||!returnAuthorization_||!pair||!receipt||!requested||
    !MagazineDetachPairCurrent(*pair,*returnAuthorization_,true,now)||pair->observedNs<seatAt_||
    pair->authorization.original!=authorization_->original||!reserve||!Fresh(*reserve,owner_,now)||!SameCounts(*reserve,*baseline_)||
    controller.BlocksActions()||result.blocksWeaponActions||phase_!=Phase::WaitAttached){Fail(17,now);return;}
  returned_=controller.Returned();attachedReceipt_=pair;attachedPair_=true;finishedAt_=now;Move(Phase::WaitAvailable,now);
 }else if(controller.Returned()<returned_||controller.Returned()>1){Fail(17,now);return;}
 if(phase_==Phase::WaitAvailable&&now>finishedAt_){
  if(controller.Phase()==MagazineDetachPhase::Idle&&!controller.BlocksActions()&&!result.blocksWeaponActions&&!requested&&!receipt&&
    reserve&&Fresh(*reserve,owner_,now)&&SameCounts(*reserve,*baseline_)){
   if(!neutralSince_)neutralSince_=now;++neutralCallbacks_;
   if(now-neutralSince_>=100000000&&neutralCallbacks_>=2){released_=true;Move(Phase::Done,now);}
  }else neutralSince_=0;
 }
}
void Bc2MagazineDetachedProbe::Report(std::ostream& o)const{
 const auto precision=o.precision();o.precision(std::numeric_limits<float>::max_digits10);
 o<<"{\"enabled\":"<<(enabled_?"true":"false")<<",\"synthetic_input\":true,\"headset_verified\":false,\"eye_textures_verified\":false,\"fire_execution_verified\":false"
  <<",\"phase\":"<<unsigned(phase_)<<",\"failure\":"<<failure_<<",\"actual_consumer_completed\":"<<(phase_==Phase::Done?"true":"false")
  <<",\"loaded_before\":"<<(baseline_?baseline_->loaded:-1)<<",\"reserve_before\":"<<(baseline_?baseline_->reserve:-1)<<",\"capacity_before\":"<<(baseline_?baseline_->capacity:-1)
  <<",\"baseline_observed_ns\":"<<(baseline_?baseline_->observedNs:0)<<",\"baseline_deadline_ns\":"<<(baseline_?baseline_->deadlineNs:0)
  <<",\"baseline_accepted_ns\":"<<baselineAccepted_<<",\"baseline_sequence\":"<<(baseline_?baseline_->sequence:0)
  <<",\"baseline_owner\":{\"player\":"<<(baseline_?baseline_->identity.owner.player:0)<<",\"soldier\":"<<(baseline_?baseline_->identity.owner.soldier:0)
  <<",\"weak\":"<<(baseline_?baseline_->identity.owner.weak:0)<<",\"weapon\":"<<(baseline_?baseline_->identity.owner.weapon:0)
  <<",\"actor_generation\":"<<(baseline_?baseline_->identity.owner.actorGeneration:0)<<",\"equip_generation\":"<<(baseline_?baseline_->identity.owner.equipGeneration:0)
  <<",\"space\":"<<(baseline_?baseline_->identity.owner.space:0)<<'}'
  <<",\"original_item\":"<<(authorization_?authorization_->original.item.id:0)<<",\"original_generation\":"<<(authorization_?authorization_->original.item.generation:0)
  <<",\"request\":"<<(authorization_?authorization_->request:0)<<",\"seat\":"<<(seat_?seat_->id:0)<<",\"returned\":"<<returned_
  <<",\"first_authorized_commit_ns\":"<<(authorization_?authorization_->committedNs:0)<<",\"first_post_commit_count_ns\":"<<(authorization_?authorization_->current.observedNs:0)
  <<",\"first_cache\":"<<(authorization_?authorization_->suppression.cache:0)<<",\"first_authorized_count_sequence\":"<<(authorization_?authorization_->current.sequence:0)
  <<",\"suppression_commits\":"<<commits_<<",\"first_commit_ns\":"<<firstCommit_<<",\"first_commit_tick\":"<<firstCommitTick_
  <<",\"first_removed_ns\":"<<firstRemoved_<<",\"first_removed_tick\":"<<firstRemovedTick_<<",\"seat_ns\":"<<seatAt_<<",\"returned_ns\":"<<finishedAt_
  <<",\"removed_publications\":"<<removedPublications_<<",\"attached_publications\":"<<attachedPublications_
  <<",\"removed_pair\":"<<removedPair_<<",\"attached_pair\":"<<attachedPair_<<",\"attached_pair_draw\":"<<(attachedReceipt_?attachedReceipt_->drawSerial:0)
  <<",\"attached_pair_ns\":"<<(attachedReceipt_?attachedReceipt_->observedNs:0)<<",\"suppression_released\":"<<released_<<",\"neutral_callbacks\":"<<neutralCallbacks_
  <<",\"attached_pair_mask\":"<<(attachedReceipt_?attachedReceipt_->copyMask:0)<<",\"attached_pair_original_item\":"<<(attachedReceipt_?attachedReceipt_->authorization.original.item.id:0)
  <<",\"attached_pair_original_generation\":"<<(attachedReceipt_?attachedReceipt_->authorization.original.item.generation:0)<<",\"attached_pair_request\":"<<(attachedReceipt_?attachedReceipt_->authorization.request:0)
  <<",\"warmup\":{\"samples\":"<<warmup_.samples<<",\"reserve_absent\":"<<warmup_.reserveAbsent
  <<",\"reserve_future\":"<<warmup_.reserveFuture<<",\"reserve_invalid\":"<<warmup_.reserveInvalid<<",\"reserve_nonidle\":"<<warmup_.reserveNonIdle
  <<",\"reserve_ineligible\":"<<warmup_.reserveIneligible<<",\"raw_absent\":"<<warmup_.rawAbsent<<",\"raw_unmatched\":"<<warmup_.rawUnmatched
  <<",\"raw_repeated\":"<<warmup_.rawRepeated<<",\"raw_detached\":"<<warmup_.rawDetached<<",\"last_now_ns\":"<<warmup_.now
  <<",\"last_reserve_observed_ns\":"<<warmup_.reserveObserved<<",\"last_reserve_deadline_ns\":"<<warmup_.reserveDeadline
  <<",\"last_input\":"<<warmup_.input<<",\"last_raw\":"<<warmup_.raw<<'}'
  <<",\"copies\":"<<packs_.copies<<",\"pairs\":"<<packs_.pairs<<",\"fallbacks\":"<<packs_.fallbacks<<",\"row_dropped\":"<<dropped_<<",\"rows\":[";
 for(unsigned n=0;n<rowCount_;++n){const auto& r=rows_[n];if(n)o<<',';o<<"{\"phase\":"<<r.phase<<",\"reason\":"<<r.reason<<",\"now_ns\":"<<r.now<<",\"input\":"<<r.input<<",\"raw\":"<<r.raw<<",\"native_tick\":"<<r.tick<<",\"position_error_m\":"<<r.position<<",\"angle_error_rad\":"<<r.angle<<",\"rail_m\":"<<r.rail<<",\"loaded\":"<<r.loaded<<",\"reserve\":"<<r.reserve<<'}';}
 o<<"]}";o.precision(precision);
}
}
