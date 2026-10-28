#include "Test.h"
#include "fvr/interaction/SelectedMeleeAttack.h"
#include "fvr/interaction/MeleeResponseTiming.h"
#include <limits>
using namespace fvr::interaction;
namespace {
constexpr std::int64_t ms=1000000;
struct Fixture {
    SelectedMeleeAttack policy;HandInteraction hands;SelectedMeleeSample s{};
    std::uint64_t claims=0;unsigned emitted=0;std::optional<SelectedMeleeIntent> last;
    Fixture(){s.motion.source={{1,2,3,4},1,1000*ms,1100*ms,1000*ms,true,{true,true},{false,false}};
        s.identity={s.motion.source.owner,{42,1},{53,1},1};s.selected=s.geometryCurrent=s.motion.attackAvailable=true;
        Refresh();(void)policy.Update(s,hands);}
    void Refresh(){
        s.motion.source.owner=s.identity.owner;hands.Update(s.motion.source);
        HandContactProof contact{{11,1},s.motion.source.sequence,s.motion.source.deadlineNs,true};
        auto held=hands.Current(InteractionHand::Right);
        if(held){auto r=hands.Renew(s.motion.source,held->token,contact);if(r.claim)s.motion.hold=r.claim->token;}
        else {auto r=hands.Acquire(s.motion.source,{s.identity.owner,InteractionHand::Right,HandClaimKind::GunHold,
            s.identity.item,contact,++claims,0});if(r.claim)s.motion.hold=r.claim->token;}
    }
    SelectedMeleeResult Step(float dz=0,float trigger=0,std::int64_t dt=10*ms){
        ++s.motion.source.sequence;s.motion.source.observedNs+=dt;s.motion.source.nowNs=s.motion.source.observedNs;
        s.motion.source.deadlineNs=s.motion.source.nowNs+100*ms;s.motion.tip.z+=dz;s.trigger=trigger;Refresh();
        auto r=policy.Update(s,hands);if(r.intent){++emitted;last=r.intent;}return r;
    }
    void Rest(unsigned n=20){while(n--)Step();}
    void Stab(){for(unsigned i=0;i<8;++i)Step(.012f);}
    bool Outcome(SelectedMeleeOutcome value){return last&&policy.Resolve(last->serial,last->identity,value);}
};
int OneMotionIntentAndExactOutcome(){
    Fixture f;f.Rest();f.Stab();CHECK(f.emitted==1&&f.last->cause==SelectedMeleeCause::Stroke&&f.last->stroke);
    CHECK(f.last->hold==f.s.motion.hold&&f.last->identity==f.s.identity);
    f.Rest();f.Stab();CHECK(f.emitted==1&&f.policy.PendingState()==SelectedMeleePending::Staged);
    CHECK(!f.policy.Resolve(f.last->serial+1,f.last->identity,SelectedMeleeOutcome::Submitted));
    CHECK(!f.Outcome(SelectedMeleeOutcome::Completed));CHECK(f.Outcome(SelectedMeleeOutcome::Submitted));
    CHECK(!f.Outcome(SelectedMeleeOutcome::RejectedNoEffect));CHECK(f.Outcome(SelectedMeleeOutcome::Completed));
    f.Stab();CHECK(f.emitted==1);f.Rest();f.Stab();CHECK(f.emitted==2);return 0;
}
int SingleJabWithdrawalAndRejab(){
    for(const auto hz:{72,90,120}){
        Fixture f;f.Rest();const auto dt=1000000000ll/hz;
        const auto jab=[&](){
            f.Step(.025f,0,dt);f.Step(.020f,0,dt);
            for(unsigned n=0;n<5;++n)f.Step(.004f,0,dt);
        };
        jab();CHECK(f.emitted==1&&f.last->cause==SelectedMeleeCause::Stroke&&f.last->stroke);
        const auto first=*f.last;
        for(unsigned n=0;n<100;++n)CHECK(!f.policy.Update(f.s,f.hands).intent);
        CHECK(f.Outcome(SelectedMeleeOutcome::Submitted));
        // Forward motion and withdrawal cannot make a second action while the
        // adapter still owes this one's outcome. These are mock completions,
        // not evidence that a native target was struck or damaged.
        f.Stab();for(unsigned n=0;n<8;++n)f.Step(-.012f,0,dt);
        f.Rest();CHECK(f.emitted==1&&f.policy.PendingState()==SelectedMeleePending::Submitted);
        CHECK(f.policy.Pending()->serial==first.serial&&
            f.policy.Pending()->sourceSequence==first.sourceSequence&&
            f.policy.Pending()->deadlineNs==first.deadlineNs&&f.policy.Pending()->hold==first.hold);
        CHECK(f.Outcome(SelectedMeleeOutcome::Completed));
        for(unsigned n=0;n<8;++n)f.Step(-.012f,0,dt);
        CHECK(f.emitted==1&&!f.policy.Pending());
        f.Rest();jab();CHECK(f.emitted==2&&f.last->serial==first.serial+1&&
            f.last->cause==SelectedMeleeCause::Stroke&&f.last->hold==first.hold);
    }
    return 0;
}
int IncidentalMotionAndReloadClaimsCannotFinishAJab(){
    Fixture drift;drift.Rest();
    for(unsigned n=0;n<30;++n)drift.Step(.003f);
    CHECK(!drift.emitted&&!drift.policy.Pending());
    Fixture lateral;lateral.Rest();
    for(unsigned n=0;n<20;++n){lateral.s.motion.tip.x+=.012f;lateral.Step();}
    CHECK(!lateral.emitted&&!lateral.policy.Pending());
    Fixture reversed;reversed.Rest();reversed.Step(.025f);reversed.Step(-.012f);reversed.Step(.05f);
    CHECK(!reversed.emitted);reversed.Rest();reversed.Stab();CHECK(reversed.emitted==1);
    for(const auto kind:{HandClaimKind::AmmoObject,HandClaimKind::Mechanism,HandClaimKind::BodyInventory}){
        Fixture f;f.Rest();f.Step(.025f);const auto knifeHold=f.s.motion.hold;
        HandContactProof contact{{11,1},f.s.motion.source.sequence,f.s.motion.source.deadlineNs,true};
        const auto activity=f.hands.Transfer(f.s.motion.source,knifeHold,
            {f.s.identity.owner,InteractionHand::Right,kind,{99,1},contact,++f.claims,0});
        CHECK(activity.claim);f.s.motion.hold=activity.claim->token;
        f.Step(.05f);CHECK(!f.emitted&&!f.policy.Pending());
        CHECK(f.hands.Current(InteractionHand::Right)->token==activity.claim->token);
        contact.inputSequence=f.s.motion.source.sequence;contact.deadlineNs=f.s.motion.source.deadlineNs;
        const auto regrip=f.hands.Transfer(f.s.motion.source,activity.claim->token,
            {f.s.identity.owner,InteractionHand::Right,HandClaimKind::GunHold,f.s.identity.item,contact,++f.claims,0});
        CHECK(regrip.claim&&regrip.claim->token.id!=knifeHold.id);f.s.motion.hold=regrip.claim->token;
        f.Step(.04f);f.Step(.04f);CHECK(!f.emitted); // Regrip cannot inherit partial travel.
        f.Rest();f.Stab();CHECK(f.emitted==1&&f.last->hold==regrip.claim->token);
    }
    return 0;
}
int TriggerAndMotionShareOneLatch(){
    Fixture f;f.Rest();for(unsigned i=0;i<4;++i)f.Step(.012f);
    f.Step(.02f,1);CHECK(f.emitted==1&&f.last->cause==SelectedMeleeCause::Trigger&&!f.last->stroke);
    for(unsigned i=0;i<30;++i)f.Step(.01f,1);CHECK(f.emitted==1);
    CHECK(f.Outcome(SelectedMeleeOutcome::RejectedNoEffect));f.Step(0,1);CHECK(f.emitted==1);
    f.Step(0,0);f.Step(0,1);CHECK(f.emitted==2);return 0;
}
int TriggerFallbackWithoutGeometry(){
    Fixture f;f.s.geometryCurrent=false;f.Rest();f.Stab();CHECK(!f.emitted);
    f.Step(0,1);CHECK(f.emitted==1&&f.last->cause==SelectedMeleeCause::Trigger);return 0;
}
int DrawRegripReconnectCannotAttack(){
    Fixture draw;draw.Stab();CHECK(!draw.emitted);draw.Rest();draw.Stab();CHECK(draw.emitted==1);
    Fixture f;f.Rest();f.hands.Release(f.s.motion.source,f.s.motion.hold);f.Step(0,1);CHECK(!f.emitted);
    for(unsigned i=0;i<20;++i)f.Step(0,1);CHECK(!f.emitted);f.Step();f.Step(0,1);CHECK(f.emitted==1);
    Fixture lost;lost.Rest();lost.s.motion.source.focused=false;lost.Step(0,1);CHECK(!lost.emitted);
    lost.s.motion.source.focused=true;for(unsigned i=0;i<20;++i)lost.Step(0,1);CHECK(!lost.emitted);
    lost.Step();lost.Step(0,1);CHECK(lost.emitted==1);return 0;
}
int PendingSurvivesInvalidationAndNeverOwnsHands(){
    Fixture f;f.Rest();f.Step(0,1);CHECK(f.emitted==1);CHECK(f.Outcome(SelectedMeleeOutcome::Submitted));
    const auto old=*f.last;const auto held=f.hands.Current(InteractionHand::Right);
    f.policy.InvalidateMotion();CHECK(f.policy.Pending()&&f.hands.Current(InteractionHand::Right)->token==held->token);
    CHECK(f.Outcome(SelectedMeleeOutcome::Unknown));++f.s.identity.binding.generation;++f.s.identity.inputEpoch;
    f.Rest();f.Stab();CHECK(f.emitted==1&&f.policy.Pending()->identity==old.identity);
    CHECK(!f.policy.Resolve(old.serial,f.s.identity,SelectedMeleeOutcome::Completed));
    CHECK(f.policy.Resolve(old.serial,old.identity,SelectedMeleeOutcome::Completed));
    f.Rest();f.Stab();CHECK(f.emitted==2);return 0;
}
int DuplicateStaleAndChangedInputCannotRepeat(){
    Fixture f;f.Rest();f.Step(0,1);const auto serial=f.last->serial;
    for(unsigned i=0;i<100;++i)CHECK(!f.policy.Update(f.s,f.hands).intent);
    CHECK(f.Outcome(SelectedMeleeOutcome::RejectedNoEffect));CHECK(!f.policy.Update(f.s,f.hands).intent);
    f.s.trigger=0;CHECK(!f.policy.Update(f.s,f.hands).intent);f.Step(0,1);CHECK(f.emitted==1);
    f.Step();f.Step(0,1);CHECK(f.emitted==2&&f.last->serial>serial);
    Fixture expiry;expiry.Rest();expiry.s.motion.source.nowNs=expiry.s.motion.source.deadlineNs;expiry.s.trigger=1;
    CHECK(!expiry.policy.Update(expiry.s,expiry.hands).intent);return 0;
}
int NativeReadinessNeverCompletesAnAttack(){
    Fixture f;f.Rest();f.Step(0,1);f.s.motion.attackAvailable=false;f.Rest();
    f.s.motion.attackAvailable=true;f.Rest();f.Stab();CHECK(f.emitted==1&&f.policy.Pending());
    CHECK(f.Outcome(SelectedMeleeOutcome::OwnerRetired));CHECK(!f.policy.Pending());
    Fixture unavailable;unavailable.Rest();unavailable.s.motion.attackAvailable=false;unavailable.Step(0,1);
    unavailable.s.motion.attackAvailable=true;unavailable.Step(0,1);CHECK(!unavailable.emitted);
    unavailable.Step();unavailable.Step(0,1);CHECK(unavailable.emitted==1);return 0;
}
int RepeatedPacketUsesItsFirstGeometry(){
    Fixture f;
    const auto repeatedGather=[&](){
        // The native gather can run again before OpenXR publishes a new
        // generation. Its render receipt is produced independently: re-derived
        // wrist rotation changes the tip/axis without another controller sample.
        const auto original=f.s;
        f.s.motion.tip.x+=.00001f;f.s.motion.direction={.00001f,0,1};
        f.s.motion.source.nowNs+=ms;
        const auto repeated=f.policy.Update(f.s,f.hands);
        f.s=original;
        return repeated;
    };
    for(unsigned n=0;n<20;++n){f.Step();const auto repeated=repeatedGather();
        CHECK(repeated.reason==SelectedMeleeReason::Duplicate&&!repeated.intent);}
    for(unsigned n=0;n<8;++n){f.Step(.012f);CHECK(!repeatedGather().intent);}
    CHECK(f.emitted==1&&f.last->cause==SelectedMeleeCause::Stroke);
    const auto originalIntent=*f.policy.Pending();
    for(unsigned n=0;n<20;++n)CHECK(!repeatedGather().intent);
    CHECK(f.policy.Pending()->serial==originalIntent.serial&&
        f.policy.Pending()->sourceSequence==originalIntent.sourceSequence&&
        f.policy.Pending()->deadlineNs==originalIntent.deadlineNs);
    return 0;
}
int DuplicateAuthorityLossRequiresFreshRecovery(){
    Fixture native;native.Rest();native.s.motion.attackAvailable=false;
    SelectedMeleeObservation observation;
    CHECK(native.policy.Update(native.s,native.hands,&observation).reason==SelectedMeleeReason::NativeUnavailable);
    CHECK(observation.armedBefore&&!observation.armedAfter&&!observation.motionUpdated);
    native.s.motion.attackAvailable=true;
    CHECK(native.policy.Update(native.s,native.hands,&observation).reason==SelectedMeleeReason::Duplicate);
    CHECK(!observation.armedAfter&&!observation.motionUpdated);
    native.Step(0,1);CHECK(!native.emitted);native.Step();native.Step(0,1);CHECK(native.emitted==1);
    const auto intent=*native.policy.Pending();native.s.motion.attackAvailable=false;
    CHECK(!native.policy.Update(native.s,native.hands,&observation).intent);
    CHECK(native.policy.Pending()->serial==intent.serial&&
        native.policy.PendingState()==SelectedMeleePending::Staged);

    Fixture geometry;geometry.Rest();geometry.s.geometryCurrent=false;
    CHECK(geometry.policy.Update(geometry.s,geometry.hands,&observation).reason==SelectedMeleeReason::Duplicate);
    CHECK(observation.armedAfter&&!observation.motionUpdated);
    geometry.s.geometryCurrent=true;CHECK(!geometry.policy.Update(geometry.s,geometry.hands).intent);
    geometry.Stab();CHECK(!geometry.emitted); // Lost geometry cannot retain old settled motion.
    geometry.Rest();geometry.Stab();CHECK(geometry.emitted==1);
    Fixture fallback;fallback.Rest();fallback.s.geometryCurrent=false;
    CHECK(!fallback.policy.Update(fallback.s,fallback.hands).intent);
    fallback.Step(0,1);CHECK(fallback.emitted==1&&fallback.last->cause==SelectedMeleeCause::Trigger);
    return 0;
}
int ObservationReportsOnlyEvaluatedMotion(){
    Fixture f;f.Rest();SelectedMeleeObservation observation;
    observation.motionUpdated=true;observation.motion.decision=MeleeStrokeDecision::Intent;
    CHECK(f.policy.Update(f.s,f.hands,&observation).reason==SelectedMeleeReason::Duplicate);
    CHECK(!observation.motionUpdated&&observation.motion.decision==MeleeStrokeDecision::None&&
        observation.armedBefore&&observation.armedAfter&&observation.pendingAfter==SelectedMeleePending::None);
    ++f.s.motion.source.sequence;f.s.motion.source.observedNs+=10*ms;
    f.s.motion.source.nowNs=f.s.motion.source.observedNs;f.s.motion.source.deadlineNs=f.s.motion.source.nowNs+100*ms;
    f.s.motion.tip.z+=.012f;f.Refresh();
    const auto result=f.policy.Update(f.s,f.hands,&observation);
    CHECK(!result.intent&&observation.motionUpdated&&observation.motion.metrics&&
        observation.motion.decision==MeleeStrokeDecision::Travel&&observation.motion.travel>.011f);
    f.s.trigger=1;CHECK(f.policy.Update(f.s,f.hands,&observation).reason==SelectedMeleeReason::Invalid);
    CHECK(!observation.motionUpdated&&!observation.armedAfter&&!f.policy.Pending());
    return 0;
}
struct TimingFixture {
    MeleeResponseTiming timing;std::int64_t now=1000*ms;std::uint64_t invocation=0;
    MeleeTimingIntent intent{{1,2,3,4,5,{11,12,13}},1,1000*ms,1100*ms,{.5f,.05f,60.f/52.f}};
    TimingFixture(){(void)timing.Begin(intent,now);}
    MeleeTimingBoundary Boundary(unsigned branch,MeleeTimingPhase phase=MeleeTimingPhase::WindupPrepared){
        return {intent.owner,intent.serial,++invocation,timing.Revision(branch),branch,now,now+100*ms,
            phase,phase==MeleeTimingPhase::WindupPrepared?.5f:intent.contract.totalCycleSeconds-.5f,.011f,true,true};
    }
    std::optional<MeleeTimingPlan> Advance(unsigned branch){return timing.Prepare(Boundary(branch),now);}
    bool Applied(const MeleeTimingPlan& p){return timing.Resolve(p,MeleeTimingWrite::Applied,p.replacementSeconds,now);}
};
int TimingThreeCopiesPreserveRecoveryAfterGestureEnds(){
    TimingFixture f;
    for(unsigned n=0;n<3;++n){const auto p=f.Advance(n);CHECK(p&&p->expectedSeconds==.5f&&p->replacementSeconds==.05f);
        CHECK(f.Applied(*p)&&f.timing.Stage(n)==MeleeTimingStage::Advanced);
        CHECK(!f.timing.Resolve(*p,MeleeTimingWrite::Applied,p->replacementSeconds,f.now));}
    CHECK(!f.timing.Complete());f.timing.RevokeAdmission();f.now+=2000*ms;
    // Gesture completion, tracking loss and its old 150 ms deadline cannot
    // erase an already applied native timing obligation.
    for(unsigned n=0;n<3;++n){auto b=f.Boundary(n,MeleeTimingPhase::RecoveryPrepared);
        const auto p=f.timing.Prepare(b,f.now);CHECK(p);
        CHECK(std::abs(p->replacementSeconds+.05f-f.intent.contract.totalCycleSeconds)<.000001f);
        CHECK(f.Applied(*p));CHECK(!f.timing.Prepare(b,f.now));
        CHECK(!f.timing.Prepare(f.Boundary(n,MeleeTimingPhase::RecoveryPrepared),f.now));}
    CHECK(f.timing.Complete());CHECK(!f.timing.Begin(f.intent,f.now));
    ++f.intent.serial;f.intent.observedNs=f.now;f.intent.deadlineNs=f.now+100*ms;
    CHECK(f.timing.Begin(f.intent,f.now));return 0;
}
int TimingHitchesAndInvalidBoundariesStayStock(){
    for(float delta:{.05f,.1f,.25f,1.f}){TimingFixture f;auto b=f.Boundary(0);b.deltaSeconds=delta;
        CHECK(!f.timing.Prepare(b,f.now)&&f.timing.Stage(0)==MeleeTimingStage::Stock);
        CHECK(!f.Advance(0));}
    for(unsigned fault=0;fault<10;++fault){TimingFixture f;auto b=f.Boundary(0);
        if(fault==0)++b.owner.actorGeneration;if(fault==1)++b.owner.itemGeneration;
        if(fault==2)++b.owner.branches[0];if(fault==3)++b.owner.cohortGeneration;
        if(fault==4)b.originalBoundaryVerified=false;if(fault==5)b.ownerRetained=false;
        if(fault==6)b.deadlineNs=f.now;if(fault==7)b.timerSeconds=std::numeric_limits<float>::quiet_NaN();
        if(fault==8)++b.revision;if(fault==9)b.deltaSeconds=0;
        CHECK(!f.timing.Prepare(b,f.now)&&f.timing.Stage(0)==MeleeTimingStage::Native);}
    TimingFixture zero;zero.intent.contract.retainedWindupSeconds=0;
    CHECK(zero.timing.Retire(zero.intent.owner,true));++zero.intent.serial;
    CHECK(!zero.timing.Begin(zero.intent,zero.now));return 0;
}
int TimingUncertainWritesRetainDebtAndNeedExactRetirement(){
    TimingFixture f;const auto p=f.Advance(0);CHECK(p);auto wrong=*p;++wrong.invocation;
    CHECK(!f.timing.Resolve(wrong,MeleeTimingWrite::Applied,wrong.replacementSeconds,f.now));
    CHECK(!f.Advance(0));CHECK(!f.timing.Resolve(*p,MeleeTimingWrite::Applied,p->replacementSeconds,p->deadlineNs));
    CHECK(f.timing.Stage(0)==MeleeTimingStage::Unknown&&!f.timing.Complete());
    f.timing.RevokeAdmission();f.now+=10000*ms;
    CHECK(!f.timing.Prepare(f.Boundary(0,MeleeTimingPhase::RecoveryPrepared),f.now));
    auto wrongOwner=f.intent.owner;++wrongOwner.actorGeneration;
    CHECK(!f.timing.Retire(wrongOwner,true));CHECK(!f.timing.Retire(f.intent.owner,false));
    CHECK(f.timing.Retire(f.intent.owner,true));CHECK(!f.timing.Resolve(*p,MeleeTimingWrite::Applied,p->replacementSeconds,f.now));
    TimingFixture failed;const auto attempt=failed.Advance(1);CHECK(attempt);
    CHECK(failed.timing.Resolve(*attempt,MeleeTimingWrite::NotApplied,attempt->expectedSeconds,failed.now));
    CHECK(failed.timing.Stage(1)==MeleeTimingStage::Native);return 0;
}
int TimingPredictionCopyCarriesExactDebtWithoutDoublePay(){
    TimingFixture f;const auto advance=f.Advance(1);CHECK(advance&&f.Applied(*advance));
    std::array<std::uint32_t,16> payload{};payload[3]=0x3d23d70a;payload[7]=42;
    auto exported=f.Boundary(1);exported.timerSeconds=.04f;
    const auto snapshot=f.timing.Capture(exported,444,payload,f.now);CHECK(snapshot);
    CHECK(!f.timing.Capture(exported,444,payload,f.now));
    auto restored=f.Boundary(0);restored.timerSeconds=.04f;auto changed=payload;++changed[15];
    CHECK(!f.timing.Restored(restored,*snapshot,444,changed,f.now));
    CHECK(!f.timing.Restored(restored,*snapshot,445,payload,f.now));
    CHECK(f.timing.Restored(restored,*snapshot,444,payload,f.now));
    CHECK(f.timing.Revision(0)==2&&f.timing.Stage(0)==MeleeTimingStage::Advanced);
    CHECK(!f.timing.Restored(restored,*snapshot,444,payload,f.now));
    const auto paid=f.timing.Prepare(f.Boundary(0,MeleeTimingPhase::RecoveryPrepared),f.now);CHECK(paid&&f.Applied(*paid));
    // A real prediction rewind can restore earlier, unpaid native state. Only
    // the exact retained copy receipt permits a new revision to pay it again.
    restored=f.Boundary(0);restored.timerSeconds=.04f;
    CHECK(f.timing.Restored(restored,*snapshot,444,payload,f.now)&&f.timing.Revision(0)==3);
    CHECK(!f.timing.Resolve(*paid,MeleeTimingWrite::Applied,paid->replacementSeconds,f.now));
    const auto replay=f.timing.Prepare(f.Boundary(0,MeleeTimingPhase::RecoveryPrepared),f.now);
    CHECK(replay&&f.Applied(*replay)&&replay->replacementSeconds==paid->replacementSeconds);
    // No pointer-only provenance when a ring slot has been overwritten.
    for(unsigned n=0;n<8;++n){auto b=f.Boundary(1);b.timerSeconds=.03f;CHECK(f.timing.Capture(b,444,payload,f.now));}
    CHECK(!f.timing.Restored(f.Boundary(2),*snapshot,444,payload,f.now));return 0;
}
int SharedHoldAndIdentityRequired(){
    for(unsigned fault=0;fault<5;++fault){Fixture f;f.Rest();
        if(fault==0)++f.s.motion.hold.id;
        if(fault==1)++f.s.identity.item.generation;
        if(fault==2)f.s.selected=false;
        if(fault==3)f.s.identity.binding={};
        if(fault==4)f.s.motion.hold.prerequisiteClaim=7;
        f.s.trigger=1;CHECK(!f.policy.Update(f.s,f.hands).intent);
    }
    for(unsigned field=0;field<4;++field){Fixture f;f.Rest();
        if(field==0)++f.s.identity.inputEpoch;
        if(field==1)++f.s.identity.binding.generation;
        if(field==2)++f.s.identity.owner.space;
        if(field==3)++f.s.identity.owner.equipGeneration;
        f.Step(0,1);CHECK(!f.emitted);f.Step(0,1);CHECK(!f.emitted);
        f.Step();f.Step(0,1);CHECK(f.emitted==1);
    }
    Fixture clock;clock.Rest();++clock.s.motion.source.sequence;clock.s.trigger=1;clock.Refresh();
    CHECK(clock.policy.Update(clock.s,clock.hands).reason==SelectedMeleeReason::Order);
    CHECK(!clock.policy.Pending());return 0;
}
}
int main(){return OneMotionIntentAndExactOutcome()||SingleJabWithdrawalAndRejab()||
    IncidentalMotionAndReloadClaimsCannotFinishAJab()||TriggerAndMotionShareOneLatch()||TriggerFallbackWithoutGeometry()||
    DrawRegripReconnectCannotAttack()||PendingSurvivesInvalidationAndNeverOwnsHands()||DuplicateStaleAndChangedInputCannotRepeat()||
    NativeReadinessNeverCompletesAnAttack()||RepeatedPacketUsesItsFirstGeometry()||DuplicateAuthorityLossRequiresFreshRecovery()||
    ObservationReportsOnlyEvaluatedMotion()||SharedHoldAndIdentityRequired()||
    TimingThreeCopiesPreserveRecoveryAfterGestureEnds()||TimingHitchesAndInvalidBoundariesStayStock()||
    TimingUncertainWritesRetainDebtAndNeedExactRetirement()||TimingPredictionCopyCarriesExactDebtWithoutDoublePay();}
