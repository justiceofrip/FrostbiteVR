#include "Test.h"
#include "Bc2KnifeDrawReturnGate.h"
#include "Bc2KnifeAttackGate.h"
#include "Bc2ChestKnifeRendering.h"
#include "Bc2ChestKnifeLiveObservation.h"
#include "Bc2ChestKnifeCurrentHoldObservation.h"
#include "Bc2ChestKnifeCoordinator.h"
#include "Bc2ChestKnifeStockHold.h"
#include "Bc2KnifeStockMotionSource.h"
#include <cstdio>
#include <sstream>
#include <limits>
using namespace fvr;using namespace fvr::bc2;using namespace fvr::interaction;
#include "KnifeCustodyFixture.inc"
namespace {
std::optional<KnifeCommand> MotionGapFrame(Fixture& f,std::int64_t elapsed=10*ms){
    ++f.s.source.sequence;f.s.source.observedNs+=elapsed;f.s.source.nowNs=f.s.source.observedNs;
    f.s.source.deadlineNs=f.s.source.nowNs+100*ms;f.s.claimIntent=f.s.source.sequence;
    f.s.menu.deadlineNs=f.s.source.deadlineNs;++f.s.menu.sequence;f.hands.Update(f.s.source);
    f.s.nativeAttackReady=false;f.s.motionTrackingReady=true;f.s.trackedKnifeOwned=false;f.s.heldContact={};
    if(const auto retained=ChestKnifeRetainedStockContact(f.knife,f.hands,f.s,true,true)){
        f.s.trackedKnifeOwned=true;f.s.heldContact=*retained;
    }
    return f.knife.Prepare(f.s,f.hands);
}
int MotionRenderSeparationCases(){
    // Test-owned physical geometry stays current while the actual stock hold
    // fallback carries only its pre-existing lease through missing rendering.
    Fixture settled;CHECK(settled.Held());CHECK(settled.Rest(11));
    const auto held=*settled.hands.Current(InteractionHand::Right);
    CHECK(!MotionGapFrame(settled));CHECK(settled.knife.RecognitionReady(settled.s.source));
    CHECK(settled.hands.Current(InteractionHand::Right)->token==held.token);
    CHECK(settled.hands.Current(InteractionHand::Right)->deadlineNs==held.deadlineNs);
    settled.s.nativeAttackReady=true;const auto jab=settled.Stab();
    CHECK(jab&&jab->stroke&&!jab->stockTriggerHold&&settled.s.stockTrigger==0);
    CHECK(settled.knife.CanCommit(*jab,settled.s,settled.hands));
    auto unavailable=settled.s;unavailable.nativeAttackReady=false;
    CHECK(!settled.knife.CanCommit(*jab,unavailable,settled.hands));
    unavailable=settled.s;unavailable.motionTrackingReady=false;unavailable.trackedKnifeOwned=false;
    CHECK(!settled.knife.CanCommit(*jab,unavailable,settled.hands));

    Fixture travelling;CHECK(travelling.Held()&&travelling.Rest());
    travelling.s.tip.z=.024f;CHECK(!travelling.Step());
    travelling.s.tip.z=.048f;CHECK(!MotionGapFrame(travelling));
    travelling.s.nativeAttackReady=true;travelling.s.tip.z=.072f;
    const auto completed=travelling.Step();CHECK(completed&&completed->stroke);
    CHECK(completed->source.sequence==travelling.s.source.sequence&&completed->source.deadlineNs==travelling.s.source.deadlineNs);

    Fixture spent;CHECK(spent.Held()&&spent.Rest());
    const auto deadline=spent.hands.Current(InteractionHand::Right)->deadlineNs;
    for(unsigned n=0;n<7;++n){spent.s.tip.z+=.012f;CHECK(!MotionGapFrame(spent));
        CHECK(spent.hands.Current(InteractionHand::Right)->deadlineNs==deadline);}
    CHECK(spent.knife.Phase()==KnifePhase::Held&&!spent.knife.RecognitionReady(spent.s.source));
    spent.s.nativeAttackReady=true;CHECK(!spent.knife.Prepare(spent.s,spent.hands));
    CHECK(!spent.Step()&&!spent.Stab()); // Neither recovery nor more forward motion replays the missed attack.
    CHECK(spent.Rest());CHECK(spent.Stab()); // A new settled stroke remains available.

    for(unsigned fault=0;fault<10;++fault){Fixture f;CHECK(f.Held()&&f.Rest());f.s.tip.z=.024f;CHECK(!f.Step());
        if(fault==0)f.s.source.focused=false;
        if(fault==1)f.s.source.tracked[1]=false;
        if(fault==2)++f.s.source.owner.actor;
        if(fault==3)++f.s.source.owner.actorGeneration;
        if(fault==4)++f.s.source.owner.equipGeneration;
        if(fault==5)++f.s.source.owner.space;
        if(fault==6)f.s.knife.generation++;
        if(fault==7)f.s.onFoot=false;
        if(fault==8)f.s.mechanicsSettled=false;
        f.s.tip.z=.048f;CHECK(!MotionGapFrame(f,fault==9?80*ms:10*ms));
        f.s.nativeAttackReady=true;f.s.source.focused=true;f.s.source.tracked[1]=true;
        f.s.onFoot=f.s.mechanicsSettled=true;f.s.tip.z=.072f;CHECK(!f.Step());
    }
    Fixture expired;CHECK(expired.Held()&&expired.Rest());
    CHECK(!MotionGapFrame(expired,100*ms));CHECK(!expired.hands.Current(InteractionHand::Right));
    expired.s.nativeAttackReady=true;CHECK(!expired.Stab());CHECK(!expired.hands.Current(InteractionHand::Right));
    return 0;
}
int MotionCalibrationCases(){
    Fixture f;CHECK(f.Held());
    ChestKnifeMotionIdentity identity{{0x10000,0x20000,0x30000,42,2,3,4},*f.knife.Hold(),{42,1},7,8,1,9,1.f};
    math::Matrix4 measured;measured.values={{{1,0,0,0},{0,1,0,0},{0,0,1,0},{0,0,0,1}}};
    bool continuous=false;ChestKnifeMotionCalibration calibration;
    CHECK(!calibration.Update(identity,std::nullopt,continuous)&&!continuous);
    const auto first=calibration.Update(identity,measured,continuous);CHECK(first&&!continuous&&first->values==measured.values);
    const auto missing=calibration.Update(identity,std::nullopt,continuous);CHECK(missing&&continuous&&missing->values==measured.values);
    auto noise=measured;noise.values[0][1]=.00001f;noise.values[1][0]=-.00001f;
    const auto unchanged=calibration.Update(identity,noise,continuous);CHECK(unchanged&&continuous&&unchanged->values==measured.values);
    auto changed=measured;const float angle=.01f;
    changed.values[0][0]=changed.values[1][1]=std::cos(angle);
    changed.values[0][1]=std::sin(angle);changed.values[1][0]=-std::sin(angle);
    const auto reset=calibration.Update(identity,changed,continuous);CHECK(reset&&!continuous&&reset->values==changed.values);
    auto invalid=measured;invalid.values[3][0]=.01f;
    CHECK(!calibration.Update(identity,invalid,continuous)&&!continuous);
    CHECK(!calibration.Update(identity,std::nullopt,continuous)); // An invalid measurement cannot fall back to older geometry.
    for(unsigned fault=0;fault<12;++fault){
        CHECK(calibration.Update(identity,measured,continuous));auto next=identity;
        if(fault==0)++next.owner.actorGeneration;if(fault==1)++next.owner.equipGeneration;
        if(fault==2)++next.owner.space;if(fault==3)++next.knife.generation;if(fault==4)++next.hold.id;
        if(fault==5)++next.bodyOperation;if(fault==6)++next.nativeLifetime;if(fault==7)++next.trackingBasis;
        if(fault==8)next.worldUnitsPerMeter=2;if(fault==9)++next.menuEpoch;
        if(fault==10)++next.owner.weapon;if(fault==11)++next.owner.soldier;
        CHECK(!calibration.Update(next,std::nullopt,continuous)&&!continuous);
        CHECK(calibration.Update(next,measured,continuous)&&!continuous);
    }
    CHECK(!calibration.Update(std::nullopt,std::nullopt,continuous)&&!continuous);
    CHECK(!calibration.Update(identity,std::nullopt,continuous));
    std::puts("Physical knife sampling: fixed measured calibration, exact identity resets, bounded missing render contact, no deferred/replayed attack.");
    return 0;
}
struct StagedMotionFixture {
    Fixture f;ChestKnifeMotionCalibration calibration;InputFrame input;
    KnifeRecognitionCoordinator projection;
    math::Matrix4 measured=private_knife::RightHandInKnife;
    private_knife::StockKnifeMotionSourceRevocation source;
    private_knife::KnifeAttackGate attacks;
    std::optional<KnifeCommand> command;std::uint64_t stagedStamp=0;
    StagedMotionFixture(){
        measured.values[3]={0,0,0,1};input.focused=input.headValid=true;
        input.trackingBasisGeneration=9;input.hands[1].gripTracked=true;
        f.s.holdInputCurrent=f.s.actionInputCurrent=true;
    }
    std::optional<KnifeCommand> Gather(bool advance,bool render=true,bool nativeReady=true){
        auto& s=f.s;
        if(advance){++s.source.sequence;s.source.observedNs+=10*ms;s.source.nowNs=s.source.observedNs;
            s.source.deadlineNs=s.source.nowNs+100*ms;s.claimIntent=s.source.sequence;
            s.menu.deadlineNs=s.source.deadlineNs;++s.menu.sequence;}
        f.hands.Update(s.source);
        s.trackedKnifeOwned=render;s.heldContact={};
        if(render)s.heldContact={{103,1},s.source.sequence,s.source.deadlineNs,true};
        else if(const auto contact=ChestKnifeRetainedStockContact(f.knife,f.hands,s,true,true)){
            s.trackedKnifeOwned=true;s.heldContact=*contact;
        }
        input.generation=s.source.sequence;input.spaceGeneration=s.source.owner.space;
        input.predictedNs=s.source.observedNs;input.menu=s.menu;
        std::optional<ChestKnifeMotionIdentity> identity;
        if(ChestKnifeRenderPoseCustody(f.knife,f.hands,s.source)&&
           f.hands.HasRecordedEvidence(s.source,InteractionHand::Right)&&!f.knife.NeedsNativeCleanup())
            identity=ChestKnifeMotionIdentity{{0x10000,0x20000,0x30000,42,2,3,4},*f.knife.Hold(),{42,1},7,8,1,9,1.f};
        // The production gather projection, then the same BeforePrepare order:
        // revoke first, resolve native consumption, and only then run policy.
        PrepareChestKnifeMotionSource(calibration,f.knife,identity,render?std::optional(measured):std::nullopt,
            input,nativeReady&&s.mechanicsSettled,s,&projection);
        source.Advance(s,f.knife.NeedsNativeCleanup());
        if(attacks.Pending()){
            const auto pending=*attacks.Pending();
            if(const auto result=attacks.ResolveBeforeSource(s.source.sequence,s.source.observedNs,s.source.nowNs))
                f.knife.Committed(pending,*result);
        }
        return f.knife.Prepare(s,f.hands);
    }
    bool StageJab(){
        if(!f.Held())return false;
        for(unsigned n=0;n<20;++n)if(Gather(true))return false;
        if(!f.knife.RecognitionReady(f.s.source))return false;
        const auto axis=f.s.direction;
        for(unsigned n=0;n<8&&!command;++n){
            auto& p=input.hands[1].grip.position;p.x+=axis.x*.012f;p.y+=axis.y*.012f;p.z-=axis.z*.012f;
            command=Gather(true);
        }
        if(!command||!command->stroke||command->stockTriggerHold||f.s.stockTrigger!=0||
           !f.knife.CanCommit(*command,f.s,f.hands)||!attacks.Stage(*command,f.s.source.nowNs))return false;
        stagedStamp=source.Stamp();return true;
    }
};
int StagedMotionSourceCases(){
    StagedMotionFixture same;CHECK(same.StageJab());
    const auto original=*same.command;const auto originalHold=*same.f.hands.Current(InteractionHand::Right);
    const auto tip=same.f.s.tip,direction=same.f.s.direction;
    for(unsigned n=0;n<3;++n){
        same.f.s.source.nowNs+=ms;CHECK(!same.Gather(false));
        CHECK(same.f.knife.Phase()==KnifePhase::AttackStaged&&!same.f.knife.RecognitionReady(same.f.s.source));
        CHECK(same.f.s.nativeAttackReady&&!same.f.s.motionTrackingReady);
        CHECK(same.projection.projection==KnifeMotionProjectionReason::Ready&&same.projection.calibrationContinuous);
        CHECK(same.projection.attackReady&&!same.projection.trackingReady);
        CHECK(same.f.s.tip==tip&&same.f.s.direction==direction&&same.source.Current(same.stagedStamp));
        CHECK(same.f.knife.CanCommit(original,same.f.s,same.f.hands));
        CHECK(same.f.hands.Current(InteractionHand::Right)->token==originalHold.token);
        CHECK(same.f.hands.Current(InteractionHand::Right)->deadlineNs==originalHold.deadlineNs);
    }
    CHECK(same.attacks.Pending()->source.sequence==original.source.sequence);
    CHECK(same.attacks.Pending()->source.observedNs==original.source.observedNs);
    CHECK(same.attacks.Pending()->source.deadlineNs==original.source.deadlineNs);
    CHECK(same.attacks.Consume(1,same.f.s.source.nowNs,same.source.Current(same.stagedStamp)));
    CHECK(!same.attacks.Consume(1,same.f.s.source.nowNs,true));
    CHECK(!same.Gather(false)&&same.f.knife.Phase()==KnifePhase::Attacking);
    // A later source loss cannot cancel or replay a consumed branch. Its
    // original sibling still drains once, without renewing the command lease.
    same.f.s.source.nowNs=original.source.deadlineNs;CHECK(!same.Gather(false,false));
    CHECK(!same.source.Current(same.stagedStamp));
    CHECK(!same.attacks.Consume(1,same.f.s.source.nowNs,false));
    CHECK(same.attacks.Consume(2,same.f.s.source.nowNs,false));
    CHECK(!same.attacks.Consume(2,same.f.s.source.nowNs,false));
    CHECK(same.attacks.Pending()->source.deadlineNs==original.source.deadlineNs);

    for(unsigned fault=0;fault<6;++fault){
        StagedMotionFixture lost;CHECK(lost.StageJab());
        const auto pending=*lost.command;
        if(fault==1)lost.input.hands[1].grip.position.x+=.001f; // Same packet, changed physical geometry.
        if(fault==3)lost.f.s.source.nowNs=pending.source.deadlineNs;
        if(fault==4)++lost.f.s.source.deadlineNs; // No deadline renewal on an immutable source.
        CHECK(!lost.Gather(fault==2,fault!=0,fault!=5));
        CHECK(!lost.source.Current(lost.stagedStamp));
        CHECK(!lost.attacks.Consume(1,lost.f.s.source.nowNs,lost.source.Current(lost.stagedStamp)));
        if(fault==2||fault==3)CHECK(!lost.attacks.Pending());
        else {
            CHECK(lost.attacks.Pending()->source.sequence==pending.source.sequence);
            CHECK(lost.attacks.Pending()->source.deadlineNs==pending.source.deadlineNs);
            lost.input.hands[1].grip.position.x-=fault==1?.001f:0;
            lost.f.s.source.deadlineNs=pending.source.deadlineNs;
            CHECK(!lost.Gather(false)); // Recovery cannot revive this old stage.
            CHECK(!lost.source.Current(lost.stagedStamp));
            CHECK(!lost.attacks.Consume(1,lost.f.s.source.nowNs,lost.source.Current(lost.stagedStamp)));
        }
    }
    std::puts("Staged knife source: unchanged zero-trigger jab consumes once; render/geometry/source/readiness loss and expiry revoke; accepted siblings drain.");
    return 0;
}
int MotionProjectionObservationCases(){
    StagedMotionFixture f;CHECK(f.f.Held());CHECK(!f.Gather(true));
    CHECK(f.projection.calibration==KnifeMotionCalibrationReason::Initial&&!f.projection.calibrationContinuous);
    CHECK(f.projection.projection==KnifeMotionProjectionReason::Ready&&f.projection.trackingReady&&f.projection.attackReady);
    for(unsigned n=0;n<20;++n)CHECK(!f.Gather(true));
    CHECK(f.f.knife.RecognitionReady(f.f.s.source));
    const auto held=*f.f.hands.Current(InteractionHand::Right);
    CHECK(!f.Gather(true,false)); // Missing measurement carries only the original live hand lease.
    CHECK(f.projection.calibration==KnifeMotionCalibrationReason::Continuous&&f.projection.calibrationContinuous);
    CHECK(f.projection.trackingReady&&!f.projection.attackReady&&f.f.knife.RecognitionReady(f.f.s.source));
    CHECK(f.f.hands.Current(InteractionHand::Right)->deadlineNs==held.deadlineNs);
    CHECK(!f.Gather(true,true,false));
    CHECK(f.projection.projection==KnifeMotionProjectionReason::Ready&&f.projection.trackingReady&&!f.projection.attackReady);
    CHECK(f.f.knife.RecognitionReady(f.f.s.source));
    f.input.hands[1].gripTracked=false;CHECK(!f.Gather(true));
    CHECK(f.projection.projection==KnifeMotionProjectionReason::Geometry&&!f.projection.trackingReady&&!f.projection.attackReady);
    CHECK(!f.f.knife.RecognitionReady(f.f.s.source));
    f.input.hands[1].gripTracked=true;CHECK(!f.Gather(true));
    CHECK(f.projection.calibration==KnifeMotionCalibrationReason::Initial&&!f.projection.calibrationContinuous);
    auto invalid=f.measured;invalid.values[3][0]=.01f;
    PrepareChestKnifeMotionSource(f.calibration,f.f.knife,
        ChestKnifeMotionIdentity{{0x10000,0x20000,0x30000,42,2,3,4},*f.f.knife.Hold(),{42,1},7,8,1,9,1.f},
        invalid,f.input,true,f.f.s,&f.projection);
    CHECK(f.projection.calibration==KnifeMotionCalibrationReason::MeasurementInvalid&&f.projection.projection==KnifeMotionProjectionReason::Calibration);
    CHECK(!f.projection.trackingReady&&!f.projection.attackReady);
    PrepareChestKnifeMotionSource(f.calibration,f.f.knife,std::nullopt,std::nullopt,f.input,true,f.f.s,&f.projection);
    CHECK(f.projection.calibration==KnifeMotionCalibrationReason::IdentityMissing&&f.projection.projection==KnifeMotionProjectionReason::Calibration);
    CHECK(!f.Gather(true));
    // Valid orientation change starts settling again; the observation does not
    // reuse the old calibration or convert geometry into attack authority.
    f.measured.values={{{1,0,0,0},{0,1,0,0},{0,0,1,0},{0,0,0,1}}};CHECK(!f.Gather(true));
    CHECK(f.projection.calibration==KnifeMotionCalibrationReason::MeasurementChanged&&!f.projection.calibrationContinuous);
    CHECK(!f.f.knife.RecognitionReady(f.f.s.source));
    Fixture stowed;ChestKnifeMotionCalibration calibration;KnifeRecognitionCoordinator observation;
    PrepareChestKnifeMotionSource(calibration,stowed.knife,std::nullopt,std::nullopt,f.input,true,stowed.s,&observation);
    CHECK(observation.projection==KnifeMotionProjectionReason::Phase&&!observation.trackingReady&&!observation.attackReady);
    std::puts("Normal knife projection observation: readiness versus tracking, geometry/calibration/phase resets, original hand deadline preserved.");
    return 0;
}
int RepeatedMotionContentionCases(){
    using namespace chestKnifeNative;
    StagedMotionFixture f;CHECK(f.f.Held());for(unsigned n=0;n<20;++n)CHECK(!f.Gather(true));
    CHECK(f.f.knife.RecognitionReady(f.f.s.source));
    const ChestKnifeMotionIdentity identity{{0x10000,0x20000,0x30000,42,2,3,4},*f.f.knife.Hold(),{42,1},7,8,1,9,1.f};
    ChestKnifeRepeatedMotion repeated;const auto held=*f.f.hands.Current(InteractionHand::Right);
    CurrentHoldWorkObservation work;work.known=work.drawOnly=true;
    work.lifetime=identity.nativeLifetime;work.bodyOperation=identity.bodyOperation;work.drawSerial=1;
    CHECK(repeated.Remember(identity,f.input,f.f.s,held,work));
    const auto same=[&](const KnifeSample& sample,const InputFrame& input,const ChestKnifeMotionIdentity& local){
        return repeated.Same(local.owner,local.knife,local.bodyOperation,input,sample,f.f.knife,f.f.hands);};
    auto source=f.f.s;source.source.nowNs+=ms;
    for(auto reason:{InspectReason::CaptureBusy,InspectReason::RejoinBusy}){
        CHECK(same(source,f.input,identity));
        CHECK(repeated.TemporaryMiss(reason,BeforePrepareReason::Available,std::nullopt,false,false));
        CHECK(repeated.TemporaryMiss(reason,BeforePrepareReason::Busy,std::nullopt,false,false));
        repeated.Suspended(source.source.nowNs);
        // Production suspends before portable Prepare: no sample progress,
        // attack or renewed contact. Revocation still observes ineligibility.
        auto unavailable=source;unavailable.nativeAttackReady=unavailable.motionTrackingReady=unavailable.onFoot=false;
        const auto stamp=f.source.Stamp();CHECK(f.source.Advance(unavailable));CHECK(!f.source.Current(stamp));
        CHECK(f.f.knife.RecognitionReady(source.source));
        CHECK(f.f.hands.Current(InteractionHand::Right)->deadlineNs==held.deadlineNs);
    }
    CHECK(repeated.TemporaryMiss(InspectReason::Available,BeforePrepareReason::Busy,identity,true,true));
    // After an attack completes, passive draw-only provenance stays false.
    // The production Remember admission must still accept settled native work.
    work.drawOnly=false;CHECK(!CurrentHoldWorkIdle(work)&&CurrentHoldWorkSettled(work));
    CHECK(repeated.Remember(identity,f.input,source,held,work));
    CHECK(same(source,f.input,identity));
    CHECK(repeated.TemporaryMiss(InspectReason::Available,BeforePrepareReason::Busy,identity,true,CurrentHoldWorkSettled(work)));
    for(unsigned fault=0;fault<11;++fault){auto busy=work;
        if(fault==0)busy.known=false;if(fault==1)busy.lifetime=0;if(fault==2)busy.bodyOperation=0;
        if(fault==3)busy.drawSerial=0;if(fault==4)busy.outstanding=true;if(fault==5)busy.commandPending=true;
        if(fault==6)busy.attackPending=true;if(fault==7)busy.returnReady=true;if(fault==8)busy.closing=true;
        if(fault==9)busy.attackNative=true;if(fault==10)busy.bridgePending=true;
        CHECK(!repeated.Remember(identity,f.input,source,held,busy));CHECK(!same(source,f.input,identity));
    }
    CHECK(repeated.Remember(identity,f.input,source,held,work));
    CHECK(!repeated.TemporaryMiss(InspectReason::Available,BeforePrepareReason::Available,identity,true,true));
    CHECK(!repeated.TemporaryMiss(InspectReason::Available,BeforePrepareReason::Busy,identity,false,true));
    CHECK(!repeated.TemporaryMiss(InspectReason::Available,BeforePrepareReason::Busy,identity,true,false));
    for(auto reason:{InspectReason::Busy,InspectReason::RejoinChanged,InspectReason::Closing,InspectReason::Disabled,
                    InspectReason::NativeRead,InspectReason::Flags,InspectReason::KnifeIdentity})
        CHECK(!repeated.TemporaryMiss(reason,BeforePrepareReason::Busy,std::nullopt,false,false));
    CHECK(!repeated.TemporaryMiss(InspectReason::CaptureBusy,BeforePrepareReason::Closing,std::nullopt,false,false));
    auto lifetime=identity;++lifetime.nativeLifetime;
    CHECK(!repeated.TemporaryMiss(InspectReason::Available,BeforePrepareReason::Busy,lifetime,true,true));
    for(unsigned fault=0;fault<26;++fault){auto changed=source;auto input=f.input;auto local=identity;
        if(fault==0)++changed.source.sequence;if(fault==1)++changed.source.observedNs;
        if(fault==2)++changed.source.deadlineNs;if(fault==3)changed.source.nowNs=changed.source.deadlineNs;
        if(fault==4)--changed.source.nowNs;if(fault==5)++changed.source.owner.actorGeneration;
        if(fault==6)++changed.source.owner.equipGeneration;if(fault==7)++changed.source.owner.space;
        if(fault==8)changed.source.focused=false;if(fault==9)changed.source.tracked[1]=false;
        if(fault==10)changed.source.released[1]=true;if(fault==11)changed.gripHeld[1]=false;
        if(fault==12)changed.fireSuppressed=false;if(fault==13)changed.holdInputCurrent=false;
        if(fault==14)++local.owner.weapon;if(fault==15)++local.knife.generation;if(fault==16)++local.bodyOperation;
        if(fault==17)++input.trackingBasisGeneration;if(fault==18)input.worldUnitsPerMeter=2;
        if(fault==19)input.hands[1].grip.position.x+=.001f;if(fault==20)input.hands[1].aim.orientation.x+=.001f;
        if(fault==21)input.head.position.x+=.001f;if(fault==22)input.hands[0].held^=Primary;
        if(fault==23)++input.menu.epoch;if(fault==24)++changed.menu.deadlineNs;
        if(fault==25)changed.source.tracked[0]=false;
        CHECK(!same(changed,input,local));
    }
    // A new unavailable packet takes the unchanged reset path. A completed
    // jab on that packet cannot be queued across recovery into a fresh source.
    ++source.source.sequence;source.source.observedNs+=10*ms;source.source.nowNs=source.source.observedNs;
    CHECK(!same(source,f.input,identity));
    PrepareChestKnifeMotionSource(f.calibration,f.f.knife,std::nullopt,std::nullopt,f.input,false,source);
    repeated.Reset();CHECK(!f.f.knife.RecognitionReady(source.source));
    CHECK(!same(f.f.s,f.input,identity));
    CHECK(!f.Gather(true));CHECK(!f.f.knife.RecognitionReady(f.f.s.source));
    f.input.hands[1].grip.position.z-=.1f;CHECK(!f.Gather(true));
    CHECK(!f.f.knife.NativeAttackPending());
    // Full identity validation after suspension also rejects a replaced native lifetime.
    bool continuous=true;CHECK(!f.calibration.Update(lifetime,std::nullopt,continuous)&&!continuous);
    CHECK(repeated.Remember(identity,f.input,f.f.s,*f.f.hands.Current(InteractionHand::Right),work));
    CHECK(f.f.hands.Release(f.f.s.source,*f.f.knife.Hold()).accepted);
    CHECK(!same(f.f.s,f.input,identity));
    std::puts("Repeated knife contention: exact packet retains stationary recognition only; new/mutated packet, rejoin, lifetime, closing, claim and deadline changes refuse.");
    return 0;
}
int ReturnObservationCases(){
    Fixture f;f.s.source.owner.actor=(std::uint64_t(0x30000)<<32)|0x20000;
    const auto draw=f.Draw();CHECK(draw);f.knife.Committed(*draw,KnifeCommit::Accepted);
    const auto drawn=f.Receipt(*draw);CHECK(f.knife.Observe(drawn));
    f.s.knifeSelected=f.s.trackedKnifeOwned=true;f.s.emptyHands=false;CHECK(!f.Step());
    f.Grip(false);const auto returned=f.Step();CHECK(returned&&returned->kind==KnifeCommandKind::Return);
    f.knife.Committed(*returned,KnifeCommit::Accepted);CHECK(f.knife.Phase()==KnifePhase::Returning);
    ChestKnifeReturnPublication history;
    // Actual544's boundary: draw1/return0 and an outstanding Return. Missing
    // Held publication, expiry or independent Empty cannot create completion.
    chestKnifeNative::ReturnPendingObservation pending;pending.command=*returned;pending.bodyOperation=17;pending.lifetime=4;
    // Synthetic rejection detail: actual544 did not expose its first blocker.
    pending.chooserMask=3;pending.returnReady=true;pending.lastObserveReason=chestKnifeNative::ReturnPendingReason::BodyRehide;
    pending.lastObservedNs=f.s.source.nowNs;
    ChestKnifeLivePublication held;held.Begin();held.Invalidate();CHECK(!held.Read(f.s.source.deadlineNs));
    std::ostringstream waiting;WriteChestKnifeReturnObservation(waiting,{history.Read(),pending});
    CHECK(waiting.str().find("\"completed\":null")!=std::string::npos);
    CHECK(waiting.str().find("\"lastObserveReason\":6")!=std::string::npos);
    CHECK(!history.Read());

    auto receipt=f.Receipt(*returned);receipt.selectedKnife=receipt.drawDidNotAttack=receipt.holdGatesOwned=false;
    private_knife::Anchor anchor;anchor.original.client.owner={0x10000,0x20000,0x30000,0x51000,2,5,4};
    anchor.original.client.sequence=3;anchor.original.client.observedNs=1000000000;anchor.original.client.selectedSlot=2;
    anchor.knifeWeapon=unsigned(returned->knife.id);anchor.knifeSlot=7;
    private_knife::Snapshot snapshot;snapshot.selectedOwner=anchor.original.client.owner;
    snapshot.selectedOwner.equipGeneration=8;snapshot.sequence=41;snapshot.observedNs=receipt.nowNs;snapshot.selected.current={2,2,2};
    for(auto* state:{&snapshot.original.client.branches[0],&snapshot.original.client.branches[1],&snapshot.original.server})
        {state->currentState=state->nextState=2;state->loaded=30;state->reserve=210;}
    BodyFreeRightEvidence empty;empty.visibility.enabled=empty.visibility.hide=true;empty.visibility.request=19;
    empty.visibility.nativeOwner=snapshot.selectedOwner;empty.visibility.input=empty.input=f.s.source;
    empty.receipt.nativeOwner=snapshot.selectedOwner;empty.receipt.request=19;empty.receipt.inputSequence=f.s.source.sequence;
    empty.receipt.physicalEquipGeneration=6;empty.receipt.drawSerial=23;empty.receipt.verifiedCopyMask=3;empty.receipt.hidden=true;
    empty.receipt.observedNs=receipt.nowNs;empty.receipt.deadlineNs=f.s.source.deadlineNs;
    auto plan=std::make_shared<WeaponVisibilityPlan>();plan->nativeOwner=snapshot.selectedOwner;plan->request=19;
    plan->inputSequence=f.s.source.sequence;plan->physicalEquipGeneration=6;plan->meshSequence=29;plan->hidden=true;
    plan->observedNs=receipt.nowNs;plan->deadlineNs=plan->inputDeadlineNs=f.s.source.deadlineNs;
    empty.receipt.evidence=plan;
    empty.suppression.owner=snapshot.selectedOwner;empty.suppression.input=f.s.source;empty.suppression.request=19;empty.suppression.nativeTick=43;
    const auto completion=chestKnifeNative::CaptureReturnCompletion(*returned,receipt,anchor,snapshot,empty,4,17,3,returned->source.nowNs);
    // Expired release source is still copied verbatim; only the real receipt
    // can pass policy.Observe. A rejected or wrong serial remains nonmatching.
    auto wrong=receipt;++wrong.serial;CHECK(!f.knife.Observe(wrong));
    const BodyItemKey original{anchor.original.client.owner.weapon,6};
    history.Publish(completion,wrong,false,original,*draw,drawn,17,1,0,receipt.nowNs);
    CHECK(history.Read()&&!history.Read()->policyMatched&&!history.Read()->policyAccepted);
    CHECK(f.knife.Observe(receipt));CHECK(f.knife.Phase()==KnifePhase::NeedNeutral);
    history.Publish(completion,receipt,true,original,*draw,drawn,17,1,1,receipt.nowNs);
    const auto first=history.Read();CHECK(first&&first->policyMatched);
    CHECK(first->native.command.source.deadlineNs==returned->source.deadlineNs);
    CHECK(first->native.freeRight.drawSerial==23&&first->native.freeRight.verifiedCopyMask==3);
    // Destruction/reset of the original successful values cannot erase or
    // restamp the copied historical proof. No current Held/armed frame exists.
    anchor={};snapshot={};empty={};plan->request=99;held.Begin();held.Invalidate();
    CHECK(first->native.freeRight.planRequest==19&&first->native.freeRight.planMeshSequence==29);
    std::ostringstream complete;WriteChestKnifeReturnObservation(complete,{history.Read(),{}});
    const auto section=complete.str();CHECK(section.size()<14000);
    CHECK(section.find("\"historical\":true,\"nativeCompleted\":true")!=std::string::npos);
    CHECK(section.find("\"policyMatched\":true")!=std::string::npos);
    CHECK(section.find("\"actionAuthority\":false,\"freshEmptyRequired\":true")!=std::string::npos);
    CHECK(section.find("\"validUntilNs\"")==std::string::npos&&section.find("\"stockTriggerArmed\"")==std::string::npos);
    CHECK(section.find("\"selected\":[2,2,2]")!=std::string::npos);
    const auto invalid=std::string("{\"valid\":false,\"invalid_reason\":\"shot_expired\",\"chestKnifeReturn\":")+section+"}";
    CHECK(invalid.starts_with("{\"valid\":false,"));
    CHECK(first->native.receipt.deadlineNs==receipt.deadlineNs&&history.Read()==first);
    for(unsigned fault=0;fault<10;++fault){auto n=completion;auto r=receipt;auto g=original;auto d=*draw;auto dr=drawn;std::uint64_t operation=17;
        if(fault==0)++r.serial;if(fault==1)++n.bodyOperation;if(fault==2)++g.id;if(fault==3)g.generation=0;
        if(fault==4)++d.knife.generation;if(fault==5)++dr.serial;if(fault==6)++n.receipt.deadlineNs;
        if(fault==7)++operation;if(fault==8)n.chooserMask=1;if(fault==9)n.lifetime=0;
        history.Publish(n,r,true,g,d,dr,operation,1,1,receipt.nowNs);CHECK(history.Read()&&!history.Read()->policyMatched);
    }
    // Capacity one; a second event replaces the slot without altering a reader's
    // retained immutable first record. Neither a count nor Empty is an input.
    auto later=completion;later.command.serial+=2;later.receipt.serial+=2;++later.bodyOperation;
    history.Publish(later,later.receipt,true,original,*draw,drawn,18,2,2,receipt.nowNs);
    CHECK(history.Read()!=first&&first->native.bodyOperation==17&&history.Read()->native.bodyOperation==18);
    std::puts("Historical knife Return: pending failure boundary, policy join, immutable original clocks, independent publication and one-slot retention.");
    return 0;
}

ChestKnifeLiveObservation LiveFixture(){
    ChestKnifeLiveObservation v;v.owner={0x10000,0x20000,0x30000,0x52000,4,7,9};
    v.originalOwner=v.owner;v.originalOwner.weapon=0x51000;v.originalOwner.equipGeneration=6;
    v.knife={v.owner.weapon,3};v.originalGun={v.originalOwner.weapon,2};
    v.input={{(std::uint64_t(v.owner.weak)<<32)|v.owner.soldier,4,99,9},20,1000000000,1100000000,1010000000,true,{true,true},{true,false}};
    auto& c=v.controls;c.generation=v.input.sequence;c.spaceGeneration=v.input.owner.space;c.predictedNs=v.input.observedNs;
    c.focused=c.headValid=true;c.trackingBasisGeneration=1;c.menu={InputMenuOwner::Gameplay,5,8,v.input.deadlineNs};
    for(auto& hand:c.hands){hand.active=Stick|Trigger|Squeeze|Primary|Secondary|StickClick;hand.gripTracked=hand.aimTracked=true;}
    c.hands[1].squeeze=1;
    v.hold={{12,v.input.owner,InteractionHand::Right,HandClaimKind::GunHold,{v.knife.id,v.knife.generation},{13,3},0},v.input.deadlineNs,v.input.sequence};
    v.bodyOperation=17;v.rendered={{v.owner,v.input,v.knife,v.bodyOperation},ChestKnifeGripProfiles()[0].rigFingerprint,{}};
    v.continuity=5;v.draws=1;v.nativeSequence=v.input.sequence;v.nativeObservedNs=v.input.nowNs;v.nativeTick=41;v.suppressionRequest=17;
    v.knifeSlot=7;v.selectedSlots={7,7,7};v.phase=KnifePhase::Held;
    v.nativeOwned=v.nativeHealthy=v.nativeObservationComplete=v.stockTriggerEnabled=true;
    v.held=v.stockTriggerReady=v.stockTriggerArmed=v.fireSuppressed=v.mechanicsSettled=true;return v;
}
#include "KnifeCurrentHoldCases.inc"
int LiveObservationCases(){
    const auto v=LiveFixture();const auto now=v.input.nowNs;
    CHECK(ChestKnifeLiveRejection(v,now).empty());
    auto jab=v;jab.motionEnabled=true;CHECK(ChestKnifeLiveRejection(jab,now).empty());
    std::ostringstream jabJson;WriteChestKnifeLiveObservation(jabJson,&jab,jab.owner,jab.input,jab.controls,now);
    CHECK(jabJson.str().find("\"motionEnabled\":true")!=std::string::npos);
    {
        auto recovered=jab;chestKnifeNative::ObservationRecovery recovery;recovery.IdleNow();
        const auto epoch=recovery.Miss();CHECK(epoch);recovery.Returned(epoch,900000000,true);
        const auto completed=recovery.Read();CHECK(recovery.Rejoin(completed,990000000));
        recovered.nativeObservationComplete=false;recovered.firstObservationGap=1*4+1;
        recovered.currentObservationReady=true;
        recovered.observationRecovery={recovery.Read(),1,v.bodyOperation,19,990000000,995000000};
        ChestKnifeLivePublication publication;publication.Request(now);
        CHECK(publication.Publish(recovered,now));CHECK(publication.Read(now));
        std::ostringstream json;WriteChestKnifeLiveObservation(json,&recovered,recovered.owner,recovered.input,recovered.controls,now);
        CHECK(json.str().find("\"available\":true")!=std::string::npos);
        CHECK(json.str().find("\"nativeObservationComplete\":false")!=std::string::npos);
        CHECK(json.str().find("\"currentObservationReady\":true")!=std::string::npos);
        CHECK(json.str().find("\"recovered\":true")!=std::string::npos);
        recovered.observationRecovery.state.pending=true;
        CHECK(!publication.Publish(recovered,now));CHECK(!publication.Read(now));
    }
    CHECK(ChestKnifeLiveJoinRejection(v,v.owner,v.input,v.controls,now).empty());
    CHECK(ChestKnifeLiveDeadline(v)==v.input.deadlineNs);
    for(unsigned fault=0;fault<49;++fault){auto bad=v;
        switch(fault){
        case 0:++bad.originalOwner.soldier;break;case 1:bad.originalOwner.equipGeneration=0;break;
        case 2:bad.originalGun.generation=0;break;case 3:++bad.knife.id;break;
        case 4:++bad.input.owner.equipGeneration;break;case 5:bad.continuity=0;break;
        case 6:bad.bodyOperation=0;break;case 7:bad.phase=KnifePhase::Attacking;break;
        case 8:bad.nativeHealthy=false;break;case 9:bad.nativeOwned=false;break;
        case 10:bad.nativeObservationComplete=false;break;case 11:bad.stockTriggerEnabled=false;break;
        case 12:bad.motionEnabled=true;bad.attackBridgeEnabled=true;break;case 13:bad.attackBridgeEnabled=true;break;
        case 14:bad.held=false;break;case 15:bad.stockTriggerReady=false;break;
        case 16:bad.stockTriggerArmed=false;break;case 17:bad.fireSuppressed=false;break;
        case 18:bad.mechanicsSettled=false;break;case 19:bad.input.observedNs=now+1;break;
        case 20:bad.rendered.source.input.deadlineNs=now;break;case 21:bad.controls.hands[1].trigger=1;break;
        case 22:bad.controls.hands[1].squeeze=0;break;case 23:bad.controls.hands[0].squeeze=.5f;break;
        case 24:bad.hold.token.id=0;break;case 25:bad.hold.token.hand=InteractionHand::Left;break;
        case 26:++bad.hold.token.owner.equipGeneration;break;case 27:++bad.hold.inputSequence;break;
        case 28:++bad.hold.deadlineNs;break;case 29:++bad.rendered.rigFingerprint;break;
        case 30:++bad.rendered.source.bodyOperation;break;case 31:++bad.rendered.source.knife.generation;break;
        case 32:--bad.rendered.source.input.deadlineNs;break;case 33:++bad.rendered.source.owner.weapon;break;
        case 34:bad.selectedSlots[2]=0;break;case 35:++bad.nativeSequence;break;
        case 36:bad.nativeObservedNs=now+1;break;case 37:bad.nativeTick=0;break;
        case 38:bad.suppressionRequest=0;break;case 39:bad.controls.trackingBasisGeneration=0;break;
        case 40:bad.input.focused=false;break;case 41:bad.input.tracked[0]=false;break;
        case 42:bad.input.released[1]=true;break;case 43:bad.controls.menu.owner=InputMenuOwner::Menu;break;
        case 44:++bad.controls.generation;break;case 45:++bad.controls.spaceGeneration;break;
        case 46:bad.input.nowNs=bad.input.observedNs-1;break;case 47:++bad.input.deadlineNs;break;
        case 48:bad.controls.menu.deadlineNs=now;break;
        }
        CHECK(!ChestKnifeLiveRejection(bad,now).empty());
        std::ostringstream denied;WriteChestKnifeLiveObservation(denied,&bad,v.owner,v.input,v.controls,now);
        CHECK(denied.str().find("\"available\":false")!=std::string::npos);
        CHECK(denied.str().find("\"stockTriggerArmed\":true")==std::string::npos);
    }
    for(unsigned fault=0;fault<11;++fault){auto owner=v.owner;auto input=v.input;auto controls=v.controls;
        if(fault==0)++owner.equipGeneration;if(fault==1)++input.owner.space;
        if(fault==2)--input.deadlineNs;if(fault==3)++controls.menu.epoch;
        if(fault==4)++controls.trackingBasisGeneration;if(fault==5)controls.hands[1].trigger=1;
        if(fault==6){++input.sequence;++controls.generation;} // Newer sequence cannot reuse the old observed time.
        if(fault==7){--input.sequence;--controls.generation;}
        if(fault==8)controls.hands[1].squeeze=0;if(fault==9)controls.hands[0].squeeze=.5f;
        if(fault==10)controls.worldUnitsPerMeter*=2;
        CHECK(!ChestKnifeLiveJoinRejection(v,owner,input,controls,now).empty());
    }
    auto next=v;next.input.sequence++;next.input.observedNs+=10000000;next.input.nowNs+=10000000;next.input.deadlineNs+=10000000;
    next.controls.generation=next.input.sequence;next.controls.predictedNs=next.input.observedNs;
    next.hold.inputSequence=next.input.sequence;next.nativeSequence=next.input.sequence;next.nativeObservedNs=next.input.nowNs;
    CHECK(ChestKnifeLiveRejection(next,next.input.nowNs).empty());
    CHECK(ChestKnifeLiveJoinRejection(v,next.owner,next.input,next.controls,next.input.nowNs).empty());
    CHECK(ChestKnifeLiveContinues(v,next,next.input.nowNs)); // New input keeps the old render/claim expiry.
    CHECK(ChestKnifeLiveDeadline(next)==v.input.deadlineNs);
    auto recovered=next;++recovered.continuity;CHECK(!ChestKnifeLiveContinues(v,recovered,next.input.nowNs));
    recovered=next;++recovered.originalGun.generation;CHECK(!ChestKnifeLiveContinues(v,recovered,next.input.nowNs));
    recovered=next;++recovered.controls.trackingBasisGeneration;CHECK(!ChestKnifeLiveContinues(v,recovered,next.input.nowNs));
    CHECK(!ChestKnifeLiveContinues(v,next,v.input.deadlineNs)); // Serializing later cannot renew original proof.
    std::ostringstream out;WriteChestKnifeLiveObservation(out,&v,v.owner,v.input,v.controls,now);
    CHECK(out.str().size()<6000&&out.str().find("\"available\":true")!=std::string::npos);
    CHECK(out.str().find("\"validUntilNs\":1100000000")!=std::string::npos&&out.str().find("\"stockTriggerArmed\":true")!=std::string::npos);
    CHECK(out.str().find("\"originalOwner\":")!=std::string::npos&&out.str().find("\"selectedSlots\":[7,7,7]")!=std::string::npos);
    std::ostringstream expired;WriteChestKnifeLiveObservation(expired,&v,v.owner,v.input,v.controls,v.input.deadlineNs);
    CHECK(expired.str().find("\"available\":false")!=std::string::npos&&expired.str().find("\"held\":true")==std::string::npos);
    std::ostringstream missing;WriteChestKnifeLiveObservation(missing,nullptr,v.owner,v.input,v.controls,now);
    CHECK(missing.str().find("observation_unavailable")!=std::string::npos&&missing.str().find("\"held\":true")==std::string::npos);
    ChestKnifeLivePublication publication;
    CHECK(!publication.Publish(v,now)&&!publication.Read(now));
    publication.Request(now);CHECK(publication.Publish(v,now));const auto first=publication.Read(now);CHECK(first);
    publication.Begin();CHECK(!publication.Read(now)); // No preceding gather while current gates are being evaluated.
    CHECK(publication.Publish(next,next.input.nowNs));const auto continuous=publication.Read(next.input.nowNs);CHECK(continuous);
    CHECK(ChestKnifeLiveContinues(*first,*continuous,next.input.nowNs));
    publication.Invalidate();CHECK(!publication.Read(next.input.nowNs));
    CHECK(publication.Publish(next,next.input.nowNs));const auto renewed=publication.Read(next.input.nowNs);CHECK(renewed);
    CHECK(!ChestKnifeLiveContinues(*first,*renewed,next.input.nowNs)); // Missing input permanently retires old admission.
    auto rejected=next;rejected.nativeHealthy=false;CHECK(!publication.Publish(rejected,next.input.nowNs));
    CHECK(!publication.Read(next.input.nowNs));CHECK(publication.Publish(next,next.input.nowNs));
    CHECK(!ChestKnifeLiveContinues(*renewed,*publication.Read(next.input.nowNs),next.input.nowNs));
    publication.Request(next.input.nowNs);CHECK(!publication.Read(v.input.deadlineNs)); // Demand renewal is not source renewal.
    CHECK(!publication.Requested(next.input.nowNs+1000000000));
    for(unsigned race=0;race<4;++race){ChestKnifeLivePublication p;p.Request(now);CHECK(p.Publish(v,now));
        unsigned reads=0;auto clock=now;
        const auto section=ObserveChestKnifeLiveJson([&](std::int64_t sampled){
            if(++reads==2){
                if(race==0)p.Begin();
                if(race==1)p.Invalidate();
                if(race==2)clock=v.input.deadlineNs;
                if(race==3){p.Invalidate();p.Publish(v,now);}
            }
            return p.Read((std::max)(sampled,clock));
        },[&](){return clock;},v.owner,v.input,v.controls);
        const auto outer=std::string("{\"valid\":true,\"nonce\":\"42\",\"chestKnife\":")+section+"}";
        CHECK(outer.starts_with("{\"valid\":true,\"nonce\":\"42\""));
        CHECK(section.find("\"available\":false")!=std::string::npos&&section.find("\"held\":true")==std::string::npos);
        CHECK(reads==2); // No retries/waits and no cancellation of the enclosing snapshot.
    }

    // Independent body observation cannot promote the invalid firearm frame.
    {
        ChestKnifeLivePublication p;p.Request(now);CHECK(p.Publish(v,now));unsigned reads=0;
        const auto section=ObserveOwnedChestKnifeLiveJson([&](std::int64_t sampled){++reads;return p.Read(sampled);},[&](){return now;});
        const auto outer=std::string("{\"valid\":false,\"invalid_reason\":\"selected_configuration_or_rig_missing_changed_or_expired\",\"chestKnife\":")+section+"}";
        CHECK(outer.starts_with("{\"valid\":false,"));CHECK(reads==2);
        CHECK(section.find("\"available\":true")!=std::string::npos);
        CHECK(section.find("\"validUntilNs\":1100000000")!=std::string::npos);
        CHECK(section.find("\"originalOwner\":")!=std::string::npos&&section.find("\"selectedSlots\":[7,7,7]")!=std::string::npos);
    }
    for(unsigned fault=0;fault<8;++fault){ChestKnifeLivePublication p;p.Request(now);CHECK(p.Publish(v,now));
        unsigned reads=0;auto clock=now;
        const auto section=ObserveOwnedChestKnifeLiveJson([&](std::int64_t sampled){
            if(++reads==2){
                if(fault==0)p.Begin();
                if(fault==1)p.Invalidate();
                if(fault==2)clock=v.input.deadlineNs;
                if(fault==3){p.Invalidate();p.Publish(v,now);}
                if(fault==4){auto changed=v;changed.owner.equipGeneration++;changed.rendered.source.owner=changed.owner;p.Publish(changed,now);}
                if(fault==5){auto changed=v;changed.hold.token.id++;p.Publish(changed,now);}
                if(fault==6){auto changed=v;changed.phase=KnifePhase::Returning;p.Publish(changed,now);}
                if(fault==7){auto changed=v;changed.hold.deadlineNs=now;p.Publish(changed,now);}
            }
            return p.Read((std::max)(sampled,clock));
        },[&](){return clock;});
        CHECK(reads==2);CHECK(section.find("\"available\":false")!=std::string::npos);
        CHECK(section.find("\"held\":true")==std::string::npos);
    }
    {
        ChestKnifeLivePublication p;p.Request(now);unsigned reads=0;
        const auto section=ObserveOwnedChestKnifeLiveJson([&](std::int64_t sampled){++reads;return p.Read(sampled);},[&](){return now;});
        CHECK(reads==1);CHECK(section.find("observation_unavailable")!=std::string::npos);
    }
    std::puts("Passive live knife observation: exact owner/source/claim/native joins, mode separation, continuity and bounded serialization negatives.");
    return 0;
}
}
#include "KnifeNativeReadDiagnosticCases.inc"
#include "KnifeInspectionCases.inc"
int main(){
    CHECK(!MotionRenderSeparationCases());
    CHECK(!MotionCalibrationCases());
    CHECK(!StagedMotionSourceCases());
    CHECK(!MotionProjectionObservationCases());
    CHECK(!RepeatedMotionContentionCases());
    CHECK(!knife_read_diagnostic_cases::Cases());
    CHECK(!knife_inspection_cases::Cases());
    {
        private_knife::Item item;
        for(auto* s:{&item.client.branches[0],&item.client.branches[1],&item.server})s->currentState=s->nextState=2;
        CHECK(chestKnifeNative::InspectReady(item).reason==chestKnifeNative::ReadyReason::Ready);
        for(unsigned branch=0;branch<3;++branch)for(unsigned field=0;field<6;++field){
            auto bad=item;auto& s=branch<2?bad.client.branches[branch]:bad.server;
            if(field==0)s.currentState=3;if(field==1)s.nextState=3;
            if(field==2)s.phaseTimer=std::numeric_limits<float>::quiet_NaN();
            if(field==3)s.phaseTimer=-1;if(field==4)s.phaseTimer=1000001;if(field==5)s.flagsA8=8;
            const auto r=chestKnifeNative::InspectReady(bad);CHECK(r.branch==branch);
            CHECK(unsigned(r.reason)==unsigned(chestKnifeNative::ReadyReason::Current)+field);
            std::ostringstream json;chestKnifeNative::WriteReadyObservation(json,r);
            CHECK(json.str().find("nan")==std::string::npos);
            if(field==2)CHECK(json.str().find("\"phaseTimer\":null")!=std::string::npos);
        }
        auto v=LiveFixture();ChestKnifeLivePublication p;const auto now=v.input.nowNs;p.Request(now);
        CHECK(p.Publish(v,now));v.nativeObservationComplete=false;v.firstObservationGap=5*4+1;
        CHECK(!p.Publish(v,now));CHECK(!p.Read(now));const auto first=p.ReadDiagnostic();CHECK(first);
        CHECK(first->first.branch=="native_observation_complete");
        p.Begin();p.Invalidate();CHECK(p.ReadDiagnostic()->first.observedNs==now);
        auto later=v;later.input.nowNs++;later.stockTriggerReady=false;
        CHECK(!p.Publish(later,now+1));const auto retained=p.ReadDiagnostic();CHECK(retained);
        CHECK(retained->first.branch=="native_observation_complete"&&retained->latest.branch=="stock_trigger_ready");
        CHECK(retained->first.value.input.deadlineNs==v.input.deadlineNs);
        std::ostringstream out;WriteChestKnifePublicationDiagnostic(out,retained);
        CHECK(out.str().size()<8192&&out.str().find("\"actionAuthority\":false")!=std::string::npos);
        CHECK(out.str().find("\"held\":true")==std::string::npos&&out.str().find("validUntilNs")==std::string::npos);
        CHECK(out.str().find("\"reason\":5,\"branch\":1")!=std::string::npos);
        p.Begin();CHECK(!p.Read(now));CHECK(p.ReadDiagnostic());
        p.Reject(later,now+2,"producer_prerequisite","render_receipt");
        CHECK(p.ReadDiagnostic()->latest.branch=="render_receipt");
        CHECK(p.ReadDiagnostic()->first.branch=="native_observation_complete");
        ++later.bodyOperation;++later.draws;p.Reject(later,now+3,"producer_prerequisite","native_view");
        CHECK(p.ReadDiagnostic()->first.branch=="native_view");
        p.Request(v.input.deadlineNs);CHECK(!p.Read(v.input.deadlineNs));
        CHECK(p.ReadDiagnostic()->latest.value.input.deadlineNs==v.input.deadlineNs);
    }
    {
        // Actual593 exhibited this dependency: a real held claim stopped
        // renewing after input7356, then expired on7360. A missed receipt must
        // not forbid rendering while that SAME claim is still live. Fixture
        // contact below is a test-owned renderer stand-in, not native evidence.
        Fixture f;CHECK(f.Held());const auto before=*f.hands.Current(InteractionHand::Right);
        f.s.trackedKnifeOwned=false;CHECK(!f.Step());
        CHECK(!ChestKnifePoseCustody(f.knife,f.hands,f.s.source));
        CHECK(ChestKnifeRenderPoseCustody(f.knife,f.hands,f.s.source));
        const auto suspended=*f.hands.Current(InteractionHand::Right);
        CHECK(suspended.token==before.token&&suspended.inputSequence==before.inputSequence&&suspended.deadlineNs==before.deadlineNs);
        // Reading render custody never mints/renews anything or enables attack.
        CHECK(!f.knife.StockTriggerArmedFor(f.s,f.hands));
        CHECK(!f.knife.RecognitionReady(f.s.source));
        CHECK(!f.knife.NeedsInitialHeldPose());
        auto future=f.s.source;future.sequence=before.inputSequence-1;
        CHECK(!ChestKnifeRenderPoseCustody(f.knife,f.hands,future));
        future.sequence=0;CHECK(!ChestKnifeRenderPoseCustody(f.knife,f.hands,future));
        for(unsigned owner=0;owner<4;++owner){auto wrong=f.s.source;
            if(owner==0)++wrong.owner.actor;if(owner==1)++wrong.owner.actorGeneration;
            if(owner==2)++wrong.owner.equipGeneration;if(owner==3)++wrong.owner.space;
            CHECK(!ChestKnifeRenderPoseCustody(f.knife,f.hands,wrong));}
        // Only a later actually supplied receipt permits the ordinary Prepare
        // path to renew. There is no receipt construction in the helper.
        f.s.trackedKnifeOwned=true;CHECK(!f.Step());
        const auto recovered=*f.hands.Current(InteractionHand::Right);
        CHECK(recovered.token==before.token&&recovered.inputSequence==f.s.source.sequence);
        CHECK(ChestKnifePoseCustody(f.knife,f.hands,f.s.source));
        CHECK(ChestKnifeRenderPoseCustody(f.knife,f.hands,f.s.source));
    }
    {
        Fixture f;CHECK(f.Held());const auto before=*f.hands.Current(InteractionHand::Right);
        f.s.trackedKnifeOwned=false;CHECK(!f.Step(100*ms));
        CHECK(!f.hands.Current(InteractionHand::Right));
        CHECK(!ChestKnifeRenderPoseCustody(f.knife,f.hands,f.s.source));
        f.s.trackedKnifeOwned=true;CHECK(!f.Step());
        CHECK(!f.hands.Current(InteractionHand::Right)); // no expired reacquire
        CHECK(!ChestKnifeRenderPoseCustody(f.knife,f.hands,f.s.source));
        CHECK(!f.knife.Hold()||f.knife.Hold()->id==before.token.id);
    }
    for(unsigned fault=0;fault<7;++fault){Fixture f;CHECK(f.Held());f.s.trackedKnifeOwned=false;
        if(fault==0)f.Grip(false);if(fault==1)f.s.source.focused=false;if(fault==2)f.s.source.tracked[1]=false;
        if(fault==3)++f.s.source.owner.actor;if(fault==4)++f.s.source.owner.actorGeneration;
        if(fault==5)++f.s.source.owner.equipGeneration;if(fault==6)++f.s.source.owner.space;
        (void)f.Step();CHECK(!f.hands.Current(InteractionHand::Right));
        CHECK(!ChestKnifeRenderPoseCustody(f.knife,f.hands,f.s.source));
    }
    {
        Fixture f;CHECK(f.Held());const auto old=*f.knife.Hold();
        auto replacement=f.hands.Transfer(f.s.source,old,{f.s.source.owner,InteractionHand::Right,
            HandClaimKind::BodyInventory,{999,1},f.s.heldContact,f.s.claimIntent+1,0});
        CHECK(replacement.claim&&replacement.claim->token!=old);
        CHECK(!ChestKnifeRenderPoseCustody(f.knife,f.hands,f.s.source));
    }
    CHECK(!ReturnObservationCases());
    CHECK(!LiveObservationCases());
    CHECK(!CurrentHoldCases());
    {private_knife::OriginalKnifeSource source;HandInteractionSample value{{1,2,3,4},9,1000000000,1100000000,1000000000,true,{true,true}};
     CHECK(source.Advance(value));CHECK(!source.Advance(value));++value.nowNs;CHECK(!source.Advance(value));
     ++value.sequence;CHECK(source.Advance(value));value.focused=false;CHECK(source.Advance(value));
     value.focused=true;CHECK(source.Advance(value));value.tracked[1]=false;CHECK(source.Advance(value));
     value.released[1]=true;CHECK(source.Advance(value));++value.owner.space;CHECK(source.Advance(value));
     ++value.deadlineNs;CHECK(source.Advance(value));value={};CHECK(source.Advance(value));CHECK(!source.Advance(value));}
    {ChestKnifeRenderSource source;source.owner={0x10000,0x20000,0x30000,0x52000,4,7,9};
     source.input={{(std::uint64_t(0x30000)<<32)|0x20000,4,99,9},2,1000000000,1100000000,1000000000,true,{true,true}};
     source.knife={0x52000,3};source.bodyOperation=17;auto current=source.input;
     CHECK(ChestKnifeRenderCurrent(source,source.owner,current,current.nowNs));
     ++current.sequence;current.observedNs+=10000000;current.deadlineNs+=10000000;current.nowNs=current.observedNs;
     CHECK(ChestKnifeRenderCurrent(source,source.owner,current,current.nowNs));
     CHECK(!ChestKnifeRenderCurrent(source,source.owner,current,source.input.deadlineNs)); // no old lease renewal
     CHECK(!ChestKnifeRenderCurrent(source,source.owner,current,current.observedNs-1));
     for(unsigned fault=0;fault<7;++fault){auto bad=source;auto now=current;
       if(fault==0)++bad.owner.equipGeneration;if(fault==1)++now.owner.equipGeneration;if(fault==2)++now.owner.space;
       if(fault==3)now.released[1]=true;if(fault==4)bad.input.focused=false;if(fault==5)bad.input.tracked[1]=false;
       if(fault==6)bad.bodyOperation=0;
       CHECK(!ChestKnifeRenderCurrent(bad,source.owner,now,current.nowNs));}}
    {const auto& profile=ChestKnifeGripProfiles()[0];CHECK(authored_grip_detail::Profile(profile));CHECK(!profile.authoredRifleSupport);
     CHECK(profile.authoredAxisEvidence.empty()&&!private_knife::LeftHandGraspSupported);
     auto mesh=std::make_shared<SelectedMeshesSnapshot>();WeaponEquipmentIdentity equipment{15,16,17,{}};
     std::copy(profile.assetName.begin(),profile.assetName.end(),equipment.asset.begin());
     mesh->owner={9,10,11,15,12,18,14};mesh->sequence=19;mesh->observedNs=990000000;mesh->deadlineNs=1200000000;
     mesh->weaponData=16;mesh->stateTypeInfo=1;mesh->meshTypeInfo=2;mesh->inventory=3;mesh->selectedSlot=7;
     std::copy(profile.assetName.begin(),profile.assetName.end(),mesh->weaponName.begin());
     mesh->stateCount=1;mesh->soleConfiguredArray=5;mesh->states[0].array=5;mesh->states[0].state=6;mesh->states[0].count=1;
     auto& selected=mesh->states[0].meshes[0];selected.address=7;selected.typeInfo=2;selected.namePointer=8;
     std::copy(profile.meshPath.begin(),profile.meshPath.end(),selected.assetPath.begin());
     const AuthoredGripOwner owner{10,11,12,13,14,20,1090000000};
     const auto bind=[&](std::uint64_t rig){return BindAuthoredGrip(ChestKnifeGripProfiles(),mesh,equipment,owner,rig,1000000000,false);};
     const auto bound=bind(profile.rigFingerprint);CHECK(bound.binding&&!AuthoredRifleSupport(*bound.binding));
     CHECK(!bind(profile.rigFingerprint+1).binding);selected.assetPath[0]='?';CHECK(!bind(profile.rigFingerprint).binding);
     CHECK(!AuthoredGripBinding::aimCapability&&!AuthoredGripBinding::muzzleCapability&&!AuthoredGripBinding::reloadCapability);}
    std::puts("Three knife source/render groups PASS; original epoch invalidation, immutable render custody/expiry, measured grip exact binding.");
}
