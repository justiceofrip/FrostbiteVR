#include "Test.h"
#include "SightGestureFixtureCheck.h"
#include "fvr/interaction/SightFlipPackets.h"
using namespace fvr;using namespace interaction;
namespace {
constexpr std::int64_t ms=1000000;
SightFlipConfig Config(){
    SightFlipConfig c;c.axis={0,0,1};c.grabRadiusMeters=.08f;c.holdRadiusMeters=.2f;c.minLeverMeters=.01f;
    c.thresholdRadians=.4f;c.hysteresisRadians=.08f;c.maxStepRadians=.7f;
    c.detentHoldNs=20*ms;c.gestureTimeoutNs=2000*ms;c.ackTimeoutNs=500*ms;c.maxSampleGapNs=250*ms;return c;
}
struct Fixture {
    SightFlipPackets packets;SightFlip policy{Config()};
    SightFlipInputPacket input;SightFlipContactPacket contact;std::int64_t qpc=1000;
    Fixture(){
        input.sample.owner={1,2,3,4};input.sample.sequence=1;input.sample.nowNs=1000*ms;
        input.sample.focused=input.sample.tracked=input.sample.nativeModeValid=true;
        input.weapon=5;input.deadline=qpc+150;input.eligible=true;
        contact.valid=contact.preferred=true;contact.generation=1;contact.deadline=input.deadline;
        contact.handLocalMeters={.1f,0,0};
    }
    void Next(float squeeze){
        ++input.sample.sequence;input.sample.nowNs+=10*ms;qpc+=10;
        input.sample.squeeze=squeeze;input.deadline=qpc+150;
    }
    void Publish(float angle=0){
        contact.generation=input.sample.sequence;contact.deadline=input.deadline;
        contact.handLocalMeters={.1f*std::cos(angle),.1f*std::sin(angle),0};
    }
    SightFlipPacketResult Packet(){return packets.Update(input,contact,qpc);}
    SightFlipResult Tick(){return policy.Update(Packet().sample);}
};
int ReachAndPress(){
    Fixture f;f.contact.distanceMeters=.3f;f.contact.preferred=false;CHECK(!f.Tick().grabbed);
    // Native updates faster than XR: duplicates away from the sight cannot
    // erase a released grip. A new press must wait for its own hand geometry.
    for(int n=0;n<3;++n){f.input.sample.nowNs+=ms;CHECK(!f.Tick().grabbed);}
    f.Next(1);auto packet=f.Packet();
    CHECK(packet.sample.squeeze==0&&!packet.advanced);
    CHECK(!f.policy.Update(packet.sample).grabbed);
    f.Publish();f.contact.distanceMeters=0;f.contact.preferred=true;packet=f.Packet();
    CHECK(packet.paired&&packet.advanced&&packet.sample.sequence==f.input.sample.sequence&&packet.sample.squeeze==1);
    CHECK(f.policy.Update(packet.sample).grabbed);
    // Native animation can republish the same XR generation at another angle.
    // Never interpret it as new tracked movement.
    f.contact.handLocalMeters={0,.1f,0};packet=f.Packet();
    CHECK(packet.paired&&!packet.advanced&&packet.sample.handLocalMeters.x==.1f);
    CHECK(!f.policy.Update(packet.sample).request);return 0;
}
int ReleaseAndLoss(){
    Fixture f;f.Tick();f.Next(1);f.Publish();CHECK(f.Tick().grabbed);
    const auto pressed=f.contact;f.Next(0);auto packet=f.Packet();
    CHECK(packet.sample.squeeze==0&&!packet.sample.contactValid);
    CHECK(f.policy.Update(packet.sample).released);
    f.Next(1);f.contact=pressed;packet=f.Packet();
    CHECK(!packet.paired&&packet.sample.squeeze==0);CHECK(!f.policy.Update(packet.sample).grabbed);
    f.Publish();CHECK(f.Tick().grabbed); // A genuinely new press is allowed.
    f.Next(1);f.input.sample.tracked=false;packet=f.Packet();
    CHECK(packet.reason==SightFlipPacketReason::InvalidInput&&!packet.sample.tracked);
    CHECK(f.policy.Update(packet.sample).cancelled);
    f.Next(1);f.input.sample.tracked=true;f.Publish();CHECK(!f.Tick().grabbed); // Recovery held is not rearmed.
    f.Next(0);f.Tick();f.Next(1);f.Publish();CHECK(f.Tick().grabbed);return 0;
}
int OwnershipAndExpiry(){
    Fixture f;f.Tick();f.Next(1);f.Publish();CHECK(f.Tick().grabbed);
    f.Next(1);++f.input.sample.owner.space;auto packet=f.Packet();
    CHECK(!packet.paired&&!packet.sample.contactValid);CHECK(!f.policy.Update(packet.sample).request);
    f.Next(0);f.Tick();f.Next(1);f.Publish();CHECK(f.Tick().grabbed);
    f.Next(1);++f.input.weapon;packet=f.Packet();CHECK(!packet.paired&&!packet.sample.contactValid);
    CHECK(f.policy.Update(packet.sample).cancelled);
    Fixture expired;expired.Tick();expired.Next(1);expired.Publish();expired.contact.deadline=expired.qpc;
    packet=expired.Packet();CHECK(!packet.paired&&packet.reason==SightFlipPacketReason::ExpiredPublication);
    expired.input.deadline=expired.qpc;packet=expired.Packet();
    CHECK(packet.reason==SightFlipPacketReason::InvalidInput&&!packet.sample.tracked);
    Fixture missing;missing.Tick();missing.Next(1);missing.contact.generation=900;
    packet=missing.Packet();CHECK(!packet.paired&&packet.reason==SightFlipPacketReason::MissingHistory);
    CHECK(!missing.policy.Update(packet.sample).grabbed);
    Fixture manual;manual.Tick();manual.Next(1);CHECK(!manual.Tick().grabbed);
    ++manual.input.weapon;manual.Next(1);manual.Publish();
    packet=manual.Packet();CHECK(packet.reason==SightFlipPacketReason::InvalidInput);
    CHECK(!manual.policy.Update(packet.sample).grabbed);
    manual.Next(1);manual.Publish();CHECK(!manual.Tick().grabbed); // New slot needs neutral.
    return 0;
}
int DelayedAcknowledgement(){
    Fixture f;f.Tick();f.Next(1);f.Publish();CHECK(f.Tick().grabbed);
    f.Next(1);f.Publish(.5f);CHECK(!f.Tick().request);
    f.Next(1);f.Publish(.5f);CHECK(!f.Tick().request);
    f.Next(1);f.Publish(.5f);auto result=f.Tick();CHECK(result.request);
    const auto id=result.request->id;const auto sequence=f.input.sample.sequence;
    f.input.awaitingAcknowledgement=true;
    ++f.input.weapon;f.input.sample.nativeMode=SightMode::Secondary;f.input.sample.acknowledgedRequest=id;
    f.contact={};auto packet=f.Packet();CHECK(!packet.paired&&packet.sample.sequence==sequence);
    result=f.policy.Update(packet.sample);CHECK(!result.committedMode&&!result.cancelled);
    f.Next(1);packet=f.Packet();CHECK(!packet.sample.contactValid&&!packet.advanced);
    result=f.policy.Update(packet.sample);CHECK(result.committedMode==SightMode::Secondary);
    // Older rifle contact cannot supply geometry after the slot handoff.
    f.contact.valid=true;f.contact.preferred=true;f.contact.generation=sequence;f.contact.deadline=f.input.deadline;
    CHECK(!f.Packet().paired);return 0;
}
bool Latch(Fixture& f){
    f.Tick();f.Next(1);f.Publish();if(!f.Tick().grabbed)return false;
    f.Next(1);f.Publish(.5f);f.Tick();f.Next(1);f.Publish(.5f);f.Tick();
    f.Next(1);f.Publish(.5f);const auto result=f.Tick();if(!result.request)return false;
    f.input.awaitingAcknowledgement=true;f.input.sample.acknowledgedRequest=result.request->id;
    ++f.input.weapon;f.input.sample.nativeMode=SightMode::Secondary;f.contact={};f.Next(1);
    const bool committed=f.Tick().committedMode==SightMode::Secondary;
    f.input.awaitingAcknowledgement=false;f.input.latched=true;f.input.sample.acknowledgedRequest=0;return committed;
}
int LatchedNativeContactGap(){
    Fixture f;CHECK(Latch(f));auto packet=f.Packet();
    CHECK(packet.reason==SightFlipPacketReason::LatchedTracking&&!packet.paired&&!packet.advanced&&!packet.geometrySequence);
    CHECK(!packet.sample.contactValid&&f.policy.Update(packet.sample).phase==SightFlipPhase::Latched);
    // A 650ms native attachment transition is fresh tracking, not stale pose.
    for(int n=0;n<65;++n){f.Next(1);packet=f.Packet();
        CHECK(packet.sample.sequence==f.input.sample.sequence&&!packet.sample.contactValid&&!packet.advanced);
        const auto result=f.policy.Update(packet.sample);CHECK(result.phase==SightFlipPhase::Latched&&!result.request&&!result.committedMode);}
    // Geometry returning one packet behind must not roll policy time backward,
    // and must retain the actual proof generation rather than restamp the pose.
    f.Next(1);f.Packet();f.contact.valid=f.contact.preferred=true;f.Publish();
    const auto original=f.contact.generation;const auto originalDeadline=f.contact.deadline;
    f.Next(1);packet=f.Packet();
    CHECK(packet.paired&&!packet.advanced&&packet.geometrySequence==original);
    CHECK(packet.sample.sequence==f.input.sample.sequence&&packet.sample.sequence>packet.geometrySequence);
    CHECK(f.contact.deadline==originalDeadline&&packet.sample.contactValid);
    CHECK(f.policy.Update(packet.sample).phase==SightFlipPhase::Latched);
    f.contact.distanceMeters=.201f;packet=f.Packet();
    CHECK(f.policy.Update(packet.sample).reason==SightFlipCancel::ContactLost);
    // The explicit bridge does not authorize unknown mode switches or expired
    // input; current release also wins over an older held publication.
    for(unsigned lost=0;lost<6;++lost){
        Fixture safety;CHECK(Latch(safety));safety.Next(1);
        if(lost==0)safety.input.sample.squeeze=0;
        if(lost==1)safety.input.sample.tracked=false;
        if(lost==2)safety.input.sample.focused=false;
        if(lost==3)++safety.input.weapon;
        if(lost==4)++safety.input.sample.owner.space;
        if(lost==5)safety.input.deadline=safety.qpc;
        packet=safety.Packet();const auto result=safety.policy.Update(packet.sample);
        CHECK(result.released&&result.phase==SightFlipPhase::Idle&&!result.request&&!result.committedMode);
    }
    Fixture expired;CHECK(Latch(expired));expired.Next(1);expired.Packet();
    expired.contact.valid=expired.contact.preferred=true;expired.Publish();expired.contact.deadline=expired.qpc;
    packet=expired.Packet();CHECK(!packet.paired&&!packet.sample.contactValid&&!packet.geometrySequence);
    CHECK(expired.policy.Update(packet.sample).phase==SightFlipPhase::Latched);
    // A latched flag alone cannot make an unacknowledged policy commit or grasp.
    Fixture uncommitted;uncommitted.input.latched=true;uncommitted.contact={};
    uncommitted.Tick();uncommitted.Next(1);const auto result=uncommitted.Tick();
    CHECK(result.phase==SightFlipPhase::Idle&&!result.grabbed&&!result.committedMode);
    return 0;
}
int ReleaseWinsAcknowledgement(){
    Fixture f;f.Tick();f.Next(1);f.Publish();CHECK(f.Tick().grabbed);
    f.Next(1);f.Publish(.5f);f.Tick();f.Next(1);f.Publish(.5f);f.Tick();
    f.Next(1);f.Publish(.5f);auto result=f.Tick();CHECK(result.request);
    f.input.awaitingAcknowledgement=true;f.input.sample.acknowledgedRequest=result.request->id;
    ++f.input.weapon;f.input.sample.nativeMode=SightMode::Secondary;
    f.Next(0);f.contact={};result=f.Tick();CHECK(result.cancelled&&!result.committedMode);
    f.input.awaitingAcknowledgement=false; // Adapter leaves Awaiting after release.
    // Even if a caller retains the old token, it cannot advance an idle press.
    f.Next(1);auto packet=f.Packet();CHECK(packet.reason!=SightFlipPacketReason::NativeAcknowledgement);
    CHECK(!f.policy.Update(packet.sample).grabbed);
    f.contact.valid=f.contact.preferred=true;f.Publish();CHECK(f.Tick().grabbed);return 0;
}
}
int main(){
    if(sight_receiver_regression::CheckGeometry()||ReachAndPress()||ReleaseAndLoss()||OwnershipAndExpiry()||DelayedAcknowledgement()||LatchedNativeContactGap()||ReleaseWinsAcknowledgement())return 1;
    return 0;
}

