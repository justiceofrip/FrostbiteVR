#include "Test.h"
#include "fvr/interaction/SightFlip.h"
#include <limits>
using namespace fvr;using namespace interaction;
namespace {
constexpr std::int64_t ms=1000000;
SightFlipConfig Config(){
    SightFlipConfig c;c.pivotMeters={1,2,3};c.axis={0,0,1};
    c.grabRadiusMeters=.05f;c.holdRadiusMeters=.15f;c.minLeverMeters=.02f;
    c.thresholdRadians=.6f;c.hysteresisRadians=.15f;c.maxStepRadians=.8f;
    c.detentHoldNs=30*ms;c.gestureTimeoutNs=2000*ms;c.ackTimeoutNs=250*ms;c.maxSampleGapNs=100*ms;return c;
}
struct Fixture {
    SightFlipConfig config=Config();SightFlip policy{config};SightFlipSample sample{};
    Fixture(){sample.owner={11,12,13,14};sample.sequence=1;sample.nowNs=1000*ms;
        sample.focused=sample.tracked=sample.contactValid=sample.nativeModeValid=true;At(0);}
    void At(float angle){sample.handLocalMeters={config.pivotMeters.x+.1f*std::cos(angle),config.pivotMeters.y+.1f*std::sin(angle),config.pivotMeters.z};}
    SightFlipResult Tick(std::int64_t elapsed=10*ms){++sample.sequence;sample.nowNs+=elapsed;return policy.Update(sample);}
    SightFlipResult Grab(){sample.squeeze=0;Tick();sample.squeeze=1;return Tick();}
    SightFlipResult Request(float sign=1){At(.4f*sign);Tick();At(.7f*sign);Tick();Tick();Tick();return Tick();}
};
int BasicAndReverse(){
    Fixture f;auto out=f.Grab();CHECK(out.grabbed&&out.phase==SightFlipPhase::Manipulating&&!out.request&&!out.committedMode);
    out=f.Request();CHECK(out.request&&out.request->target==SightMode::Secondary&&out.request->owner==f.sample.owner);
    const auto first=out.request->id;CHECK(first&&!out.committedMode&&out.phase==SightFlipPhase::AwaitingAcknowledgement);
    out=f.Tick();CHECK(!out.request&&!out.committedMode);
    f.sample.nativeMode=SightMode::Secondary;out=f.Tick();CHECK(!out.committedMode); // Seeing mode alone is not ack.
    f.sample.acknowledgedRequest=first+100;out=f.Tick();CHECK(!out.committedMode);
    f.sample.acknowledgedRequest=first;out=f.Tick();CHECK(out.committedMode==SightMode::Secondary&&out.phase==SightFlipPhase::Latched);
    out=f.Tick();CHECK(!out.committedMode&&!out.request); // No repeat dispatch or commit.
    f.At(0);out=f.Tick();CHECK(out.phase==SightFlipPhase::Latched&&!out.request);
    f.sample.squeeze=0;out=f.Tick();CHECK(out.released&&!out.cancelled&&out.phase==SightFlipPhase::Idle);
    f.sample.squeeze=1;out=f.Tick();CHECK(out.grabbed);out=f.Request(-1);
    CHECK(out.request&&out.request->target==SightMode::Primary&&out.request->id>first);
    const auto second=out.request->id;f.sample.nativeMode=SightMode::Primary;f.sample.acknowledgedRequest=first;
    CHECK(!f.Tick().committedMode);f.sample.acknowledgedRequest=second;CHECK(f.Tick().committedMode==SightMode::Primary);return 0;
}
int IntentAndHysteresis(){
    Fixture f;f.sample.squeeze=1;CHECK(!f.Tick().grabbed); // Startup held cannot grab.
    f.sample.squeeze=0;f.Tick();f.sample.squeeze=.69f;CHECK(!f.Tick().grabbed);
    f.sample.squeeze=.71f;CHECK(f.Tick().grabbed);
    f.At(-.7f);CHECK(!f.Tick().request);for(int n=0;n<5;++n)CHECK(!f.Tick().request); // Wrong opening direction.
    f.At(-.2f);f.Tick();f.At(.4f);f.Tick();f.At(.61f);CHECK(!f.Tick().request);
    f.At(.5f);CHECK(!f.Tick().request);CHECK(!f.Tick().request);
    CHECK(f.Tick().request); // Stay inside lower detent threshold during dwell.
    Fixture reset;CHECK(reset.Grab().grabbed);reset.At(.61f);reset.Tick();reset.At(.44f);reset.Tick();
    for(int n=0;n<5;++n)CHECK(!reset.Tick().request); // Crossing lower threshold clears dwell.
    reset.At(.61f);CHECK(!reset.Tick().request);CHECK(!reset.Tick().request);CHECK(!reset.Tick().request);
    CHECK(reset.Tick().request);
    Fixture release;release.Grab();release.sample.squeeze=.5f;CHECK(release.Tick().phase==SightFlipPhase::Manipulating);
    release.sample.squeeze=.35f;auto out=release.Tick();CHECK(out.released&&out.cancelled&&out.reason==SightFlipCancel::Released);
    release.sample.squeeze=.69f;CHECK(!release.Tick().grabbed);release.sample.squeeze=.7f;CHECK(release.Tick().grabbed);return 0;
}
int ContactAndGeometry(){
    Fixture f;f.sample.squeeze=0;f.Tick();f.sample.squeeze=1;f.sample.contactDistanceMeters=.051f;
    CHECK(!f.Tick().grabbed);f.sample.contactDistanceMeters=0;CHECK(!f.Tick().grabbed); // Must release after remote squeeze.
    CHECK(f.Grab().grabbed);f.sample.contactDistanceMeters=.14f;CHECK(f.Tick().phase==SightFlipPhase::Manipulating);
    f.sample.contactDistanceMeters=.151f;auto out=f.Tick();CHECK(out.cancelled&&out.reason==SightFlipCancel::ContactLost);
    f.sample.contactDistanceMeters=0;CHECK(!f.Tick().grabbed);CHECK(f.Grab().grabbed);
    f.At(1.f);out=f.Tick();CHECK(out.cancelled&&out.reason==SightFlipCancel::InvalidGeometry); // Teleported angular step.
    Fixture pivot;pivot.sample.handLocalMeters=pivot.config.pivotMeters;CHECK(!pivot.Grab().grabbed);
    Fixture nan;nan.Grab();nan.sample.handLocalMeters.x=std::numeric_limits<float>::quiet_NaN();
    CHECK(nan.Tick().reason==SightFlipCancel::InvalidGeometry);
    Fixture parallel;parallel.Grab();parallel.sample.handLocalMeters.z+=.3f;
    out=parallel.Tick();CHECK(Near(out.signedRadians,0)&&!out.request); // Project along supplied axis, not world Y.
    Fixture inverse;auto config=inverse.config;config.axis={0,0,-1};SightFlip reversed(config);
    inverse.sample.squeeze=0;reversed.Update(inverse.sample);inverse.sample.squeeze=1;++inverse.sample.sequence;inverse.sample.nowNs+=10*ms;
    CHECK(reversed.Update(inverse.sample).grabbed);inverse.At(-.7f);
    for(int n=0;n<4;++n){++inverse.sample.sequence;inverse.sample.nowNs+=10*ms;out=reversed.Update(inverse.sample);}
    CHECK(out.request&&out.request->target==SightMode::Secondary&&Near(out.signedRadians,.7f));
    Fixture otherAxis;config=otherAxis.config;config.axis={1,0,0};SightFlip aroundX(config);
    otherAxis.sample.handLocalMeters={1,2.1f,3};otherAxis.sample.squeeze=0;aroundX.Update(otherAxis.sample);
    ++otherAxis.sample.sequence;otherAxis.sample.nowNs+=10*ms;otherAxis.sample.squeeze=1;CHECK(aroundX.Update(otherAxis.sample).grabbed);
    otherAxis.sample.handLocalMeters={1,2+.1f*std::cos(.7f),3+.1f*std::sin(.7f)};
    for(int n=0;n<4;++n){++otherAxis.sample.sequence;otherAxis.sample.nowNs+=10*ms;out=aroundX.Update(otherAxis.sample);}
    CHECK(out.request&&Near(out.signedRadians,.7f));return 0;
}
int CancellationAndAcknowledgement(){
    for(unsigned which=0;which<4;++which){
        Fixture f;f.Grab();auto out=f.Request();CHECK(out.request);const auto id=out.request->id;
        if(which==0)++f.sample.owner.actor;if(which==1)++f.sample.owner.generation;
        if(which==2)++f.sample.owner.equipped;if(which==3)++f.sample.owner.space;
        f.sample.acknowledgedRequest=id;f.sample.nativeMode=SightMode::Secondary;out=f.Tick();
        CHECK(out.cancelled&&out.cancelledRequest==id&&!out.committedMode&&out.reason==SightFlipCancel::IdentityChanged);
        CHECK(!f.Tick().grabbed); // Unexpected physical item changes require release.
    }
    for(unsigned which=0;which<5;++which){
        Fixture f;f.Grab();auto out=f.Request();CHECK(out.request);const auto id=out.request->id;
        if(which==0)f.sample.tracked=false;if(which==1)f.sample.focused=false;
        if(which==2){f.sample.contactValid=false;f.sample.acknowledgedRequest=id+1;}if(which==3)f.sample.squeeze=0;
        if(which==4)f.sample.nativeModeValid=false;
        f.sample.nativeMode=SightMode::Secondary;if(which!=2)f.sample.acknowledgedRequest=id;out=f.Tick();
        CHECK(out.cancelled&&out.cancelledRequest==id&&!out.committedMode);
    }
    Fixture staleAck;staleAck.Grab();auto out=staleAck.Request();CHECK(out.request);const auto old=out.request->id;
    staleAck.sample.squeeze=0;CHECK(staleAck.Tick().cancelledRequest==old);
    staleAck.At(0);staleAck.sample.squeeze=1;CHECK(staleAck.Tick().grabbed);out=staleAck.Request();CHECK(out.request);
    const auto current=out.request->id;CHECK(current>old);staleAck.sample.nativeMode=SightMode::Secondary;
    staleAck.sample.acknowledgedRequest=old;CHECK(!staleAck.Tick().committedMode);
    staleAck.sample.acknowledgedRequest=current;CHECK(staleAck.Tick().committedMode==SightMode::Secondary);
    Fixture changed;changed.Grab();changed.sample.nativeMode=SightMode::Secondary;
    CHECK(changed.Tick().reason==SightFlipCancel::NativeModeChanged);
    Fixture wrongMode;wrongMode.Grab();out=wrongMode.Request();CHECK(out.request);
    wrongMode.sample.acknowledgedRequest=out.request->id;CHECK(!wrongMode.Tick().committedMode);return 0;
}
int NativeAckWithoutOldContact(){
    Fixture f;f.Grab();auto out=f.Request();CHECK(out.request);const auto id=out.request->id;
    f.sample.nativeMode=SightMode::Secondary;f.sample.acknowledgedRequest=id;
    f.sample.contactValid=false;f.sample.handLocalMeters.x=std::numeric_limits<float>::quiet_NaN();
    out=f.Tick();CHECK(out.committedMode==SightMode::Secondary&&!out.cancelled); // Expected backend slot changed before pose.
    Fixture noAck;noAck.Grab();CHECK(noAck.Request().request);noAck.sample.contactValid=false;
    CHECK(noAck.Tick().reason==SightFlipCancel::ContactLost);
    Fixture duplicate;duplicate.Grab();out=duplicate.Request();CHECK(out.request);
    duplicate.sample.nativeMode=SightMode::Secondary;duplicate.sample.acknowledgedRequest=out.request->id;
    duplicate.sample.contactValid=false;out=duplicate.policy.Update(duplicate.sample);
    CHECK(!out.committedMode&&!out.cancelled&&out.phase==SightFlipPhase::AwaitingAcknowledgement);
    duplicate.sample.nowNs+=10*ms;out=duplicate.policy.Update(duplicate.sample);
    CHECK(!out.committedMode&&!out.cancelled&&out.phase==SightFlipPhase::AwaitingAcknowledgement);
    out=duplicate.Tick();CHECK(out.committedMode==SightMode::Secondary&&!out.cancelled);
    // A wrong/old token must not preserve a duplicate with missing contact.
    Fixture staleId;staleId.Grab();out=staleId.Request();CHECK(out.request);const auto cancelledId=out.request->id;
    staleId.sample.squeeze=0;CHECK(staleId.Tick().cancelledRequest==cancelledId);
    staleId.At(0);staleId.sample.squeeze=1;CHECK(staleId.Tick().grabbed);
    out=staleId.Request();CHECK(out.request&&out.request->id>cancelledId);
    staleId.sample.nativeMode=SightMode::Secondary;staleId.sample.acknowledgedRequest=cancelledId;
    staleId.sample.contactValid=false;out=staleId.policy.Update(staleId.sample);
    CHECK(out.cancelled&&!out.committedMode&&out.reason==SightFlipCancel::ContactLost);
    // The duplicate exception cannot hold an operation forever or survive release.
    Fixture stalePose;stalePose.Grab();out=stalePose.Request();CHECK(out.request);
    stalePose.sample.nativeMode=SightMode::Secondary;stalePose.sample.acknowledgedRequest=out.request->id;
    stalePose.sample.contactValid=false;stalePose.sample.nowNs+=101*ms;
    out=stalePose.policy.Update(stalePose.sample);CHECK(out.cancelled&&out.reason==SightFlipCancel::StaleTracking);
    Fixture releaseDuplicate;releaseDuplicate.Grab();out=releaseDuplicate.Request();CHECK(out.request);
    releaseDuplicate.sample.nativeMode=SightMode::Secondary;releaseDuplicate.sample.acknowledgedRequest=out.request->id;
    releaseDuplicate.sample.contactValid=false;releaseDuplicate.sample.squeeze=0;
    out=releaseDuplicate.policy.Update(releaseDuplicate.sample);CHECK(out.cancelled&&!out.committedMode);
    Fixture wrongItem;wrongItem.Grab();out=wrongItem.Request();CHECK(out.request);
    wrongItem.sample.nativeMode=SightMode::Secondary;wrongItem.sample.acknowledgedRequest=out.request->id;
    wrongItem.sample.contactValid=false;++wrongItem.sample.owner.equipped;
    out=wrongItem.Tick();CHECK(!out.committedMode&&out.reason==SightFlipCancel::IdentityChanged);return 0;
}
int LatchedContactTransition(){
    // Headset capture133933: every committed mode lost contact on the very next
    // duplicate packet while squeeze remained1. Native attachment is pending.
    Fixture f;CHECK(f.Grab().grabbed);auto out=f.Request();CHECK(out.request);
    f.sample.nativeMode=SightMode::Secondary;f.sample.acknowledgedRequest=out.request->id;
    f.sample.contactValid=false;out=f.Tick();CHECK(out.committedMode==SightMode::Secondary);
    f.sample.acknowledgedRequest=0;f.sample.nowNs+=5*ms;
    out=f.policy.Update(f.sample);
    CHECK(out.phase==SightFlipPhase::Latched&&!out.released&&!out.request&&!out.committedMode);
    for(int n=0;n<65;++n){out=f.Tick();CHECK(out.phase==SightFlipPhase::Latched&&!out.request&&!out.committedMode);}
    // Fresh native geometry ends transition grace and enforces distance again.
    f.sample.contactValid=true;f.sample.contactDistanceMeters=.14f;out=f.Tick();
    CHECK(out.phase==SightFlipPhase::Latched);
    f.sample.contactValid=false;out=f.Tick();
    CHECK(out.phase==SightFlipPhase::Idle&&out.reason==SightFlipCancel::ContactLost);
    return 0;
}
int LatchedSafetyAndBounds(){
    const auto latch=[](Fixture& f){
        f.Grab();const auto request=f.Request();if(!request.request)return false;
        f.sample.nativeMode=SightMode::Secondary;f.sample.acknowledgedRequest=request.request->id;
        f.sample.contactValid=false;const auto result=f.Tick();f.sample.acknowledgedRequest=0;
        return result.committedMode==SightMode::Secondary;
    };
    // Duplicates and fresh tracking do not extend the one-shot geometry grace.
    Fixture bounded;CHECK(latch(bounded));auto out=bounded.policy.Update(bounded.sample);
    for(int n=0;n<99;++n){out=bounded.Tick();CHECK(out.phase==SightFlipPhase::Latched);}
    out=bounded.Tick();CHECK(out.released&&out.reason==SightFlipCancel::ContactLost);
    CHECK(!bounded.Tick().grabbed&&!bounded.Tick().request);
    // Valid contact remains governed by the physical hold radius immediately,
    // including during grace and even when supplied on a duplicate packet.
    for(unsigned bad=0;bad<4;++bad){
        Fixture f;CHECK(latch(f));f.sample.contactValid=true;
        if(bad==0)f.sample.contactDistanceMeters=f.config.holdRadiusMeters+.001f;
        if(bad==1)f.sample.contactDistanceMeters=-1;
        if(bad==2)f.sample.contactDistanceMeters=std::numeric_limits<float>::quiet_NaN();
        if(bad==3)f.sample.handLocalMeters.x=std::numeric_limits<float>::quiet_NaN();
        out=f.policy.Update(f.sample);CHECK(out.released&&out.phase==SightFlipPhase::Idle);
        CHECK(out.reason==(bad==3?SightFlipCancel::InvalidGeometry:SightFlipCancel::ContactLost));
    }
    for(unsigned lost=0;lost<10;++lost){
        Fixture f;CHECK(latch(f));
        if(lost==0)f.sample.focused=false;if(lost==1)f.sample.tracked=false;
        if(lost==2)++f.sample.owner.actor;if(lost==3)++f.sample.owner.generation;
        if(lost==4)++f.sample.owner.equipped;if(lost==5)++f.sample.owner.space;
        if(lost==6)f.sample.nativeMode=SightMode::Primary;
        if(lost==7)f.sample.nativeModeValid=false;
        if(lost==8)f.sample.nowNs+=101*ms;
        if(lost==9)--f.sample.sequence;
        out=f.policy.Update(f.sample);CHECK(out.released&&out.phase==SightFlipPhase::Idle&&!out.request&&!out.committedMode);
        if(lost==6)CHECK(out.reason==SightFlipCancel::NativeModeChanged);
        if(lost==8)CHECK(out.reason==SightFlipCancel::StaleTracking);
        if(lost==9)CHECK(out.reason==SightFlipCancel::ClockDiscontinuity);
    }
    Fixture released;CHECK(latch(released));released.sample.squeeze=0;
    out=released.policy.Update(released.sample);CHECK(out.released&&out.reason==SightFlipCancel::Released);
    // A duplicate neutral releases safety ownership, but cannot rearm a grab.
    released.sample.squeeze=1;released.sample.contactValid=true;CHECK(!released.Tick().grabbed);
    released.sample.squeeze=0;released.Tick();released.sample.squeeze=1;CHECK(released.Tick().grabbed);
    Fixture settled;CHECK(latch(settled));settled.sample.contactValid=true;
    for(int n=0;n<150;++n){out=settled.Tick();CHECK(out.phase==SightFlipPhase::Latched&&!out.request&&!out.committedMode);}
    settled.sample.contactDistanceMeters=.151f;CHECK(settled.Tick().reason==SightFlipCancel::ContactLost);
    return 0;
}
int PacketAndTimeGuards(){
    Fixture f;f.sample.squeeze=0;f.policy.Update(f.sample);f.sample.squeeze=1;
    CHECK(!f.policy.Update(f.sample).grabbed);CHECK(f.Tick().grabbed);
    f.At(.7f);CHECK(!f.Tick().request);f.sample.nowNs+=40*ms;
    CHECK(!f.policy.Update(f.sample).request); // Duplicate cannot complete the detent dwell.
    auto out=f.Tick();CHECK(out.request);const auto request=out.request->id;
    f.sample.nativeMode=SightMode::Secondary;f.sample.acknowledgedRequest=request;
    CHECK(!f.policy.Update(f.sample).committedMode);CHECK(f.Tick().committedMode==SightMode::Secondary);
    Fixture timeout;timeout.Grab();out=timeout.Request();CHECK(out.request);const auto timedRequest=out.request->id;
    for(int n=0;n<24;++n)CHECK(!timeout.Tick().cancelled);
    out=timeout.Tick();CHECK(out.cancelledRequest==timedRequest&&out.reason==SightFlipCancel::AcknowledgementTimeout);
    timeout.sample.nativeMode=SightMode::Secondary;timeout.sample.acknowledgedRequest=timedRequest;
    CHECK(!timeout.Tick().committedMode&&!timeout.Tick().grabbed); // Late ack cannot revive it.
    Fixture gesture;gesture.Grab();for(int n=0;n<19;++n)CHECK(!gesture.Tick(100*ms).cancelled);
    CHECK(gesture.Tick(100*ms).reason==SightFlipCancel::GestureTimeout);
    Fixture stale;stale.Grab();out=stale.Request();CHECK(out.request);stale.sample.nowNs+=101*ms;
    out=stale.policy.Update(stale.sample);CHECK(out.cancelledRequest&&out.reason==SightFlipCancel::StaleTracking);
    for(int which=0;which<2;++which){Fixture rollback;rollback.Grab();
        if(which==0)--rollback.sample.sequence;else rollback.sample.nowNs-=1;
        CHECK(rollback.policy.Update(rollback.sample).reason==SightFlipCancel::ClockDiscontinuity);}
    return 0;
}
int IdleDuplicateCadence(){
    // Native gather can run several times per XR packet. A neutral hand away
    // from the sight arms on the fresh packet; duplicate absent geometry must
    // not silently erase that state before the next intentional near squeeze.
    for(unsigned missing=0;missing<3;++missing){
        Fixture f;f.sample.contactValid=false;f.sample.contactDistanceMeters=.24f;
        CHECK(f.Tick().phase==SightFlipPhase::Idle);
        for(unsigned n=0;n<3;++n){
            f.sample.nowNs+=2*ms;
            if(missing==1)f.sample.contactDistanceMeters=std::numeric_limits<float>::quiet_NaN();
            if(missing==2)f.sample.handLocalMeters.x=std::numeric_limits<float>::quiet_NaN();
            const auto out=f.policy.Update(f.sample);
            CHECK(out.phase==SightFlipPhase::Idle&&!out.grabbed&&!out.request&&!out.committedMode);
        }
        f.sample.contactValid=true;f.sample.contactDistanceMeters=0;f.At(0);f.sample.squeeze=1;
        CHECK(f.Tick().grabbed);
    }
    // Repeated packets do not themselves arm or begin a gesture.
    Fixture held;held.sample.squeeze=1;CHECK(!held.Tick().grabbed);
    held.sample.squeeze=0;held.sample.contactValid=false;
    CHECK(!held.policy.Update(held.sample).grabbed);
    held.sample.contactValid=true;held.sample.squeeze=1;CHECK(!held.Tick().grabbed);
    held.sample.squeeze=0;held.sample.contactValid=false;held.Tick();
    held.sample.contactValid=true;held.sample.squeeze=1;
    CHECK(!held.policy.Update(held.sample).grabbed);CHECK(held.Tick().grabbed);
    // Cancellation remains authoritative even when it arrives on a duplicate;
    // a restored held squeeze cannot use arming from before focus/tracking loss.
    for(unsigned lost=0;lost<2;++lost){
        Fixture f;f.sample.contactValid=false;f.Tick();
        if(lost==0)f.sample.focused=false;else f.sample.tracked=false;
        CHECK(f.policy.Update(f.sample).reason==SightFlipCancel::TrackingLost);
        f.sample.focused=f.sample.tracked=f.sample.contactValid=true;f.sample.squeeze=1;
        CHECK(!f.Tick().grabbed);CHECK(f.Grab().grabbed);
    }
    // Active loss still cancels on the duplicate, and a duplicate neutral after
    // cancellation cannot rearm it. A fresh neutral is required.
    Fixture active;CHECK(active.Grab().grabbed);active.sample.contactValid=false;
    auto out=active.policy.Update(active.sample);
    CHECK(out.cancelled&&out.reason==SightFlipCancel::ContactLost);
    active.sample.squeeze=0;CHECK(!active.policy.Update(active.sample).grabbed);
    active.sample.contactValid=true;active.sample.squeeze=1;CHECK(!active.Tick().grabbed);
    CHECK(active.Grab().grabbed);
    // Fresh remote squeeze still consumes arming; entering contact while held
    // does not turn an earlier remote press into an implicit sight grab.
    Fixture remote;remote.sample.contactValid=false;remote.Tick();remote.sample.squeeze=1;
    CHECK(remote.Tick().reason==SightFlipCancel::ContactLost);
    remote.sample.contactValid=true;CHECK(!remote.Tick().grabbed);CHECK(remote.Grab().grabbed);
    return 0;
}
int IdleGeometryRefresh(){
    Fixture f;f.sample.squeeze=0;f.Tick();
    const math::Vec3 settled{4,-2,7};
    CHECK(f.policy.SetIdleGeometry(settled,{0,0,1}));
    CHECK(f.policy.Config().pivotMeters.x==4&&f.policy.Config().pivotMeters.y==-2&&f.policy.Config().pivotMeters.z==7);
    CHECK(f.policy.Config().thresholdRadians==f.config.thresholdRadians);
    // Invalid replacements are atomic and cannot erase the prior neutral arm.
    for(unsigned invalid=0;invalid<5;++invalid){
        auto pivot=settled;math::Vec3 axis{0,0,1};
        if(invalid==0)pivot.x=std::numeric_limits<float>::infinity();
        if(invalid==1)pivot.y=std::numeric_limits<float>::quiet_NaN();
        if(invalid==2)axis={};if(invalid==3)axis={0,0,2};
        if(invalid==4)axis.z=std::numeric_limits<float>::quiet_NaN();
        CHECK(!f.policy.SetIdleGeometry(pivot,axis));
        CHECK(f.policy.Config().pivotMeters.x==4&&f.policy.Config().pivotMeters.y==-2&&f.policy.Config().pivotMeters.z==7);
        CHECK(f.policy.Config().axis.x==0&&f.policy.Config().axis.y==0&&f.policy.Config().axis.z==1);
    }
    f.config.pivotMeters=settled;f.At(0);f.sample.squeeze=1;
    CHECK(f.Tick().grabbed); // Geometry refresh preserved fresh neutral arming.
    CHECK(!f.policy.SetIdleGeometry({100,100,100},{1,0,0}));
    auto out=f.Request();CHECK(out.request&&std::abs(out.signedRadians-.7f)<.00002f);
    const auto first=out.request->id;
    CHECK(!f.policy.SetIdleGeometry({100,100,100},{1,0,0})); // Awaiting native ack.
    f.sample.nativeMode=SightMode::Secondary;f.sample.acknowledgedRequest=first;
    CHECK(f.Tick().committedMode==SightMode::Secondary);
    CHECK(!f.policy.SetIdleGeometry({100,100,100},{1,0,0})); // Latched is still active.
    f.sample.squeeze=0;CHECK(f.Tick().released);
    f.config.pivotMeters={-3,5,2};CHECK(f.policy.SetIdleGeometry(f.config.pivotMeters,{0,0,1}));
    f.At(0);f.sample.squeeze=1;CHECK(f.Tick().grabbed);
    out=f.Request(-1);CHECK(out.request&&out.request->id==first+1&&out.request->target==SightMode::Primary);
    const auto second=out.request->id;
    f.sample.nativeMode=SightMode::Primary;f.sample.acknowledgedRequest=first;
    CHECK(!f.Tick().committedMode); // Refresh did not reuse an earlier request token.
    f.sample.acknowledgedRequest=second;CHECK(f.Tick().committedMode==SightMode::Primary);return 0;
}
int DetentDiagnostics(){
    Fixture f;CHECK(f.Grab().grabbed);f.At(.61f);auto out=f.Tick();
    CHECK(out.detent&&out.detentDwellNs==0&&!out.request);
    // Repeated tracking advances wall-time diagnostics but cannot dispatch.
    f.sample.nowNs+=20*ms;out=f.policy.Update(f.sample);
    CHECK(out.detent&&out.detentDwellNs==20*ms&&!out.request);
    f.sample.squeeze=0;out=f.Tick(5*ms);
    CHECK(out.cancelled&&out.reason==SightFlipCancel::Released&&out.detent&&out.detentDwellNs==25*ms);
    out=f.Tick();CHECK(!out.detent&&out.detentDwellNs==0);
    f.At(0);CHECK(f.Grab().grabbed);f.At(.61f);out=f.Tick();CHECK(out.detent&&out.detentDwellNs==0);
    f.At(.44f);out=f.Tick();CHECK(!out.detent&&out.detentDwellNs==0&&!out.request);
    f.At(.61f);out=f.Tick();CHECK(out.detent&&out.detentDwellNs==0);
    out=f.Tick(30*ms);CHECK(out.request&&out.detent&&out.detentDwellNs==30*ms);return 0;
}
int InvalidBindings(){
    for(int which=0;which<10;++which){auto config=Config();
        if(which==0)config.axis={};if(which==1)config.axis={0,0,2};
        if(which==2)config.hysteresisRadians=config.thresholdRadians;if(which==3)config.ackTimeoutNs=0;
        if(which==4)config.holdRadiusMeters=config.grabRadiusMeters;if(which==5)config.pressSqueeze=config.releaseSqueeze;
        if(which==6)config.pivotMeters.x=std::numeric_limits<float>::infinity();if(which==7)config.maxStepRadians=4;
        if(which==8)config.latchedContactGraceNs=0;if(which==9)config.latchedContactGraceNs=-1;
        SightFlip policy(config);Fixture f;CHECK(policy.Update(f.sample).reason==SightFlipCancel::InvalidSample);}
    Fixture bad;bad.Grab();bad.sample.squeeze=std::numeric_limits<float>::quiet_NaN();
    CHECK(bad.Tick().reason==SightFlipCancel::InvalidSample);return 0;
}
}
int main(){
    CHECK(BasicAndReverse()==0);CHECK(IntentAndHysteresis()==0);CHECK(ContactAndGeometry()==0);
    CHECK(CancellationAndAcknowledgement()==0);CHECK(NativeAckWithoutOldContact()==0);CHECK(LatchedContactTransition()==0);CHECK(LatchedSafetyAndBounds()==0);CHECK(PacketAndTimeGuards()==0);CHECK(IdleDuplicateCadence()==0);CHECK(IdleGeometryRefresh()==0);CHECK(DetentDiagnostics()==0);CHECK(InvalidBindings()==0);return 0;
}
