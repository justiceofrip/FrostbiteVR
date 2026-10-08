#include "Test.h"
#include "fvr/interaction/BoltWeaponCustody.h"
#include "MeasuredM95Fixture.h"
using namespace fvr;
using namespace fvr::interaction;
namespace {
constexpr std::int64_t ms=1000000;
constexpr auto left=InteractionHand::Left,right=InteractionHand::Right;
math::Matrix4 Identity(){math::Matrix4 m{};for(unsigned n=0;n<4;++n)m.values[n][n]=1;return m;}
bool Near(const math::Matrix4& a,const math::Matrix4& b,float tolerance=.00005f){for(unsigned n=0;n<4;++n)for(unsigned k=0;k<4;++k)if(std::abs(a.values[n][k]-b.values[n][k])>tolerance)return false;return true;}
WeaponCycleProfile Profile(const bolt_custody_fixture::MeasuredBolt& measured){
    WeaponCycleProfile p;p.id=7;p.revision=1;p.family=WeaponCycleFamily::Bolt;p.axis=measured.axis;p.closedContact=measured.initial;
    p.stroke=measured.stroke;p.rearTolerance=p.frontTolerance=.002f;p.contactRadius=.03f;p.lateralTolerance=.004f;
    p.unlockRadians=measured.unlock;p.rotationTolerance=.02f;p.maxStepMeters=.025f;p.endpointDwellNs=50*ms;p.maximumCycleNs=10000*ms;
    p.hands={right,left};return p;
}
struct Fixture {
    const bolt_custody_fixture::MeasuredBolt& measured;
    HandInteraction hands;BoltWeaponCustody custody;WeaponCycleProfile profile;
    HandInteractionSample source{{10,1,1,1},1,1000*ms,1150*ms,1000*ms,true,{true,true},{false,false}};
    WeaponCycleLease lease{source.owner,{100,1},{30,1},1,1,1,source.observedNs,source.deadlineNs,true};
    HandClaimToken initialGun{},support{};std::array<std::uint64_t,2> intents{};
    math::Matrix4 leftWrist=Identity(),rightWrist=Identity(),worldWeapon=Identity();
    std::optional<WeaponCycleRelease> release;unsigned requests=0;
    explicit Fixture(const bolt_custody_fixture::MeasuredBolt& m):measured(m),profile(Profile(m)){
        leftWrist.values[3][0]=-.2f;leftWrist.values[3][1]=1.2f;rightWrist.values[3][0]=.1f;rightWrist.values[3][1]=1.1f;
        worldWeapon.values[3][1]=1.15f;worldWeapon.values[3][2]=-.3f;
        auto g=hands.Acquire(source,Request(right,HandClaimKind::GunHold,1));if(g.claim)initialGun=g.claim->token;
        auto s=hands.Acquire(source,Request(left,HandClaimKind::WeaponSupport,2,initialGun.id));if(s.claim)support=s.claim->token;
    }
    HandClaimRequest Request(InteractionHand hand,HandClaimKind kind,std::uint64_t contact,std::uint64_t parent=0){
        return {source.owner,hand,kind,{100,1},{{contact,1},source.sequence,source.deadlineNs,true},++intents[static_cast<std::size_t>(hand)],parent};
    }
    void Next(){source.nowNs+=10*ms;source.observedNs=source.nowNs;source.deadlineNs=source.nowNs+150*ms;++source.sequence;
        lease.sequence=source.sequence;lease.observedNs=source.observedNs;lease.deadlineNs=source.deadlineNs;}
    bool Enter(){
        if(!custody.Arm(profile,lease,measured.contactFromWrist,source.nowNs))return false;
        HandGunCustodyTransfer transfer{initialGun,support,{Request(left,HandClaimKind::GunHold,2),source},std::nullopt};
        return custody.EnterCustody(source,transfer,worldWeapon,leftWrist,hands).transaction.accepted;
    }
    BoltCustodyResult Step(const math::Matrix4& part,bool grip=true,bool advance=true){
        if(advance)Next();source.released[1]=!grip;
        BoltCustodySample sample;sample.source=source;sample.lease=lease;sample.mechanismGrip=grip;
        sample.gunWristInWorld=custody.Custody()==BoltCustodyPhase::Returned?rightWrist:leftWrist;
        sample.mechanismWristInWeapon=Multiply(measured.contactFromWrist,part);
        const auto gun=custody.Gun();sample.gunContact={gun?gun->contact:HandInteractionKey{},source.sequence,source.deadlineNs,true};
        sample.mechanismContact={{30,1},source.sequence,source.deadlineNs,true};sample.acquireIntent=++intents[1];
        auto result=custody.Update(sample,hands);if(result.physical.release){release=result.physical.release;++requests;}return result;
    }
    int Dwell(const math::Matrix4& part,WeaponCyclePhase expected){
        for(unsigned n=0;n<7;++n){auto r=Step(part);CHECK(r.weapon&&Near(r.weapon->weaponInWorld,worldWeapon)&&r.blocksFire);}
        CHECK(custody.CyclePhase()==expected);return 0;
    }
    int CompleteStroke(){
        auto r=Step(measured.initial,false);CHECK(r.weapon&&r.blocksFire&&!r.physical.ownsMechanism&&!hands.Current(right));
        r=Step(measured.initial);CHECK(r.physical.ownsMechanism&&r.physical.cycle.phase==WeaponCyclePhase::Unlock&&r.weapon);
        for(unsigned n=1;n<4;++n){r=Step(weapon_cycle_detail::Target(profile,0,profile.unlockRadians*float(n)/4));CHECK(r.physical.ownsMechanism&&r.weapon&&Near(r.weapon->weaponInWorld,worldWeapon));}
        CHECK(Dwell(measured.rotated,WeaponCyclePhase::Rear)==0);
        for(unsigned n=1;n<8;++n){auto part=measured.rotated;for(unsigned k=0;k<3;++k)part.values[3][k]+=(measured.rear.values[3][k]-measured.rotated.values[3][k])*float(n)/8;r=Step(part);CHECK(r.physical.ownsMechanism);}
        CHECK(Dwell(measured.rear,WeaponCyclePhase::Forward)==0);
        for(unsigned n=1;n<8;++n){auto part=measured.rear;for(unsigned k=0;k<3;++k)part.values[3][k]+=(measured.advanced.values[3][k]-measured.rear.values[3][k])*float(n)/8;r=Step(part);CHECK(r.physical.ownsMechanism);}
        CHECK(Dwell(measured.advanced,WeaponCyclePhase::Lock)==0);
        for(unsigned n=3;n>0;--n){r=Step(weapon_cycle_detail::Target(profile,0,profile.unlockRadians*float(n)/4));CHECK(r.physical.ownsMechanism);}
        CHECK(Dwell(measured.finalPose,WeaponCyclePhase::AwaitingNative)==0);
        CHECK(release&&requests==1&&!hands.Current(right)&&hands.Current(left)->token==*custody.Gun());return 0;
    }
    HandGunCustodyResult Return(){
        Next();source.released[1]=false;
        HandGunCustodyTransfer transfer{*custody.Gun(),std::nullopt,{Request(right,HandClaimKind::GunHold,1),source},HandCustodyTarget{Request(left,HandClaimKind::WeaponSupport,2),source}};
        return custody.ReturnCustody(source,transfer,leftWrist,rightWrist,hands);
    }
};
int MeasuredM95CustodyStrokeAndReturn(){
    CHECK(!bolt_custody_fixture::NativeHandleVerified&&!bolt_custody_fixture::NativeClosedVerified);
    for(const auto* measured:{&bolt_custody_fixture::M95}){
        Fixture f(*measured);CHECK(f.initialGun.id&&f.support.id&&f.Enter());
        CHECK(!f.hands.Current(right)&&f.hands.Current(left)->token==*f.custody.Gun());CHECK(f.CompleteStroke()==0);
        CHECK(!f.Return().transaction.accepted); // Still held at bolt: no invented return grip.
        auto held=f.Step(measured->finalPose);CHECK(held.blocksFire&&held.weapon&&!held.physical.release&&f.requests==1);
        auto neutral=f.Step(measured->finalPose,false);CHECK(neutral.blocksFire&&neutral.weapon);
        auto restored=f.Return();CHECK(restored.transaction.accepted&&restored.transaction.claim&&restored.companion);
        CHECK(restored.transaction.claim->token.hand==right&&restored.companion->token.prerequisiteClaim==restored.transaction.claim->token.id);
        auto pending=f.Step(measured->finalPose);CHECK(pending.blocksFire&&pending.weapon&&Near(pending.weapon->weaponInWorld,f.worldWeapon)&&!pending.physical.release);
        WeaponCycleReady ready{*f.release,f.lease.sequence+1,f.source.nowNs,f.source.deadlineNs,true,true};auto wrong=ready;++wrong.release.request;
        CHECK(!f.custody.ObserveReady(wrong,f.source.nowNs));CHECK(f.custody.ObserveReady(ready,f.source.nowNs));
        const auto complete=f.Step(measured->finalPose);CHECK(!complete.blocksFire&&complete.weapon&&f.custody.CyclePhase()==WeaponCyclePhase::Complete&&f.requests==1);
        f.rightWrist.values[3][0]+=.1f;const auto moved=f.Step(measured->finalPose);CHECK(moved.weapon&&std::abs(moved.weapon->weaponInWorld.values[3][0]-.1f)<.0001f);
    }return 0;
}
int HandAssignmentAndRendererValidation(){
    Fixture f(bolt_custody_fixture::M95);auto bad=f.profile;bad.hands={right,right};PhysicalWeaponCycle rejected;CHECK(!rejected.Begin(bad,f.lease,f.source.nowNs));
    bad=f.profile;bad.hands.mechanism=static_cast<InteractionHand>(255);CHECK(!rejected.Begin(bad,f.lease,f.source.nowNs));
    CHECK(f.Enter());f.Step(f.measured.initial,false);auto r=f.Step(f.measured.initial);CHECK(r.physical.target);
    const auto mechanism=f.hands.Current(right),gun=f.hands.Current(left);CHECK(mechanism&&gun);
    CHECK(CurrentPhysicalWeaponCycleTarget(*r.physical.target,f.lease,f.source,*mechanism,*gun,f.profile.hands));
    CHECK(!CurrentPhysicalWeaponCycleTarget(*r.physical.target,f.lease,f.source,*mechanism,*gun));
    auto target=*r.physical.target;target.hands={left,right};CHECK(!CurrentPhysicalWeaponCycleTarget(target,f.lease,f.source,*mechanism,*gun,f.profile.hands));
    CHECK(r.weapon&&CurrentBoltCustodyWeaponTarget(*r.weapon,f.source,*gun));
    auto stale=*r.weapon;--stale.inputSequence;CHECK(!CurrentBoltCustodyWeaponTarget(stale,f.source,*gun));
    stale=*r.weapon;stale.gun.hand=right;CHECK(!CurrentBoltCustodyWeaponTarget(stale,f.source,*gun));
    auto untracked=f.source;untracked.tracked[0]=false;CHECK(!CurrentBoltCustodyWeaponTarget(*r.weapon,untracked,*gun));
    return 0;
}
int WrongOrderReleaseAndRegripCannotBypassStroke(){
    Fixture f(bolt_custody_fixture::M95);CHECK(f.Enter());f.Step(f.measured.initial,false);f.Step(f.measured.initial);
    auto early=f.Step(weapon_cycle_detail::Target(f.profile,.01f,0));CHECK(early.physical.cycle.phase==WeaponCyclePhase::Cancelled&&early.blocksFire&&!early.physical.release&&!f.hands.Current(right));
    auto held=f.Step(f.measured.initial);CHECK(held.physical.cycle.phase==WeaponCyclePhase::Cancelled&&!held.physical.ownsMechanism);
    auto neutral=f.Step(f.measured.initial,false);CHECK(neutral.physical.cycle.phase==WeaponCyclePhase::AwaitingGrip&&neutral.weapon);
    auto regrip=f.Step(f.measured.initial);CHECK(regrip.physical.cycle.phase==WeaponCyclePhase::Unlock&&regrip.physical.ownsMechanism);
    auto released=f.Step(f.measured.initial,false);CHECK(released.physical.cycle.phase==WeaponCyclePhase::Cancelled&&!released.physical.release&&released.weapon);
    CHECK(f.requests==0&&!f.Return().transaction.accepted);return 0;
}
int MovingMechanismNeverDragsWeaponAndSafetyLossBlocks(){
    Fixture f(bolt_custody_fixture::M95);CHECK(f.Enter());f.Step(f.measured.initial,false);f.Step(f.measured.initial);
    auto r=f.Step(weapon_cycle_detail::Target(f.profile,0,f.profile.unlockRadians*.5f));CHECK(r.weapon&&Near(r.weapon->weaponInWorld,f.worldWeapon));
    f.leftWrist.values[3][0]+=.04f;r=f.Step(weapon_cycle_detail::Target(f.profile,0,f.profile.unlockRadians*.5f));
    CHECK(r.weapon&&std::abs(r.weapon->weaponInWorld.values[3][0]-.04f)<.0001f);
    f.Next();f.source.tracked[0]=false;r=f.Step(f.measured.initial,true,false);CHECK(r.blocksFire&&!r.weapon&&!r.physical.target&&!f.hands.Current(left)&&!f.hands.Current(right)&&!r.physical.release);
    f.Next();f.source.tracked[0]=true;r=f.Step(f.measured.initial,false,false);CHECK(r.blocksFire&&!r.weapon&&!r.physical.ownsMechanism);return 0;
}
int RepeatedPacketCannotAccrueDwellOrReturnOutcome(){
    Fixture f(bolt_custody_fixture::M95);CHECK(f.Enter());f.Step(f.measured.initial,false);f.Step(f.measured.initial);
    f.Step(f.measured.rotated);for(unsigned n=0;n<12;++n){const auto r=f.Step(f.measured.rotated,true,false);CHECK(r.physical.cycle.phase==WeaponCyclePhase::Unlock&&!r.physical.release);}
    CHECK(f.requests==0&&f.custody.CyclePhase()==WeaponCyclePhase::Unlock);return 0;
}
int RightHandHistoricalContactUsesOriginalPacket(){
    Fixture f(bolt_custody_fixture::M95);CHECK(f.Enter());f.Step(f.measured.initial,false);auto original=f.source;
    auto historical=[&](const HandInteractionSample& evidence,bool currentGrip){
        f.Next();f.source.released[1]=!currentGrip;BoltCustodySample sample;sample.source=f.source;sample.lease=f.lease;
        sample.gunWristInWorld=f.leftWrist;sample.mechanismWristInWeapon=Multiply(f.measured.contactFromWrist,f.measured.initial);
        sample.gunContact={f.custody.Gun()->contact,f.source.sequence,f.source.deadlineNs,true};
        sample.mechanismContact={{30,1},evidence.sequence,evidence.deadlineNs,true};sample.contactSource=evidence;
        sample.mechanismGrip=currentGrip;sample.acquireIntent=++f.intents[1];return f.custody.Update(sample,f.hands);
    };
    auto neutral=historical(original,true);CHECK(!neutral.physical.ownsMechanism&&neutral.physical.cycle.phase==WeaponCyclePhase::AwaitingGrip);
    original=f.source;auto held=historical(original,true);CHECK(held.physical.ownsMechanism&&held.physical.target&&held.physical.cycle.phase==WeaponCyclePhase::Unlock);
    CHECK(held.physical.target->inputSequence==original.sequence&&held.physical.target->observedNs==original.observedNs);
    CHECK(f.hands.Current(right)->inputSequence==original.sequence&&f.hands.Current(right)->deadlineNs==original.deadlineNs);
    CHECK(f.hands.Current(left)->inputSequence==f.source.sequence);
    CHECK(CurrentPhysicalWeaponCycleTarget(*held.physical.target,f.lease,f.source,*f.hands.Current(right),*f.hands.Current(left),f.profile.hands));
    original=f.source;const auto lost=historical(original,false);CHECK(lost.blocksFire&&!lost.physical.ownsMechanism&&!lost.physical.target&&!f.hands.Current(right)&&f.hands.Current(left));return 0;
}
int FailedHandoffAndWrongReturnCannotSteal(){
    Fixture f(bolt_custody_fixture::M95);CHECK(f.custody.Arm(f.profile,f.lease,f.measured.contactFromWrist,f.source.nowNs));
    auto make=[&](){return HandGunCustodyTransfer{f.initialGun,f.support,{f.Request(left,HandClaimKind::GunHold,2),f.source},std::nullopt};};
    auto transfer=make();transfer.nextGun.request.contact.eligible=false;
    CHECK(!f.custody.EnterCustody(f.source,transfer,f.worldWeapon,f.leftWrist,f.hands).transaction.accepted);
    CHECK(f.hands.Current(right)->token==f.initialGun&&f.hands.Current(left)->token==f.support&&f.custody.Custody()==BoltCustodyPhase::AwaitingCustody);
    transfer=make();CHECK(f.custody.EnterCustody(f.source,transfer,f.worldWeapon,f.leftWrist,f.hands).transaction.accepted);CHECK(f.CompleteStroke()==0);
    f.Step(f.measured.finalPose,false);f.Next();f.source.released[1]=false;
    HandGunCustodyTransfer restore{*f.custody.Gun(),std::nullopt,{f.Request(right,HandClaimKind::GunHold,999),f.source},HandCustodyTarget{f.Request(left,HandClaimKind::WeaponSupport,2),f.source}};
    CHECK(!f.custody.ReturnCustody(f.source,restore,f.leftWrist,f.rightWrist,f.hands).transaction.accepted);
    CHECK(f.hands.Current(left)->token==*f.custody.Gun()&&!f.hands.Current(right));return 0;
}
int SustainedMeasuredCyclesAndLateExactCompletion(){
    for(const auto* measured:{&bolt_custody_fixture::M95}){
        Fixture f(*measured);std::uint64_t previousRequest=0;
        for(unsigned cycle=0;cycle<24;++cycle){
            CHECK(f.Enter());CHECK(f.CompleteStroke()==0);CHECK(f.release->request>previousRequest);previousRequest=f.release->request;
            const auto receipt=*f.release;f.Step(measured->finalPose,false);const auto returned=f.Return();CHECK(returned.transaction.accepted&&returned.transaction.claim&&returned.companion);
            WeaponCycleReady ready{receipt,f.lease.sequence+1,f.source.nowNs,f.source.nowNs+50*ms,true,true};
            for(unsigned n=0;n<6;++n){const auto waiting=f.Step(measured->finalPose);CHECK(waiting.blocksFire&&waiting.weapon&&!waiting.physical.release);}
            CHECK(!f.custody.ObserveReady(ready,f.source.nowNs));CHECK(f.custody.ReconcileReady(ready,f.source.nowNs));
            const auto completed=f.Step(measured->finalPose);CHECK(!completed.blocksFire&&completed.weapon&&f.requests==1);
            f.initialGun=returned.transaction.claim->token;f.support=returned.companion->token;f.release.reset();f.requests=0;
            ++f.lease.cycle;++f.lease.shot;f.Next();
        }
    }return 0;
}
}
int main(){
    const auto geometry=fvr::bc2::M95BoltProfile(fvr::bc2::M95AuthoredReferencePart,1);
    CHECK(weapon_cycle_detail::Profile(geometry)&&!fvr::bc2::M95NativeBoltHandleVerified);
    for(auto part:{bolt_custody_fixture::M95.initial,bolt_custody_fixture::M95.rear}){
        const auto inverse=fvr::bc2::M95BoltPartFromRawWrist(fvr::bc2::M95BoltWristFromPart(part));CHECK(inverse&&Near(*inverse,part));}
    for(const auto& finger:fvr::bc2::M95BoltFingers)CHECK(feed_mechanism_detail::Pose(finger.wristFromFinger));
    if(MeasuredM95CustodyStrokeAndReturn()||HandAssignmentAndRendererValidation()||WrongOrderReleaseAndRegripCannotBypassStroke()||
       MovingMechanismNeverDragsWeaponAndSafetyLossBlocks()||RepeatedPacketCannotAccrueDwellOrReturnOutcome()||RightHandHistoricalContactUsesOriginalPacket()||
       FailedHandoffAndWrongReturnCannotSteal()||SustainedMeasuredCyclesAndLateExactCompletion())return 1;
    std::puts("Bolt custody: 8 groups passed, including 24 sustained measured-pose cycles; real shared arbiter/cycle, right regrip, ordered stroke, left attachment, history and exact late outcome.");return 0;
}
