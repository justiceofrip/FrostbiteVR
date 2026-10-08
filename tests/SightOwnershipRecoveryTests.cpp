#include "Test.h"
#include "fvr/interaction/SightOwnershipRecovery.h"
#include "fvr/interaction/SightFlip.h"
#include <limits>
using namespace fvr::interaction;
namespace {
constexpr std::int64_t ms=1000000;
constexpr HandInteractionKey item{100,1};
struct Fixture {
    HandInteraction arbiter;
    HandInteractionSample current{{10,1,1,1},1,1000*ms,1150*ms,1000*ms,true,{true,true}};
    HandInteractionSample original=current;
    std::optional<HandClaim> before;
    HandClaimToken gun{};
    std::uint64_t intent=0;
    Fixture(std::int64_t gunLease=100*ms,std::int64_t sightLease=100*ms){
        arbiter.Update(current);
        const auto right=arbiter.Acquire(current,Request(InteractionHand::Right,HandClaimKind::GunHold,1,current.nowNs+gunLease));
        if(right.claim)gun=right.claim->token;
        const auto left=arbiter.Acquire(current,Request(InteractionHand::Left,HandClaimKind::Sight,3,current.nowNs+sightLease,gun.id));
        before=left.claim;
    }
    HandClaimRequest Request(InteractionHand hand,HandClaimKind kind,std::uint64_t contact,std::int64_t deadline,std::uint64_t parent=0){
        return {current.owner,hand,kind,item,{{contact,1},current.sequence,deadline,true},++intent,parent};
    }
    HandInteractionResult Advance(std::int64_t delta=110*ms){
        current.nowNs+=delta;current.observedNs=current.nowNs;current.deadlineNs=current.nowNs+150*ms;++current.sequence;
        return arbiter.Update(current);
    }
    bool Eligible(const HandInteractionResult& result)const{
        return SightLeaseContinuationEligible(before,result,current,item,false,.8f);
    }
};
int ExactLeasePaths(){
    for(unsigned path=0;path<3;++path){
        Fixture f(path==0?140*ms:100*ms,path==1?140*ms:100*ms);CHECK(f.before&&f.gun.id);
        const auto out=f.Advance();CHECK(f.Eligible(out));
        if(path==0)CHECK(f.arbiter.Current(InteractionHand::Right));
        else CHECK(!f.arbiter.Current(InteractionHand::Right));
        CHECK(!f.arbiter.Current(InteractionHand::Left));
    }
    Fixture notExpired(140*ms,140*ms);CHECK(notExpired.before);
    CHECK(!notExpired.Eligible(notExpired.Advance()));
    return 0;
}
int NewReservationAndStrictOldEvidence(){
    Fixture f(100*ms,140*ms);CHECK(f.before);const auto old=*f.before;
    const auto lifecycle=f.Advance();CHECK(f.Eligible(lifecycle));
    const auto gun=f.arbiter.Acquire(f.current,f.Request(InteractionHand::Right,HandClaimKind::GunHold,1,f.current.deadlineNs));
    CHECK(gun.accepted&&gun.claim);
    auto request=f.Request(InteractionHand::Left,HandClaimKind::Sight,3,f.current.deadlineNs,gun.claim->token.id);
    request.contact.inputSequence=f.original.sequence;request.contact.deadlineNs=f.original.deadlineNs;
    // The new helper does not authorize historical geometry past dependency loss.
    auto rejected=f.arbiter.AcquireFrom(f.current,f.original,request);
    CHECK(!rejected.accepted&&!f.arbiter.Current(InteractionHand::Left));
    request=f.Request(InteractionHand::Left,HandClaimKind::Sight,3,f.current.deadlineNs,gun.claim->token.id);
    const auto reserved=f.arbiter.Acquire(f.current,request);
    CHECK(reserved.accepted&&reserved.claim&&reserved.claim->token.id>old.token.id);
    CHECK(reserved.claim->inputSequence==f.current.sequence&&reserved.claim->deadlineNs==f.current.deadlineNs);
    CHECK(!f.arbiter.Release(f.current,old.token).accepted);
    CHECK(f.arbiter.Current(InteractionHand::Left)->token==reserved.claim->token);
    return 0;
}
int OtherCancellationCauses(){
    for(unsigned cause=0;cause<5;++cause){
        Fixture f(140*ms,140*ms);CHECK(f.before);
        HandInteractionResult out;
        if(cause==0){f.current.tracked[0]=false;out=f.arbiter.Update(f.current);}
        if(cause==1){f.current.tracked[1]=false;out=f.arbiter.Update(f.current);}
        if(cause==2){f.current.released[0]=true;out=f.arbiter.Update(f.current);}
        if(cause==3)out=f.arbiter.Release(f.current,f.gun);
        if(cause==4)out=f.arbiter.Reset();
        CHECK(!f.Eligible(out));
        f.current.tracked={true,true};f.current.released={};
        // Restoring current safety cannot disguise the recorded cancellation.
        CHECK(!f.Eligible(out));
    }
    for(unsigned field=0;field<4;++field){
        Fixture f;CHECK(f.before);f.current.nowNs+=10*ms;f.current.observedNs=f.current.nowNs;
        f.current.deadlineNs=f.current.nowNs+150*ms;++f.current.sequence;
        if(field==0)++f.current.owner.actor;
        if(field==1)++f.current.owner.actorGeneration;
        if(field==2)++f.current.owner.equipGeneration;
        if(field==3)++f.current.owner.space;
        CHECK(!f.Eligible(f.arbiter.Update(f.current)));
    }
    return 0;
}
int CurrentSafetyAndExactIdentity(){
    Fixture f;CHECK(f.before);const auto lifecycle=f.Advance();CHECK(f.Eligible(lifecycle));
    for(unsigned bad=0;bad<14;++bad){
        auto current=f.current;auto physical=item;bool cancelling=false;float squeeze=.8f;
        if(bad==0)current.focused=false;
        if(bad==1)current.tracked[0]=false;
        if(bad==2)current.tracked[1]=false;
        if(bad==3)current.released[0]=true;
        if(bad==4)current.released[1]=true;
        if(bad==5)cancelling=true;
        if(bad==6)squeeze=.35f;
        if(bad==7)squeeze=std::numeric_limits<float>::quiet_NaN();
        if(bad==8)squeeze=std::numeric_limits<float>::infinity();
        if(bad==9)squeeze=1.1f;
        if(bad==10)++physical.generation;
        if(bad==11)current.deadlineNs=current.nowNs;
        if(bad==12)current.observedNs=current.nowNs+1;
        if(bad==13)current.sequence=0;
        CHECK(!SightLeaseContinuationEligible(f.before,lifecycle,current,physical,cancelling,squeeze));
    }
    for(unsigned bad=0;bad<5;++bad){
        auto prior=f.before;
        if(bad==0)prior->token.kind=HandClaimKind::WeaponSupport;
        if(bad==1)prior->token.hand=InteractionHand::Right;
        if(bad==2)++prior->token.prerequisiteClaim;
        if(bad==3)prior->token.contact.generation=0;
        if(bad==4)prior->inputSequence=f.current.sequence+1;
        CHECK(!SightLeaseContinuationEligible(prior,lifecycle,f.current,item,false,.8f));
    }
    CHECK(!SightLeaseContinuationEligible({},lifecycle,f.current,item,false,.8f));
    return 0;
}
int TrialAcknowledgementDenialKeepsCancellationIdentity(){
    SightFlipConfig config;config.axis={0,0,1};config.grabRadiusMeters=.05f;config.holdRadiusMeters=.15f;
    config.minLeverMeters=.02f;config.thresholdRadians=.4f;config.hysteresisRadians=.08f;
    config.maxStepRadians=.7f;config.detentHoldNs=20*ms;config.gestureTimeoutNs=2000*ms;
    config.ackTimeoutNs=1000*ms;config.maxSampleGapNs=250*ms;
    SightFlip original(config);SightFlipSample sample;
    sample.owner={10,1,item.id,1};sample.sequence=1;sample.nowNs=1000*ms;
    sample.focused=sample.tracked=sample.contactValid=sample.nativeModeValid=true;
    sample.handLocalMeters={.1f,0,0};original.Update(sample); // Real neutral.
    ++sample.sequence;sample.nowNs+=10*ms;sample.squeeze=.8f;
    CHECK(original.Update(sample).grabbed);
    ++sample.sequence;sample.nowNs+=10*ms;
    sample.handLocalMeters={.1f*std::cos(.5f),.1f*std::sin(.5f),0};
    CHECK(!original.Update(sample).request);
    ++sample.sequence;sample.nowNs+=25*ms;const auto requested=original.Update(sample);
    CHECK(requested.request&&requested.phase==SightFlipPhase::AwaitingAcknowledgement);
    const auto requestId=requested.request->id;
    Fixture ownership;CHECK(ownership.before);const auto lifecycle=ownership.Advance();
    CHECK(ownership.Eligible(lifecycle)); // Lease-only recovery is otherwise eligible.
    auto acknowledged=sample;acknowledged.nowNs=ownership.current.nowNs;++acknowledged.sequence;
    acknowledged.nativeMode=SightMode::Secondary;acknowledged.acknowledgedRequest=requestId;
    acknowledged.contactValid=false; // Native mode switched before new geometry.
    auto wrongAck=acknowledged;wrongAck.acknowledgedRequest=requestId+1;
    auto wrongTrial=original;const auto wrong=wrongTrial.Update(wrongAck);
    CHECK(!wrong.committedMode&&wrong.cancelled&&wrong.cancelledRequest==requestId);
    CHECK(!(ownership.Eligible(lifecycle)&&wrong.committedMode));
    auto duplicate=acknowledged;duplicate.sequence=sample.sequence;
    auto duplicateTrial=original;const auto duplicateResult=duplicateTrial.Update(duplicate);
    CHECK(!duplicateResult.committedMode&&duplicateResult.phase==SightFlipPhase::AwaitingAcknowledgement);
    auto trial=original;const auto committed=trial.Update(acknowledged);
    CHECK(committed.committedMode==SightMode::Secondary&&!committed.request);
    // Commit was only a trial. Another valid owner occupies the hand, so a
    // strict new reservation cannot steal it even after a genuine native ack.
    auto gun=ownership.arbiter.Acquire(ownership.current,
        ownership.Request(InteractionHand::Right,HandClaimKind::GunHold,1,ownership.current.deadlineNs));
    CHECK(gun.claim);
    auto ammoRequest=ownership.Request(InteractionHand::Left,HandClaimKind::AmmoObject,5,ownership.current.deadlineNs);
    ammoRequest.item={200,1};auto ammo=ownership.arbiter.Acquire(ownership.current,ammoRequest);CHECK(ammo.claim);
    const auto reservation=ownership.arbiter.Acquire(ownership.current,
        ownership.Request(InteractionHand::Left,HandClaimKind::Sight,3,ownership.current.deadlineNs,gun.claim->token.id));
    CHECK(!reservation.accepted&&ownership.arbiter.Current(InteractionHand::Left)->token==ammo.claim->token);
    // Cancelling Latched would have lost requestId. Cancel the original pending
    // state, and clear the adapter's acknowledgement when discarding the trial.
    std::uint64_t adapterAck=requestId;
    auto denied=original;const auto cancellation=denied.Update({});adapterAck=0;
    CHECK(cancellation.cancelled&&cancellation.released&&cancellation.cancelledRequest==requestId);
    CHECK(!cancellation.committedMode&&!cancellation.request&&cancellation.phase==SightFlipPhase::Idle);
    original=denied;acknowledged.acknowledgedRequest=adapterAck;++acknowledged.sequence;acknowledged.nowNs+=10*ms;
    acknowledged.contactValid=true;
    const auto held=original.Update(acknowledged);
    CHECK(!held.grabbed&&!held.committedMode&&!held.request&&held.phase==SightFlipPhase::Idle);
    CHECK(ownership.arbiter.Current(InteractionHand::Left)->token==ammo.claim->token);
    return 0;
}
int ReleaseRecordProvenance(){
    Fixture f(100*ms,140*ms);CHECK(f.before);const auto observed=f.Advance();CHECK(f.Eligible(observed));
    for(unsigned bad=0;bad<9;++bad){
        auto out=observed;
        std::optional<HandClaimRelease>* parent=nullptr;
        std::optional<HandClaimRelease>* sight=nullptr;
        for(auto& event:out.released)if(event){
            if(event->token==f.before->token)sight=&event;else parent=&event;
        }
        CHECK(parent&&sight);
        if(bad==0)++(*parent)->token.id;
        if(bad==1)(*parent)->reason=HandInteractionReason::Released;
        if(bad==2)(*parent)->reason=HandInteractionReason::TrackingLost;
        if(bad==3)(*parent)->token.kind=HandClaimKind::AmmoObject;
        if(bad==4)parent->reset();
        if(bad==5)(*sight)->reason=HandInteractionReason::LeaseExpired; // Its own lease has NOT expired.
        if(bad==6)out.reason=HandInteractionReason::IdentityChanged;
        if(bad==7)out.accepted=true;
        if(bad==8)out.inputValid=false;
        CHECK(!f.Eligible(out));
    }
    Fixture direct(140*ms,100*ms);CHECK(direct.before);auto out=direct.Advance();CHECK(direct.Eligible(out));
    // A direct sight timeout plus an unrelated/tracking cancellation is not the narrow recovery.
    for(auto& event:out.released)if(!event){event=HandClaimRelease{direct.gun,HandInteractionReason::TrackingLost};break;}
    CHECK(!direct.Eligible(out));
    return 0;
}
}
int main(){
    if(ExactLeasePaths()||NewReservationAndStrictOldEvidence()||OtherCancellationCauses()||
       CurrentSafetyAndExactIdentity()||TrialAcknowledgementDenialKeepsCancellationIdentity()||
       ReleaseRecordProvenance())return 1;
    return 0;
}
