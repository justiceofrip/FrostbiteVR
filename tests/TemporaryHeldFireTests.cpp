#include "fvr/interaction/TemporaryHeldFire.h"
#include "Bc2GroundPickup.h"
#include <cstdio>
#include <stdexcept>
using namespace fvr::interaction;
#define CHECK(v) do { if(!(v))throw std::runtime_error(#v); }while(false)
namespace {
constexpr TemporaryFireCapabilities accepted{true,true,true,true,true,true};
constexpr auto right=InteractionHand::Right;
bool Fires(const TemporaryFireResult& r){return r.command&&r.command->triggerHeld;}
struct Fixture {
    TemporaryHeldFire gate{accepted};HandInteraction arbiter;TemporaryHeldFireSample s;
    HandClaimToken token{};std::uint64_t intent=0;
    Fixture(){
        s.inputEpoch=9;s.input.generation=1;s.input.spaceGeneration=7;s.input.predictedNs=1000000000;
        s.input.focused=s.input.headValid=true;s.input.hands[1].active=Trigger|Squeeze;
        s.input.hands[1].gripTracked=s.input.hands[1].aimTracked=true;
        s.input.hands[1].grip.position={.2f,1.3f,-.3f};s.input.hands[1].aim.position={.2f,1.3f,-.4f};
        s.hand={{30,4,8,7},1,1000000000,1100000000,1000000000,true,{false,true},{false,false}};
        CHECK(arbiter.Update(s.hand).inputValid);
        auto preview=arbiter.Acquire(s.hand,{s.hand.owner,right,HandClaimKind::BodyInventory,{501,9},
            {{900,1},1,s.hand.deadlineNs,true},++intent,0});CHECK(preview.accepted&&preview.claim);
        auto gun=arbiter.Transfer(s.hand,preview.claim->token,{s.hand.owner,right,HandClaimKind::GunHold,{501,9},
            {{901,1},1,s.hand.deadlineNs,true},++intent,0});CHECK(gun.accepted&&gun.claim);token=gun.claim->token;
        TemporaryHeldIdentity id;id.lease={100,2};id.actor={10,2,30,4};id.originalWorldItem={10,2,500,9};
        id.controlOwner=s.hand.owner;id.heldItem={501,9};id.target={{600,1},{601,1},{602,1},{603,1}};
        id.activationClear={605,1};
        id.projection.permanent={50,6,7};id.projection.nativeInventoryRevision=8;
        id.projection.restoration={604,1};id.projection.assignmentsUnchanged=true;
        id.inputEpoch=9;id.activationInput=1;id.activatedNs=s.hand.observedNs;
        s.native=TemporaryHeldNativeReceipt{id,{},1,true,true,true,true,true,true,true};Refresh();
    }
    void Refresh(){
        auto renewed=arbiter.Renew(s.hand,token,{token.contact,s.hand.sequence,s.hand.deadlineNs,true});CHECK(renewed.accepted&&renewed.claim);
        s.gunClaim=renewed.claim;
        s.native->source={s.hand.sequence,s.hand.observedNs,s.hand.deadlineNs};
        TemporaryHeldAimReceipt aim;aim.lease=s.native->identity.lease;aim.target=s.native->identity.target;aim.claim=token;
        aim.inputEpoch=s.inputEpoch;aim.space=s.input.spaceGeneration;aim.inputSource=s.native->source;
        aim.rawGrip=s.input.hands[1].grip;aim.rawAim=s.input.hands[1].aim;
        for(unsigned i=0;i<4;++i)aim.trackedMuzzle.values[i][i]=1;
        aim.trackedMuzzle.values[3][0]=.2f;aim.trackedMuzzle.values[3][1]=1.3f;aim.trackedMuzzle.values[3][2]=.5f;
        aim.nativeAimBound=true;s.aim=aim;
    }
    void Next(float trigger=0){
        ++s.input.generation;s.input.predictedNs+=10000000;s.input.hands[1].trigger=trigger;
        s.hand.sequence=s.input.generation;s.hand.nowNs=s.hand.observedNs=s.input.predictedNs;s.hand.deadlineNs=s.hand.nowNs+100000000;
        CHECK(arbiter.Update(s.hand).inputValid);Refresh();
    }
    TemporaryFireResult Run(float trigger){Next(trigger);return gate.Update(s);}
    void Armed(){CHECK(!Fires(gate.Update(s)));auto neutral=Run(0);CHECK(neutral.armed&&!Fires(neutral));}
};
PickupAmmoLedger Ammo(std::uint64_t n){PickupAmmoLedger a;a.count=1;a.entries[0]={7,n};return a;}
void NoNativeBindingNoFire(){Fixture f;TemporaryHeldFire disabled;CHECK(!Fires(disabled.Update(f.s)));CHECK(disabled.Update(f.s).reason==TemporaryFireReason::Disabled);CHECK(!fvr::bc2::TemporaryHeldFireCapabilities().Ready(TemporaryNativeEquip::ExternalActiveEntity));}
void HeldTriggerPickupNeverFires(){Fixture f;f.s.input.hands[1].trigger=1;CHECK(!Fires(f.gate.Update(f.s)));for(int i=0;i<8;++i)CHECK(!Fires(f.Run(1)));CHECK(!Fires(f.Run(.5f)));CHECK(!Fires(f.Run(1)));auto neutral=f.Run(.1f);CHECK(neutral.armed&&!Fires(neutral));auto press=f.Run(.75f);CHECK(Fires(press)&&press.command->triggerPressed);}
void NativeLevelNotShotReplay(){Fixture f;f.Armed();auto r=f.Run(1);CHECK(Fires(r)&&r.command->triggerPressed);for(int i=0;i<6;++i){r=f.gate.Update(f.s);CHECK(Fires(r)&&!r.command->triggerPressed);}r=f.Run(1);CHECK(Fires(r)&&!r.command->triggerPressed);r=f.Run(0);CHECK(!Fires(r));r=f.Run(1);CHECK(Fires(r)&&r.command->triggerPressed);}
void TargetChangeRequiresRelease(){Fixture f;f.Armed();CHECK(Fires(f.Run(1)));const auto old=f.s.native->identity;f.Next(1);++f.s.native->identity.lease.generation;++f.s.native->identity.target.weapon.generation;f.s.native->identity.activationInput=f.s.hand.sequence;f.s.native->identity.activatedNs=f.s.hand.nowNs;f.Refresh();auto r=f.gate.Update(f.s);CHECK(!Fires(r)&&r.revoked==std::optional(old));CHECK(!Fires(f.Run(1)));CHECK(!Fires(f.Run(0)));CHECK(Fires(f.Run(1)));}
void FocusRecoveryNeedsFreshRelease(){Fixture f;f.Armed();CHECK(Fires(f.Run(1)));f.s.hand.focused=false;CHECK(!Fires(f.gate.Update(f.s)));f.s.hand.focused=true;CHECK(!Fires(f.gate.Update(f.s)));CHECK(!Fires(f.Run(1)));CHECK(!Fires(f.Run(1)));CHECK(!Fires(f.Run(0)));CHECK(Fires(f.Run(1)));}
void ReconnectAndNewSpaceSequenceReset(){Fixture f;f.Armed();CHECK(Fires(f.Run(1)));f.arbiter.Reset();++f.s.inputEpoch;++f.s.input.spaceGeneration;f.s.input.generation=1;f.s.input.predictedNs+=10000000;f.s.hand.sequence=1;f.s.hand.owner.space=f.s.input.spaceGeneration;f.s.hand.nowNs=f.s.hand.observedNs=f.s.input.predictedNs;f.s.hand.deadlineNs=f.s.hand.nowNs+100000000;CHECK(f.arbiter.Update(f.s.hand).inputValid);auto claim=f.arbiter.Acquire(f.s.hand,{f.s.hand.owner,right,HandClaimKind::GunHold,{501,9},{{901,2},1,f.s.hand.deadlineNs,true},++f.intent,0});CHECK(claim.claim);f.token=claim.claim->token;auto& id=f.s.native->identity;++id.lease.generation;id.controlOwner=f.s.hand.owner;id.inputEpoch=f.s.inputEpoch;id.activationInput=1;id.activatedNs=f.s.hand.nowNs;f.Refresh();CHECK(!Fires(f.gate.Update(f.s)));CHECK(!Fires(f.Run(1)));CHECK(!Fires(f.Run(0)));CHECK(Fires(f.Run(1)));}
void RegrabComposed(){Fixture f;f.Armed();CHECK(Fires(f.Run(1)));CHECK(f.arbiter.Release(f.s.hand,f.token).accepted);++f.s.input.generation;f.s.input.predictedNs+=10000000;f.s.hand.sequence=f.s.input.generation;f.s.hand.nowNs=f.s.hand.observedNs=f.s.input.predictedNs;f.s.hand.deadlineNs=f.s.hand.nowNs+100000000;auto claim=f.arbiter.Acquire(f.s.hand,{f.s.hand.owner,right,HandClaimKind::GunHold,{501,9},{{901,1},f.s.hand.sequence,f.s.hand.deadlineNs,true},++f.intent,0});CHECK(claim.claim);f.token=claim.claim->token;f.Refresh();CHECK(!Fires(f.gate.Update(f.s)));CHECK(!Fires(f.Run(1)));CHECK(!Fires(f.Run(0)));CHECK(Fires(f.Run(1)));}
void MissingOrOldAuthority(){Fixture f;f.Armed();CHECK(Fires(f.Run(1)));const auto native=f.s.native;f.s.native.reset();CHECK(!Fires(f.gate.Update(f.s)));f.s.native=native;CHECK(!Fires(f.gate.Update(f.s)));CHECK(!Fires(f.Run(1)));f.Next(0);f.s.native->source.deadlineNs=f.s.hand.nowNs-1;CHECK(!Fires(f.gate.Update(f.s)));CHECK(!Fires(f.Run(1)));CHECK(!Fires(f.Run(0)));CHECK(Fires(f.Run(1)));}
void RawAimExactAndNativeBound(){for(unsigned bad=0;bad<5;++bad){Fixture f;f.Armed();f.Next(1);if(bad==0)f.s.aim->rawAim.position.y+=.1f;if(bad==1)++f.s.aim->inputSource.sequence;if(bad==2)f.s.aim->nativeAimBound=false;if(bad==3)++f.s.aim->target.serverFiring.generation;if(bad==4)f.s.aim->trackedMuzzle.values[0][0]=0;const auto out=f.gate.Update(f.s);CHECK(!out.command&&out.reason==TemporaryFireReason::InvalidAim);}}
void MissingTriggerIsNotNeutral(){Fixture f;CHECK(!Fires(f.gate.Update(f.s)));f.Next(0);f.s.input.hands[1].active&=~Trigger;CHECK(!Fires(f.gate.Update(f.s)));CHECK(!Fires(f.Run(0)));f.s.input.hands[1].active|=Trigger;CHECK(!Fires(f.Run(1)));CHECK(!Fires(f.Run(1)));CHECK(!Fires(f.Run(0)));CHECK(Fires(f.Run(1)));}
void SamePacketMutationRejected(){Fixture f;f.Armed();f.s.input.hands[1].trigger=1;CHECK(f.gate.Update(f.s).reason==TemporaryFireReason::StalePacket);CHECK(!Fires(f.Run(1)));CHECK(!Fires(f.Run(0)));CHECK(Fires(f.Run(1)));}
void NativeAmmoRevisionOnlyObserved(){Fixture f;f.Armed();f.Next(1);f.s.native->ammunitionRevision=10;CHECK(Fires(f.gate.Update(f.s)));f.Next(1);f.s.native->ammunitionRevision=9;CHECK(!Fires(f.gate.Update(f.s)));f.s.native->ammunitionRevision=11;CHECK(!Fires(f.Run(1)));CHECK(!Fires(f.Run(0)));CHECK(Fires(f.Run(1)));}
void BorrowedSlotNeedsIndependentCapability(){Fixture f;auto& p=f.s.native->identity.projection;p.mode=TemporaryNativeEquip::BorrowedNativeSlot;p.displaced=BodyItemKey{700,2};p.borrowedSlot=1;p.displacedStillOwned=true;TemporaryHeldFire externalOnly{{true,false,true,true,true,true}};CHECK(externalOnly.Update(f.s).reason==TemporaryFireReason::Disabled);f.Armed();CHECK(Fires(f.Run(1)));f.Next(1);f.s.native->identity.projection.displacedStillOwned=false;CHECK(!Fires(f.gate.Update(f.s)));}
void OffhandLossDoesNotStealFire(){Fixture f;f.Armed();f.s.input.hands[0].gripTracked=f.s.input.hands[0].aimTracked=false;f.s.hand.tracked[0]=false;CHECK(Fires(f.Run(1)));}
void PresentationAndAmmoProofRequired(){for(unsigned bad=0;bad<5;++bad){Fixture f;f.Armed();f.Next(1);if(bad==0)f.s.native->currentFireTarget=false;if(bad==1)f.s.native->ammunitionBound=false;if(bad==2)f.s.native->heldPresentationPaired=false;if(bad==3)f.s.native->permanentWeaponSuppressed=false;if(bad==4)f.s.native->identity.projection.assignmentsUnchanged=false;CHECK(!Fires(f.gate.Update(f.s)));}}
void ActivationMustClearNativeTriggerBeforeEquip(){for(unsigned bad=0;bad<3;++bad){Fixture f;f.s.input.hands[1].trigger=1;if(bad==0)f.s.native->identity.activationClear={};if(bad==1)f.s.native->activationTriggerCleared=false;if(bad==2)f.s.native->triggerRouteOwned=false;CHECK(!f.gate.Update(f.s).command);}}
void LeaseIdentityCannotMutateInPlace(){Fixture f;f.Armed();f.Next(1);++f.s.native->identity.target.serverFiring.generation;f.Refresh();CHECK(f.gate.Update(f.s).reason==TemporaryFireReason::InvalidNative);CHECK(!Fires(f.Run(0)));CHECK(!Fires(f.Run(1)));++f.s.native->identity.lease.generation;f.Refresh();CHECK(!Fires(f.gate.Update(f.s)));CHECK(!Fires(f.Run(0)));CHECK(Fires(f.Run(1)));}
void ImmutableNativeDeadline(){Fixture f;f.Armed();CHECK(Fires(f.Run(1)));++f.s.native->source.deadlineNs;CHECK(f.gate.Update(f.s).reason==TemporaryFireReason::StalePacket);CHECK(!Fires(f.Run(1)));CHECK(!Fires(f.Run(0)));CHECK(Fires(f.Run(1)));}
void LongGapRequiresReleaseAgain(){Fixture f;f.Armed();CHECK(Fires(f.Run(1)));++f.s.input.generation;f.s.input.predictedNs+=210000000;f.s.hand.sequence=f.s.input.generation;f.s.hand.observedNs=f.s.hand.nowNs=f.s.input.predictedNs;f.s.hand.deadlineNs=f.s.hand.nowNs+100000000;CHECK(f.arbiter.Update(f.s.hand).inputValid);CHECK(!f.arbiter.Current(right));auto claim=f.arbiter.Acquire(f.s.hand,{f.s.hand.owner,right,HandClaimKind::GunHold,{501,9},{{901,1},f.s.hand.sequence,f.s.hand.deadlineNs,true},++f.intent,0});CHECK(claim.claim);f.token=claim.claim->token;f.Refresh();CHECK(!Fires(f.gate.Update(f.s)));CHECK(!Fires(f.Run(1)));CHECK(!Fires(f.Run(0)));CHECK(Fires(f.Run(1)));}
struct SettlementFixture {
    Fixture firing;PickupInventory before;WorldPickupLease held;TemporaryHeldSettlementReceipt settled;
    SettlementFixture(){
        before.actor=firing.s.native->identity.actor;before.inventory=50;before.generation=6;before.revision=7;
        before.source={1,1000000000,1100000000};before.capacity=2;before.count=1;
        before.bundles[0]={{700,2},{701,2},1,1,{{{702,2}}}};before.ammo=Ammo(80);
        held={firing.s.native->identity.originalWorldItem,{501,9},{502,9},before.source,Ammo(40)};
        settled.identity=firing.s.native->identity;settled.permanentAfter=before;
        settled.permanentAfter.source={2,1010000000,1110000000};settled.heldAfter=held;
        settled.heldAfter.source=settled.permanentAfter.source;settled.heldAfter.contents.generation++;
        settled.heldAfter.ammo=Ammo(37);settled.nativeConsumed=Ammo(3);settled.nativeConsumptionReceipt={800,1};
        settled.triggerCleared=settled.nativeCallsDrained=settled.projectionSettled=true;
    }
    bool Valid(std::int64_t now=1010000000)const{return ValidateTemporarySettlement(*firing.s.native,before,held,settled,now);}
};
void SettlementUsesActualCountsNotTriggerCount(){
    SettlementFixture f;CHECK(f.Valid());const auto good=f.settled;
    f.settled.nativeCallsDrained=false;CHECK(!f.Valid());f.settled=good;
    f.settled.heldAfter.ammo=Ammo(40);CHECK(!f.Valid());f.settled=good;
    f.settled.nativeConsumptionReceipt={};CHECK(!f.Valid());f.settled=good;
    f.settled.permanentAfter.bundles[0].item.generation++;CHECK(!f.Valid());
}
void SettlementBindsHeldAndBorrowedInstances(){
    SettlementFixture f;CHECK(f.Valid());++f.held.interaction.generation;
    f.settled.heldAfter.interaction=f.held.interaction;CHECK(!f.Valid());
    SettlementFixture borrowed;auto& projection=borrowed.firing.s.native->identity.projection;
    projection.mode=TemporaryNativeEquip::BorrowedNativeSlot;projection.displaced=BodyItemKey{700,2};
    projection.borrowedSlot=1;projection.displacedStillOwned=true;
    borrowed.settled.identity=borrowed.firing.s.native->identity;CHECK(borrowed.Valid());
    projection.borrowedSlot=0;borrowed.settled.identity=borrowed.firing.s.native->identity;CHECK(!borrowed.Valid());
    projection.borrowedSlot=1;projection.displaced->generation++;
    borrowed.settled.identity=borrowed.firing.s.native->identity;CHECK(!borrowed.Valid());
}
void SettlementCannotReverseSnapshotTime(){
    for(unsigned which=0;which<2;++which){SettlementFixture f;
        auto& source=which?f.held.source:f.before.source;source.observedNs=1015000000;source.deadlineNs=1115000000;
        CHECK(!f.Valid(1020000000)); // Baseline is past, but later sequence has older time.
        CHECK(!f.Valid(1010000000)); // Baseline itself is in the future.
    }
}
}
int main(){using Test=void(*)();const Test tests[]={NoNativeBindingNoFire,HeldTriggerPickupNeverFires,NativeLevelNotShotReplay,
    TargetChangeRequiresRelease,FocusRecoveryNeedsFreshRelease,ReconnectAndNewSpaceSequenceReset,RegrabComposed,MissingOrOldAuthority,
    RawAimExactAndNativeBound,MissingTriggerIsNotNeutral,SamePacketMutationRejected,NativeAmmoRevisionOnlyObserved,
    BorrowedSlotNeedsIndependentCapability,OffhandLossDoesNotStealFire,PresentationAndAmmoProofRequired,SettlementUsesActualCountsNotTriggerCount,
    ActivationMustClearNativeTriggerBeforeEquip,LeaseIdentityCannotMutateInPlace,ImmutableNativeDeadline,LongGapRequiresReleaseAgain,
    SettlementBindsHeldAndBorrowedInstances,SettlementCannotReverseSnapshotTime};
    for(std::size_t i=0;i<std::size(tests);++i)try{tests[i]();}catch(const std::exception& e){std::printf("FAIL group %zu: %s\n",i+1,e.what());return 1;}
    std::printf("PASS %zu TemporaryHeldFire groups\n",std::size(tests));return 0;
}
