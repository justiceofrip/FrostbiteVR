#include "Bc2InventoryReloadProbe.h"
#include <cmath>
namespace fvr::bc2 {
namespace {
using namespace interaction;
constexpr math::Vec3 Neutral{.15f,-.10f,-.20f};
float Distance(math::Vec3 a,math::Vec3 b)noexcept{return std::hypot(a.x-b.x,a.y-b.y,a.z-b.z);}
bool Static(math::Pose p)noexcept{return Distance(p.position,{})<.0001f&&
 std::hypot(p.orientation.x,p.orientation.y,p.orientation.z)<.0001f&&std::abs(std::abs(p.orientation.w)-1.f)<.0001f;}
}
void Bc2InventoryReloadProbe::To(Phase p,std::int64_t now)noexcept{
 phase_=p;phaseAt_=now;if(count_<rows_.size()){auto r=actual_;r.phase=unsigned(p);r.reason=failure_;r.now=now;rows_[count_++]=r;}
}
void Bc2InventoryReloadProbe::Fail(unsigned why,std::int64_t now)noexcept{
 if(CancelConsumer()||Completed())return;failure_=why;squeeze_=0;To(Phase::Failed,now);
}
bool Bc2InventoryReloadProbe::Current(const BodyHolsterProbeSample& s,std::int64_t now)const noexcept{
 return s.nativeOwner.player==initial_.player&&s.nativeOwner.soldier==initial_.soldier&&s.nativeOwner.weak==initial_.weak&&
 s.nativeOwner.space==initial_.space&&s.sampledNs>0&&s.sampledNs<=now&&now-s.sampledNs<=150000000&&
 s.hand.observedNs>0&&s.hand.observedNs<=now&&s.hand.deadlineNs>now&&s.hand.focused&&s.hand.tracked[0]&&s.hand.tracked[1]&&
 s.input.generation==s.hand.sequence&&s.input.spaceGeneration==initial_.space&&s.nativeTick&&
 s.selectedSlot&&(s.selectedSlot==original_||s.selectedSlot==other_)&&s.selectedSlot->item.id==s.nativeOwner.weapon;
}
bool Bc2InventoryReloadProbe::Held(const BodyHolsterProbeSample& s,const BodySlotAssignment& slot,std::int64_t now)const noexcept{
 return Current(s,now)&&s.selectedSlot==slot&&s.phase==BodyHolsterPhase::Held&&s.right&&
 s.right->token.kind==HandClaimKind::GunHold&&s.right->token.owner==s.hand.owner&&s.right->token.item==s.physicalGun&&
 s.right->token.hand==InteractionHand::Right&&s.right->deadlineNs>now&&!s.left&&!s.outcome.freeRight&&
 !s.outcome.blockWeaponActions&&s.outcome.allowAutomaticGunHold&&!s.queuedTarget;
}
bool Bc2InventoryReloadProbe::Empty(const BodyHolsterProbeSample& s,std::int64_t now)const noexcept{
 return Current(s,now)&&s.phase==BodyHolsterPhase::Empty&&!s.left&&!s.right&&s.outcome.inventory.emptyHands&&
 s.outcome.freeRight&&BodyFreeRightEvidenceCurrent(*s.outcome.freeRight,now)&&s.visibility&&s.visibility->hidden&&
 s.visibility->verifiedCopyMask==3&&s.visibility->nativeOwner==s.nativeOwner&&s.visibility->request==s.request&&s.visibility->deadlineNs>now&&
 s.suppression&&s.suppression->nativeTick==s.nativeTick&&s.suppression->request==s.request&&
 HolsterSuppressionCurrent(*s.suppression,*s.suppression)&&!s.queuedTarget;
}
void Bc2InventoryReloadProbe::Prepare(InputFrame& in,const ReloadStateOwner& owner,std::string_view asset,
 std::shared_ptr<const BodyHolsterProbeSample> body,const std::optional<BodyInventoryDisplay>& display,
 const MagazineRawContact& raw,const MagazinePhysicalProbeState& state,std::int64_t observed,std::int64_t deadline,std::int64_t now)noexcept{
 if(!first_){first_=phaseAt_=now;command_.position=Neutral;initial_=owner;}
 if(now<=0||now<lastNow_||now-first_>=55000000000ll)Fail(1,now);lastNow_=now;
 if(!ValidInput(in)||!in.focused||!in.headValid||!Static(in.head)||!Static(in.referenceHead)||
 !in.hands[0].gripTracked||!in.hands[1].gripTracked||!in.hands[1].aimTracked||observed<=0||observed>now||deadline<=now||deadline-observed>100000000)Fail(2,now);
 if(owner.player!=initial_.player||owner.soldier!=initial_.soldier||owner.weak!=initial_.weak||owner.space!=initial_.space)Fail(3,now);
 if(lastInput_&&in.generation<=lastInput_){
  if(in.generation<lastInput_||observed!=observed_||deadline!=deadline_)Fail(4,now);
  in.hands[1].grip=in.hands[1].aim=command_;in.hands[1].squeeze=CancelConsumer()?0:squeeze_;
  if(phase_==Phase::Reload)reload_.Prepare(in,owner,asset,raw,state,observed,deadline,now);
  return;
 }
 lastInput_=in.generation;observed_=observed;deadline_=deadline;
 if(body)actual_={unsigned(phase_),failure_,body->nativeOwner.weapon,unsigned(body->phase),body->selectedSlot?body->selectedSlot->slot:0,
 now,in.generation,body->request,body->right?body->right->token.id:0,body->outcome.inventory.committedRequest};
 // Let the ordinary automatic startup stow settle before issuing any gesture.
 // A transient first Held sample does not prove that startup is finished.
 if(phase_==Phase::Warmup&&now-first_>=2000000000ll&&body&&display&&BodyInventoryDisplayFresh(*display,now)&&asset==Xm8MagazineAsset&&
 display->selectedOwner==owner&&body->nativeOwner==owner&&body->selectedSlot){
  original_=body->selectedSlot;anchors_=body->anchors;
  for(unsigned n=0;n<display->count;++n){const auto slot=display->slots[n].assignment;
   if(slot!=*original_&&(slot.slot==anchors_.shoulders[0].slot||slot.slot==anchors_.shoulders[1].slot))other_=slot;}
  if(other_&&Current(*body,now)){
   if(Held(*body,*original_,now)){baselineClaim_=body->right->token.id;To(Phase::ReachStow,now);}
   else if(Empty(*body,now))To(Phase::ReachInitial,now);
  }
 }
 const bool fresh=body&&Current(*body,now);
 if(phase_!=Phase::Warmup&&phase_<Phase::Reload&&display&&BodyInventoryDisplayFresh(*display,now)){
  bool a=false,b=false;for(unsigned n=0;n<display->count;++n){a|=display->slots[n].assignment==original_;b|=display->slots[n].assignment==other_;}
  if(!a||!b)Fail(5,now);
 }
 math::Vec3 target=Neutral;squeeze_=0;
 const auto shoulder=[&](bool other){const auto slot=other?other_:original_;if(!slot){Fail(6,now);return Neutral;}
  for(const auto& a:anchors_.shoulders)if(a.slot==slot->slot)return math::Vec3{a.center.x,a.center.y,-a.center.z};Fail(6,now);return Neutral;};
 const auto reach=[&](bool other,Phase press){target=shoulder(other);if(Distance(command_.position,target)<.001f&&now-phaseAt_>=300000000)To(press,now);};
 const auto draw=[&](bool other,Phase clear){target=shoulder(other);squeeze_=1;
  if(fresh&&Held(*body,*(other?other_:original_),now)){const auto claim=body->right->token.id;
   if(other)otherClaim_=claim;else if(phase_==Phase::DrawInitial)baselineClaim_=claim;else restoredClaim_=claim;To(clear,now);}};
 const auto stow=[&](bool other,Phase clear){target=shoulder(other);squeeze_=1;if(fresh&&Empty(*body,now))To(clear,now);};
 const auto clear=[&](bool held,Phase next){squeeze_=held?1.f:0.f;
  const auto slot=phase_==Phase::ClearOther?other_:original_;
  const bool expected=fresh&&(held?Held(*body,*slot,now):Empty(*body,now));
  if(fresh&&!held&&body->phase==BodyHolsterPhase::Held){Fail(10,now);return;}
  if(expected&&Distance(command_.position,Neutral)<.001f&&Distance(body->input.hands[1].grip.position,Neutral)<.01f&&now-phaseAt_>=350000000)To(next,now);};
 switch(phase_){
 case Phase::ReachInitial:reach(false,Phase::DrawInitial);break;
 case Phase::DrawInitial:draw(false,Phase::ClearInitial);break;
 case Phase::ClearInitial:clear(true,Phase::ReachStow);break;
 case Phase::ReachStow:reach(false,Phase::Stow);break;
 case Phase::Stow:stow(false,Phase::ClearEmpty);break;
 case Phase::ClearEmpty:clear(false,Phase::ReachOther);break;
 case Phase::ReachOther:reach(true,Phase::DrawOther);break;
 case Phase::DrawOther:draw(true,Phase::ClearOther);break;
 case Phase::ClearOther:clear(true,Phase::ReachOtherStow);break;
 case Phase::ReachOtherStow:reach(true,Phase::StowOther);break;
 case Phase::StowOther:stow(true,Phase::ClearOtherEmpty);break;
 case Phase::ClearOtherEmpty:clear(false,Phase::ReachOriginal);break;
 case Phase::ReachOriginal:reach(false,Phase::DrawOriginal);break;
 case Phase::DrawOriginal:draw(false,Phase::ClearOriginal);break;
 case Phase::ClearOriginal:
  if(restoredClaim_==baselineClaim_||!otherClaim_)Fail(7,now);else clear(true,Phase::Reload);break;
 default:break;
 }
 const auto d=Distance(command_.position,target);const float t=d>.025f?.025f/d:1.f;
 command_.position={command_.position.x+(target.x-command_.position.x)*t,command_.position.y+(target.y-command_.position.y)*t,command_.position.z+(target.z-command_.position.z)*t};
 in.hands[1].grip=in.hands[1].aim=command_;in.hands[1].squeeze=CancelConsumer()?0:squeeze_;
 if(phase_==Phase::Reload){reload_.Prepare(in,owner,asset,raw,state,observed,deadline,now);
  if(reload_.Completed())To(Phase::Done,now);else if(reload_.CancelConsumer())Fail(8,now);}
 else {in.hands[0].grip.position={-.2f,-.25f,-.45f};in.hands[0].aim=in.hands[0].grip;in.hands[0].squeeze=0;}
 if(phase_<Phase::Reload&&now-phaseAt_>6000000000ll)Fail(9,now);
}
void Bc2InventoryReloadProbe::Observe(const MagazinePhysicalProbeState& s,const MagazinePackCounters& p,std::int64_t now)noexcept{
 if(phase_==Phase::Reload){reload_.Observe(s,p,now);if(reload_.CancelConsumer()&&!reload_.Completed())Fail(8,now);}
}
void Bc2InventoryReloadProbe::Report(std::ostream& o)const{
 o<<"{\"synthetic_input\":true,\"headset_verified\":false,\"persistent_consumers\":true,\"phase\":"<<unsigned(phase_)<<",\"failure\":"<<failure_
 <<",\"completed\":"<<(Completed()?"true":"false")<<",\"original_weapon\":"<<(original_?original_->item.id:0)<<",\"other_weapon\":"<<(other_?other_->item.id:0)
 <<",\"baseline_claim\":"<<baselineClaim_<<",\"other_claim\":"<<otherClaim_<<",\"restored_claim\":"<<restoredClaim_<<",\"rows\":[";
 for(unsigned n=0;n<count_;++n){const auto& r=rows_[n];if(n)o<<',';o<<"{\"phase\":"<<r.phase<<",\"reason\":"<<r.reason<<",\"now_ns\":"<<r.now
 <<",\"input\":"<<r.input<<",\"weapon\":"<<r.weapon<<",\"body_phase\":"<<r.bodyPhase<<",\"slot\":"<<r.slot<<",\"request\":"<<r.request<<",\"claim\":"<<r.claim<<",\"commit\":"<<r.committed<<'}';}
 o<<"],\"magazine_physical_probe\":";reload_.Report(o);o<<'}';
}
}
