#include "fvr/interaction/DetachableMagazine.h"
#include "fvr/interaction/AmmunitionInventory.h"
#define ExperimentalMagazineGeometry MeasuredSampledPathTestGeometry
#include "../profiles/experimental-pistol-sampled242/PistolGeometry.h"
#undef ExperimentalMagazineGeometry
#include "Test.h"
#include <cstdio>
using namespace fvr;using namespace fvr::interaction;using namespace reload_insertion_detail;
namespace {
constexpr std::int64_t Ms=1000000;
struct Fixture {
 DetachableMagazineConfig config;HandInteraction hands;DetachableMagazine policy;
 AmmoSupply supply{{InteractionHand::Left,1000,{1001,1},{-.25f,-.3f,0},.12f,200*Ms}};
 AmmunitionInventory<4> inventory;DetachableMagazineSample s{};AmmoSupplySample source{};
 std::optional<HandClaim> gun;std::optional<AmmunitionReceipt> receipt;
 std::uint64_t intent=0;DetachableMagazineResult last{};AmmunitionCounts initial;
 static DetachableMagazineConfig Resource(DetachableMagazineConfig c){c.backend=MagazineControlBackend::AmmunitionResource;return c;}
 Fixture(const DetachableMagazineConfig& c,int loaded,int capacity):config(Resource(c)),policy(config),initial{loaded,191,capacity}{
  s.input={{1,2,3,4},0,1000*Ms,1100*Ms,1000*Ms,true,{true,true},{true,false}};
  s.weapon={5,3};s.trackingEpoch=8;s.native.owner=s.input.owner;s.native.weapon=s.weapon;s.native.bindingsVerified=true;
  source.source={{{1,2,3,4},{5,3},{c.insertion.id,c.insertion.revision},{7,1},8},ReloadInsertionFamily::Magazine,191,unsigned(capacity),1,1000*Ms,1100*Ms,true};
  source.trackingEpoch=8;source.bodyFromHand=Identity();source.bodyFromHand.values[3][0]=10;s.resource.emplace();
 }
 DetachableMagazineResult SendPose(bool grip,const math::Matrix4& item,std::int64_t dt=10*Ms){
  ++s.input.sequence;s.input.nowNs+=dt;s.input.observedNs=s.input.nowNs;s.input.deadlineNs=s.input.nowNs+100*Ms;
  s.input.released[0]=!grip;s.gripPressed=grip;s.weaponFromHandMeters=Multiply(config.insertion.itemFromHand,item);
  s.geometrySequence=s.input.sequence;s.intent=++intent;s.native.observedNs=s.input.nowNs;s.native.deadlineNs=s.input.deadlineNs;
  auto& r=*s.resource;const AmmoResourceContext context{{s.input.owner.actor,s.input.owner.actorGeneration,s.weapon.id,100},s.input.owner.equipGeneration,s.input.owner.space};
  const auto* ledger=inventory.Find(context.resource);
  r.snapshot={context,s.input.sequence,s.input.observedNs,s.input.deadlineNs,ledger?ledger->Snapshot().counts:initial,true};r.original=ledger?ledger->Original():std::nullopt;
  // The ordinary resource adapter refreshes this original snapshot whenever
  // no cycle is active. A returned magazine cannot reuse an expired snapshot.
  if(last.phase==DetachableMagazinePhase::Attached)s.original=OriginalMagazine{s.input.owner,s.weapon,{800,++intent},
   {config.insertion.id,config.insertion.revision},{7,1},s.trackingEpoch,s.input.sequence,s.input.observedNs,s.input.deadlineNs,
   unsigned(r.snapshot.counts.loaded),unsigned(r.snapshot.counts.capacity)};
  source.input=s.input;source.geometrySequence=s.input.sequence;source.gripPressed=grip;source.intent=++intent;
  ++source.source.sequence;source.source.observedNs=s.input.nowNs;source.source.deadlineNs=s.input.deadlineNs;
  hands.Update(s.input);if(gun)gun=hands.Renew(s.input,gun->token,{{20,1},s.input.sequence,s.input.deadlineNs,true}).claim;
  if(!gun)gun=hands.Acquire(s.input,{s.input.owner,InteractionHand::Right,HandClaimKind::GunHold,s.weapon,{{20,1},s.input.sequence,s.input.deadlineNs,true},++intent,0}).claim;
  supply.Update(source,hands);s.replacement=supply.Held();s.intent=++intent;
  last=policy.Update(s,hands);s.native.acknowledgement={};s.native.acknowledgementVerified=false;return last;
 }
 DetachableMagazineResult Send(bool grip,double along,std::int64_t dt=10*Ms){return SendPose(grip,InsertionItemPose(config.insertion,along),dt);}
 int Native(const ManualReloadRequest& request,AmmunitionOperation operation){
  const auto& sample=s.resource->snapshot;const auto now=s.input.nowNs;
  CHECK(inventory.Select(sample,now));const auto* ledger=inventory.Find(sample.context.resource);
  const auto original=operation==AmmunitionOperation::ReturnMagazine?ledger->Original():std::nullopt;
  const auto c=inventory.Submit({sample.context,++intent,now,now+50*Ms,operation,original},sample,now);CHECK(c&&inventory.Dispatch(*c,sample,now));
  receipt=AmmunitionReceipt{*c,c->id+100,now+1,now+2,c->before,c->after,true,true};CHECK(inventory.Complete(*receipt,now+2));
  s.resource->receipt=receipt;if(operation==AmmunitionOperation::RemoveMagazine)s.native.cycle=c->id;
  s.native.acknowledgement={request.id,request.owner,request.operation,ReloadAcknowledgement::Applied};s.native.acknowledgementVerified=request.id!=0;
  return 0;
 }
 std::optional<HandClaim> Support(){return hands.Acquire(s.input,{s.input.owner,InteractionHand::Left,HandClaimKind::WeaponSupport,s.weapon,
   {{2000,1},s.input.sequence,s.input.deadlineNs,true},++intent,gun->token.id}).claim;}
 int Remove(){const auto length=config.insertion.travelMeters;Send(false,length);
  auto r=Send(true,length);CHECK(r.removalGrabbed&&r.transaction.request&&r.prop&&Distance(r.prop->weaponFromItemMeters,InsertionItemPose(config.insertion,length))<1e-5f);
  CHECK(Native(*r.transaction.request,AmmunitionOperation::RemoveMagazine)==0);
  for(unsigned n=1;n<=10;++n){r=Send(true,length*(10-n)/10);CHECK(r.phase!=DetachableMagazinePhase::Cancelled);}
  CHECK(r.phase==DetachableMagazinePhase::RemovedHeld&&r.removalClaim&&r.prop&&r.prop->handTarget);
  CHECK(inventory.Find(s.resource->snapshot.context.resource)->Snapshot().counts.loaded==0&&!s.native.allThreeHeld);return 0;
 }
};
int OriginalRoundTrip(const fvr::bc2::MagazineGeometryProfile& g,int loaded,int capacity){
 Fixture f(g.interaction,loaded,capacity);CHECK(f.Remove()==0);const double length=f.config.insertion.travelMeters;
 f.Send(true,-.025);DetachableMagazineResult r;
 for(unsigned n=0;n<=12;++n){r=f.Send(true,-.025+(length+.025)*n/12);CHECK(r.phase!=DetachableMagazinePhase::Cancelled);}
 for(unsigned n=0;n<20&&!r.originalSeat;++n)r=f.Send(true,length);
 CHECK(r.originalSeat&&r.phase==DetachableMagazinePhase::AwaitingOriginalReturn&&r.prop&&!r.prop->handTarget);
 CHECK(Distance(r.prop->weaponFromItemMeters,g.attachedItem)<1e-5f&&!f.hands.Current(InteractionHand::Left));
 for(unsigned n=0;n<10;++n){r=f.Send(false,length);CHECK(r.phase==DetachableMagazinePhase::AwaitingOriginalReturn&&!r.transaction.completed);}
 f.Send(true,length);const auto support=f.Support();CHECK(support);
 CHECK(f.Native({},AmmunitionOperation::ReturnMagazine)==0);f.s.input.nowNs+=3;
 auto bad=*f.receipt;++bad.command.context.equipGeneration;CHECK(!f.policy.CompleteOriginalResourceReturn(f.s.input,f.hands,bad));
 CHECK(f.policy.CompleteOriginalResourceReturn(f.s.input,f.hands,*f.receipt));CHECK(!f.policy.CompleteOriginalResourceReturn(f.s.input,f.hands,*f.receipt));
 CHECK(f.hands.Current(InteractionHand::Left)->token==support->token);
 f.s.native.cycle=0;f.last={}; // same adapter retirement as Bc2MagazineResourceReload
 r=f.Send(false,length);CHECK(r.phase==DetachableMagazinePhase::Attached&&f.s.resource->snapshot.counts==f.initial);
 // A full just-returned magazine must be removable immediately with the same
 // ordinary consumer and a new exact removal intent.
 r=f.Send(true,length);CHECK(r.removalGrabbed&&r.transaction.request);return 0;
}
int DiscardAndReplacement(const fvr::bc2::MagazineGeometryProfile& g){Fixture f(g.interaction,7,15);CHECK(f.Remove()==0);
 const auto token=*f.inventory.Find(f.s.resource->snapshot.context.resource)->Original();
 auto far=InsertionItemPose(f.config.insertion,0);far.values[3][0]+=.5f;auto r=f.SendPose(true,far);
 CHECK(r.phase==DetachableMagazinePhase::RemovedHeld&&r.removalClaim&&r.prop&&r.prop->handTarget); // free carry stays unconstrained after withdrawal
 r=f.SendPose(false,far);CHECK(r.phase==DetachableMagazinePhase::WellEmpty&&f.inventory.Discard(token));
 f.Send(false,-.1);f.source.bodyFromHand=Identity();f.source.bodyFromHand.values[3][0]=-.25f;f.source.bodyFromHand.values[3][1]=-.3f;
 r=f.Send(true,-.1);CHECK(f.supply.Held());const double length=f.config.insertion.travelMeters;
 for(unsigned n=1;n<=20;++n){r=f.Send(true,-.1+(length+.1)*n/20);CHECK(r.phase!=DetachableMagazinePhase::Cancelled);}
 for(unsigned n=0;n<20&&!r.seat;++n)r=f.Send(true,length);
 CHECK(r.seat&&r.transaction.request&&r.phase==DetachableMagazinePhase::AwaitingSeat&&r.prop&&!r.prop->handTarget);
 const auto request=*r.transaction.request;const auto reservation=f.supply.Reserve(f.source,f.hands,*r.seat,request,f.s.native.cycle);CHECK(reservation&&reservation->units==15);
 CHECK(f.supply.ReleaseSubmitted(f.s.input,f.hands,*reservation));
 for(unsigned n=0;n<20;++n){r=f.Send(false,length);CHECK(!r.transaction.completed&&r.phase==DetachableMagazinePhase::AwaitingSeat);}
 f.source.bodyFromHand=Identity();f.source.bodyFromHand.values[3][0]=10;
 f.Send(true,length);const auto support=f.Support();CHECK(support);
 CHECK(f.Native(request,AmmunitionOperation::RefillMagazine)==0);auto after=f.source.source;++after.sequence;after.reserveUnits=176;after.observedNs=f.s.input.nowNs+2;f.s.input.nowNs+=3;
 CHECK(f.supply.Resolve(f.s.input,f.hands,{*reservation,{request.id,request.owner,request.operation,ReloadAcknowledgement::Applied},after,
  f.receipt->authorityInvocation,f.receipt->completedNs,f.s.input.deadlineNs,true}).consumed);
 CHECK(f.hands.Current(InteractionHand::Left)->token==support->token);
 f.source.source=after;r=f.Send(false,length);CHECK(r.phase==DetachableMagazinePhase::Complete&&r.transaction.completed);
 CHECK((f.s.resource->snapshot.counts==AmmunitionCounts{15,176,15}));CHECK(Distance(r.prop->weaponFromItemMeters,g.attachedItem)<1e-5f);return 0;
}
int RemovalRejectsInvalidMotion(const fvr::bc2::MagazineGeometryProfile& g){
 for(unsigned fault=0;fault<5;++fault){Fixture f(g.interaction,7,15);const auto length=f.config.insertion.travelMeters;
  f.Send(false,length);auto r=f.Send(true,length);CHECK(r.removalGrabbed&&r.transaction.request);
  CHECK(f.Native(*r.transaction.request,AmmunitionOperation::RemoveMagazine)==0);
  r=f.Send(true,length);CHECK(r.phase==DetachableMagazinePhase::Pulling);
  auto bad=InsertionItemPose(f.config.insertion,length);
  if(fault==0)bad.values[3][0]+=.3f;
  if(fault==1){for(unsigned n=1;n<=8;++n){auto rotation=Identity();const float angle=(f.config.insertion.releaseAngleRadians+.02f)*n/8;
   rotation.values[0][0]=rotation.values[1][1]=std::cos(angle);rotation.values[0][1]=std::sin(angle);rotation.values[1][0]=-std::sin(angle);
   r=f.SendPose(true,Multiply(rotation,bad));if(n<8)CHECK(r.phase==DetachableMagazinePhase::Pulling);}}
  if(fault==2)bad.values[3][0]=std::numeric_limits<float>::quiet_NaN();
  if(fault==3)++f.s.input.owner.equipGeneration;
  if(fault==4)f.s.input.tracked[0]=false;
  if(fault!=1)r=f.SendPose(true,bad);
  if(fault==2){ // malformed geometry cannot progress; loss of its lease still cancels
   CHECK(!r.physicallyRemoved&&!r.transaction.acknowledged);
   f.s.input.nowNs=f.s.input.deadlineNs;r=f.policy.Update(f.s,f.hands);
  }
  CHECK(r.phase==DetachableMagazinePhase::Cancelled&&!r.originalSeat&&!r.seat);
  CHECK(!f.hands.Current(InteractionHand::Left));
  if(fault==0)CHECK(r.motionFailure&&r.motionFailure->check==MagazineMotionFailureCheck::TranslationStep);
  if(fault==1)CHECK(r.motionFailure&&r.motionFailure->check==MagazineMotionFailureCheck::PathOrientation);
 }
 return 0;
}
}
int main(){for(const auto& g:fvr::bc2::generated::MeasuredSampledPathTestGeometry){CHECK(g.interaction.insertion.pathCount>1);
 for(int capacity:{15,30})for(int loaded:{0,capacity/2,capacity})CHECK(OriginalRoundTrip(g,loaded,capacity)==0);
 CHECK(DiscardAndReplacement(g)==0);CHECK(RemovalRejectsInvalidMotion(g)==0);}
 std::puts("Measured M9/MP443 sampled paths: 12 original return cycles and two discard/refill cycles through shared hands, magazine, supply and ledger (CPU receipts only).");return 0;}
