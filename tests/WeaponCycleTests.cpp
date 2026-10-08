#include "Test.h"
#include "fvr/interaction/WeaponCycle.h"
#include <cstdio>
using namespace fvr::interaction;
namespace {
fvr::math::Matrix4 Identity(){fvr::math::Matrix4 m{};for(unsigned n=0;n<4;++n)m.values[n][n]=1;return m;}
struct Fixture {
    WeaponCycle policy;WeaponCycleProfile profile;WeaponCycleLease lease;WeaponCycleInput input;WeaponCycleResult result;
    Fixture(bool bolt=false){profile.id=1;profile.revision=1;profile.family=bolt?WeaponCycleFamily::Bolt:WeaponCycleFamily::Pump;
        profile.closedContact=Identity();profile.axis={0,0,-1};profile.stroke=.095f;profile.rearTolerance=.003f;profile.frontTolerance=.003f;
        profile.contactRadius=.06f;profile.lateralTolerance=.03f;profile.maxStepMeters=.03f;profile.endpointDwellNs=10000000;
        profile.rotationTolerance=.15f;profile.unlockRadians=bolt?1.1f:0;profile.maximumCycleNs=3000000000;
        input.source={{1,2,3,4},1,1000000000,1100000000,1000000000,true,{true,true},{false,false}};
        input.rawContact=Identity();input.mechanismClaim=true;lease={input.source.owner,{10,2},{20,3},30,0,1,1000000000,1100000000,true};
    }
    bool Begin(){return policy.Begin(profile,lease,input.source.nowNs);}
    WeaponCycleResult Step(float travel,bool grip=true,float angle=0){
        ++input.source.sequence;input.source.nowNs+=10000000;input.source.observedNs=input.source.nowNs;input.source.deadlineNs=input.source.nowNs+100000000;
        ++lease.sequence;lease.observedNs=input.source.nowNs;lease.deadlineNs=input.source.deadlineNs;
        input.rawContact=weapon_cycle_detail::Target(profile,travel,angle);input.grip=grip;
        return result=policy.Update(input,lease);
    }
    bool Grip(){if(!Begin())return false;Step(0,false);Step(0);return policy.Phase()==(profile.family==WeaponCycleFamily::Pump?WeaponCyclePhase::Rear:WeaponCyclePhase::Unlock);}
    void Stroke(float angle=0){for(float t:{.02f,.04f,.06f,.08f,.095f,.095f})Step(t,true,angle);
        for(float t:{.075f,.055f,.035f,.015f,0.f,0.f})Step(t,true,angle);}
    WeaponCycleReady Ready(){return {*result.release,lease.sequence+1,input.source.nowNs+1000,input.source.nowNs+100000000,true,true};}
};
int OrderedPumpAndNativeReceipt(){Fixture f;CHECK(f.Grip());f.Stroke();CHECK(f.result.phase==WeaponCyclePhase::AwaitingNative&&f.result.blocksFire&&f.result.release);
    CHECK(f.result.release->cycle.shot==0);auto ready=f.Ready();CHECK(f.policy.Complete(ready,ready.observedNs));
    CHECK(f.policy.Phase()==WeaponCyclePhase::Complete);CHECK(!f.policy.Begin(f.profile,f.lease,ready.observedNs));return 0;}
int NeutralAndNoFalsePartial(){Fixture f;CHECK(f.Begin());for(unsigned n=0;n<5;++n)f.Step(0);CHECK(f.policy.Phase()==WeaponCyclePhase::AwaitingGrip);
    f.Step(0,false);f.Step(0);for(float t:{.02f,.04f,.06f,.04f,.02f,0.f,0.f})f.Step(t);CHECK(f.policy.Phase()==WeaponCyclePhase::Rear&&!f.result.release);return 0;}
int OrderedBoltReuse(){Fixture f(true);CHECK(f.Grip());for(float a:{.4f,.8f,1.1f,1.1f})f.Step(0,true,a);CHECK(f.policy.Phase()==WeaponCyclePhase::Rear);
    f.Stroke(1.1f);CHECK(f.policy.Phase()==WeaponCyclePhase::Lock&&!f.result.release);
    for(float a:{.8f,.4f,0.f,0.f})f.Step(0,true,a);CHECK(f.policy.Phase()==WeaponCyclePhase::AwaitingNative);
    auto ready=f.Ready();CHECK(f.policy.Complete(ready,ready.observedNs));Fixture skip(true);CHECK(skip.Grip());skip.Step(.02f);CHECK(skip.policy.Phase()==WeaponCyclePhase::Cancelled);return 0;}
int NativeReceiptMustMatch(){for(unsigned test=0;test<9;++test){Fixture f;CHECK(f.Grip());f.Stroke();auto r=f.Ready();
    switch(test){case 0:++r.release.request;break;case 1:++r.release.cycle.owner.equipGeneration;break;case 2:++r.release.cycle.shot;break;
        case 3:++r.release.cycle.sequence;break;case 4:--r.sequence;r.sequence=f.lease.sequence;break;case 5:r.nativeReady=false;break;
        case 6:r.unchangedAmmunition=false;break;case 7:r.observedNs=f.result.release->observedNs-1;break;case 8:r.deadlineNs=r.observedNs;break;}
    CHECK(!f.policy.Complete(r,f.input.source.nowNs+1000));CHECK(f.policy.Phase()==WeaponCyclePhase::AwaitingNative);}return 0;}
int OwnerAndEvidenceLoss(){for(unsigned test=0;test<8;++test){Fixture f;CHECK(f.Grip());f.Step(.02f);
    switch(test){case 0:++f.lease.owner.equipGeneration;break;case 1:++f.input.source.owner.space;break;case 2:f.lease.held=false;break;
        case 3:f.lease.deadlineNs=f.input.source.nowNs;break;case 4:f.input.source.focused=false;break;
        case 5:f.input.source.tracked[0]=false;break;case 6:--f.lease.sequence;break;case 7:--f.lease.observedNs;break;}
    const auto out=f.policy.Update(f.input,f.lease);CHECK(out.phase==WeaponCyclePhase::Cancelled&&out.blocksFire&&!out.contact&&!out.release);}return 0;}
int RawDiscontinuityAndRelease(){for(unsigned test=0;test<4;++test){Fixture f;CHECK(f.Grip());f.Step(.02f);
    if(test==0)f.Step(.095f);else if(test==1)f.Step(.02f,false);else if(test==2){f.input.mechanismClaim=false;f.Step(.02f);}
    else {f.input.rawContact.values[3][0]=.1f;++f.input.source.sequence;f.result=f.policy.Update(f.input,f.lease);}
    CHECK(f.policy.Phase()==WeaponCyclePhase::Cancelled&&!f.result.release);}return 0;}
int DuplicateCannotAccrueDwell(){Fixture f;CHECK(f.Grip());for(float t:{.02f,.04f,.06f,.08f,.095f})f.Step(t);
    CHECK(f.policy.Phase()==WeaponCyclePhase::Rear);f.input.source.nowNs+=20000000;
    for(unsigned n=0;n<20;++n)f.policy.Update(f.input,f.lease);CHECK(f.policy.Phase()==WeaponCyclePhase::Rear);
    f.Step(.095f);CHECK(f.policy.Phase()==WeaponCyclePhase::Forward);return 0;}
int GuidedPoseDoesNotReplaceRaw(){Fixture f;CHECK(f.Grip());f.Step(.02f);CHECK(f.result.contact);
    const float shown=f.result.contact->values[3][2];CHECK(std::abs(shown+.02f)<1e-5f);
    for(unsigned n=0;n<10;++n)f.Step(.02f);CHECK(f.policy.Phase()==WeaponCyclePhase::Rear&&!f.result.release);return 0;}
int ProfileAndBound(){Fixture f;f.profile.axis={0,0,-2};CHECK(!f.Begin());Fixture valid;CHECK(valid.Grip());
    valid.input.source.nowNs+=4000000000ll;auto out=valid.policy.Update(valid.input,valid.lease);CHECK(out.failure==WeaponCycleFailure::Expired&&out.blocksFire);return 0;}
int OffCenterGrabUsesActualDisplacement(){Fixture f;CHECK(f.Begin());f.Step(0,false);f.input.rawContact=Identity();f.input.rawContact.values[3][0]=.02f;
    f.input.grip=true;++f.input.source.sequence;f.policy.Update(f.input,f.lease);CHECK(f.policy.Phase()==WeaponCyclePhase::Rear);
    f.Step(.02f);CHECK(f.result.phase==WeaponCyclePhase::Rear&&std::abs(f.result.travel-.02f)<1e-5f);return 0;}
int SameLeaseCannotRenew(){for(unsigned k=0;k<2;++k){Fixture f;CHECK(f.Grip());if(k==0)++f.lease.deadlineNs;else ++f.lease.observedNs;
    ++f.input.source.nowNs;CHECK(f.policy.Update(f.input,f.lease).failure==WeaponCycleFailure::Evidence);}return 0;}
int MissingFramesDoNotFinishStroke(){Fixture f;CHECK(f.Grip());for(float t:{.02f,.04f,.06f,.08f,.095f})f.Step(t);
    f.input.source.nowNs+=WeaponCycle::MaximumInputGapNs;f.Step(.095f);CHECK(f.policy.Phase()==WeaponCyclePhase::Cancelled&&!f.result.release);return 0;}
int VerifiedAlreadyHeldDoesNotNeedRegrip(){Fixture f;f.input.grip=true;
    HandInteraction arbiter;arbiter.Update(f.input.source);std::uint64_t intent=1;
    auto gun=arbiter.Acquire(f.input.source,{f.input.source.owner,InteractionHand::Right,HandClaimKind::GunHold,f.lease.item,
        {{1,1},f.input.source.sequence,f.input.source.deadlineNs,true},intent++,0});CHECK(gun.accepted&&gun.claim);
    auto support=arbiter.Acquire(f.input.source,{f.input.source.owner,InteractionHand::Left,HandClaimKind::WeaponSupport,f.lease.item,
        {f.lease.mechanism,f.input.source.sequence,f.input.source.deadlineNs,true},intent++,gun.claim->token.id});CHECK(support.accepted&&support.claim);
    CHECK(!f.policy.BeginHeld(f.profile,f.lease,f.input,*support.claim,*gun.claim));
    CHECK(arbiter.Release(f.input.source,support.claim->token).accepted);
    auto mechanism=arbiter.Acquire(f.input.source,{f.input.source.owner,InteractionHand::Left,HandClaimKind::Mechanism,f.lease.item,
        {f.lease.mechanism,f.input.source.sequence,f.input.source.deadlineNs,true},intent++,gun.claim->token.id});CHECK(mechanism.accepted&&mechanism.claim);
    CHECK(f.policy.BeginHeld(f.profile,f.lease,f.input,*mechanism.claim,*gun.claim));f.Stroke();CHECK(f.result.release);
    Fixture bad;bad.input.grip=true;auto stale=*mechanism.claim;--stale.inputSequence;
    CHECK(!bad.policy.BeginHeld(bad.profile,bad.lease,bad.input,stale,*gun.claim));
    stale=*mechanism.claim;++stale.token.contact.id;CHECK(!bad.policy.BeginHeld(bad.profile,bad.lease,bad.input,stale,*gun.claim));return 0;}
}
int main(){if(OrderedPumpAndNativeReceipt()||NeutralAndNoFalsePartial()||OrderedBoltReuse()||NativeReceiptMustMatch()||OwnerAndEvidenceLoss()||RawDiscontinuityAndRelease()||DuplicateCannotAccrueDwell()||GuidedPoseDoesNotReplaceRaw()||ProfileAndBound()||OffCenterGrabUsesActualDisplacement()||SameLeaseCannotRenew()||MissingFramesDoNotFinishStroke()||VerifiedAlreadyHeldDoesNotNeedRegrip())return 1;
    std::puts("13 weapon-cycle groups passed; portable gesture and exact native receipt contract only.");}
