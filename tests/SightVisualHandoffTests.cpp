#include "Test.h"
#include "fvr/interaction/SightVisualHandoff.h"
#include "fvr/interaction/SightFlipPackets.h"
#include <limits>
using namespace fvr;using namespace interaction;
namespace {
constexpr float halfPi=1.5707963267948966f;
math::Matrix4 At(float x=.032f,float y=.054f,float z=-.585f){
    math::Matrix4 m{};for(unsigned n=0;n<4;++n)m.values[n][n]=1;
    m.values[3][0]=x;m.values[3][1]=y;m.values[3][2]=z;return m;
}
math::Matrix4 Rotate(const math::Matrix4& m,float angle){
    return *RotateAboutHinge(m,{m.values[3][0],m.values[3][1],m.values[3][2]},
        {m.values[0][0],m.values[0][1],m.values[0][2]},angle);
}
int MonotonicNativeHandoff(){
    for(auto mode:{SightMode::Primary,SightMode::Secondary}){
        const float sign=mode==SightMode::Primary?1.f:-1.f;
        const auto baseline=mode==SightMode::Primary?At():Rotate(At(),halfPi);
        auto visual=SightVisualHandoff::Begin(baseline,mode);CHECK(visual);std::int64_t time=1000000000;
        const auto step=[&](SightFlipPhase phase,float gesture,const std::optional<math::Matrix4>& native=std::nullopt){return visual->Update(phase,gesture,time+=20000000,native);};
        auto result=step(SightFlipPhase::Manipulating,sign*.4f);CHECK(result&&Near(result->appliedRadians,sign*.4f));
        result=step(SightFlipPhase::Manipulating,sign*.3f);CHECK(result&&Near(result->appliedRadians,sign*.3f));
        result=step(SightFlipPhase::AwaitingAcknowledgement,sign*.5f,baseline);
        CHECK(result&&result->nativeObserved&&Near(result->appliedRadians,sign*.5f));
        // Waiting and acknowledgements cannot invent native motion.
        for(unsigned n=0;n<100;++n){result=step(SightFlipPhase::Latched,sign*.5f,baseline);CHECK(result&&Near(result->appliedRadians,sign*.5f)&&Near(result->nativeProgressRadians,0));}
        result=step(SightFlipPhase::Latched,sign*.5f,Rotate(baseline,sign*.8f));
        CHECK(result&&result->nativeObserved&&sign*result->appliedRadians>.5f&&sign*result->appliedRadians<.8f);
        for(unsigned n=0;n<10;++n)result=step(SightFlipPhase::Latched,sign*.5f,Rotate(baseline,sign*.8f));
        CHECK(result&&Near(result->appliedRadians,sign*.8f));
        result=step(SightFlipPhase::Latched,sign*.5f,Rotate(baseline,sign*.7f));CHECK(result&&Near(result->appliedRadians,sign*.8f));
        result=step(SightFlipPhase::Latched,sign*.5f);CHECK(result&&Near(result->appliedRadians,sign*.8f)&&!result->nativeObserved);
        for(unsigned n=0;n<20;++n)result=step(SightFlipPhase::Latched,sign*.5f,Rotate(baseline,sign*halfPi));
        CHECK(result&&Near(result->appliedRadians,sign*halfPi));
        CHECK(!step(SightFlipPhase::Manipulating,sign*.2f));CHECK(!step(SightFlipPhase::Idle,0));
    }
    return 0;
}
int GraspAndNonAxisAlignedGeometry(){
    const auto baseline=*RotateAboutHinge(At(),{0,0,0},{0,1,0},.7f);
    const math::Vec3 pivot{baseline.values[3][0],baseline.values[3][1],baseline.values[3][2]};
    const math::Vec3 axis{baseline.values[0][0],baseline.values[0][1],baseline.values[0][2]};
    const auto graspPoint=sight_grasp_detail::Point({0,0,-.09f},baseline);const math::Vec3 palm{.01f,.025f,.06f};
    const auto grasp=SightGraspBinding::Begin(pivot,axis,baseline,At(),graspPoint,palm);CHECK(grasp);
    auto visual=SightVisualHandoff::Begin(baseline,SightMode::Primary);CHECK(visual);std::int64_t time=1000000000;
    CHECK(visual->Update(SightFlipPhase::Manipulating,.5f,time));float previous=.5f;
    for(float nativeAngle:{0.f,.2f,.8f,1.2f,halfPi}){
        for(unsigned n=0;n<20;++n){
            const auto result=visual->Update(SightFlipPhase::Latched,.5f,time+=16000000,Rotate(baseline,nativeAngle));CHECK(result&&result->nativeObserved);
            CHECK(result->appliedRadians>=previous&&result->appliedRadians-previous<=SightVisualHandoff::MaxNativeRadiansPerSecond*.016f+.00001f);previous=result->appliedRadians;
            const auto posed=grasp->Evaluate(result->appliedRadians,SightMode::Primary);CHECK(posed);
            const auto contact=sight_grasp_detail::Point(palm,posed->wrist);
            CHECK(Near(contact.x,posed->graspPoint.x)&&Near(contact.y,posed->graspPoint.y)&&Near(contact.z,posed->graspPoint.z));
        }
        CHECK(Near(previous,std::max(.5f,nativeAngle)));
    }
    return 0;
}
int RecordedJumpAndClockLimits(){
    // Dense closing evidence: applied-.757346392 at generation50538 and
    // -1.53541183 at50541 moved the fixed weapon-local wrist100.99mm. The
    // runtime supplies actual predicted time; no frame-rate estimate is used.
    for(unsigned hz:{30u,72u,144u})for(auto mode:{SightMode::Primary,SightMode::Secondary}){
        const float sign=mode==SightMode::Primary?1.f:-1.f;
        const auto baseline=mode==SightMode::Primary?At():Rotate(At(),halfPi);
        auto visual=SightVisualHandoff::Begin(baseline,mode);CHECK(visual);std::int64_t time=1000000000,dt=1000000000/hz;
        constexpr float gesture=.757346392f,native=1.53541183f;
        CHECK(visual->Update(SightFlipPhase::Manipulating,sign*gesture,time));float previous=gesture;
        for(unsigned n=0;n<hz;++n){
            const auto result=visual->Update(SightFlipPhase::Latched,sign*gesture,time+=dt,Rotate(baseline,sign*native));CHECK(result);
            const float progress=sign*result->appliedRadians,advance=progress-previous;
            CHECK(advance>=-.000001f&&advance<=SightVisualHandoff::MaxNativeStepRadians+.000001f);
            CHECK(advance<=SightVisualHandoff::MaxNativeRadiansPerSecond*float(double(dt)*1e-9)+.000001f);
            // At a133mm wrist lever, the fixed-grasp arc stays below11.7mm per
            // update instead of the recorded100.99mm native-driven movement.
            CHECK(2*.133f*std::sin(advance*.5f)<.0117f);previous=progress;
        }
        CHECK(Near(previous,native));
    }
    auto visual=SightVisualHandoff::Begin(At(),SightMode::Primary);CHECK(visual);std::int64_t time=1000000000;
    CHECK(visual->Update(SightFlipPhase::Manipulating,.5f,time));
    auto result=visual->Update(SightFlipPhase::Latched,.5f,time,Rotate(At(),halfPi));CHECK(result&&Near(result->appliedRadians,.5f));
    for(unsigned n=0;n<100;++n){result=visual->Update(SightFlipPhase::Latched,.5f,time,Rotate(At(),halfPi));CHECK(result&&Near(result->appliedRadians,.5f));}
    result=visual->Update(SightFlipPhase::Latched,.5f,time+=2000000000,Rotate(At(),halfPi));CHECK(result&&Near(result->appliedRadians,.5f+SightVisualHandoff::MaxNativeStepRadians));
    const float held=result->appliedRadians;
    CHECK(!visual->Update(SightFlipPhase::Latched,.5f,time-1,Rotate(At(),halfPi)));
    CHECK(!visual->Update(SightFlipPhase::Latched,.5f,0,Rotate(At(),halfPi)));
    for(unsigned n=0;n<100;++n){result=visual->Update(SightFlipPhase::Latched,.5f,time+=16000000);CHECK(result&&Near(result->appliedRadians,held)&&!result->nativeObserved);}
    return 0;
}
int RawContinuationAfterAcknowledgement(){
    // Observed opening/closing paths pass the90-degree stop while the old
    // visual stayed at its45-degree request angle. Use a measured-at-grab
    // palm in this fixture; runtime never reconstructs it from telemetry.
    for(auto mode:{SightMode::Primary,SightMode::Secondary}){
        const float sign=mode==SightMode::Primary?1.f:-1.f;
        const auto baseline=mode==SightMode::Primary?At():Rotate(At(),halfPi);
        const math::Vec3 pivot{baseline.values[3][0],baseline.values[3][1],baseline.values[3][2]},palm{.015f,.025f,.07f};
        auto raw=At(pivot.x-palm.x,pivot.y-palm.y,pivot.z-.09f-palm.z);
        if(mode==SightMode::Secondary)raw=*RotateAboutHinge(raw,pivot,{1,0,0},halfPi);
        const auto moved=[&](float angle){return *RotateAboutHinge(raw,pivot,{1,0,0},sign*angle);};
        auto visual=SightVisualHandoff::Begin(baseline,mode,SightVisualHandCapture{raw,palm,10});CHECK(visual);
        std::int64_t time=1000000000;std::uint64_t generation=10;
        const auto source=[&](float angle){++generation;return SightVisualRawHand::Fresh(moved(angle),generation,generation,time,time+100000000);};
        auto result=visual->Update(SightFlipPhase::Manipulating,sign*.3f,time,baseline,source(.3f));CHECK(result&&!result->rawObserved&&Near(sign*result->appliedRadians,.3f));
        result=visual->Update(SightFlipPhase::Manipulating,sign*.2f,time+=16000000,baseline,source(.2f));CHECK(result&&Near(sign*result->appliedRadians,.2f));
        result=visual->Update(SightFlipPhase::AwaitingAcknowledgement,sign*.79f,time+=16000000,baseline,source(1.1f));
        CHECK(result&&!result->rawObserved&&Near(sign*result->appliedRadians,.79f)); // No visual continuation before exact native acknowledgement.
        const auto pose=SightGraspBinding::Begin(pivot,{1,0,0},baseline,raw,sight_grasp_detail::Point(palm,raw),palm);CHECK(pose);
        for(float rawAngle:{.92f,1.12f,1.27f,1.38f,1.52f,1.67f,2.2f,2.98f}){
            auto sample=source(rawAngle);
            result=visual->Update(SightFlipPhase::Latched,sign*.79f,time+=16000000,baseline,sample);
            CHECK(result&&result->rawObserved&&Near(sign*result->appliedRadians,std::min(rawAngle,halfPi)));
            CHECK(result->nativeObserved&&Near(result->nativeProgressRadians,0)); // Raw motion advances while native geometry has not moved.
            const auto shown=pose->Evaluate(result->appliedRadians,mode);CHECK(shown);
            const auto contact=sight_grasp_detail::Point(palm,shown->wrist);
            CHECK(Near(contact.x,shown->graspPoint.x)&&Near(contact.y,shown->graspPoint.y)&&Near(contact.z,shown->graspPoint.z));
        }
        result=visual->Update(SightFlipPhase::Latched,sign*.79f,time+=16000000,baseline,source(.8f));
        CHECK(result&&Near(sign*result->appliedRadians,halfPi)); // Committed mode remains at stop when the same grasp reverses.
        CHECK(!visual->Update(SightFlipPhase::Idle,0,time+=16000000,baseline,source(1.f)));
    }
    return 0;
}
int RawSourceFreshnessAndNoVisualFeedback(){
    const math::Vec3 pivot{.032f,.054f,-.585f},palm{.015f,.025f,.07f};
    const auto raw=At(pivot.x-palm.x,pivot.y-palm.y,pivot.z-.09f-palm.z);
    const auto moved=[&](float angle){return *RotateAboutHinge(raw,pivot,{1,0,0},angle);};
    auto visual=SightVisualHandoff::Begin(At(),SightMode::Primary,SightVisualHandCapture{raw,palm,10});CHECK(visual);
    std::int64_t time=1000000000;
    CHECK(visual->Update(SightFlipPhase::Manipulating,.5f,time));
    auto sample=SightVisualRawHand::Fresh(moved(.8f),11,11,time,time+100000000);CHECK(sample);
    auto result=visual->Update(SightFlipPhase::Latched,.5f,time+=16000000,Rotate(At(),halfPi),sample);
    CHECK(result&&result->rawObserved&&Near(result->appliedRadians,.8f)); // Fresh raw target takes priority over fully-open native animation.
    sample->wrist=moved(1.2f);
    result=visual->Update(SightFlipPhase::Latched,.5f,time+=16000000,Rotate(At(),halfPi),sample);
    CHECK(result&&result->rawObserved&&Near(result->appliedRadians,.8f)); // Same source packet cannot advance via re-rendered geometry.
    for(auto stale:{0ull,9ull,10ull}){
        result=visual->Update(SightFlipPhase::Latched,.5f,time+=16000000,{},SightVisualRawHand{moved(1.2f),stale});
        CHECK(result&&!result->rawObserved&&Near(result->appliedRadians,.8f));
    }
    CHECK(!SightVisualRawHand::Fresh(moved(1.2f),12,11,time,time+100000000));
    CHECK(!SightVisualRawHand::Fresh(moved(1.2f),12,12,time,time));
    CHECK(!SightVisualRawHand::Fresh(moved(1.2f),12,12,time,time-1));
    CHECK(!SightVisualRawHand::Fresh(moved(1.2f),12,12,0,time));
    auto bad=moved(1.2f);bad.values[3][0]=std::numeric_limits<float>::quiet_NaN();
    CHECK(!SightVisualRawHand::Fresh(bad,12,12,time,time+100000000));
    result=visual->Update(SightFlipPhase::Latched,.5f,time+=16000000,{},SightVisualRawHand{bad,12});
    CHECK(result&&!result->rawObserved&&Near(result->appliedRadians,.8f));
    CHECK(!SightVisualHandoff::Begin(At(),SightMode::Primary,SightVisualHandCapture{raw,palm,0}));
    CHECK(!SightVisualHandoff::Begin(At(),SightMode::Primary,SightVisualHandCapture{bad,palm,10}));
    const auto atAxis=At(pivot.x-palm.x,pivot.y-palm.y,pivot.z-palm.z);
    CHECK(!SightVisualHandoff::Begin(At(),SightMode::Primary,SightVisualHandCapture{atAxis,palm,10}));
    // Displayed grasp placement is never fed back to policy. With the raw
    // controller unchanged and a new packet, visual geometry alone cannot move it.
    const auto shown=SightGraspBinding::Begin(pivot,{1,0,0},At(),raw,{pivot.x,pivot.y,pivot.z-.09f},palm);CHECK(shown);
    CHECK(shown->Evaluate(halfPi,SightMode::Primary));
    result=visual->Update(SightFlipPhase::Latched,.5f,time+=16000000,{},SightVisualRawHand{moved(.8f),12});
    CHECK(result&&result->rawObserved&&Near(result->appliedRadians,.8f));
    result=visual->Update(SightFlipPhase::Latched,.5f,time+=16000000,Rotate(At(),halfPi));
    CHECK(result&&!result->rawObserved&&result->appliedRadians>.8f&&result->appliedRadians<=.8f+SightVisualHandoff::MaxNativeStepRadians); // Bounded fallback remains available.
    return 0;
}
int InvalidNativeDoesNotManufactureProgress(){
    auto visual=SightVisualHandoff::Begin(At(),SightMode::Primary);CHECK(visual);
    const float nan=std::numeric_limits<float>::quiet_NaN();
    auto bad=At();bad.values[0][0]=nan;CHECK(!SightVisualHandoff::Begin(bad,SightMode::Primary));
    CHECK(!SightVisualHandoff::Begin(At(),static_cast<SightMode>(99)));
    std::int64_t time=1000000000;
    CHECK(!visual->Update(SightFlipPhase::Manipulating,nan,time));
    CHECK(!visual->Update(static_cast<SightFlipPhase>(99),0,time));
    for(unsigned variant=0;variant<7;++variant){
        auto source=Rotate(At(),.9f);
        if(variant==0)source.values[1][1]=nan;
        if(variant==1)source.values[3][0]+=.1f;
        if(variant==2)source=*RotateAboutHinge(At(),{.032f,.054f,-.585f},{0,1,0},.9f);
        if(variant==3)source=Rotate(At(),-.4f);
        if(variant==4)source=Rotate(At(),2.f);
        if(variant==5)source.values[0][0]=-source.values[0][0];
        if(variant==6)source.values[2][2]*=2;
        const auto result=visual->Update(SightFlipPhase::AwaitingAcknowledgement,.5f,time+=16000000,source);
        CHECK(result&&!result->nativeObserved&&Near(result->appliedRadians,.5f));
    }
    return 0;
}
struct LeaseFixture {
    HandInteraction ownership;
    HandInteractionSample sample{{1,2,3,4},1,1000000,101000000,1000000,true,{true,true},{false,false}};
    HandInteractionKey item{7,8};HandClaimToken gun{},sight{};
    bool Begin(){
        ownership.Update(sample);
        const auto right=ownership.Acquire(sample,{sample.owner,InteractionHand::Right,HandClaimKind::GunHold,item,{{1,1},1,sample.deadlineNs,true},1,0});
        if(!right.accepted)return false;gun=right.claim->token;
        const auto left=ownership.Acquire(sample,{sample.owner,InteractionHand::Left,HandClaimKind::Sight,item,{{3,8},1,sample.deadlineNs,true},1,gun.id});
        if(!left.accepted)return false;sight=left.claim->token;return true;
    }
    void Next(std::int64_t ms=20){++sample.sequence;sample.observedNs=sample.nowNs=ms*1000000;sample.deadlineNs=sample.nowNs+100000000;}
    SightFlipResult Latched(){SightFlipResult r;r.phase=SightFlipPhase::Latched;return r;}
};
int CurrentReservationKeepsTokenWithoutContact(){
    LeaseFixture f;CHECK(f.Begin());
    for(unsigned n=0;n<50;++n){
        f.Next(20+20*n);CHECK(f.ownership.Update(f.sample).inputValid);
        CHECK(f.ownership.Renew(f.sample,f.gun,{{1,1},f.sample.sequence,f.sample.deadlineNs,true}).accepted);
        CHECK(RenewLatchedSightReservation(f.ownership,f.sample,f.item,SightFlipPhase::Latched,f.Latched()));
        const auto held=f.ownership.Current(InteractionHand::Left);
        CHECK(held&&held->token==f.sight&&held->deadlineNs==f.sample.deadlineNs);
    }
    // Duplicate input cannot buy any more time even if caller tries a new deadline.
    const auto before=f.ownership.Current(InteractionHand::Left)->deadlineNs;
    f.sample.nowNs+=1000000;f.sample.deadlineNs+=1000000;
    RenewLatchedSightReservation(f.ownership,f.sample,f.item,SightFlipPhase::Latched,f.Latched());
    CHECK(f.ownership.Current(InteractionHand::Left)->deadlineNs==before);
    return 0;
}
int ReservationCancellationAndNoResurrection(){
    for(unsigned variant=0;variant<9;++variant){
        LeaseFixture f;CHECK(f.Begin());f.Next();auto result=f.Latched();
        auto previous=SightFlipPhase::Latched;auto item=f.item;
        if(variant==0)f.Next(120); // Both original claims have really expired.
        if(variant==1)f.sample.released[0]=true;
        if(variant==2)f.sample.tracked[0]=false;
        if(variant==3)f.sample.focused=false;
        if(variant==4)++f.sample.owner.space;
        if(variant==5)++item.id;
        if(variant==6)previous=SightFlipPhase::AwaitingAcknowledgement;
        if(variant==7)result.committedMode=SightMode::Secondary;
        if(variant==8)result.cancelled=true;
        f.ownership.Update(f.sample); // Runtime observes current safety before any reservation renewal.
        CHECK(!RenewLatchedSightReservation(f.ownership,f.sample,item,previous,result));
        if(variant<=4)CHECK(!f.ownership.Current(InteractionHand::Left)||variant==0);
    }
    return 0;
}
int NativeModeContactSequenceStaysIndependent(){
    SightFlipPackets packets;
    SightFlipInputPacket live;live.sample.owner={1,2,7,4};live.weapon=100;
    live.sample.sequence=1;live.sample.nowNs=100;live.sample.focused=live.sample.tracked=live.sample.nativeModeValid=true;
    live.sample.squeeze=0;live.deadline=1000;live.eligible=true;
    packets.Update(live,{},100);
    live.sample.sequence=2;live.sample.nowNs=200;live.sample.squeeze=.9f;
    auto result=packets.Update(live,{true,true,2,1000,{0,0,-.1f},.01f},200);
    CHECK(result.paired&&result.geometrySequence==2&&result.sample.sequence==2);
    live.awaitingAcknowledgement=true;live.weapon=101;live.sample.sequence=3;live.sample.nowNs=300;
    live.sample.nativeMode=SightMode::Secondary;live.sample.acknowledgedRequest=9;
    result=packets.Update(live,{},300);CHECK(!result.paired&&result.sample.acknowledgedRequest==9);
    live.awaitingAcknowledgement=false;live.latched=true;live.sample.acknowledgedRequest=0;
    live.sample.sequence=4;live.sample.nowNs=400;
    result=packets.Update(live,{true,true,3,1000,{0,0,-.1f},.02f},400);
    CHECK(result.paired&&result.geometrySequence==3&&result.sample.sequence==4&&!result.advanced);
    live.sample.sequence=5;live.sample.nowNs=500;
    result=packets.Update(live,{},500);CHECK(!result.paired&&result.sample.sequence==5&&!result.sample.contactValid);
    live.weapon=999;live.sample.sequence=6;live.sample.nowNs=600;
    result=packets.Update(live,{},600);CHECK(!result.sample.tracked); // An unrelated slot cannot inherit the reservation.
    return 0;
}
}
int main(){return MonotonicNativeHandoff()||GraspAndNonAxisAlignedGeometry()||RecordedJumpAndClockLimits()||RawContinuationAfterAcknowledgement()||RawSourceFreshnessAndNoVisualFeedback()||InvalidNativeDoesNotManufactureProgress()||
    CurrentReservationKeepsTokenWithoutContact()||ReservationCancellationAndNoResurrection()||NativeModeContactSequenceStaysIndependent();}
