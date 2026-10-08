#include "Bc2AmmoSupply.h"
#include "Test.h"
using namespace fvr::bc2;
using namespace fvr::interaction;
namespace {
struct Fixture {
    HandInteraction hands;
    AmmoSupply supply{{InteractionHand::Left,1000,{1001,1},{0,0,0},.15f,200000000}};
    Bc2ReloadRequestBridge bridge{true};
    Bc2ReloadInteractionSample sample{};
    ReloadRoundLease native{};
    AmmoSupplySample s{};
    std::optional<HandClaim> gun;
    std::uint64_t intent=0;
    Fixture(){
        native.identity.owner={0x10000,0x20000,0x30000,0x40000,5,3,7};
        native.identity.firing={0x50000,0x60000,0x70000};native.identity.serverPlayer=0x80000;
        native.identity.serverSoldier=0x90000;native.identity.serverItem=0xa0000;
        native.cycle=12;native.sequence=20;native.observedNs=1000000000;native.deadlineNs=1100000000;
        native.loaded=2;native.reserve=8;native.capacity=8;native.nativeBindingVerified=native.allThreeHeld=true;
        const HandInteractionOwner owner{(std::uint64_t(0x30000)<<32)|0x20000,5,17,7};
        sample.assetName=SpasReloadAsset;sample.meshPath=SpasReloadMesh;sample.rigFingerprint=SpasReloadRig;
        sample.selectedMeshIdentityVerified=true;
        auto& i=sample.insertion;i.identity={owner,{0x40000,17},{},29};i.nowNs=1000000000;
        sample.rawLeftWristWorldMeters=sample.weaponWorldMeters=reload_insertion_detail::Identity();
        s.input={owner,50,1000000000,1100000000,1000000000,true,{true,true},{true,false}};
        s.trackingEpoch=29;s.geometrySequence=50;s.bodyFromHand=reload_insertion_detail::Identity();
    }
    void Sync(){
        auto& i=sample.insertion;i.sequence=i.geometrySequence=s.input.sequence;i.nowNs=s.input.nowNs;
        i.observedNs=s.input.observedNs;i.deadlineNs=s.input.deadlineNs;i.focused=i.weaponTracked=i.itemTracked=i.held=i.eligible=true;
        const auto p=SpasReloadInsertionProfile();i.profile={p.id,p.revision};
        s.source=*SpasAmmoSupplySource(sample,native);
        sample.native=*ToBc2ReloadInteractionLease(*BindBc2ReloadOwners(i.identity.owner,i.identity.weapon,native,i.nowNs),native,i.nowNs);
        hands.Update(s.input);
        if(!gun)gun=hands.Acquire(s.input,{s.input.owner,InteractionHand::Right,HandClaimKind::GunHold,i.identity.weapon,
            {{1002,1},s.input.sequence,s.input.deadlineNs,true},1,0}).claim;
        else gun=hands.Renew(s.input,gun->token,{{1002,1},s.input.sequence,s.input.deadlineNs,true}).claim;
        supply.Update(s,hands);i.weaponClaim=*gun;
        if(supply.Held()){i.identity.item=supply.Held()->item;i.itemClaim=supply.Held()->claim;}
    }
    void Next(bool pressed){
        ++s.input.sequence;s.input.observedNs=s.input.nowNs+=10000000;s.input.deadlineNs=s.input.nowNs+100000000;
        s.input.released[0]=!pressed;s.gripPressed=pressed;s.intent=++intent;s.geometrySequence=s.input.sequence;
        ++native.sequence;native.observedNs=s.input.nowNs;native.deadlineNs=s.input.deadlineNs;
    }
    std::optional<AmmoSupplyReservation> Begin(){
        Sync();Next(true);Sync();const auto& i=sample.insertion;
        const ReloadInsertionSeat seat{31,i.sequence,i.identity,i.profile,i.itemClaim.token,i.weaponClaim.token,ReloadOperation::InsertRound};
        const ManualReloadRequest request{41,{i.identity.owner.actor,i.identity.owner.actorGeneration,i.identity.weapon.id,
            i.identity.owner.equipGeneration,i.identity.owner.space},ReloadOperation::InsertRound,0,0};
        const Bc2ReloadTargets targets{i.identity,i.itemClaim.token,i.weaponClaim.token,i.sequence,native.cycle,i.observedNs,i.deadlineNs,
            reload_insertion_detail::Identity(),reload_insertion_detail::Identity()};
        auto result=bridge.Begin(sample,native,seat,request,targets);
        if(!result.submit)return {};
        auto reservation=supply.Reserve(s,hands,seat,request,native.cycle);
        if(!reservation||!SameAmmoReservation(*reservation,result.submit->reservation))return {};
        return reservation; // dispatcher can now submit ONCE; this test never calls native code
    }
    Bc2ReloadAckEvidence Ack(){
        const auto& o=native.identity.owner;
        return {{{41,{o.soldier,o.actorGeneration,o.weapon,o.equipGeneration,o.space},ReloadOperation::InsertRound,
            ReloadAcknowledgement::Applied},native.identity,native.cycle,native.sequence,200},s.input.nowNs,s.input.nowNs+50000000,true};
    }
};
int RealBridgePipeline(){
    Fixture f;const auto reserve=f.Begin();CHECK(reserve&&f.supply.Held());
    const auto original=f.bridge.Owners();
    f.Next(false);f.Sync();f.Next(true);f.Sync();const auto replacement=*f.supply.Held();
    CHECK(replacement.item!=reserve->item);
    f.Next(true);++f.native.loaded;--f.native.reserve;f.native.allThreeHeld=false;f.Sync();
    const auto evidence=f.Ack();const auto result=f.bridge.Update(f.sample,f.native,evidence);
    CHECK(result.consumed&&SameAmmoReservation(*reserve,*result.consumed));
    const auto receipt=SpasAmmoSupplyReceipt(*reserve,original,result,evidence,f.native,f.s.input.nowNs);
    CHECK(receipt&&receipt->currentReserve.reserveUnits==7);
    const auto resolved=f.supply.Resolve(f.s.input,f.hands,*receipt);
    CHECK(resolved.consumed==reserve&&!f.supply.Pending());
    CHECK(f.supply.Held()->item==replacement.item&&f.hands.Current(InteractionHand::Left)->token==replacement.claim.token);
    CHECK(!f.supply.Resolve(f.s.input,f.hands,*receipt).accepted);return 0;
}
int CancelAndUnverifiedCannotConsume(){
    Fixture f;const auto reserve=f.Begin();CHECK(reserve);
    const auto original=f.bridge.Owners();const auto cancelled=f.bridge.Cancel();
    CHECK(cancelled.unresolvedNative&&!SpasAmmoSupplyReceipt(*reserve,original,cancelled,f.Ack(),f.native,f.s.input.nowNs));
    CHECK(f.supply.Pending()==reserve&&f.native.reserve==8);
    for(unsigned mode=0;mode<6;++mode){
        Fixture p;p.Sync();
        if(mode==0)p.sample.selectedMeshIdentityVerified=false;
        if(mode==1)p.sample.assetName="XM8_sp";
        if(mode==2)p.native.nativeBindingVerified=false;
        if(mode==3)++p.native.identity.owner.equipGeneration;
        if(mode==4)p.native.reserve=-1;
        if(mode==5)p.native.deadlineNs=p.s.input.nowNs;
        const auto source=SpasAmmoSupplySource(p.sample,p.native);
        if(mode!=3)CHECK(!source);
        else CHECK(source&&source->identity.pool.generation==4&&source->identity.owner.equipGeneration==17);
    }return 0;
}
int NativeIdentityAndAckRejection(){
    Fixture f;const auto reserve=f.Begin();CHECK(reserve);const auto original=f.bridge.Owners();
    f.Next(true);++f.native.loaded;--f.native.reserve;f.native.allThreeHeld=false;f.Sync();
    const auto evidence=f.Ack();const auto result=f.bridge.Update(f.sample,f.native,evidence);CHECK(result.consumed);
    auto reserveDomain=*reserve;reserveDomain.sourceSequence=1000;
    Bc2AmmoReserveLease postReserve{f.native.identity,1001,f.native.observedNs,f.native.deadlineNs,
        f.native.loaded,f.native.reserve,f.native.capacity,true};
    const auto independent=SpasAmmoSupplyReceipt(reserveDomain,original,result,evidence,f.native,postReserve,f.s.input.nowNs);
    CHECK(independent&&independent->currentReserve.sequence==1001&&evidence.acknowledgement.sampleSequence<1000);
    postReserve.sequence=1000;CHECK(!SpasAmmoSupplyReceipt(reserveDomain,original,result,evidence,f.native,postReserve,f.s.input.nowNs));
    for(unsigned mode=0;mode<10;++mode){
        auto n=f.native;auto e=evidence;auto b=result;auto o=original;
        switch(mode){
        case 0:e.verified=false;break;case 1:++e.acknowledgement.semantic.owner.equipGeneration;break;
        case 2:++n.identity.owner.equipGeneration;break;case 3:++e.acknowledgement.serverInvocation;e.acknowledgement.serverInvocation=0;break;
        case 4:--n.sequence;break;case 5:b.consumed->item.generation++;break;
        case 6:++b.acknowledged->owner.equipGeneration;break;case 7:++o.native.serverItem;break;
        case 8:b.unresolvedNative=true;break;case 9:b.phase=Bc2ReloadBridgePhase::Pending;break;
        }
        CHECK(!SpasAmmoSupplyReceipt(*reserve,o,b,e,n,f.s.input.nowNs));
    }return 0;
}
int IdleReserveBeforeAnyNativeCycle(){
    Fixture f;f.native.cycle=0;f.native.allThreeHeld=false;
    f.sample.insertion.nowNs=f.s.input.nowNs;
    const Bc2AmmoReserveLease idle{f.native.identity,f.native.sequence,f.native.observedNs,f.native.deadlineNs,
        f.native.loaded,f.native.reserve,f.native.capacity,true};
    auto source=SpasAmmoSupplySource(f.sample,idle);CHECK(source&&source->reserveUnits==8);
    CHECK(SpasAmmoSupplySource(f.sample,f.native)); // legacy overload no longer creates the cycle dependency
    f.s.source=*source;f.hands.Update(f.s.input);CHECK(!f.supply.Update(f.s,f.hands).held);
    f.Next(true);f.sample.insertion.nowNs=f.s.input.nowNs;
    auto fresh=idle;fresh.sequence++;fresh.observedNs=f.s.input.nowNs;fresh.deadlineNs=f.s.input.deadlineNs;
    f.s.source=*SpasAmmoSupplySource(f.sample,fresh);f.hands.Update(f.s.input);
    CHECK(f.supply.Update(f.s,f.hands).held&&f.native.cycle==0&&f.native.reserve==8);
    for(unsigned mode=0;mode<7;++mode){
        auto bad=fresh;
        switch(mode){case 0:bad.verified=false;break;case 1:bad.sequence=0;break;
        case 2:bad.identity.owner.weak++;break;case 3:bad.identity.firing[2]=bad.identity.firing[1];break;
        case 4:bad.deadlineNs=f.s.input.nowNs;break;case 5:bad.identity.serverItem=0;break;case 6:bad.reserve=-1;break;}
        CHECK(!SpasAmmoSupplySource(f.sample,bad));
    }return 0;
}
}
int main(){if(RealBridgePipeline()||CancelAndUnverifiedCannotConsume()||NativeIdentityAndAckRejection()||IdleReserveBeforeAnyNativeCycle())return 1;
    std::puts("Four BC2 ammo supply groups passed: genuine idle reserve before any cycle, actual bridge, replacement and rejected evidence.");return 0;}
