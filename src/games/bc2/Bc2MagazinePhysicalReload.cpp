#include "Bc2MagazinePhysicalReload.h"
#include <limits>
namespace fvr::bc2 {
namespace {
using namespace interaction;using namespace reload_insertion_detail;
bool SameInput(const HandInteractionSample& a,const HandInteractionSample& b)noexcept {
 return a.owner==b.owner&&a.sequence==b.sequence&&a.observedNs==b.observedNs&&a.deadlineNs==b.deadlineNs&&
 a.focused==b.focused&&a.tracked==b.tracked&&a.released==b.released;
}
bool Fresh(std::int64_t a,std::int64_t b,std::int64_t now)noexcept{return a>0&&a<=now&&b>now&&b-a<=200000000;}
bool CanSupply(DetachableMagazinePhase p)noexcept{return p==DetachableMagazinePhase::WellEmpty||
 p==DetachableMagazinePhase::ReplacementHeld||p==DetachableMagazinePhase::Guided;}
}
interaction::AmmoSupplyConfig Bc2MagazinePhysicalReload::DefaultPouch()noexcept {
 return {InteractionHand::Left,0x4243324d41474full,{0x4243324d504f55ull,1},{-.23f,-.55f,.02f},.18f,200000000};
}
Bc2MagazinePhysicalReload::Bc2MagazinePhysicalReload(bool enabled,MagazinePhysicalApi api,AmmoSupplyConfig pouch)noexcept
 :supply_(pouch),pouch_(pouch),api_(api),enabled_(enabled){}
ReloadKeepAliveResult Bc2MagazinePhysicalReload::Keep(const ReloadCycleControl& c,std::int64_t now)noexcept {
 const auto result=api_.keepObserved?api_.keepObserved(api_.context,c):
  api_.keep(api_.context,c)?ReloadKeepAliveResult::Accepted:ReloadKeepAliveResult::Rejected;
 if(result==ReloadKeepAliveResult::Accepted)acceptedControlDeadline_=c.deadlineNs;
 if(result==ReloadKeepAliveResult::Deferred&&!ReloadKeepAliveDeferredWithinOriginalDeadline(result,now,acceptedControlDeadline_))return ReloadKeepAliveResult::Rejected;
 return result;
}
bool Bc2MagazinePhysicalReload::Api()const noexcept{return api_.reserve&&api_.identity&&api_.start&&api_.inspectStart&&api_.keep&&api_.lease&&
 api_.gate&&api_.submit&&api_.ack&&api_.cancel&&api_.retire;}
std::int64_t Bc2MagazinePhysicalReload::Now(std::int64_t fallback)const noexcept{return api_.clock?api_.clock(api_.context):fallback;}
void Bc2MagazinePhysicalReload::PublishReplacementFrame(MagazineTracking& tracking,const MagazinePhysicalSample& s,bool canCapture)noexcept {
 if(tracking.target&&tracking.target->role==MagazinePropRole::Removed&&last_.insertion.phase==ReloadInsertionPhase::Free){
  replacementFrame_.reset();const auto& v=*tracking.target;
  if(!removalFrame_||removalFrame_->owner!=v.owner||removalFrame_->item!=v.item||removalFrame_->handClaim!=v.handClaim||
   removalFrame_->gunClaim!=v.gunClaim||removalFrame_->inputSequence!=v.inputSequence||removalFrame_->cycle!=v.nativeCycle||
   removalFrame_->observedNs!=v.observedNs||removalFrame_->deadlineNs!=v.deadlineNs){
   removalFrame_.reset();if(canCapture)removalFrame_=MagazineRemovalFrame(tracking,s.raw);
  }
  if(removalFrame_)tracking.removalFrame=removalFrame_;else tracking.target.reset();return;
 }
 removalFrame_.reset();
 if(!tracking.target||tracking.target->role!=MagazinePropRole::Replacement||last_.insertion.phase!=ReloadInsertionPhase::Free){replacementFrame_.reset();return;}
 const auto& v=*tracking.target;
 const auto matches=[&](const MagazineReplacementFrame& f){return f.owner==v.owner&&f.item==v.item&&f.handClaim==v.handClaim&&
  f.gunClaim==v.gunClaim&&f.inputSequence==v.inputSequence&&f.cycle==v.nativeCycle&&f.observedNs==v.observedNs&&f.deadlineNs==v.deadlineNs;};
 // Same source sequence is immutable, even when native/renderer reads repeat.
 if(!replacementFrame_||!matches(*replacementFrame_)){
  replacementFrame_.reset();const auto& raw=s.raw;
  if(canCapture&&raw.valid&&raw.owner==s.nativeOwner&&s.originalHandEvidence&&SameInput(raw.inputEvidence,*s.originalHandEvidence)&&
   raw.inputEvidence.owner==v.owner&&raw.inputEvidence.sequence==v.inputSequence&&raw.inputEvidence.observedNs==v.observedNs&&
   raw.inputEvidence.deadlineNs>=v.deadlineNs&&Rigid(raw.weaponWorldMeters))
    replacementFrame_=MagazineReplacementFrame{v.owner,v.item,v.handClaim,v.gunClaim,v.inputSequence,v.nativeCycle,v.observedNs,v.deadlineNs,raw.weaponWorldMeters};
 }
 if(replacementFrame_)tracking.replacementFrame=replacementFrame_;
 else tracking.target.reset(); // Missing carry basis never becomes a gun-relative replacement.
}
void Bc2MagazinePhysicalReload::ObserveRetirement(const MagazinePhysicalSample& s)noexcept {
 if(retiring_&&api_.retire){
  if(retirement_&&retirement_->deadlineNs<=s.input.nowNs)retirement_.reset();
  if(!retirement_)retirement_=api_.retire(api_.context,owners_.native,owners_.cycle);
  if(retirement_&&retirement_->verified&&retirement_->identity==owners_.native&&retirement_->cycle==owners_.cycle&&
   retirement_->event&&Fresh(retirement_->observedNs,retirement_->deadlineNs,s.input.nowNs))drained_=true;
 }
 // Callback drain is durable old-cycle retirement, not an ammo outcome. An
 // unrelated current item can become usable while old supply stays quarantined.
 const bool same=s.nativeOwner==owners_.native.owner&&s.input.owner==owners_.physical&&s.asset==profile_->geometry->asset;
 blocksCurrent_=active_||(retiring_&&(!drained_||same));
}
void Bc2MagazinePhysicalReload::Record(unsigned kind,const HandInteractionSample& in,std::uint64_t geometry)noexcept {
 if(eventCount_==events_.size()){++eventDropped_;return;}
 const auto* pending=supply_.Pending()?&*supply_.Pending():nullptr;
 events_[eventCount_++]={kind,unsigned(last_.phase),unsigned(last_.reason),owners_.cycle,in.sequence,geometry,
  supply_.Held()?supply_.Held()->item.generation:pending?pending->item.generation:0,
  last_.transaction.request?last_.transaction.request->id:pending?pending->request:0,last_.seat?last_.seat->id:last_.originalSeat?last_.originalSeat->id:pending?pending->seat:0,
  in.observedNs,in.nowNs,last_.insertion.progress,kind==7?unsigned(cancelCause_):0,
  originalMagazine_?originalMagazine_->item.id:0,originalMagazine_?originalMagazine_->item.generation:0,
  originalMagazine_?originalMagazine_->rounds:0,lastReserve_?lastReserve_->loaded:-1,lastReserve_?lastReserve_->reserve:-1,kind==2?startOrigin_:ReloadStartOrigin{},kind==7?last_.claimFailure:std::nullopt,last_.nativeFailureCheck,kind==7?nativeBoundary_:std::nullopt,kind==7?last_.motionFailure:std::nullopt};
}
bool Bc2MagazinePhysicalReload::RollbackUnstarted(const HandInteractionSample& in,HandInteraction& hands)noexcept {
 if(!unseat_||supply_.Pending()||supply_.Held()||!interaction_.RejectUnstarted(in,hands,*unseat_))return false;
 startupUnknown_=active_=retiring_=blocksCurrent_=gateApplied_=drained_=false;pulseUntil_=0;
 unseat_.reset();startupPulse_.reset();lease_.reset();gate_.reset();ack_.reset();originalMagazine_.reset();originalReserve_.reset();last_={};history_={};historyNext_=0;++unstarted_;
 return true;
}
void Bc2MagazinePhysicalReload::Cancel(const HandInteractionSample& in,HandInteraction& hands,MagazineCancelCause cause)noexcept {
 if(!enabled_||retiring_)return;
 if(startupUnknown_){pulseUntil_=0;blocksCurrent_=true;return;} // No global native cancellation before ownership is known.
 if(active_){cancelCause_=cause;api_.cancel(api_.context);active_=false;retiring_=true;blocksCurrent_=true;++cancelled_;}
 if(last_.phase!=DetachableMagazinePhase::Attached&&last_.phase!=DetachableMagazinePhase::Cancelled)last_=interaction_.Cancel(in,hands);
 else if(last_.phase==DetachableMagazinePhase::Attached)interaction_.ResetAttached();
 if(retiring_)Record(7,in);
 supply_.Cancel(in,hands);pulseUntil_=0;lease_.reset();gate_.reset();ack_.reset();replacementFrame_.reset();removalFrame_.reset();
}
MagazinePhysicalResult Bc2MagazinePhysicalReload::Tick(const MagazinePhysicalSample& supplied,HandInteraction& hands,
 std::uint64_t& sharedIntent)noexcept {
 MagazinePhysicalResult out;if(!enabled_)return out;
 nativeBoundary_.reset();
 auto s=supplied;s.input.nowNs=Now(s.input.nowNs);const auto cancel=[&](MagazineCancelCause cause=MagazineCancelCause::External){Cancel(s.input,hands,cause);ObserveRetirement(s);out.tracking.target.reset();out.blocksWeaponActions=BlocksEquipment();};
 if(!Api()||s.input.nowNs<lastNow_){cancel(MagazineCancelCause::ApiOrClock);return out;}lastNow_=s.input.nowNs;
 if(startupUnknown_){
  ++startInspections_;startupResult_=api_.inspectStart(api_.context,owners_.native,owners_.cycle,startupPulse_);
  if(startupResult_==MagazineCycleStartResult::NotStarted){
   if(!RollbackUnstarted(s.input,hands)){out.blocksWeaponActions=true;return out;}
   out.blocksWeaponActions=false;return out; // New neutral packet must rearm.
  }
  if(startupResult_==MagazineCycleStartResult::RegisteredCancelled){startupUnknown_=false;active_=true;cancel();return out;}
  out.blocksWeaponActions=true;return out; // Unknown/unsupported outcome cannot become a receipt.
 }
 ObserveRetirement(s);
 if(active_&&(s.nativeOwner!=owners_.native.owner||s.input.owner!=owners_.physical||s.asset!=profile_->geometry->asset))cancel(MagazineCancelCause::OwnerChanged);
 out.blocksWeaponActions=BlocksEquipment();
 if(active_&&gateApplied_&&observationGapDeadline_&&Now(s.input.nowNs)>=observationGapDeadline_){
  ++observationExpired_;cancel(MagazineCancelCause::NativeObservationExpired);return out;}
 const auto reserveObservation=api_.reserveObserved?api_.reserveObserved(api_.context):ReloadReserveObservation{ReloadObservationResult::Available,api_.reserve(api_.context)};
 const auto reserve=reserveObservation.lease;s.input.nowNs=Now(s.input.nowNs);
 if(reserveObservation.result==ReloadObservationResult::Deferred){
  const auto currentGun=hands.Current(InteractionHand::Right);
  const bool currentSafe=active_&&s.nativeOwner==owners_.native.owner&&s.input.owner==owners_.physical&&
   s.weapon==owners_.weapon&&s.family.binding==owners_.family.binding&&
   MagazineFamilyFresh(s.family,s.nativeOwner,s.input.owner,s.weapon,s.input.nowNs)&&
   profile_&&profile_->geometry&&s.asset==profile_->geometry->asset&&s.meshes&&s.family.binding.profile&&
   MagazineSelected(*s.meshes,s.nativeOwner,s.asset,*s.family.binding.profile,s.input.nowNs)&&
   !s.cancel&&s.input.sequence>=lastSequence_&&s.input.focused&&s.input.tracked[0]&&s.input.tracked[1]&&
   !s.input.released[1]&&Fresh(s.input.observedNs,s.input.deadlineNs,s.input.nowNs)&&
   s.trackingEpoch==s.nativeOwner.space&&currentGun&&currentGun->token.owner==s.input.owner&&
   currentGun->token.item==s.weapon&&currentGun->token.kind==HandClaimKind::GunHold&&
   currentGun->inputSequence==s.input.sequence&&currentGun->deadlineNs>s.input.nowNs;
  if(currentSafe&&ReloadKeepAliveDeferredWithinOriginalDeadline(ReloadKeepAliveResult::Deferred,s.input.nowNs,acceptedControlDeadline_)){
   out.blocksWeaponActions=true;out.ownsLeftHand=bool(last_.removalClaim)||bool(supply_.Held());return out;}
  if(active_&&!currentSafe){cancel(s.cancel?MagazineCancelCause::ExplicitInput:MagazineCancelCause::InputOrClaim);return out;}
  if(active_)cancel(MagazineCancelCause::ReserveUnavailable);return out;
 }
 if(reserveObservation.result==ReloadObservationResult::Rejected&&api_.reserveObserved){
  if(active_)cancel(MagazineCancelCause::ReserveUnavailable);return out;
 }
 lastReserve_=reserve; // Read-only diagnostic snapshot; never used as a fresh lease.
 if(!reserve){
  ++reserveReadMisses_;
  // Three native branches transfer on separate callbacks. A coherent pooled
  // count read can be unavailable BETWEEN these callbacks without owner loss.
  // A submitted seat or an already acknowledged all-three native hold may
  // wait only within ORIGINAL source/input/held-lease deadlines. This grants
  // no new contact, acquisition, seat, completion or presentation lifetime.
  const auto gun=hands.Current(InteractionHand::Right);
  const bool submitted=submittedLease_&&supply_.Pending()&&last_.phase==DetachableMagazinePhase::AwaitingSeat;
  const bool held=gateApplied_&&lease_&&lease_->nativeBindingVerified&&lease_->allThreeHeld&&lease_->identity==owners_.native&&
   lease_->cycle==owners_.cycle&&Fresh(lease_->observedNs,lease_->deadlineNs,s.input.nowNs)&&
   last_.phase>=DetachableMagazinePhase::Pulling&&last_.phase<=DetachableMagazinePhase::Guided;
  const bool same=active_&&(submitted||held)&&
   s.nativeOwner==owners_.native.owner&&s.input.owner==owners_.physical&&s.weapon==owners_.weapon&&
   s.family.binding==owners_.family.binding&&MagazineFamilyFresh(s.family,s.nativeOwner,s.input.owner,s.weapon,s.input.nowNs)&&
   s.asset==profile_->geometry->asset&&s.meshes&&s.family.binding.profile&&MagazineSelected(*s.meshes,s.nativeOwner,s.asset,*s.family.binding.profile,s.input.nowNs)&&
   !s.cancel&&s.input.sequence>=lastSequence_&&s.input.focused&&s.input.tracked[0]&&s.input.tracked[1]&&!s.input.released[1]&&
   Fresh(s.input.observedNs,s.input.deadlineNs,s.input.nowNs)&&s.trackingEpoch==s.nativeOwner.space&&
   gun&&gun->token.owner==s.input.owner&&gun->token.item==s.weapon&&gun->token.kind==HandClaimKind::GunHold&&
   gun->inputSequence==s.input.sequence&&gun->deadlineNs>s.input.nowNs;
  if(same&&retainedReserve_&&Fresh(retainedReserve_->observedNs,retainedReserve_->deadlineNs,s.input.nowNs)&&lastInputDeadline_>s.input.nowNs){
   const auto keep=Keep({owners_.native,owners_.cycle,s.input.sequence,s.input.observedNs,s.input.deadlineNs,true},s.input.nowNs);
  if(keep==ReloadKeepAliveResult::Deferred){out.blocksWeaponActions=true;out.ownsLeftHand=bool(last_.removalClaim)||bool(supply_.Held());return out;}
  if(keep==ReloadKeepAliveResult::Rejected){
    cancel(MagazineCancelCause::KeepAlive);return out;}
   if(submitted)++transferReadDeferrals_;else ++heldReadDeferrals_;out.tracking={s.family,true,s.nativeOwner,s.input,s.meshes,*retainedReserve_,owners_.cycle,last_.prop};
   // Retain only the ORIGINAL published target; target/hand evidence keeps its
   // own original expiry even though current safety was checked above.
   PublishReplacementFrame(out.tracking,s);
   if(!MagazineTargetFresh(out.tracking,Now(s.input.nowNs)))out.tracking.target.reset();
   out.blocksWeaponActions=true;out.ownsLeftHand=bool(last_.removalClaim)||bool(supply_.Held());return out;
  }
  if(same)++transferReadExpirations_;cancel(MagazineCancelCause::ReserveUnavailable);return out;
 }
 const auto map=BindMagazineOwners(s.input.owner,s.weapon,*reserve,active_||retiring_?owners_.cycle:0,s.input.nowNs,s.family);
 if(!map||map->native.owner!=s.nativeOwner||!s.meshes||(!s.family.binding.profile||!MagazineSelected(*s.meshes,s.nativeOwner,s.asset,*s.family.binding.profile,s.input.nowNs))){
 cancel(MagazineCancelCause::Mapping);return out;}
 retainedReserve_=reserve;
 out.tracking={s.family,true,s.nativeOwner,s.input,s.meshes,*reserve,active_||retiring_?owners_.cycle:0,{}};
 const auto gun=hands.Current(InteractionHand::Right);
 if(!MagazineTrackingFresh(out.tracking,s.input.nowNs)||!gun||gun->token.owner!=s.input.owner||gun->token.item!=s.weapon||
 gun->token.kind!=HandClaimKind::GunHold||gun->inputSequence!=s.input.sequence||gun->deadlineNs<=s.input.nowNs||
 s.trackingEpoch!=s.nativeOwner.space||(s.cancel&&!retiring_)){cancel(s.cancel?MagazineCancelCause::ExplicitInput:MagazineCancelCause::InputOrClaim);return out;}
 if(active_&&*map!=owners_)cancel(MagazineCancelCause::Mapping);
 if(active_&&*map==owners_)owners_.family=map->family; // Renew only freshly proven mapping, not native request evidence.
 const auto publishBodyAmmo=[&]{
  if(!bodyAmmoEnabled_||retiring_||startupUnknown_||supply_.Held()||supply_.Pending()||last_.removalClaim||
   (out.tracking.target&&out.tracking.target->handTarget)||reserve->loaded<0||reserve->loaded>reserve->capacity||reserve->reserve<=0)return;
  if(active_){if(!lease_||!lease_->nativeBindingVerified||!lease_->allThreeHeld||lease_->identity!=reserve->identity||
    lease_->cycle!=owners_.cycle||lease_->loaded!=reserve->loaded||lease_->reserve!=reserve->reserve||lease_->capacity!=reserve->capacity||
    !Fresh(lease_->observedNs,lease_->deadlineNs,s.input.nowNs))return;}
  else if(!reserve->reloadInputReady&&!reserve->allThreeIdle)return;
  const auto source=MagazineBodySupply(*map,*reserve,s.trackingEpoch,s.input.nowNs);if(!source)return;
  AmmoSupplyVisualSample v;v.enabled=true;v.source=*source;v.input=s.input;v.gun=*gun;
  v.contact=pouch_.alternateContact.value_or(AmmoSupplyContact{pouch_.pouchCenterMeters,pouch_.pouchRadiusMeters});v.frame=bodyAmmoFrame_;
  if(AmmoSupplyVisualFresh(v,Now(s.input.nowNs)))out.bodyAmmo=v;
 };
 // Retirement belongs to old identity; fresh source may be the new owner.
 if(!active_&&!retiring_){
  if(profile_!=map->family.binding.profile){
   // Switch measured data only after old native/supply ownership has retired.
   if(supply_.Held()||supply_.Pending()){cancel(MagazineCancelCause::Mapping);return out;}
   if(!interaction_.Reconfigure(map->family.binding.profile->geometry->interaction)){cancel(MagazineCancelCause::Mapping);return out;}
   profile_=map->family.binding.profile;
   history_={};historyNext_=0;last_={};retainedReserve_=reserve;
  }
  if((owners_.physical.actor&&owners_.physical!=map->physical)||(lastInputDeadline_&&s.input.observedNs>=lastInputDeadline_))interaction_.ResetAttached();
  owners_=*map;
 }
 if(retiring_){
  // A retained original magazine is never a replacement supply reservation.
  // Stop proof and unchanged native counts are separate from callback drain.
  if(originalReturning_&&(s.input.owner!=owners_.physical||s.nativeOwner!=owners_.native.owner||s.weapon!=owners_.weapon||
    s.cancel||s.family.binding!=owners_.family.binding||!originalReserve_||reserve->identity!=originalReserve_->identity||reserve->loaded!=originalReserve_->loaded||
    reserve->reserve!=originalReserve_->reserve||reserve->capacity!=originalReserve_->capacity)){
   last_=interaction_.Cancel(s.input,hands);originalReturning_=false;++originalReturnRejected_;
  }
  ObserveRetirement(s);
  const auto& r=retirement_;
  // The drained request still belongs to owners_.native, but this attachment
  // observation belongs to the currently selected, independently verified map.
  // Requiring the old rig here strands retirement after a profile switch and
  // prevents the new weapon from ever starting another manual reload.
  const bool attached=s.raw.valid&&s.raw.owner==s.nativeOwner&&s.raw.rigFingerprint==map->family.binding.profile->geometry->rigFingerprint&&s.raw.nativeMagazineAttached&&
   s.raw.inputEvidence.owner==s.input.owner&&s.raw.inputEvidence.deadlineNs>s.input.nowNs&&r&&s.raw.inputEvidence.observedNs>=r->observedNs;
  if(r&&!attached)++retireAttachedWaits_;
  if(r&&r->verified&&r->identity==owners_.native&&r->cycle==owners_.cycle&&Fresh(r->observedNs,r->deadlineNs,s.input.nowNs)&&
   reserve->observedNs>=r->observedNs&&attached){
   if(originalReturning_){
    // reserve reloadInputReady is produced only by coherent all-three idle
    // snapshots. Draining alone or a merely attached animated mag is insufficient.
    const OriginalMagazineReturnReceipt receipt{originalMagazine_.value_or(OriginalMagazine{}),owners_.cycle,
     last_.originalSeat?last_.originalSeat->id:0,r->event,reserve->observedNs,std::min(reserve->deadlineNs,r->deadlineNs),true};
    if(originalMagazine_&&originalReserve_&&last_.originalSeat&&reserve->reloadInputReady&&reserve->observedNs>=originalReturnNs_&&
       reserve->identity==originalReserve_->identity&&reserve->loaded==originalReserve_->loaded&&
       reserve->reserve==originalReserve_->reserve&&reserve->capacity==originalReserve_->capacity&&
       interaction_.CompleteOriginalReturn(s.input,hands,receipt)){
     originalReceipt_=receipt;
     originalReturning_=retiring_=gateApplied_=blocksCurrent_=drained_=false;++originalReturns_;++reconciled_;
     Record(10,s.input);retirement_.reset();lease_.reset();submittedLease_.reset();unseat_.reset();gate_.reset();ack_.reset();
     originalMagazine_.reset();originalReserve_.reset();last_={};history_={};historyNext_=0;out.tracking.cycle=0;
    }
   }else{
   bool reconciled=!supply_.Pending();
   if(supply_.Pending())if(const auto source=MagazineSupply(*map,*reserve,s.trackingEpoch,s.input.nowNs,supply_.Pending()->units)){
    reconciled=supply_.Rebaseline(s.input,hands,{*supply_.Pending(),*source,r->event,r->observedNs,r->deadlineNs,true}).accepted;}
   if(reconciled&&interaction_.Rebaseline(s.input,hands,{owners_.physical,owners_.weapon,last_.nativeCycle,last_.cancelledRequest,
    r->event,r->observedNs,r->deadlineNs,true})){
    retiring_=false;gateApplied_=false;blocksCurrent_=false;drained_=false;++reconciled_;retirement_.reset();lease_.reset();submittedLease_.reset();unseat_.reset();gate_.reset();ack_.reset();
    Record(8,s.input);originalMagazine_.reset();originalReserve_.reset();last_={};history_={};historyNext_=0;out.tracking.cycle=0;
   }
   }
  }
  if(retirement_&&retirement_->deadlineNs<=s.input.nowNs)retirement_.reset();
  if(!s.cancel&&(originalReturning_||last_.phase==DetachableMagazinePhase::Complete)&&last_.prop){auto target=*last_.prop;target.gunClaim=gun->token;target.inputSequence=s.input.sequence;
   target.observedNs=s.input.observedNs;target.deadlineNs=std::min({s.input.deadlineNs,reserve->deadlineNs,gun->deadlineNs});
   out.tracking.target=target;if(!MagazineTargetFresh(out.tracking,s.input.nowNs))out.tracking.target.reset();}
  out.interaction=last_;out.interaction.originalSeat.reset();out.blocksWeaponActions=BlocksEquipment();return out;
 }
 // Empty admission requires actual all-three scoped Step receipts. Missing
 // control evidence preserves native fallback; it never fabricates readiness.
 out.ordinaryReloadAllowed=!active_&&reserve->loaded==0&&!reserve->emptyReloadControlled;
 const bool fresh=s.input.sequence>lastSequence_;
 // Startup duplicates cannot acknowledge or progress, but still owe the
 // exact original unseat transaction's timeout and current input safety.
 if(!fresh&&active_&&!gateApplied_){
  DetachableMagazineSample waiting;waiting.input=s.input;waiting.weapon=s.weapon;waiting.trackingEpoch=s.trackingEpoch;
  waiting.gripPressed=s.gripPressed;waiting.nativeObservationDeferred=true;
  waiting.native={s.input.owner,s.weapon,owners_.cycle,reserve->observedNs,reserve->deadlineNs,true,false,false,{}};
  last_=interaction_.Update(waiting,hands);
  if(last_.phase==DetachableMagazinePhase::Cancelled){cancel(MagazineCancelCause::InteractionRejected);out.interaction=last_;return out;}
 }
 if(!fresh){out.interaction=last_;out.interaction.transaction.request.reset();out.interaction.transaction.acknowledged=false;
  out.interaction.transaction.completed=false;out.interaction.seat.reset();out.interaction.originalSeat.reset();out.interaction.removalGrabbed=false;
  out.interaction.physicallyRemoved=false;out.interaction.insertion.captured=false;out.interaction.insertion.seat.reset();
  out.interaction.insertion.haptic=ReloadInsertionHaptic::None;
  out.tracking.target=last_.prop;out.blocksWeaponActions=BlocksEquipment();out.reloadHeld=active_&&s.input.nowNs<pulseUntil_;
  PublishReplacementFrame(out.tracking,s);
  if(!MagazineTargetFresh(out.tracking,s.input.nowNs))out.tracking.target.reset();publishBodyAmmo();return out;}
 lastSequence_=s.input.sequence;
 lastInputDeadline_=s.input.deadlineNs;
 if(sharedIntent>std::numeric_limits<std::uint64_t>::max()-2){cancel(MagazineCancelCause::CounterExhausted);return out;}
 MagazineNativeObservation native{s.input.owner,s.weapon,active_?owners_.cycle:0,reserve->observedNs,reserve->deadlineNs,true,false,false,{}};
 const auto describeLease=[&](const std::optional<ReloadMagazineLease>& l){NativeLeaseEvidence e;
  if(l)e={true,l->nativeBindingVerified,l->identity==owners_.native,l->cycle==owners_.cycle,l->allThreeHeld,l->sequence,l->cycle,l->observedNs,l->deadlineNs};return e;};
 nativeBoundary_.emplace();auto& boundary=*nativeBoundary_;boundary.supplied=native;boundary.prior=describeLease(lease_);
 boundary.gateApplied=gateApplied_;boundary.phase=unsigned(last_.phase);boundary.reserveObservedNs=reserve->observedNs;
 boundary.reserveDeadlineNs=reserve->deadlineNs;boundary.controlDeadlineNs=acceptedControlDeadline_;boundary.currentClaim=hands.Current(InteractionHand::Left);
 if(!active_&&(reserve->loaded<0||(reserve->loaded==0&&!reserve->emptyReloadControlled)||reserve->loaded>=reserve->capacity||reserve->reserve<=0||!reserve->reloadInputReady)){
  DetachableMagazineSample unavailable;unavailable.input=s.input;unavailable.weapon=s.weapon;unavailable.trackingEpoch=s.trackingEpoch;
  unavailable.gripPressed=s.gripPressed;unavailable.ejectPressed=s.ejectPressed;unavailable.native=native;unavailable.removalPermitted=false;
  last_=interaction_.Update(unavailable,hands);out.interaction=last_;publishBodyAmmo();return out;
 }
 if(active_){
  const auto priorControlDeadline=acceptedControlDeadline_;
  const auto keep=Keep({owners_.native,owners_.cycle,s.input.sequence,s.input.observedNs,s.input.deadlineNs,true},s.input.nowNs);
  if(keep==ReloadKeepAliveResult::Deferred){out.blocksWeaponActions=true;out.ownsLeftHand=bool(last_.removalClaim)||bool(supply_.Held());return out;}
  if(keep==ReloadKeepAliveResult::Rejected){cancel(MagazineCancelCause::KeepAlive);return out;}
  const auto observation=api_.leaseObserved?api_.leaseObserved(api_.context,owners_.native,owners_.cycle):
   ReloadMagazineLeaseObservation{ReloadMagazineObservationResult::Ready,api_.lease(api_.context,owners_.native,owners_.cycle)};
  if(observation.result==ReloadMagazineObservationResult::Rejected){cancel(MagazineCancelCause::NativeObservationRejected);return out;}
  s.input.nowNs=Now(s.input.nowNs);
  if(observation.result==ReloadMagazineObservationResult::Deferred&&!submittedLease_){
   // A lost post-gate positive hold receipt gets one fixed freshness gap.
   // Startup has no prior hold receipt: its original unseat transaction
   // acknowledgement timeout remains the bound, checked below by interaction.
   // Keep may renew control, but cannot renew either original deadline.
   if(priorControlDeadline<=s.input.nowNs){cancel(MagazineCancelCause::KeepAlive);return out;}
   if(gateApplied_&&!observationGapDeadline_)observationGapDeadline_=s.input.nowNs+std::min<std::int64_t>(priorControlDeadline-s.input.nowNs,50000000);
   if((gateApplied_&&s.input.nowNs>=observationGapDeadline_)||acceptedControlDeadline_<=s.input.nowNs){
    ++observationExpired_;cancel(MagazineCancelCause::NativeObservationExpired);return out;}
   ++observationDeferred_;
   const auto source=MagazineSupply(*map,*reserve,s.trackingEpoch,s.input.nowNs);
   if(!source){cancel(MagazineCancelCause::SourceUnavailable);return out;}
   const auto held=supply_.Held();
   AmmoSupplySample waiting{s.input,*source,0,s.trackingEpoch,s.bodyFromHand,s.gripPressed,++sharedIntent};
   const auto supplied=supply_.WaitHeld(waiting,hands);
   if(held&&s.gripPressed&&!s.input.released[0]&&!supplied.held){cancel(MagazineCancelCause::InputOrClaim);return out;}
   DetachableMagazineSample physical;physical.input=s.input;physical.weapon=s.weapon;physical.trackingEpoch=s.trackingEpoch;
   physical.gripPressed=s.gripPressed;physical.native=native;physical.native.cycle=owners_.cycle;
   physical.nativeObservationDeferred=true;physical.replacement=supplied.held;
   last_=interaction_.Update(physical,hands);
   if(last_.phase==DetachableMagazinePhase::Cancelled){cancel(MagazineCancelCause::InteractionRejected);out.interaction=last_;return out;}
   out.interaction=last_;out.blocksWeaponActions=true;
   // A missing observation cannot interrupt an already authorized startup
   // Reload pulse. Its original deadline remains unchanged.
   out.reloadHeld=active_&&s.input.nowNs<pulseUntil_;
   out.ownsLeftHand=bool(last_.removalClaim)||bool(supply_.Held());
   out.acquired=acquired_;out.submitted=submitted_;out.completed=completed_;
   // Old presentation evidence is allowed to expire; never publish a new pose.
   out.tracking.target=last_.prop;PublishReplacementFrame(out.tracking,s);
   if(!MagazineTargetFresh(out.tracking,s.input.nowNs))out.tracking.target.reset();
   return out;
  }
  if(observation.result==ReloadMagazineObservationResult::Ready)observationGapDeadline_=0;
  const auto observedLease=observation.lease;boundary.returned=describeLease(observedLease);
  if(observedLease)lease_=observedLease;
  if(auto observed=api_.gate(api_.context,owners_.native,owners_.cycle))gate_=observed;
  if(auto observed=api_.ack(api_.context,owners_.native,owners_.cycle))ack_=observed;
  s.input.nowNs=Now(s.input.nowNs);
  if(lease_){const auto& l=*lease_;
   if(!l.nativeBindingVerified||l.identity!=owners_.native||l.cycle!=owners_.cycle||!l.sequence||!Fresh(l.observedNs,l.deadlineNs,s.input.nowNs))lease_.reset();
   else{native.observedNs=l.observedNs;native.deadlineNs=l.deadlineNs;native.allThreeHeld=l.allThreeHeld;}}
  if(gate_&&unseat_&&!gateApplied_)if(const auto g=MagazineGateObservation(owners_,*gate_,*unseat_,s.input.nowNs))native=*g;
  if(ack_&&submittedLease_&&supply_.Pending()){
   const auto receipt=MagazineSupplyReceipt(owners_,*supply_.Pending(),*submittedLease_,*ack_,*reserve,s.input.nowNs);
   if(receipt&&supply_.Resolve(s.input,hands,*receipt).consumed){native.acknowledgement=receipt->acknowledgement;
    native.acknowledgementVerified=true;++completed_;}
  }
 }
 boundary.supplied=native;boundary.accepted=describeLease(lease_);boundary.controlDeadlineNs=acceptedControlDeadline_;
 boundary.ownerMatches=native.owner==s.input.owner;boundary.weaponMatches=native.weapon==s.weapon;
 const unsigned units=supply_.Pending()?supply_.Pending()->units:
  (native.acknowledgementVerified&&native.acknowledgement.operation==ReloadOperation::SeatMagazine&&submittedLease_?
   unsigned(std::min(submittedLease_->capacity-submittedLease_->loaded,submittedLease_->reserve)):0);
 const auto source=MagazineSupply(*map,*reserve,s.trackingEpoch,s.input.nowNs,units);
 boundary.sourceChecked=true;boundary.sourceAvailable=bool(source);
 if(!source){cancel(MagazineCancelCause::SourceUnavailable);return out;}
 AmmoSupplySample current{s.input,*source,s.geometrySequence,s.trackingEpoch,s.bodyFromHand,
  CanSupply(last_.phase)&&s.gripPressed,++sharedIntent};
 // A genuine native completion is resolved BEFORE a reduced pool is observed
 // by the carried-item policy. Unknown native outcomes stay quarantined.
 const auto suppliedAmmo=supply_.Update(current,hands);if(suppliedAmmo.acquired)++acquired_;
 history_[historyNext_]=Evidence{current};historyNext_=(historyNext_+1)%history_.size();
 const Evidence* original=nullptr;
 if(s.raw.valid&&s.raw.owner==s.nativeOwner&&s.raw.rigFingerprint==profile_->geometry->rigFingerprint&&s.originalHandEvidence&&
  SameInput(s.raw.inputEvidence,*s.originalHandEvidence)&&s.raw.inputEvidence.deadlineNs>s.input.nowNs){
  for(const auto& e:history_)if(e&&SameInput(e->supply.input,s.raw.inputEvidence)&&e->supply.trackingEpoch==s.trackingEpoch){original=&*e;break;}}
 DetachableMagazineSample physical;physical.input=s.input;physical.weapon=s.weapon;physical.trackingEpoch=s.trackingEpoch;
 physical.intent=++sharedIntent;physical.gripPressed=s.gripPressed;physical.ejectPressed=s.ejectPressed;physical.native=native;
 physical.replacement=supply_.Held();
 if(active_)physical.original=originalMagazine_;
 else if(nextCycle_<std::numeric_limits<std::uint64_t>::max())physical.original=OriginalMagazine{
  s.input.owner,s.weapon,{0x4243324f4d4147ull,nextCycle_+1},{profile_->geometry->interaction.insertion.id,profile_->geometry->interaction.insertion.revision},
  {reserve->identity.serverItem,reserve->identity.owner.equipGeneration},s.trackingEpoch,reserve->sequence,
  reserve->observedNs,reserve->deadlineNs,unsigned(reserve->loaded),unsigned(reserve->capacity)};
 if(original&&Rigid(s.raw.rawLeftWristWorldMeters)&&Rigid(s.raw.weaponWorldMeters)){
  physical.geometryInput=original->supply.input;physical.geometrySequence=original->supply.input.sequence;
  physical.weaponFromHandMeters=Multiply(s.raw.rawLeftWristWorldMeters,*InverseRigid(s.raw.weaponWorldMeters));}
 boundary.currentClaim=hands.Current(InteractionHand::Left);
 last_=interaction_.Update(physical,hands);out.interaction=last_;
 // Keep the FIRST failed grab, before held-input NeedNeutral hides its cause.
 // Observation only: never retries a gesture or alters hand/native admission.
 if(last_.phase==DetachableMagazinePhase::Attached&&s.gripPressed&&last_.reason!=DetachableMagazineReason::None){
  if(admissionReason_!=last_.reason){
   const auto claim=hands.Current(InteractionHand::Left);
   if(admissionCount_<admissions_.size())admissions_[admissionCount_++]={s.input.nowNs,s.input.sequence,physical.geometrySequence,
    claim?claim->token.id:0,unsigned(last_.reason),claim?unsigned(claim->token.kind):0,s.input.released[0],reserve->reloadInputReady};
   else ++admissionDropped_;
  }
  admissionReason_=last_.reason;
 }else admissionReason_.reset();
 if(last_.removalGrabbed)Record(1,s.input,physical.geometrySequence);
 if(last_.physicallyRemoved)Record(3,s.input,physical.geometrySequence);
 if(last_.insertion.captured)Record(4,s.input,physical.geometrySequence);
 if(last_.seat)Record(5,s.input,physical.geometrySequence);
 if(last_.phase==DetachableMagazinePhase::Cancelled){cancel(MagazineCancelCause::InteractionRejected);return out;}
 if(last_.transaction.acknowledged&&native.acknowledgement.operation==ReloadOperation::UnseatMagazine)gateApplied_=true;
 if(last_.originalSeat){
  const auto& seat=*last_.originalSeat;
  if(!active_||!gateApplied_||!lease_||!lease_->nativeBindingVerified||!lease_->allThreeHeld||
     !Fresh(lease_->observedNs,lease_->deadlineNs,s.input.nowNs)||lease_->identity!=owners_.native||lease_->cycle!=owners_.cycle||
     !originalMagazine_||!originalReserve_||last_.original!=originalMagazine_||seat.identity.item!=originalMagazine_->item||
     reserve->identity!=originalReserve_->identity||reserve->loaded!=originalReserve_->loaded||reserve->reserve!=originalReserve_->reserve||
     reserve->capacity!=originalReserve_->capacity||supply_.Pending()||supply_.Held()){
   cancel(MagazineCancelCause::SeatSubmission);return out;}
  api_.cancel(api_.context);active_=false;retiring_=originalReturning_=blocksCurrent_=true;drained_=false;
  originalReturnNs_=s.input.nowNs;pulseUntil_=0;retirement_.reset();Record(9,s.input,physical.geometrySequence);
 }
 if(last_.transaction.request){const auto& r=*last_.transaction.request;
  if(r.operation==ReloadOperation::UnseatMagazine){
   const auto identity=api_.identity(api_.context);
   if(active_){cancel();return out;}
   if(!identity||*identity!=reserve->identity||nextCycle_==std::numeric_limits<std::uint64_t>::max()){
    // No native Start was called, so this exact local pre-gate intent can be
    // abandoned without inventing retirement for an unregistered cycle.
    unseat_=r;RollbackUnstarted(s.input,hands);out.tracking.target.reset();out.reloadHeld=false;return out;}
   owners_=*map;owners_.cycle=++nextCycle_;originalMagazine_=last_.original;originalReserve_=*reserve;originalReceipt_.reset();drained_=false;retirement_.reset();unseat_=r;auto request=r;request.owner=MagazineNativeOwner(owners_);
   startOrigin_={s.actionFlagsKnown,s.gripPressed,s.ejectPressed,s.input.sequence,s.actionHeld,s.actionPressed};
   if(s.input.deadlineNs-s.input.observedNs<100000000ll){
    unseat_=r;RollbackUnstarted(s.input,hands);out.tracking.target.reset();out.reloadHeld=false;return out;}
   const ReloadCycleControl control{owners_.native,owners_.cycle,s.input.sequence,s.input.observedNs,s.input.deadlineNs,true};
   const ReloadMagazineStartupPulse pulse{control,s.input.observedNs+100000000ll};
   if(pulse.endNs<=Now(s.input.nowNs)){unseat_=r;RollbackUnstarted(s.input,hands);out.tracking.target.reset();out.reloadHeld=false;return out;}
   startupPulse_=pulse;startupResult_=api_.start(api_.context,control,request,pulse);
   if(startupResult_!=MagazineCycleStartResult::Started){
    out.tracking.target.reset();out.reloadHeld=false;
    if(startupResult_==MagazineCycleStartResult::NotStarted){
     if(!RollbackUnstarted(s.input,hands)){startupUnknown_=true;blocksCurrent_=true;}
    }else if(startupResult_==MagazineCycleStartResult::RegisteredCancelled){active_=true;cancel();}
    else{startupUnknown_=true;blocksCurrent_=true;}
    out.blocksWeaponActions=BlocksEquipment();return out;
   }
   observationGapDeadline_=0;acceptedControlDeadline_=s.input.deadlineNs;active_=true;blocksCurrent_=true;++started_;Record(2,s.input,physical.geometrySequence);pulseUntil_=pulse.endNs;out.tracking.cycle=owners_.cycle;
  }else if(r.operation==ReloadOperation::SeatMagazine){
   if(!active_||!lease_||!last_.seat||!original){cancel(MagazineCancelCause::SeatSubmission);return out;}
   const auto reservation=supply_.ReserveFrom(current,hands,original->supply,*last_.seat,r,owners_.cycle);
   const auto request=reservation?MagazineSeatRequest(owners_,*lease_,*reservation,r,s.input.nowNs):std::nullopt;
   if(!request||!api_.submit(api_.context,*request)){cancel(MagazineCancelCause::SeatSubmission);return out;}
   submittedLease_=*lease_;++submitted_;
  }
 }
 if(last_.transaction.completed){Record(6,s.input);api_.cancel(api_.context);active_=false;retiring_=true;blocksCurrent_=true;pulseUntil_=0;}
 out.tracking.target=last_.prop;
 PublishReplacementFrame(out.tracking,s,original!=nullptr);
 if(!MagazineTargetFresh(out.tracking,s.input.nowNs)||(!active_&&!retiring_))out.tracking.target.reset();
 out.reloadHeld=active_&&s.input.nowNs<pulseUntil_;out.blocksWeaponActions=BlocksEquipment();
 out.ownsLeftHand=bool(last_.removalClaim)||supply_.Held().has_value();out.acquired=acquired_;out.submitted=submitted_;out.completed=completed_;
 publishBodyAmmo();return out;
}
void Bc2MagazinePhysicalReload::Report(std::ostream& o)const {
 o<<"\"magazine_physical\":{\"active\":"<<active_<<",\"retiring\":"<<retiring_<<",\"drained\":"<<drained_<<",\"blocks_current\":"<<blocksCurrent_<<",\"cycle\":"<<owners_.cycle
  <<",\"phase\":"<<unsigned(last_.phase)<<",\"reason\":"<<unsigned(last_.reason)<<",\"transaction_reason\":"<<unsigned(last_.transaction.reason)<<",\"acquired\":"<<acquired_
  <<",\"started\":"<<started_<<",\"submitted\":"<<submitted_<<",\"completed\":"<<completed_
  <<",\"original_returns\":"<<originalReturns_<<",\"original_returning\":"<<originalReturning_<<",\"original_return_rejected\":"<<originalReturnRejected_
  <<",\"startup_result\":"<<unsigned(startupResult_)<<",\"startup_unknown\":"<<startupUnknown_<<",\"unstarted\":"<<unstarted_<<",\"start_inspections\":"<<startInspections_
  <<",\"cancel_cause\":"<<unsigned(cancelCause_)<<",\"held_read_deferrals\":"<<heldReadDeferrals_<<",\"reserve_read_misses\":"<<reserveReadMisses_<<",\"transfer_read_deferrals\":"<<transferReadDeferrals_<<",\"transfer_read_expirations\":"<<transferReadExpirations_<<",\"retire_attached_waits\":"<<retireAttachedWaits_
  <<",\"observation_deferred\":"<<observationDeferred_<<",\"observation_expired\":"<<observationExpired_
  <<",\"cancelled\":"<<cancelled_<<",\"reconciled\":"<<reconciled_<<",\"pending\":"<<supply_.Pending().has_value()
  <<",\"admission_dropped\":"<<admissionDropped_<<",\"admissions\":[";
 for(unsigned n=0;n<admissionCount_;++n){const auto& a=admissions_[n];if(n)o<<',';
  o<<"{\"now_ns\":"<<a.now<<",\"input\":"<<a.input<<",\"geometry\":"<<a.geometry<<",\"claim\":"<<a.claim
   <<",\"reason\":"<<a.reason<<",\"claim_kind\":"<<a.claimKind<<",\"released\":"<<a.released<<",\"ready\":"<<a.ready<<'}';}
 o<<"],\"event_dropped\":"<<eventDropped_<<",\"events\":[";
 for(unsigned n=0;n<eventCount_;++n){const auto& e=events_[n];if(n)o<<',';
  o<<"{\"kind\":"<<e.kind<<",\"phase\":"<<e.phase<<",\"reason\":"<<e.reason<<",\"cycle\":"<<e.cycle
   <<",\"cause\":"<<e.cause<<",\"input\":"<<e.input<<",\"geometry\":"<<e.geometry<<",\"item\":"<<e.item<<",\"request\":"<<e.request
   <<",\"seat\":"<<e.seat<<",\"observed_ns\":"<<e.observedNs<<",\"now_ns\":"<<e.nowNs<<",\"progress\":"<<e.progress
   <<",\"original_item\":"<<e.originalItem<<",\"original_generation\":"<<e.originalGeneration<<",\"original_rounds\":"<<e.originalRounds
   <<",\"native_loaded\":"<<e.nativeLoaded<<",\"native_reserve\":"<<e.nativeReserve<<",\"start_flags_known\":"<<(e.startOrigin.flagsKnown?"true":"false")
   <<",\"start_grip_pressed\":"<<(e.startOrigin.gripPressed?"true":"false")<<",\"start_eject_pressed\":"<<(e.startOrigin.ejectPressed?"true":"false")
   <<",\"start_input_sequence\":"<<e.startOrigin.inputSequence<<",\"start_held\":"<<e.startOrigin.held<<",\"start_pressed\":"<<e.startOrigin.pressed;
  if(e.motionFailure){const auto& f=*e.motionFailure;
   o<<",\"motion_failure\":{\"check\":"<<unsigned(f.check)<<",\"grip_pressed\":"<<f.gripPressed
    <<",\"released\":"<<f.released<<",\"pulled\":"<<f.pulled<<",\"geometry_input\":"<<f.geometrySequence
    <<",\"measured\":"<<f.measured<<",\"limit\":"<<f.limit<<'}';}
  if(e.claimFailure){const auto& f=*e.claimFailure;
   o<<",\"claim_failure\":{\"renew_reason\":"<<unsigned(f.reason)
    <<",\"expected_id\":"<<f.expected.token.id<<",\"expected_input\":"<<f.expected.inputSequence<<",\"expected_deadline_ns\":"<<f.expected.deadlineNs
    <<",\"current_present\":"<<(f.current?"true":"false")<<",\"current_id\":"<<(f.current?f.current->token.id:0)
    <<",\"current_input\":"<<(f.current?f.current->inputSequence:0)<<",\"current_deadline_ns\":"<<(f.current?f.current->deadlineNs:0)
    <<",\"geometry_input\":"<<f.geometrySequence<<",\"native_deadline_ns\":"<<f.nativeDeadlineNs<<'}';}
  if(e.nativeBoundary){const auto& b=*e.nativeBoundary;const auto& v=b.supplied;
   o<<",\"native_boundary\":{\"failure_check\":"<<unsigned(e.nativeFailureCheck)<<",\"consumer_phase_before\":"<<b.phase
    <<",\"observed_ns\":"<<v.observedNs<<",\"deadline_ns\":"<<v.deadlineNs<<",\"cycle\":"<<v.cycle
    <<",\"bindings_verified\":"<<v.bindingsVerified<<",\"all_three_held\":"<<v.allThreeHeld
    <<",\"owner_matches\":"<<b.ownerMatches<<",\"weapon_matches\":"<<b.weaponMatches
    <<",\"acknowledgement_verified\":"<<v.acknowledgementVerified<<",\"ack_request\":"<<v.acknowledgement.request
    <<",\"ack_operation\":"<<unsigned(v.acknowledgement.operation)<<",\"ack_status\":"<<unsigned(v.acknowledgement.status)
    <<",\"gate_applied\":"<<b.gateApplied<<",\"source_checked\":"<<b.sourceChecked<<",\"source_available\":"<<b.sourceAvailable
    <<",\"reserve_observed_ns\":"<<b.reserveObservedNs<<",\"reserve_deadline_ns\":"<<b.reserveDeadlineNs
    <<",\"control_deadline_ns\":"<<b.controlDeadlineNs<<",\"current_claim_present\":"<<bool(b.currentClaim)
    <<",\"current_claim_id\":"<<(b.currentClaim?b.currentClaim->token.id:0)<<",\"current_claim_input\":"<<(b.currentClaim?b.currentClaim->inputSequence:0)
    <<",\"current_claim_deadline_ns\":"<<(b.currentClaim?b.currentClaim->deadlineNs:0);
   const auto lease=[&](const char* name,const NativeLeaseEvidence& l){o<<",\""<<name<<"\":{\"present\":"<<l.present
    <<",\"verified\":"<<l.verified<<",\"identity_matches\":"<<l.identityMatches<<",\"cycle_matches\":"<<l.cycleMatches
    <<",\"all_three_held\":"<<l.held<<",\"sequence\":"<<l.sequence<<",\"cycle\":"<<l.cycle
    <<",\"observed_ns\":"<<l.observedNs<<",\"deadline_ns\":"<<l.deadlineNs<<'}';};
   lease("prior_lease",b.prior);lease("returned_lease",b.returned);lease("accepted_lease",b.accepted);o<<'}';}
  o<<'}';}
 o<<"]}";
}
} // namespace fvr::bc2

