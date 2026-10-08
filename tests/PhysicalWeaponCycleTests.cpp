#include "Test.h"
#include "fvr/interaction/PhysicalWeaponCycle.h"
#include <cstdio>
using namespace fvr::interaction;
namespace {
fvr::math::Matrix4 Pose(){fvr::math::Matrix4 m{};for(unsigned n=0;n<4;++n)m.values[n][n]=1;return m;}
struct Fixture {
    PhysicalWeaponCycle physical;HandInteraction hands;WeaponCycleProfile profile{};
    PhysicalWeaponCycleSample sample{};PhysicalWeaponCycleResult result{};
    std::uint64_t intent=0;std::optional<WeaponCycleRelease> sent;
    unsigned submissions=0;
    Fixture(){
        profile.id=1;profile.revision=2;profile.closedContact=Pose();profile.axis={0,0,-1};
        // Measured SPAS stroke; other values are explicit synthetic UX bounds.
        profile.stroke=.09495844f;profile.rearTolerance=.003f;profile.frontTolerance=.003f;
        profile.contactRadius=.06f;profile.lateralTolerance=.03f;profile.maxStepMeters=.03f;
        profile.endpointDwellNs=10000000;profile.rotationTolerance=.15f;profile.maximumCycleNs=2000000000;
        sample.source={{1,2,3,4},1,1000000000,1100000000,1000000000,true,{true,true},{false,false}};
        sample.lease={sample.source.owner,{10,2},{20,3},30,1,1,1000000000,1100000000,true};
        sample.rawContact=profile.closedContact;
        hands.Update(sample.source);RenewGun();
    }
    void RenewGun(){
        const auto gun=hands.Current(InteractionHand::Right);
        if(gun){hands.Renew(sample.source,gun->token,{{1,1},sample.source.sequence,sample.source.deadlineNs,true});sample.gun=gun->token;}
        else {const auto acquired=hands.Acquire(sample.source,{sample.source.owner,InteractionHand::Right,HandClaimKind::GunHold,
            sample.lease.item,{{1,1},sample.source.sequence,sample.source.deadlineNs,true},++intent,0});
            if(acquired.claim)sample.gun=acquired.claim->token;}
    }
    bool Begin(){return physical.Begin(profile,sample.lease,sample.source.nowNs);}
    void Advance(float travel,bool grip=true,bool renewSupport=true){
        ++sample.source.sequence;sample.source.nowNs+=10000000;sample.source.observedNs=sample.source.nowNs;
        sample.source.deadlineNs=sample.source.nowNs+100000000;sample.source.released[0]=!grip;
        ++sample.lease.sequence;sample.lease.observedNs=sample.source.nowNs;sample.lease.deadlineNs=sample.source.deadlineNs;
        sample.grip=grip;sample.rawContact=weapon_cycle_detail::Target(profile,travel,0);
        sample.contact={sample.lease.mechanism,sample.source.sequence,sample.source.deadlineNs,true};sample.acquireIntent=++intent;
        hands.Update(sample.source);RenewGun();
        if(const auto held=hands.Current(InteractionHand::Left);renewSupport&&held&&held->token.kind==HandClaimKind::WeaponSupport)
            hands.Renew(sample.source,held->token,{held->token.contact,sample.source.sequence,sample.source.deadlineNs,true});
    }
    auto Run(){result=physical.Update(sample,hands);if(result.release){sent=result.release;++submissions;}return result;}
    auto Step(float travel,bool grip=true){Advance(travel,grip);return Run();}
    bool Grip(){if(!Begin())return false;Step(0,false);Step(0);return physical.Phase()==WeaponCyclePhase::Rear;}
    void Back(){for(float t:{.02f,.04f,.06f,.08f,profile.stroke,profile.stroke})Step(t);}
    void Forward(){for(float t:{.075f,.055f,.035f,.015f,0.f,0.f})Step(t);}
    void Stroke(){Back();Forward();}
    WeaponCycleReady Ready(){return {*sent,sent->cycle.sequence+1,sample.source.nowNs+1000,sample.source.nowNs+100000000,true,true};}
    bool Support(){
        const auto support=hands.Acquire(sample.source,{sample.source.owner,InteractionHand::Left,HandClaimKind::WeaponSupport,
            sample.lease.item,{sample.lease.mechanism,sample.source.sequence,sample.source.deadlineNs,true},++intent,sample.gun.id});
        if(!support.claim)return false;sample.support=support.claim->token;return true;
    }
};
int InactiveCycleCannotInvalidateOtherConsumers(){for(unsigned state=0;state<2;++state){
    Fixture f;if(state){CHECK(f.Grip());f.Stroke();const auto ready=f.Ready();CHECK(f.physical.ObserveReady(ready,ready.observedNs));f.Step(0);}
    const auto saved=f.sample.source;auto later=saved;later.nowNs+=10000;
    CHECK(f.hands.Update(later).inputValid);const auto gun=f.hands.Current(InteractionHand::Right);CHECK(gun);
    const auto ammo=f.hands.Acquire(later,{later.owner,InteractionHand::Left,HandClaimKind::AmmoObject,{111,222},{{333,444},later.sequence,later.deadlineNs,true},++f.intent,0});
    CHECK(ammo.claim);const auto result=f.Run();
    CHECK(f.hands.Current(InteractionHand::Right)&&f.hands.Current(InteractionHand::Right)->token==gun->token);
    CHECK(f.hands.Current(InteractionHand::Left)&&f.hands.Current(InteractionHand::Left)->token==ammo.claim->token);
    CHECK(!result.target&&!result.release&&!result.ownsMechanism&&!result.cycle.blocksFire);
    CHECK(f.hands.Current(InteractionHand::Left)->deadlineNs==later.deadlineNs);
    // Global safety still owns expiration; an idle mechanism grants no renewal.
    later.nowNs=later.deadlineNs;CHECK(!f.hands.Update(later).inputValid);
    CHECK(!f.hands.Current(InteractionHand::Left)&&!f.hands.Current(InteractionHand::Right));
    }return 0;
}
int ActiveCycleStillRejectsReversedClock(){Fixture f;CHECK(f.Grip());auto later=f.sample.source;later.nowNs+=10000;
    CHECK(f.hands.Update(later).inputValid);f.Run();CHECK(!f.result.ownsMechanism&&!f.result.target&&f.result.cycle.blocksFire);
    CHECK(!f.hands.Current(InteractionHand::Left)&&!f.hands.Current(InteractionHand::Right));return 0;}
int CompletePumpAndRepeatedShot(){Fixture f;CHECK(f.Grip());CHECK(f.result.ownsMechanism&&f.result.target&&f.result.cycle.blocksFire);
    f.Stroke();CHECK(f.submissions==1&&f.physical.Phase()==WeaponCyclePhase::AwaitingNative&&f.result.cycle.blocksFire);
    CHECK(!f.result.ownsMechanism&&!f.result.target&&!f.hands.Current(InteractionHand::Left));
    for(unsigned n=0;n<5;++n)f.Step(0);CHECK(f.submissions==1&&f.result.cycle.blocksFire);
    auto ready=f.Ready();CHECK(f.physical.ObserveReady(ready,ready.observedNs));f.Step(0);
    CHECK(!f.result.cycle.blocksFire&&f.physical.Phase()==WeaponCyclePhase::Complete);
    CHECK(!f.Begin());++f.sample.lease.cycle;++f.sample.lease.shot;CHECK(f.Begin());
    f.Step(0,false);f.Step(0);f.Stroke();CHECK(f.submissions==2&&f.sent->cycle.shot==2);return 0;
}
int ExplicitSupportTransfer(){Fixture f;f.sample.grip=true;CHECK(f.Support());const auto original=*f.sample.support;
    CHECK(f.Begin());f.Step(0);CHECK(f.physical.Phase()==WeaponCyclePhase::Rear&&f.result.target&&f.result.ownsMechanism);
    CHECK(f.hands.Current(InteractionHand::Left)->token.id!=original.id);
    CHECK(f.hands.Current(InteractionHand::Left)->token.kind==HandClaimKind::Mechanism);
    CHECK(!f.hands.Release(f.sample.source,original).accepted);f.Stroke();CHECK(f.submissions==1);return 0;
}
int HeldBitAndPartialStrokeCannotCycle(){Fixture f;CHECK(f.Begin());for(unsigned n=0;n<5;++n)f.Step(0);
    CHECK(f.physical.Phase()==WeaponCyclePhase::AwaitingGrip&&!f.result.ownsMechanism);
    f.Step(0,false);f.Step(0);for(float t:{.02f,.04f,.06f,.04f,.02f,0.f,0.f})f.Step(t);
    CHECK(f.submissions==0&&f.physical.Phase()==WeaponCyclePhase::Rear&&f.result.cycle.blocksFire);return 0;
}
int ReleaseRegripRestartsFullStroke(){Fixture f;CHECK(f.Grip());f.Back();f.Step(.075f,false);
    CHECK(f.physical.Phase()==WeaponCyclePhase::Cancelled&&!f.result.target&&!f.hands.Current(InteractionHand::Left));
    f.Step(0);CHECK(f.physical.Phase()==WeaponCyclePhase::Cancelled);
    f.Step(0,false);CHECK(f.physical.Phase()==WeaponCyclePhase::AwaitingGrip);f.Step(0);
    CHECK(f.physical.Phase()==WeaponCyclePhase::Rear);
    for(unsigned n=0;n<3;++n)f.Step(0);CHECK(!f.submissions);f.Stroke();CHECK(f.submissions==1);return 0;
}
int TrackingInterruptionNeedsNeutral(){Fixture f;CHECK(f.Grip());f.Step(.02f);f.Advance(.04f);f.sample.source.tracked[0]=false;f.Run();
    CHECK(f.physical.Phase()==WeaponCyclePhase::Cancelled&&!f.result.target&&!f.result.ownsMechanism);
    f.sample.source.tracked[0]=true;f.Step(0);CHECK(f.physical.Phase()==WeaponCyclePhase::Cancelled);
    f.Step(0,false);f.Step(0);CHECK(f.physical.Phase()==WeaponCyclePhase::Rear);f.Stroke();CHECK(f.submissions==1);return 0;
}
int NativeSubmissionSurvivesPhysicalInterruptions(){for(unsigned mutation=0;mutation<4;++mutation){Fixture f;CHECK(f.Grip());f.Stroke();
    const auto release=*f.sent;f.Advance(0,false);f.sample.lease.held=false;
    if(mutation==0)f.sample.source.tracked[0]=false;
    if(mutation==1)f.sample.source.focused=false;
    if(mutation==2)++f.sample.source.owner.equipGeneration;
    if(mutation==3)++f.sample.source.owner.space;
    f.Run();CHECK(f.physical.Phase()==WeaponCyclePhase::AwaitingNative&&f.submissions==1&&f.result.cycle.blocksFire);
    auto ready=f.Ready();CHECK(ready.release==release);CHECK(f.physical.ObserveReady(ready,ready.observedNs));
    CHECK(f.physical.Phase()==WeaponCyclePhase::Complete);}return 0;
}
int NativeReadyAndRejectionAreExact(){for(unsigned mutation=0;mutation<6;++mutation){Fixture f;CHECK(f.Grip());f.Stroke();auto ready=f.Ready();
    switch(mutation){case 0:++ready.release.request;break;case 1:++ready.release.cycle.shot;break;
        case 2:ready.nativeReady=false;break;case 3:ready.unchangedAmmunition=false;break;
        case 4:ready.sequence=ready.release.cycle.sequence;break;case 5:ready.deadlineNs=ready.observedNs;break;}
    CHECK(!f.physical.ObserveReady(ready,ready.observedNs));CHECK(f.physical.Phase()==WeaponCyclePhase::AwaitingNative);
    auto wrong=*f.sent;++wrong.request;f.physical.RejectNative(wrong);CHECK(f.physical.Phase()==WeaponCyclePhase::AwaitingNative);
    f.physical.RejectNative(*f.sent);CHECK(f.physical.Phase()==WeaponCyclePhase::Cancelled);
    f.Step(0,false);f.Step(0);CHECK(f.physical.Phase()==WeaponCyclePhase::Cancelled&&f.submissions==1);}return 0;
}
int ContactAndOtherRolesAreNotStolen(){for(unsigned mutation=0;mutation<5;++mutation){Fixture f;CHECK(f.Begin());f.Step(0,false);f.Advance(0);
    std::optional<HandClaimToken> other;
    if(mutation==0){const auto acquired=f.hands.Acquire(f.sample.source,{f.sample.source.owner,InteractionHand::Left,HandClaimKind::AmmoObject,
        {50,1},{{51,1},f.sample.source.sequence,f.sample.source.deadlineNs,true},++f.intent,0});CHECK(acquired.claim);other=acquired.claim->token;f.sample.acquireIntent=++f.intent;}
    if(mutation==1)--f.sample.contact.inputSequence;
    if(mutation==2)++f.sample.contact.key.generation;
    if(mutation==3)f.sample.rawContact.values[3][0]=.5f;
    if(mutation==4)f.sample.contact.eligible=false;
    f.Run();CHECK(!f.result.ownsMechanism&&!f.result.target&&f.submissions==0);
    if(other)CHECK(f.hands.Current(InteractionHand::Left)->token==*other);
    f.Step(0);CHECK(!f.result.ownsMechanism); // Failed press cannot become a held-grip retry.
    }return 0;
}
int NativeLeaseLossCannotRearm(){for(unsigned mutation=0;mutation<4;++mutation){Fixture f;CHECK(f.Grip());f.Advance(.02f);
    if(mutation==0)f.sample.lease.held=false;
    if(mutation==1)--f.sample.lease.cycle;
    if(mutation==2)f.sample.lease.deadlineNs=f.sample.source.nowNs;
    if(mutation==3)f.sample.lease.sequence=1;
    f.Run();CHECK(f.physical.Phase()==WeaponCyclePhase::Cancelled&&!f.result.target);
    f.sample.lease.held=true;f.sample.lease.cycle=30;f.sample.lease.sequence=100;
    f.Step(0,false);f.Step(0);CHECK(f.physical.Phase()==WeaponCyclePhase::Cancelled&&f.submissions==0);}return 0;
}
int OriginalCycleDeadlineSurvivesRecovery(){Fixture f;f.profile.maximumCycleNs=200000000;CHECK(f.Grip());f.Step(.02f);f.Step(0,false);
    f.Step(0,false);f.Step(0);CHECK(f.physical.Phase()==WeaponCyclePhase::Rear);
    for(unsigned n=0;n<20;++n)f.Step(0);CHECK(f.physical.Phase()==WeaponCyclePhase::Cancelled&&f.submissions==0&&f.result.cycle.blocksFire);
    f.Step(0,false);f.Step(0);CHECK(f.physical.Phase()==WeaponCyclePhase::Cancelled);return 0;
}
int PresentationExpiresAndRejectsNewOwners(){Fixture f;CHECK(f.Grip());f.Step(.02f);CHECK(f.result.target);
    const auto target=*f.result.target;const auto mechanism=*f.hands.Current(InteractionHand::Left),gun=*f.hands.Current(InteractionHand::Right);
    CHECK(CurrentPhysicalWeaponCycleTarget(target,f.sample.lease,f.sample.source,mechanism,gun));
    for(unsigned mutation=0;mutation<8;++mutation){auto input=f.sample.source;auto lease=f.sample.lease;auto hand=mechanism;auto copy=target;
        switch(mutation){case 0:input.nowNs=target.deadlineNs;break;case 1:++hand.token.id;break;case 2:++lease.cycle;break;
            case 3:input.focused=false;break;case 4:input.tracked[0]=false;break;case 5:++copy.inputSequence;break;
            case 6:++copy.lease.deadlineNs;break;case 7:copy.contact.values[3][0]=std::numeric_limits<float>::quiet_NaN();break;}
        CHECK(!CurrentPhysicalWeaponCycleTarget(copy,lease,input,hand,gun));}
    f.Step(.04f);CHECK(f.result.target&&f.result.target->inputSequence>target.inputSequence);
    CHECK(!CurrentPhysicalWeaponCycleTarget(target,f.sample.lease,f.sample.source,mechanism,gun));return 0;
}
int DuplicateInputCannotCompleteOrRenew(){Fixture f;CHECK(f.Grip());for(float t:{.02f,.04f,.06f,.08f,f.profile.stroke})f.Step(t);
    CHECK(f.physical.Phase()==WeaponCyclePhase::Rear&&f.result.target);const auto deadline=f.result.target->deadlineNs;
    f.sample.source.nowNs+=20000000;for(unsigned n=0;n<10;++n)f.Run();
    CHECK(f.physical.Phase()==WeaponCyclePhase::Rear&&f.result.target->deadlineNs==deadline&&f.submissions==0);
    f.Step(f.profile.stroke);CHECK(f.physical.Phase()==WeaponCyclePhase::Forward);return 0;
}
int LosingAnOldClaimDoesNotReleaseReplacement(){Fixture f;CHECK(f.Grip());const auto old=f.hands.Current(InteractionHand::Left)->token;
    f.Advance(.02f);CHECK(f.hands.Release(f.sample.source,old).accepted);
    const auto replacement=f.hands.Acquire(f.sample.source,{f.sample.source.owner,InteractionHand::Left,HandClaimKind::Sight,
        f.sample.lease.item,{{77,1},f.sample.source.sequence,f.sample.source.deadlineNs,true},++f.intent,f.sample.gun.id});CHECK(replacement.claim);
    f.Run();CHECK(f.physical.Phase()==WeaponCyclePhase::Cancelled&&!f.result.ownsMechanism);
    CHECK(f.hands.Current(InteractionHand::Left)->token==replacement.claim->token);return 0;
}
int NativeWaitHasOriginalBound(){Fixture f;CHECK(f.Grip());f.Stroke();f.sample.source.nowNs=4000000000;
    f.Run();CHECK(f.physical.Phase()==WeaponCyclePhase::Cancelled&&f.result.cycle.blocksFire&&f.submissions==1);
    const auto ready=f.Ready();CHECK(!f.physical.ObserveReady(ready,ready.observedNs));
    ++f.sample.lease.cycle;f.sample.lease.observedNs=f.sample.source.nowNs;f.sample.lease.deadlineNs=f.sample.source.nowNs+100000000;
    CHECK(!f.Begin()); // Ambiguous timed-out request cannot be replaced.
    f.physical.RejectNative(*f.sent);CHECK(f.Begin());return 0;
}
int PhysicalInterruptionMatrix(){for(unsigned mutation=0;mutation<4;++mutation){Fixture f;CHECK(f.Grip());f.Step(.02f);f.Advance(.04f);
    if(mutation==0)f.sample.source.focused=false;
    if(mutation==1)f.sample.source.tracked[1]=false;
    if(mutation==2)f.sample.rawContact.values[3][0]+=.1f;
    if(mutation==3)CHECK(f.hands.Release(f.sample.source,f.sample.gun).accepted);
    f.Run();CHECK(f.physical.Phase()==WeaponCyclePhase::Cancelled&&!f.result.target&&!f.result.ownsMechanism&&f.result.cycle.blocksFire);
    f.sample.source.focused=true;f.sample.source.tracked={true,true};f.Step(0);CHECK(f.physical.Phase()==WeaponCyclePhase::Cancelled);
    f.Step(0,false);f.Step(0);CHECK(f.physical.Phase()==WeaponCyclePhase::Rear);f.Stroke();CHECK(f.submissions==1);
    }return 0;
}
int OwnerRetirementDoesNotRearm(){for(unsigned mutation=0;mutation<3;++mutation){Fixture f;CHECK(f.Grip());f.Advance(.02f);
    if(mutation==0)++f.sample.source.owner.equipGeneration;
    if(mutation==1)++f.sample.source.owner.actorGeneration;
    if(mutation==2)++f.sample.source.owner.space;
    f.Run();CHECK(f.physical.Phase()==WeaponCyclePhase::Cancelled&&!f.result.target);
    f.sample.source.owner=f.sample.lease.owner;f.Step(0,false);f.Step(0);CHECK(f.physical.Phase()==WeaponCyclePhase::Cancelled&&f.submissions==0);
    }return 0;
}
int PhysicalClaimOutlivesShortNativeView(){Fixture f;CHECK(f.Grip());const auto token=f.hands.Current(InteractionHand::Left)->token;
    for(float travel:{.02f,.04f,.06f}){f.Advance(travel);f.sample.lease.deadlineNs=f.sample.source.nowNs+5000000;f.Run();
        CHECK(f.result.ownsMechanism&&f.result.target&&f.hands.Current(InteractionHand::Left)->token==token);
        CHECK(f.hands.Current(InteractionHand::Left)->deadlineNs==f.sample.source.deadlineNs);
        CHECK(f.result.target->deadlineNs==f.sample.lease.deadlineNs);}
    return 0;
}
struct DelayedFixture {
    Fixture f;
    PhysicalWeaponCycleResult Step(float travel,bool grip=true){
        const auto original=f.sample;f.Advance(travel,grip);const auto fresh=f.sample.rawContact;
        f.sample.contactSource=original.source;f.sample.rawContact=original.rawContact;
        f.sample.contact={f.sample.lease.mechanism,original.source.sequence,original.source.deadlineNs,true};
        const auto result=f.Run();f.sample.rawContact=fresh;f.sample.contactSource.reset();return result;
    }
    bool Grip(){if(!f.Begin())return false;Step(0,false);Step(0,false);Step(0);Step(0);return f.physical.Phase()==WeaponCyclePhase::Rear;}
};
int OriginalRendererPacketControlsGesture(){DelayedFixture d;CHECK(d.Grip());auto& f=d.f;
    for(float t:{.02f,.04f,.06f,.08f,f.profile.stroke,f.profile.stroke,.075f,.055f,.035f,.015f,0.f,0.f,0.f})d.Step(t);
    CHECK(f.submissions==1&&f.sent&&f.sent->inputSequence+1==f.sample.source.sequence);
    CHECK(f.sent->observedNs+10000000==f.sample.source.observedNs);CHECK(f.result.cycle.blocksFire);return 0;}
int OriginalContactKeepsClaimDeadline(){DelayedFixture d;CHECK(d.Grip());auto& f=d.f;const auto token=f.hands.Current(InteractionHand::Left)->token;
    const auto prior=f.sample.source;const auto result=d.Step(.02f);
    CHECK(result.target&&result.ownsMechanism&&result.target->inputSequence==prior.sequence&&result.target->observedNs==prior.observedNs);
    CHECK(f.hands.Current(InteractionHand::Left)->token==token&&f.hands.Current(InteractionHand::Left)->deadlineNs==prior.deadlineNs);
    CHECK(result.target->deadlineNs<=prior.deadlineNs);return 0;}
int RestampedContactCannotRenew(){for(unsigned mutation=0;mutation<3;++mutation){DelayedFixture d;CHECK(d.Grip());auto& f=d.f;
    auto original=f.sample;f.Advance(.02f);f.sample.contactSource=original.source;f.sample.rawContact=original.rawContact;
    if(mutation==0)++f.sample.contactSource->observedNs;if(mutation==1)++f.sample.contactSource->deadlineNs;
    if(mutation==2)++f.sample.contactSource->owner.equipGeneration;
    f.sample.contact={f.sample.lease.mechanism,original.source.sequence,original.source.deadlineNs,true};f.Run();
    CHECK(f.result.cycle.blocksFire&&!f.result.ownsMechanism&&!f.result.target&&!f.submissions);}return 0;}
int OriginalSupportTransferAndImmediateCurrentRelease(){DelayedFixture d;auto& f=d.f;f.sample.grip=true;CHECK(f.Support());CHECK(f.Begin());
    const auto result=d.Step(0);CHECK(result.ownsMechanism&&result.target&&f.physical.Phase()==WeaponCyclePhase::Rear);
    const auto dropped=d.Step(.02f,false);CHECK(!dropped.ownsMechanism&&!dropped.target&&dropped.cycle.blocksFire&&!f.submissions);return 0;}
int SupportRenewedByPreviousRendererPacket(){
    // Gameplay processes the mechanism before this frame's ordinary support
    // renewal. The original renderer packet and existing support are both N-1.
    Fixture f;f.sample.grip=true;CHECK(f.Support());const auto support=*f.sample.support;
    CHECK(f.Begin());const auto original=f.sample.source;f.Advance(0,true,false);
    f.sample.contactSource=original;f.sample.contact={f.sample.lease.mechanism,original.sequence,original.deadlineNs,true};
    f.Run();CHECK(f.result.ownsMechanism&&f.result.target&&f.physical.Phase()==WeaponCyclePhase::Rear);
    const auto claim=f.hands.Current(InteractionHand::Left);CHECK(claim&&claim->token.kind==HandClaimKind::Mechanism);
    CHECK(claim->token.id!=support.id&&claim->inputSequence==original.sequence&&claim->deadlineNs==original.deadlineNs);
    CHECK(f.result.target->inputSequence==original.sequence);return 0;
}
int DelayedSupportTransferRejectsStaleEvidence(){for(unsigned mutation=0;mutation<5;++mutation){
    Fixture f;f.sample.grip=true;CHECK(f.Support());CHECK(f.Begin());auto original=f.sample.source;
    if(mutation==0){for(unsigned n=0;n<11;++n)f.Advance(0,true,false);original=f.sample.source;} // Expired support cannot transfer.
    f.Advance(0,true,false);f.sample.contactSource=original;
    if(mutation==1)++f.sample.contactSource->observedNs;
    if(mutation==2)++f.sample.contactSource->deadlineNs;
    if(mutation==3)f.sample.source.released[0]=true;
    if(mutation==4)++f.sample.support->id;
    f.sample.contact={f.sample.lease.mechanism,original.sequence,original.deadlineNs,true};
    f.Run();CHECK(!f.result.ownsMechanism&&!f.result.target&&!f.submissions);
    }return 0;
}
int HeldSupportCannotReplaySpentPress(){Fixture f;CHECK(f.Begin());f.Step(0,false);f.Advance(0);
    f.sample.contact.eligible=false;f.Run();CHECK(!f.result.ownsMechanism);
    CHECK(f.Support());const auto token=*f.sample.support;
    f.Step(0);CHECK(!f.result.ownsMechanism&&!f.result.target&&!f.submissions);
    CHECK(f.hands.Current(InteractionHand::Left)->token==token);return 0;
}
}
int main(){if(InactiveCycleCannotInvalidateOtherConsumers()||ActiveCycleStillRejectsReversedClock()||CompletePumpAndRepeatedShot()||ExplicitSupportTransfer()||HeldBitAndPartialStrokeCannotCycle()||
    ReleaseRegripRestartsFullStroke()||TrackingInterruptionNeedsNeutral()||NativeSubmissionSurvivesPhysicalInterruptions()||
    NativeReadyAndRejectionAreExact()||ContactAndOtherRolesAreNotStolen()||NativeLeaseLossCannotRearm()||
    OriginalCycleDeadlineSurvivesRecovery()||PresentationExpiresAndRejectsNewOwners()||DuplicateInputCannotCompleteOrRenew()||
    LosingAnOldClaimDoesNotReleaseReplacement()||NativeWaitHasOriginalBound()||PhysicalInterruptionMatrix()||
    OwnerRetirementDoesNotRearm()||PhysicalClaimOutlivesShortNativeView()||OriginalRendererPacketControlsGesture()||OriginalContactKeepsClaimDeadline()||
    RestampedContactCannotRenew()||OriginalSupportTransferAndImmediateCurrentRelease()||
    SupportRenewedByPreviousRendererPacket()||DelayedSupportTransferRejectsStaleEvidence()||HeldSupportCannotReplaySpentPress())return 1;
    std::puts("26 physical weapon-cycle groups passed; real arbiter/controller policy, mocked native held/ready receipts.");}
