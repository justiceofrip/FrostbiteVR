#include "fvr/interaction/DetachableMagazine.h"
#include "fvr/interaction/AmmunitionInventory.h"
#include "Test.h"
#include <cstdio>
using namespace fvr;using namespace fvr::interaction;
namespace {
constexpr std::int64_t Ms=1000000;
math::Matrix4 Pose(float z=0,float x=0,float angle=0){auto m=reload_insertion_detail::Identity();m.values[3][2]=z;m.values[3][0]=x;
    m.values[0][0]=m.values[1][1]=std::cos(angle);m.values[0][1]=std::sin(angle);m.values[1][0]=-std::sin(angle);return m;}
DetachableMagazineConfig Config(bool resource=false){DetachableMagazineConfig c;if(resource)c.backend=MagazineControlBackend::AmmunitionResource;
    auto& p=c.insertion;p.id=10;p.revision=1;p.family=ReloadInsertionFamily::Magazine;
    p.approach=ReloadInsertionApproach::RailContact;p.itemFromHand=p.itemFromInsertion=p.weaponFromEntry=Pose();
    p.travelMeters=.1f;p.captureDistanceMeters=.06f;p.releaseDistanceMeters=.12f;p.postCaptureTravelMeters=.02f;
    p.captureAngleRadians=.6f;p.releaseAngleRadians=1.1f;p.seatToleranceMeters=.002f;p.maxStepMeters=.04f;p.maxStepRadians=.8f;
    p.alignmentNs=40*Ms;p.seatDwellNs=20*Ms;p.maxSampleGapNs=100*Ms;p.maxGuidedNs=5000*Ms;
    c.removalContact={50,1};return c;}
struct Fixture {
    HandInteraction hands;DetachableMagazine policy{Config()};
    AmmoSupply supply{{InteractionHand::Left,1000,{1001,1},{-.25f,-.3f,0},.12f,200*Ms}};
    DetachableMagazineSample s{};AmmoSupplySample source{};std::optional<HandClaim> gun;
    std::uint64_t intent=0,rightIntent=0;DetachableMagazineResult last{};std::int64_t nativeLifetime=100*Ms;
    AmmunitionInventory<4> inventory;bool resourceMode=false;std::optional<AmmunitionReceipt> lastNative;AmmunitionCounts initial{22,191,30};
    Fixture(bool resource=false,AmmunitionCounts counts={22,191,30}):policy(Config(resource)),resourceMode(resource),initial(counts){s.input={{1,2,3,4},0,1000*Ms,1100*Ms,1000*Ms,true,{true,true},{true,false}};
        s.weapon={5,3};s.trackingEpoch=8;s.weaponFromHandMeters=Pose(.1f);
        s.native.owner=s.input.owner;s.native.weapon=s.weapon;s.native.bindingsVerified=true;
        source.source={{{1,2,3,4},{5,3},{10,1},{7,1},8},ReloadInsertionFamily::Magazine,90,3,1,1000*Ms,1100*Ms,true};
        source.trackingEpoch=8;source.bodyFromHand=Pose();source.bodyFromHand.values[3][0]=10;
        if(resource){s.resource.emplace();source.source.reserveUnits=unsigned(counts.reserve);source.source.objectUnits=unsigned(counts.capacity);}
    }
    DetachableMagazineResult Send(bool grip=false,bool eject=false,float z=.1f,std::int64_t dt=10*Ms,float x=0,float angle=0){
        ++s.input.sequence;s.input.nowNs+=dt;s.input.observedNs=s.input.nowNs;s.input.deadlineNs=s.input.nowNs+100*Ms;
        s.input.released[0]=!grip;s.gripPressed=grip;s.ejectPressed=eject;s.weaponFromHandMeters=Pose(z,x,angle);
        s.geometrySequence=s.geometryInput?s.geometryInput->sequence:s.input.sequence;s.intent=++intent;s.native.observedNs=s.input.nowNs;s.native.deadlineNs=s.input.nowNs+nativeLifetime;
        if(resourceMode){auto& r=*s.resource;
            const AmmoResourceContext context{{s.input.owner.actor,s.input.owner.actorGeneration,s.weapon.id,100},s.input.owner.equipGeneration,s.input.owner.space};
            const auto* ledger=inventory.Find(context.resource);
            r.snapshot={context,s.input.sequence,s.input.observedNs,s.input.deadlineNs,ledger?ledger->Snapshot().counts:initial,true};
            r.original=ledger?ledger->Original():std::nullopt;
        }
        source.input=s.input;source.geometrySequence=s.input.sequence;source.gripPressed=grip;source.intent=++intent;
        ++source.source.sequence;source.source.observedNs=s.input.nowNs;source.source.deadlineNs=s.input.deadlineNs;
        hands.Update(s.input);
        if(gun)gun=hands.Renew(s.input,gun->token,{{20,1},s.input.sequence,s.input.deadlineNs,true}).claim;
        if(!gun)gun=hands.Acquire(s.input,{s.input.owner,InteractionHand::Right,HandClaimKind::GunHold,s.weapon,
            {{20,1},s.input.sequence,s.input.deadlineNs,true},++rightIntent,0}).claim;
        supply.Update(source,hands);s.replacement=supply.Held();s.intent=++intent;
        last=policy.Update(s,hands);s.native.acknowledgement={};s.native.acknowledgementVerified=false;return last;
    }
    void Ack(const ManualReloadRequest& r,bool held=true){s.native.cycle=22;s.native.allThreeHeld=held;
        s.native.acknowledgement={r.id,r.owner,r.operation,ReloadAcknowledgement::Applied};s.native.acknowledgementVerified=true;}
    int Native(const ManualReloadRequest& request,AmmunitionOperation operation){
        const auto& sample=s.resource->snapshot;const auto now=s.input.nowNs;
        CHECK(inventory.Select(sample,now));const auto* ledger=inventory.Find(sample.context.resource);
        const auto original=operation==AmmunitionOperation::ReturnMagazine?ledger->Original():std::nullopt;
        const auto c=inventory.Submit({sample.context,++intent,now,now+50*Ms,operation,original},sample,now);CHECK(c);
        CHECK(inventory.Dispatch(*c,sample,now));
        lastNative=AmmunitionReceipt{*c,c->id+100,now+1,now+2,c->before,c->after,true,true};
        CHECK(inventory.Complete(*lastNative,now+2));s.resource->receipt=lastNative;
        if(operation==AmmunitionOperation::RemoveMagazine)s.native.cycle=c->id;
        s.native.acknowledgement={request.id,request.owner,request.operation,ReloadAcknowledgement::Applied};
        s.native.acknowledgementVerified=request.id!=0;s.native.allThreeHeld=false;return 0;
    }
    DetachableMagazineResult Eject(){Send();const auto start=Send(false,true);if(!start.transaction.request)return start;
        Ack(*start.transaction.request);return Send(false,true);}
    DetachableMagazineResult GrabReplacement(){Send(false);source.bodyFromHand=Pose();source.bodyFromHand.values[3][0]=-.25f;source.bodyFromHand.values[3][1]=-.3f;
        return Send(true,false,-.09f);}
    DetachableMagazineResult Insert(){GrabReplacement();for(float z:{-.055f,-.02f,.015f,.05f,.08f,.1f,.1f,.1f})Send(true,false,z);return last;}
};
int EjectNeedsEdgeAndGateReceipt(){Fixture f;CHECK(f.Send(false,true).phase==DetachableMagazinePhase::Attached);
    f.Send();auto r=f.Send(false,true);CHECK(r.transaction.request&&r.transaction.request->operation==ReloadOperation::UnseatMagazine);
    CHECK(r.phase==DetachableMagazinePhase::PreparingRemoval&&!r.physicallyRemoved);
    for(unsigned n=0;n<3;++n)CHECK(!f.Send(false,true).transaction.request);
    f.Ack(*r.transaction.request);r=f.Send(false,true);CHECK(r.phase==DetachableMagazinePhase::WellEmpty&&r.physicallyRemoved);
    CHECK(r.prop&&r.prop->role==MagazinePropRole::Hidden&&f.source.source.reserveUnits==90&&!f.supply.Pending());return 0;}
int GripPullPreservesContactAndNoReserveCredit(){Fixture f;f.Send();auto r=f.Send(true,false,.1f);
    CHECK(r.removalGrabbed&&r.removalClaim&&r.transaction.request&&r.prop);
    CHECK(reload_insertion_detail::Distance(r.prop->weaponFromItemMeters,Pose(.1f))<1e-5f);
    CHECK(r.removalClaim->token.kind==HandClaimKind::Mechanism&&!f.supply.Held());
    f.Ack(*r.transaction.request);r=f.Send(true,false,.07f);CHECK(r.phase==DetachableMagazinePhase::Pulling);
    r=f.Send(true,false,.035f);CHECK(r.physicallyRemoved&&r.phase==DetachableMagazinePhase::RemovedHeld);
    const auto nativeCycle=r.nativeCycle;CHECK(nativeCycle==22&&r.prop->handTarget);
    r=f.Send(false,false,.035f);CHECK(r.phase==DetachableMagazinePhase::WellEmpty&&!f.hands.Current(InteractionHand::Left));
    CHECK(f.source.source.reserveUnits==90&&!f.supply.Pending());return 0;}
int RemovedMagazineCanMoveFreelyBeforeReplacement(){
 for(bool retained:{false,true}){Fixture f;f.Send();
  if(retained)f.s.original=OriginalMagazine{f.s.input.owner,f.s.weapon,{800,1},{10,1},{7,1},8,1,
   f.s.input.nowNs,f.s.input.deadlineNs,27,30};
  auto r=f.Send(true);CHECK(r.transaction.request);f.Ack(*r.transaction.request);
  f.Send(true,false,.07f);r=f.Send(true,false,.035f);
  CHECK(r.phase==DetachableMagazinePhase::RemovedHeld&&r.removalClaim);const auto token=r.removalClaim->token;
  // A removed magazine is carried freely. These movements exceed the pull
  // rail's per-sample position and angle limits without losing tracking.
  for(unsigned n=0;n<5;++n){r=f.Send(true,false,-.3f-float(n)*.1f,10*Ms,.25f,float(n+1)*1.1f);
   CHECK(r.phase==DetachableMagazinePhase::RemovedHeld&&r.removalClaim&&r.removalClaim->token==token);
   CHECK(r.prop&&r.prop->handTarget&&r.prop->role==MagazinePropRole::Removed);
   CHECK(!r.cancelNativeCycle&&!r.transaction.request&&!r.transaction.completed&&!r.originalSeat);
   CHECK(!f.supply.Held()&&!f.supply.Pending()&&f.source.source.reserveUnits==90);
  }
  r=f.Send(false,false,-.7f);CHECK(r.phase==DetachableMagazinePhase::WellEmpty&&!f.hands.Current(InteractionHand::Left));
  r=f.Insert();CHECK(r.phase==DetachableMagazinePhase::AwaitingSeat&&r.seat&&r.transaction.request);
  CHECK(!r.transaction.completed&&f.source.source.reserveUnits==90);
 }
 return 0;
}
int ReplacementUsesSupplyAndNativeAck(){Fixture f;CHECK(f.Eject().phase==DetachableMagazinePhase::WellEmpty);auto r=f.Insert();
    CHECK(r.phase==DetachableMagazinePhase::AwaitingSeat&&r.seat&&r.transaction.request&&f.supply.Held());
    CHECK(r.prop&&r.prop->role==MagazinePropRole::Attached&&!r.prop->handTarget&&!r.prop->handClaim.id);
    CHECK(!r.transaction.completed&&!r.transaction.acknowledged); // Attachment cannot invent native ammo completion.
    CHECK(r.transaction.request->operation==ReloadOperation::SeatMagazine&&!r.transaction.completed&&f.source.source.reserveUnits==90);
    const auto request=*r.transaction.request;const auto reservation=f.supply.Reserve(f.source,f.hands,*r.seat,request,22);
    CHECK(reservation&&reservation->units==3&&f.supply.Pending());
    r=f.Send(true,false,.1f);CHECK(!r.transaction.completed&&!r.transaction.request&&r.prop&&r.prop->role==MagazinePropRole::Attached&&!r.prop->handTarget);
    const auto& pending=*f.supply.Pending();auto after=f.source.source;after.sequence++;after.reserveUnits=87;
    AmmoSupplyReceipt receipt{pending,{request.id,request.owner,request.operation,ReloadAcknowledgement::Applied},after,99,
        f.s.input.nowNs,f.s.input.deadlineNs,true};CHECK(f.supply.Resolve(f.s.input,f.hands,receipt).consumed);
    f.source.source=after;f.Ack(request,false);r=f.Send(true,false,.1f);
    CHECK(r.transaction.completed&&r.phase==DetachableMagazinePhase::Complete&&!f.supply.Pending()&&!f.supply.Held());return 0;}
int SeatedAttachmentSurvivesLoadingHandReleaseWithoutCompletion(){
 Fixture f;CHECK(f.Eject().phase==DetachableMagazinePhase::WellEmpty);auto r=f.Insert();
 CHECK(r.transaction.request&&r.seat);const auto request=*r.transaction.request;
 CHECK(f.supply.Reserve(f.source,f.hands,*r.seat,request,22));
 const auto reservation=*f.supply.Pending();const auto seat=*r.seat;
 f.s.native.allThreeHeld=false; // The original native continuation is running.
 for(unsigned n=0;n<280;++n){r=f.Send(false,false,.7f);
  CHECK(r.phase==DetachableMagazinePhase::AwaitingSeat&&r.prop&&r.prop->role==MagazinePropRole::Attached);
  CHECK(!r.prop->handTarget&&!r.prop->handClaim.id&&!f.hands.Current(InteractionHand::Left));
  CHECK(reload_insertion_detail::Distance(r.prop->weaponFromItemMeters,Pose(.1f))<.00001f);
  CHECK(r.prop->observedNs==f.s.input.observedNs&&r.prop->deadlineNs<=f.s.input.deadlineNs);
  CHECK(!r.transaction.completed&&!r.transaction.acknowledged&&!r.transaction.request&&!r.seat);
  CHECK(f.supply.Pending()&&f.supply.Pending()->seat==seat.id&&f.supply.Pending()->startedNs==reservation.startedNs);
  CHECK(f.source.source.reserveUnits==90);
 }
 const auto target=*r.prop;f.s.input.nowNs+=1;r=f.policy.Update(f.s,f.hands);
 CHECK(r.prop&&r.prop->observedNs==target.observedNs&&r.prop->deadlineNs==target.deadlineNs&&!r.transaction.completed);
 auto after=f.source.source;++after.sequence;after.reserveUnits=87;after.observedNs=f.s.input.nowNs;
 CHECK(f.supply.Resolve(f.s.input,f.hands,{reservation,{request.id,request.owner,request.operation,ReloadAcknowledgement::Applied},
  after,99,f.s.input.nowNs,f.s.input.deadlineNs,true}).consumed);
 f.source.source=after;f.Ack(request,false);r=f.Send(false);
 CHECK(r.transaction.completed&&r.phase==DetachableMagazinePhase::Complete&&!f.supply.Pending());return 0;
}
int NoNativeAcknowledgementFromTimeOrPose(){Fixture f;CHECK(f.Eject().phase==DetachableMagazinePhase::WellEmpty);const auto r=f.Insert();
    CHECK(r.transaction.request);for(unsigned n=0;n<20;++n){const auto pending=f.Send(true,false,.1f);CHECK(!pending.transaction.completed&&!pending.transaction.request);}
    CHECK(f.supply.Held()&&f.source.source.reserveUnits==90);return 0;}
int WrongOrUnverifiedAcknowledgement(){Fixture f;f.Send();const auto r=f.Send(false,true);CHECK(r.transaction.request);
    auto wrong=*r.transaction.request;++wrong.id;f.Ack(wrong);CHECK(f.Send(false,true).phase==DetachableMagazinePhase::PreparingRemoval);
    f.Ack(*r.transaction.request);f.s.native.acknowledgementVerified=false;CHECK(f.Send(false,true).phase==DetachableMagazinePhase::PreparingRemoval);
    f.Ack(*r.transaction.request,false);CHECK(f.Send(false,true).phase==DetachableMagazinePhase::Cancelled);return 0;}
int PullReleaseAndTrackingJumpCancel(){for(unsigned mode=0;mode<3;++mode){Fixture f;f.Send();const auto r=f.Send(true);CHECK(r.transaction.request);
    f.Ack(*r.transaction.request);f.Send(true,false,.08f);
    const auto cancelled=mode==0?f.Send(false,false,.08f):mode==1?f.Send(true,false,-.2f):f.Send(true,false,.08f,101*Ms);
    CHECK(cancelled.phase==DetachableMagazinePhase::Cancelled&&cancelled.cancelNativeCycle&&!cancelled.prop);
    CHECK(!f.hands.Current(InteractionHand::Left)&&f.hands.Current(InteractionHand::Right));
    CHECK(f.Send().reason==DetachableMagazineReason::NeedsReconciliation);
    }return 0;}
int NoStealingSupportClaim(){Fixture f;f.Send();f.s.input.nowNs+=1;f.s.input.released[0]=false; // claim on fresh next input is acquired below
    ++f.s.input.sequence;f.s.input.observedNs=f.s.input.nowNs;f.s.input.deadlineNs=f.s.input.nowNs+100*Ms;
    f.hands.Update(f.s.input);f.gun=f.hands.Renew(f.s.input,f.gun->token,{{20,1},f.s.input.sequence,f.s.input.deadlineNs,true}).claim;
    const auto support=f.hands.Acquire(f.s.input,{f.s.input.owner,InteractionHand::Left,HandClaimKind::WeaponSupport,f.s.weapon,
        {{2000,1},f.s.input.sequence,f.s.input.deadlineNs,true},++f.intent,f.gun->token.id});CHECK(support.claim);
    auto r=f.Send(true);CHECK(r.reason==DetachableMagazineReason::HandUnavailable&&!r.transaction.request);
    CHECK(f.hands.Current(InteractionHand::Left)->token==support.claim->token);return 0;}
int DuplicateCannotPullOrRenewEvidence(){Fixture f;f.Send();auto r=f.Send(true);CHECK(r.transaction.request);f.Ack(*r.transaction.request);f.Send(true,false,.08f);
    const auto before=f.last.prop;f.s.weaponFromHandMeters=Pose(-.2f);f.s.input.nowNs+=30*Ms;
    r=f.policy.Update(f.s,f.hands);CHECK(r.phase==DetachableMagazinePhase::Pulling&&!r.physicallyRemoved&&r.prop);
    CHECK(r.prop->deadlineNs==before->deadlineNs&&reload_insertion_detail::Distance(r.prop->weaponFromItemMeters,before->weaponFromItemMeters)<1e-5f);return 0;}
int RebaselineRequiresRetirement(){Fixture f;f.Send();const auto r=f.Send(false,true);CHECK(r.transaction.request);const auto stop=f.policy.Cancel(f.s.input,f.hands);
    MagazineRebaseline proof{f.s.input.owner,f.s.weapon,0,r.transaction.request->id,1,f.s.input.nowNs,f.s.input.deadlineNs,false};
    CHECK(!f.policy.Rebaseline(f.s.input,f.hands,proof));proof.verified=true;proof.retiredCycle=99;CHECK(!f.policy.Rebaseline(f.s.input,f.hands,proof));
    proof.retiredCycle=0;CHECK(f.policy.Rebaseline(f.s.input,f.hands,proof));CHECK(f.Send(false,true).phase==DetachableMagazinePhase::Attached);
    f.Send();const auto next=f.Send(false,true);CHECK(next.transaction.request&&next.transaction.request->id>r.transaction.request->id&&stop.cancelNativeCycle);return 0;}
int KeyedRailRejectsBackwardsAndRequiresTravel(){auto p=Config().insertion;CHECK(ValidateReloadInsertionProfile(p));
    auto raw=Pose(.1f,0,3.14159265f);CHECK(!reload_insertion_detail::CaptureGeometry(raw,p));
    raw=Pose(.09f,.05f,.3f);CHECK(reload_insertion_detail::CaptureGeometry(raw,p));
    Fixture f;f.Eject();f.GrabReplacement();for(unsigned n=0;n<15;++n){auto r=f.Send(true,false,-.055f);CHECK(!r.seat&&!r.transaction.request);}
    CHECK(f.last.insertion.phase==ReloadInsertionPhase::Guided&&!f.last.transaction.completed);return 0;}
int LateRendererSeatUsesOriginalSupplyEvidence(){Fixture f;f.Eject();f.GrabReplacement();DetachableMagazineResult r;AmmoSupplySample original;
    for(float z:{-.09f,-.055f,-.02f,.015f,.05f,.08f,.1f,.1f,.1f}){
        original=f.source;f.s.geometryInput=f.s.input;r=f.Send(true,false,z);
    }
    CHECK(r.seat&&r.transaction.request&&r.seat->inputSequence==original.input.sequence&&r.seat->inputSequence<f.s.input.sequence);
    CHECK(r.prop&&r.prop->role==MagazinePropRole::Attached&&!r.prop->handTarget);
    CHECK(r.prop->observedNs==f.s.input.observedNs&&r.prop->deadlineNs<=f.s.input.deadlineNs);
    const auto reservation=f.supply.ReserveFrom(f.source,f.hands,original,*r.seat,*r.transaction.request,22);CHECK(reservation);
    CHECK(reservation->sourceSequence==f.source.source.sequence&&reservation->claim==r.seat->itemClaim);
    CHECK(reservation->seat==r.seat->id&&reservation->request==r.transaction.request->id);
    return 0;}
int MissingContactRetainsOnlyOriginalGuidedTarget(){Fixture f;f.Eject();f.GrabReplacement();auto r=f.Send(true,false,-.055f);CHECK(r.insertion.captured&&r.prop);
    const auto target=*r.prop;auto forged=f.s.input;forged.deadlineNs++;
    f.s.geometryInput=forged;r=f.Send(true,false,.1f);
    CHECK(r.phase==DetachableMagazinePhase::Guided&&r.prop&&r.prop->deadlineNs==target.deadlineNs&&!r.seat&&!r.transaction.request);
    CHECK(reload_insertion_detail::Distance(r.prop->weaponFromItemMeters,target.weaponFromItemMeters)<1e-5f);
    for(unsigned n=0;n<10;++n)r=f.Send(true,false,.1f);
    CHECK(!r.prop||r.prop->role==MagazinePropRole::Hidden);CHECK(!r.seat&&!r.transaction.request);return 0;}
int OriginalMagazineHasSeparateIdentityAndReturnReceipt(){
 Fixture f;f.Send();f.s.original=OriginalMagazine{f.s.input.owner,f.s.weapon,{800,1},{10,1},{7,1},8,1,
  f.s.input.nowNs,f.s.input.deadlineNs,27,30};
 auto result=f.Send(true);CHECK(result.transaction.request&&result.original&&result.removalClaim);
 CHECK(result.removalClaim->token.item==f.s.weapon&&result.original->item!=f.s.weapon);
 f.Ack(*result.transaction.request);
 for(float z:{.07f,.035f,0.f,-.035f,0.f,.035f,.07f,.1f,.1f,.1f,.1f}){result=f.Send(true,false,z);if(result.originalSeat)break;}
 for(unsigned n=0;n<12&&!result.originalSeat;++n)result=f.Send(true,false,.1f);
 CHECK(result.originalSeat&&result.original&&result.phase==DetachableMagazinePhase::AwaitingOriginalReturn);
 CHECK(!result.transaction.request&&!result.transaction.completed&&!f.supply.Pending()&&!f.supply.Held());
 const auto seat=*result.originalSeat;const auto original=*result.original;
 CHECK(seat.identity.item==original.item&&seat.identity.weapon==f.s.weapon&&original.rounds==27);
 OriginalMagazineReturnReceipt receipt{original,22,seat.id,1,f.s.input.nowNs,f.s.input.deadlineNs,true};
 for(unsigned bad=0;bad<7;++bad){auto wrong=receipt;if(bad==0)wrong.verified=false;if(bad==1)++wrong.original.item.generation;
  if(bad==2)++wrong.original.rounds;if(bad==3)++wrong.cycle;if(bad==4)++wrong.seat;
  if(bad==5)wrong.deadlineNs=f.s.input.nowNs;if(bad==6)wrong.observedNs=0;
  CHECK(!f.policy.CompleteOriginalReturn(f.s.input,f.hands,wrong));}
 CHECK(f.policy.CompleteOriginalReturn(f.s.input,f.hands,receipt));CHECK(!f.policy.CompleteOriginalReturn(f.s.input,f.hands,receipt));
 CHECK(f.Send(true).phase==DetachableMagazinePhase::Attached&&f.source.source.reserveUnits==90);
 return 0;
}
int UnstartedRollbackRequiresExactPreGateRequest(){Fixture f;f.Send();auto start=f.Send(true);CHECK(start.transaction.request&&start.removalClaim);
 auto wrong=*start.transaction.request;++wrong.id;CHECK(!f.policy.RejectUnstarted(f.s.input,f.hands,wrong));
 CHECK(f.policy.RejectUnstarted(f.s.input,f.hands,*start.transaction.request));CHECK(!f.hands.Current(InteractionHand::Left));
 CHECK(!f.Send(true).transaction.request);f.Send();auto next=f.Send(true);CHECK(next.transaction.request&&next.transaction.request->id>start.transaction.request->id);
 f.Ack(*next.transaction.request);f.Send(true);CHECK(!f.policy.RejectUnstarted(f.s.input,f.hands,*next.transaction.request));
 CHECK(!f.supply.Pending()&&f.source.source.reserveUnits==90);return 0;}

int ClaimFailureRetainsExactRenewalEvidence(){
 for(unsigned mode=0;mode<2;++mode){Fixture f;f.Send();auto r=f.Send(true);CHECK(r.transaction.request&&r.removalClaim);
  f.Ack(*r.transaction.request);r=f.Send(true,false,.08f);CHECK(r.phase==DetachableMagazinePhase::Pulling&&r.removalClaim);
  const auto expected=*r.removalClaim;
  // Expiry and a later arbiter processing clock are distinct failed renewals.
  if(mode==0)CHECK(f.hands.Renew(f.s.input,expected.token,{expected.token.contact,f.s.input.sequence,f.s.input.nowNs+5*Ms,true}).accepted);
  ++f.s.input.sequence;f.s.input.nowNs+=10*Ms;f.s.input.observedNs=f.s.input.nowNs;f.s.input.deadlineNs=f.s.input.nowNs+100*Ms;
  f.s.geometrySequence=f.s.input.sequence;f.s.native.observedNs=f.s.input.nowNs;f.s.native.deadlineNs=f.s.input.deadlineNs;
  CHECK(f.hands.Update(f.s.input).inputValid);
  f.gun=f.hands.Renew(f.s.input,f.gun->token,{{20,1},f.s.input.sequence,f.s.input.deadlineNs,true}).claim;CHECK(f.gun);
  if(mode==1){auto later=f.s.input;++later.nowNs;CHECK(f.hands.Update(later).inputValid);}
  r=f.policy.Update(f.s,f.hands);
  CHECK(r.phase==DetachableMagazinePhase::Cancelled&&r.reason==DetachableMagazineReason::ClaimLost&&r.claimFailure);
  const auto& evidence=*r.claimFailure;
  CHECK(evidence.expected.token==expected.token&&evidence.expected.inputSequence==expected.inputSequence&&evidence.expected.deadlineNs==expected.deadlineNs);
  CHECK(evidence.nativeDeadlineNs==f.s.native.deadlineNs&&evidence.geometrySequence==f.s.geometrySequence);
  if(mode==0)CHECK(evidence.reason==HandInteractionReason::StaleToken&&!evidence.current);
  else CHECK(evidence.reason==HandInteractionReason::StaleInput&&evidence.current&&evidence.current->token==expected.token);
  CHECK(!r.transaction.request&&!r.seat&&!r.prop&&r.cancelNativeCycle);
 }
 return 0;
}
int ShortNativeObservationCannotExpirePhysicalGrip(){
 Fixture f;f.Send();f.nativeLifetime=5*Ms;auto r=f.Send(true);CHECK(r.transaction.request&&r.removalClaim&&r.prop);
 const auto claim=r.removalClaim->token;const auto oldNativeDeadline=f.s.native.deadlineNs;
 // Visual geometry follows the original input/claim bounds, not the native lease.
 CHECK(r.prop->deadlineNs==f.s.input.deadlineNs&&r.prop->deadlineNs>oldNativeDeadline);
 f.Ack(*r.transaction.request);r=f.Send(true,false,.075f);
 CHECK(f.s.input.nowNs>oldNativeDeadline&&r.phase==DetachableMagazinePhase::Pulling&&r.removalClaim&&r.removalClaim->token==claim);
 CHECK(!r.claimFailure&&r.prop&&r.prop->deadlineNs==f.s.input.deadlineNs&&r.removalClaim->deadlineNs==f.s.input.deadlineNs);
 r=f.Send(true,false,.04f);CHECK(r.removalClaim&&r.removalClaim->token==claim);
 r=f.Send(true,false,.01f);CHECK(r.phase==DetachableMagazinePhase::RemovedHeld&&r.removalClaim->token==claim);
 CHECK(!r.transaction.request&&!r.seat&&!r.transaction.completed&&f.source.source.reserveUnits==90);
 const auto deadline=r.removalClaim->deadlineNs;auto oldPose=*r.prop;
 ++f.s.input.nowNs;r=f.policy.Update(f.s,f.hands);
 CHECK(r.removalClaim&&r.removalClaim->deadlineNs==deadline&&r.prop&&r.prop->deadlineNs==oldPose.deadlineNs);
 // A longer physical claim does not make the native observation current.
 f.s.input.nowNs=f.s.native.deadlineNs;r=f.policy.Update(f.s,f.hands);
 CHECK(r.phase==DetachableMagazinePhase::Cancelled&&r.reason==DetachableMagazineReason::UnverifiedNative&&!r.prop);
 CHECK(!f.hands.Current(InteractionHand::Left)&&!r.transaction.completed);
 return 0;
}
int NativeFailureCheckDistinguishesObservationGateAndHold(){
 for(unsigned mode=0;mode<3;++mode){Fixture f;f.Send();auto r=f.Send(true);CHECK(r.transaction.request);
  const auto request=*r.transaction.request;
  if(mode==1){f.Ack(request,false);r=f.Send(true);}
  else {f.Ack(request);r=f.Send(true);CHECK(r.phase==DetachableMagazinePhase::Pulling);
   if(mode==0)f.nativeLifetime=0;else f.s.native.allThreeHeld=false;r=f.Send(true);}
  CHECK(r.phase==DetachableMagazinePhase::Cancelled&&r.reason==DetachableMagazineReason::UnverifiedNative);
  CHECK(r.nativeFailureCheck==(mode==0?MagazineNativeFailureCheck::Observation:mode==1?MagazineNativeFailureCheck::UnseatAcknowledgement:MagazineNativeFailureCheck::HeldCycle));
 }
 return 0;
}
int ResourceBackendUsesTheSameHandsAndRail(){
 for(bool returnOriginal:{true,false}){
  Fixture f(true);f.Send();f.s.original=OriginalMagazine{f.s.input.owner,f.s.weapon,{800,1},{10,1},{7,1},8,1,
   f.s.input.nowNs,f.s.input.deadlineNs,22,30};
  auto r=f.Send(true);CHECK(r.transaction.request&&r.removalClaim);
  CHECK(f.Native(*r.transaction.request,AmmunitionOperation::RemoveMagazine)==0);
  const auto resource=*f.inventory.Find(f.s.resource->snapshot.context.resource)->Original();
  r=f.Send(true,false,.07f);CHECK(r.phase==DetachableMagazinePhase::Pulling&&!f.s.native.allThreeHeld);
  r=f.Send(true,false,.035f);CHECK(r.phase==DetachableMagazinePhase::RemovedHeld&&r.physicallyRemoved&&r.prop->handTarget);
  CHECK(f.s.resource->snapshot.counts.loaded==0&&resource.rounds==22);
  if(returnOriginal){
   for(float z:{0.f,-.035f,0.f,.035f,.07f,.1f,.1f,.1f,.1f}){r=f.Send(true,false,z);if(r.originalSeat)break;}
   for(unsigned n=0;n<12&&!r.originalSeat;++n)r=f.Send(true,false,.1f);
   CHECK(r.originalSeat&&r.phase==DetachableMagazinePhase::AwaitingOriginalReturn);
   CHECK(!f.hands.Current(InteractionHand::Left)&&!r.prop->handTarget);
   CHECK(!f.policy.CompleteOriginalReturn(f.s.input,f.hands,{*r.original,f.s.native.cycle,r.originalSeat->id,1,f.s.input.nowNs,f.s.input.deadlineNs,true}));
   CHECK(f.Native({},AmmunitionOperation::ReturnMagazine)==0);f.s.input.nowNs+=3;
   auto wrong=*f.lastNative;++wrong.command.original->rounds;
   CHECK(!f.policy.CompleteOriginalResourceReturn(f.s.input,f.hands,wrong));
   CHECK(f.policy.CompleteOriginalResourceReturn(f.s.input,f.hands,*f.lastNative));
   CHECK(!f.policy.CompleteOriginalResourceReturn(f.s.input,f.hands,*f.lastNative));
   r=f.Send();CHECK(r.phase==DetachableMagazinePhase::Attached);
   CHECK((f.s.resource->snapshot.counts==AmmunitionCounts{22,191,30}));
  }else{
   r=f.Send(false);CHECK(r.phase==DetachableMagazinePhase::WellEmpty);
   CHECK(f.inventory.Discard(resource));CHECK(f.inventory.Find(resource.owner)->Snapshot().counts.loaded==0);
   r=f.Insert();CHECK(r.phase==DetachableMagazinePhase::AwaitingSeat&&r.transaction.request&&r.seat);
   const auto request=*r.transaction.request;
   const auto reservation=f.supply.Reserve(f.source,f.hands,*r.seat,request,f.s.native.cycle);CHECK(reservation&&reservation->units==30);
   CHECK(f.supply.ReleaseSubmitted(f.s.input,f.hands,*reservation));
   // The real insertion result frees the hand before a native completion. No
   // reload animation, 3-second delay or fake allThreeHeld enters this path.
   for(unsigned n=0;n<20;++n){r=f.Send(false);CHECK(r.phase==DetachableMagazinePhase::AwaitingSeat&&!r.transaction.completed);
    CHECK(r.prop&&!r.prop->handTarget&&!f.hands.Current(InteractionHand::Left));}
   f.source.bodyFromHand.values[3][0]=10; // move from the pouch to the gun's support contact
   r=f.Send(true);CHECK(r.phase==DetachableMagazinePhase::AwaitingSeat&&!r.prop->handTarget);
   auto support=f.hands.Acquire(f.s.input,{f.s.input.owner,InteractionHand::Left,HandClaimKind::WeaponSupport,f.s.weapon,
     {{2000,1},f.s.input.sequence,f.s.input.deadlineNs,true},++f.intent,f.gun->token.id});
   CHECK(support.claim);
   CHECK(f.Native(request,AmmunitionOperation::RefillMagazine)==0);
   auto after=f.source.source;++after.sequence;after.reserveUnits=161;after.observedNs=f.s.input.nowNs+2;
   f.s.input.nowNs+=3;
   CHECK(f.supply.Resolve(f.s.input,f.hands,{*reservation,{request.id,request.owner,request.operation,ReloadAcknowledgement::Applied},
      after,f.lastNative->authorityInvocation,f.lastNative->completedNs,f.s.input.deadlineNs,true}).consumed);
   CHECK(f.hands.Current(InteractionHand::Left)->token==support.claim->token);
   f.source.source=after;r=f.Send(false);CHECK(r.phase==DetachableMagazinePhase::Complete&&r.transaction.completed);
   CHECK((f.s.resource->snapshot.counts==AmmunitionCounts{30,161,30}));
   CHECK(f.policy.FinishResourceCycle());CHECK(!f.policy.FinishResourceCycle());
  }
 }
 return 0;
}
int ResourceBackendRejectsAnimationAndForgedEvidence(){
 for(unsigned reason=0;reason<7;++reason){
  Fixture f(true);f.Send();auto r=f.Send(true);CHECK(r.transaction.request);
  CHECK(f.Native(*r.transaction.request,AmmunitionOperation::RemoveMagazine)==0);
  if(reason==0)f.s.resource->receipt.reset();
  if(reason==1)f.s.resource->receipt->copiesVerified=false;
  if(reason==2)++f.s.resource->receipt->command.context.resource.weaponGeneration;
  if(reason==3)++f.s.resource->receipt->after.reserve;
  if(reason==4)f.s.resource->receipt->beganNs=0;
  if(reason==5)f.s.resource->receipt->command.requestedNs=f.s.input.nowNs-1;
  if(reason==6)++f.s.native.cycle;
  f.s.native.allThreeHeld=true; // legacy proof cannot bypass resource validation
  r=f.Send(true);CHECK(r.phase==DetachableMagazinePhase::Cancelled&&!r.transaction.acknowledged);
  const auto* ledger=f.inventory.Find(f.s.resource->snapshot.context.resource);
  CHECK(ledger->WellEmpty()&&ledger->Original()->rounds==22); // cancellation doesn't manufacture rounds
 }
 return 0;
}
int ResourceOriginalReturnsScaleAcrossCapacities(){
 for(int capacity:{8,15,30,32,100})for(int loaded:{0,capacity/2,capacity})for(int reserve:{0,191}){
  Fixture f(true,{loaded,reserve,capacity});f.Send();
  f.s.original=OriginalMagazine{f.s.input.owner,f.s.weapon,{800,1},{10,1},{7,1},8,1,
   f.s.input.nowNs,f.s.input.deadlineNs,unsigned(loaded),unsigned(capacity)};
  auto r=f.Send(true);CHECK(r.transaction.request);CHECK(f.Native(*r.transaction.request,AmmunitionOperation::RemoveMagazine)==0);
  CHECK(f.lastNative->command.mutationRequired==(loaded!=0));
  for(float z:{.07f,.035f,0.f,-.035f,0.f,.035f,.07f,.1f,.1f,.1f,.1f}){r=f.Send(true,false,z);if(r.originalSeat)break;}
  for(unsigned n=0;n<12&&!r.originalSeat;++n)r=f.Send(true,false,.1f);
  CHECK(r.originalSeat&&r.phase==DetachableMagazinePhase::AwaitingOriginalReturn);
  CHECK(f.Native({},AmmunitionOperation::ReturnMagazine)==0);f.s.input.nowNs+=3;
  CHECK(f.lastNative->command.mutationRequired==(loaded!=0));
  CHECK(f.policy.CompleteOriginalResourceReturn(f.s.input,f.hands,*f.lastNative));
  CHECK(f.inventory.Find(f.s.resource->snapshot.context.resource)->Snapshot().counts==f.initial);
 }
 return 0;
}
}
int main(){if(ResourceBackendUsesTheSameHandsAndRail()||ResourceBackendRejectsAnimationAndForgedEvidence()||ResourceOriginalReturnsScaleAcrossCapacities())return 1;if(RemovedMagazineCanMoveFreelyBeforeReplacement())return 1;if(NativeFailureCheckDistinguishesObservationGateAndHold())return 1;if(ShortNativeObservationCannotExpirePhysicalGrip())return 1;if(ClaimFailureRetainsExactRenewalEvidence())return 1;if(EjectNeedsEdgeAndGateReceipt()||GripPullPreservesContactAndNoReserveCredit()||ReplacementUsesSupplyAndNativeAck()||
    NoNativeAcknowledgementFromTimeOrPose()||WrongOrUnverifiedAcknowledgement()||PullReleaseAndTrackingJumpCancel()||NoStealingSupportClaim()||
    DuplicateCannotPullOrRenewEvidence()||RebaselineRequiresRetirement()||KeyedRailRejectsBackwardsAndRequiresTravel()||
    LateRendererSeatUsesOriginalSupplyEvidence()||MissingContactRetainsOnlyOriginalGuidedTarget()||UnstartedRollbackRequiresExactPreGateRequest()||OriginalMagazineHasSeparateIdentityAndReturnReceipt()||SeatedAttachmentSurvivesLoadingHandReleaseWithoutCompletion())return 1;
    std::puts("DetachableMagazine: legacy and resource-backed hand/rail/return/discard groups passed (mock native receipts).");return 0;}
