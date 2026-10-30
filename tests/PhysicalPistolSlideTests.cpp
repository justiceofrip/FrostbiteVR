#include "Test.h"
#include "fvr/interaction/PhysicalPistolSlide.h"
#include <type_traits>
#include <limits>
using namespace fvr::interaction;
namespace {
template<class T> concept HasLoaded=requires(T t){t.loaded;};
static_assert(!HasLoaded<PistolSlideAuthority> && !HasLoaded<PistolSlideReady>);
static_assert(!std::is_convertible_v<WeaponCycleLease,PistolSlideAuthority>);
static_assert(!std::is_convertible_v<WeaponCycleReady,PistolSlideReady>);
static_assert(!std::is_copy_constructible_v<PhysicalPistolSlide>);
fvr::math::Matrix4 Pose(){fvr::math::Matrix4 p{};for(unsigned n=0;n<4;++n)p.values[n][n]=1;return p;}
struct Fixture {
    PhysicalPistolSlide slide;HandInteraction hands;PistolSlideProfile profile{};
    WeaponMechanismDescriptor descriptor{};PhysicalPistolSlideSample sample{};
    PhysicalPistolSlideResult result{};std::optional<PistolSlideRequest> sent;
    std::uint64_t intent=0;unsigned submissions=0;
    Fixture(){
        // Synthetic metre geometry, not native pistol geometry qualification.
        profile.id=11;profile.revision=2;profile.closedContact=Pose();profile.stroke=.04f;
        profile.frontTolerance=.002f;profile.rearTolerance=.002f;profile.hysteresis=.002f;
        profile.contactRadius=.03f;profile.lateralTolerance=.015f;profile.rotationTolerance=.2f;
        profile.maxStepMeters=.015f;profile.endpointDwellNs=10000000;profile.maximumStrokeNs=2000000000;
        descriptor.id=21;descriptor.revision=3;descriptor.afterEmptyFeed=WeaponAction::Slide;
        descriptor.tacticalRetainsChamber=true;descriptor.feedPlan.stepCount=2;
        descriptor.feedPlan.steps[0]={ReloadOperation::UnseatMagazine,1};
        descriptor.feedPlan.steps[1]={ReloadOperation::SeatMagazine,1};
        descriptor.feedPlan.maxSampleGapNs=100000000;descriptor.feedPlan.ackTimeoutNs=200000000;
        descriptor.feedPlan.transactionTimeoutNs=2000000000;
        sample.source={{1,2,3,4},1,1000000000,1100000000,1000000000,true,{true,true},{false,false}};
        sample.authority.identity={sample.source.owner,{10,1},{20,2},{30,3},{40,4},21,3,51,61,71};
        sample.authority.sequence=1;sample.authority.observedNs=sample.source.observedNs;
        sample.authority.deadlineNs=sample.source.deadlineNs;sample.authority.chamber=ChamberKnowledge::Empty;
        sample.authority.verifiedAfterFeed=true;sample.authority.feedSeated=true;sample.authority.cycleOwed=true;
        sample.rawContact=profile.closedContact;sample.grip=true;AcquireGun();
    }
    bool AcquireGun(){
        hands.Update(sample.source);
        auto current=hands.Current(profile.hands.gun);
        if(current&&current->token.owner==sample.source.owner&&current->token.item==sample.authority.identity.item){
            sample.gun=current->token;
            hands.Renew(sample.source,sample.gun,{sample.gun.contact,sample.source.sequence,sample.source.deadlineNs,true});
        }else{
            const auto acquired=hands.Acquire(sample.source,{sample.source.owner,profile.hands.gun,HandClaimKind::GunHold,
                sample.authority.identity.item,{{90,1},sample.source.sequence,sample.source.deadlineNs,true},++intent,0});
            if(!acquired.claim)return false;sample.gun=acquired.claim->token;
        }
        sample.gunContact={sample.gun.contact,sample.source.sequence,sample.source.deadlineNs,true};return true;
    }
    bool Begin(){return slide.Begin(profile,descriptor,WeaponMechanismIntent::EmptyFeed,sample.authority,sample.source.nowNs);}
    void Advance(float travel,bool grip=true,std::int64_t elapsed=10000000){
        ++sample.source.sequence;sample.source.nowNs+=elapsed;sample.source.observedNs=sample.source.nowNs;
        sample.source.deadlineNs=sample.source.nowNs+100000000;
        sample.source.released[weapon_cycle_detail::HandIndex(profile.hands.mechanism)]=!grip;
        ++sample.authority.sequence;sample.authority.observedNs=sample.source.observedNs;
        sample.authority.deadlineNs=sample.source.deadlineNs;sample.grip=grip;
        sample.rawContact=profile.closedContact;
        for(unsigned k=0;k<3;++k)sample.rawContact.values[3][k]+=profile.axis[k]*travel;
        sample.contact={sample.authority.identity.mechanism,sample.source.sequence,sample.source.deadlineNs,true};
        sample.gunContact={sample.gun.contact,sample.source.sequence,sample.source.deadlineNs,true};
        sample.acquireIntent=++intent;
    }
    auto Run(){result=slide.Update(sample,hands);if(result.request){sent=result.request;++submissions;}return result;}
    auto Step(float travel,bool grip=true){Advance(travel,grip);return Run();}
    bool Grip(){if(!Begin())return false;Step(0,false);Step(0);return slide.Phase()==PistolSlidePhase::Pull;}
    void Back(){for(float t:{.01f,.02f,.03f,.04f,.04f})Step(t);}
    void Forward(){for(float t:{.03f,.02f,.01f,0.f,0.f})Step(t);}
    void Stroke(){Back();Forward();Step(0,false);}
    auto Ready()const {return PistolSlideReady{*sent,{sent->authority.identity,ChamberKnowledge::Occupied,true,true},
        sent->authority.sequence+1,sample.source.nowNs+1000,sample.source.nowNs+100000000};}
};
int MeasuredStrokeAndExactAcknowledgement(){Fixture f;CHECK(f.Grip());
    CHECK(f.result.ownsMechanism&&f.result.target&&f.result.blocksFire);
    CHECK(f.result.target->identity==f.sample.authority.identity&&f.result.target->inputSequence==f.sample.source.sequence);
    f.Back();CHECK(f.slide.Phase()==PistolSlidePhase::Return&&!f.submissions);
    f.Forward();CHECK(f.slide.Phase()==PistolSlidePhase::AwaitingRelease&&!f.submissions);
    f.Step(0,false);CHECK(f.submissions==1&&f.slide.Phase()==PistolSlidePhase::AwaitingNative&&f.result.blocksFire);
    CHECK(!f.result.ownsMechanism&&!f.result.target&&!f.hands.Current(InteractionHand::Left));
    CHECK(f.sent->mechanism.prerequisiteClaim==f.sent->gun.id&&f.sent->authority.identity==f.sample.authority.identity);
    for(unsigned n=0;n<5;++n){f.Run();f.Step(0,false);CHECK(!f.result.request&&f.result.blocksFire);}
    CHECK(f.submissions==1);const auto ready=f.Ready();CHECK(f.slide.ObserveReady(ready,ready.observedNs));
    CHECK(!f.slide.ObserveReady(ready,ready.observedNs));f.Run();CHECK(!f.result.blocksFire&&!f.slide.Owed());
    CHECK(!f.Begin());return 0;
}
int AuthorityDefaultsAndSelectionFailClosed(){
    for(unsigned mutation=0;mutation<18;++mutation){Fixture f;
        switch(mutation){
        case 0:f.sample.authority={};break;
        case 1:f.sample.authority.verifiedAfterFeed=false;break;
        case 2:f.sample.authority.feedSeated=false;break;
        case 3:f.sample.authority.cycleOwed=false;break;
        case 4:f.sample.authority.chamber=ChamberKnowledge::Unknown;break;
        case 5:f.sample.authority.chamber=ChamberKnowledge::Occupied;break;
        case 6:f.sample.authority.identity.magazine.generation=0;break;
        case 7:f.sample.authority.identity.ammunition.id=0;break;
        case 8:f.sample.authority.identity.owner.actorGeneration=0;break;
        case 9:f.sample.authority.identity.feedTransaction=0;break;
        case 10:f.sample.authority.identity.grant=0;break;
        case 11:f.sample.authority.identity.cycle=0;break;
        case 12:++f.sample.authority.identity.revision;break;
        case 13:f.sample.authority.deadlineNs=f.sample.source.nowNs;break;
        case 14:f.descriptor.afterEmptyFeed=WeaponAction::Pump;break;
        case 15:f.descriptor.afterEmptyFeed=WeaponAction::Bolt;break;
        case 16:f.descriptor.afterEmptyFeed=WeaponAction::ChargingHandle;break;
        case 17:f.descriptor.afterEmptyFeed=WeaponAction::Automatic;break;
        }
        CHECK(!f.Begin());CHECK(!f.slide.Owed());f.Step(0,false);f.Step(0);f.Stroke();CHECK(!f.submissions);
    }
    Fixture occupied;occupied.sample.authority.chamber=ChamberKnowledge::Occupied;
    const auto selection=SelectWeaponMechanismPlan(occupied.descriptor,WeaponMechanismIntent::TacticalFeed,ChamberKnowledge::Occupied);
    CHECK(selection.plan&&selection.plan->purpose==WeaponCyclePurpose::None);
    CHECK(!occupied.slide.Begin(occupied.profile,occupied.descriptor,WeaponMechanismIntent::TacticalFeed,
        occupied.sample.authority,occupied.sample.source.nowNs));
    Fixture afterShot;afterShot.descriptor.afterShot=WeaponAction::Slide;
    CHECK(!afterShot.slide.Begin(afterShot.profile,afterShot.descriptor,WeaponMechanismIntent::AfterShot,
        afterShot.sample.authority,afterShot.sample.source.nowNs));
    // A matching feed plan is not permission to relabel belt mechanics as slide.
    Fixture belt;belt.descriptor.feed=WeaponFeed::Belt;belt.descriptor.feedPlan.stepCount=3;
    belt.descriptor.feedPlan.steps[0]={ReloadOperation::OpenBreech,1};
    belt.descriptor.feedPlan.steps[1]={ReloadOperation::InsertRound,1};
    belt.descriptor.feedPlan.steps[2]={ReloadOperation::CloseBreech,1};CHECK(!belt.Begin());return 0;
}
int CanonicalMetresAndFiniteGeometry(){for(unsigned mutation=0;mutation<12;++mutation){Fixture f;
    switch(mutation){case 0:f.profile.stroke=4;break;case 1:f.profile.stroke=.001f;break;
    case 2:f.profile.axis={0,0,-100};break;case 3:f.profile.closedContact.values[0][0]=100;break;
    case 4:f.profile.closedContact.values[0][0]=-1;break;case 5:f.profile.closedContact.values[3][3]=0;break;
    case 6:f.profile.closedContact.values[3][0]=std::numeric_limits<float>::quiet_NaN();break;
    case 7:f.profile.hysteresis=0;break;case 8:f.profile.endpointDwellNs=0;break;
    case 9:f.profile.maxStepMeters=f.profile.stroke;break;
    case 10:f.profile.hands.mechanism=f.profile.hands.gun;break;
    case 11:f.profile.rearTolerance=f.profile.stroke*.5f;break;}
    CHECK(!f.Begin());}return 0;
}
int NeutralDwellAndHysteresis(){Fixture f;CHECK(f.Begin());for(unsigned n=0;n<6;++n)f.Step(0);
    CHECK(f.slide.Phase()==PistolSlidePhase::AwaitingGrip&&!f.result.ownsMechanism);
    f.Step(0,false);f.Step(0);for(float t:{.01f,.02f,.03f,.04f})f.Step(t);
    CHECK(f.slide.Phase()==PistolSlidePhase::Pull);
    for(unsigned n=0;n<4;++n){f.sample.source.nowNs+=1000000;f.Run();}
    CHECK(f.slide.Phase()==PistolSlidePhase::Pull&&!f.submissions);
    // Leaving the hysteresis band restarts the endpoint dwell.
    f.Step(.034f);f.Step(.04f);CHECK(f.slide.Phase()==PistolSlidePhase::Pull);
    f.Step(.04f);CHECK(f.slide.Phase()==PistolSlidePhase::Return);
    for(float t:{.03f,.02f,.01f,0.f})f.Step(t);CHECK(f.slide.Phase()==PistolSlidePhase::Return);
    f.Step(.006f);f.Step(0);CHECK(f.slide.Phase()==PistolSlidePhase::Return);
    f.Step(0);CHECK(f.slide.Phase()==PistolSlidePhase::AwaitingRelease);
    f.Step(.006f);CHECK(f.slide.Phase()==PistolSlidePhase::Return);
    f.Step(0);f.Step(0);f.Step(0,false);CHECK(f.submissions==1);return 0;
}
int PartialJitterAndEarlyReleaseCannotComplete(){for(unsigned mode=0;mode<3;++mode){Fixture f;CHECK(f.Grip());
    if(mode==0)for(unsigned n=0;n<12;++n){f.Step(.003f);f.Step(0);}
    if(mode==1)for(float t:{.01f,.02f,.025f,.02f,.01f,0.f,0.f})f.Step(t);
    if(mode==2){f.Back();f.Step(.03f);}
    f.Step(mode==2?.02f:0.f,false);CHECK(f.submissions==0&&f.slide.Owed()&&!f.result.ownsMechanism);
    f.Step(0);CHECK(!f.result.ownsMechanism);f.Step(0,false);f.Step(0);f.Stroke();CHECK(f.submissions==1);
    }return 0;
}
int RawPoseRejectsJumpOffAxisAndBadFrames(){for(unsigned mode=0;mode<6;++mode){Fixture f;CHECK(f.Grip());f.Advance(.01f);
    switch(mode){case 0:f.sample.rawContact.values[3][0]=.03f;break;
    case 1:f.sample.rawContact.values[3][2]=-.04f;break;
    case 2:f.sample.rawContact.values[0][0]=2;break;
    case 3:f.sample.rawContact.values[1][1]=-1;break;
    case 4:f.sample.rawContact.values[3][2]=std::numeric_limits<float>::infinity();break;
    case 5:f.sample.rawContact.values[0][0]=-1;f.sample.rawContact.values[1][1]=-1;break;}
    f.Run();CHECK(f.result.failure==PistolSlideFailure::Geometry&&!f.result.target&&!f.result.ownsMechanism&&f.slide.Owed());
    CHECK(!f.submissions);f.Step(0);CHECK(!f.result.ownsMechanism);f.Step(0,false);f.Step(0);f.Stroke();CHECK(f.submissions==1);
    }return 0;
}
int FreshContactAndGunCustodyRequired(){for(unsigned mode=0;mode<6;++mode){Fixture f;CHECK(f.Begin());f.Step(0,false);f.Advance(0);
    switch(mode){case 0:--f.sample.contact.inputSequence;break;case 1:++f.sample.contact.key.generation;break;
    case 2:f.sample.contact.eligible=false;break;case 3:++f.sample.gun.id;break;
    case 4:f.sample.gunContact.eligible=false;break;case 5:--f.sample.gunContact.inputSequence;break;}
    f.Run();CHECK(!f.result.ownsMechanism&&!f.result.target&&!f.submissions&&f.slide.Owed());
    f.Step(0);CHECK(!f.result.ownsMechanism); // Failed press cannot be retried as held.
    }return 0;
}
int OtherHandRolesAreNeverStolen(){for(auto kind:{HandClaimKind::AmmoObject,HandClaimKind::WeaponSupport,HandClaimKind::Sight}){
    Fixture f;CHECK(f.Begin());f.Step(0,false);f.Advance(0);CHECK(f.AcquireGun());
    const auto other=f.hands.Acquire(f.sample.source,{f.sample.source.owner,InteractionHand::Left,kind,
        kind==HandClaimKind::AmmoObject?HandInteractionKey{70,1}:f.sample.authority.identity.item,
        {{80,1},f.sample.source.sequence,f.sample.source.deadlineNs,true},++f.intent,kind==HandClaimKind::AmmoObject?0:f.sample.gun.id});
    CHECK(other.claim);f.sample.acquireIntent=++f.intent;f.Run();CHECK(!f.result.ownsMechanism&&!f.submissions);
    CHECK(f.hands.Current(InteractionHand::Left)&&f.hands.Current(InteractionHand::Left)->token==other.claim->token);
    f.slide.CancelPhysical(f.sample.source,f.hands);CHECK(f.hands.Current(InteractionHand::Left)->token==other.claim->token);
    }return 0;
}
int LostMechanismAndReplacedGunInvalidateProgress(){for(unsigned mode=0;mode<2;++mode){Fixture f;CHECK(f.Grip());f.Back();
    const auto old=f.hands.Current(InteractionHand::Left);CHECK(old);
    CHECK(f.hands.Release(f.sample.source,mode?f.sample.gun:old->token).accepted);
    if(mode){CHECK(f.AcquireGun());}
    else {const auto other=f.hands.Acquire(f.sample.source,{f.sample.source.owner,InteractionHand::Left,HandClaimKind::AmmoObject,
        {100,1},{{101,1},f.sample.source.sequence,f.sample.source.deadlineNs,true},++f.intent,0});CHECK(other.claim);}
    f.Step(.03f);CHECK(!f.result.target&&!f.result.ownsMechanism&&!f.submissions&&f.slide.Owed());
    if(!mode)CHECK(f.hands.Current(InteractionHand::Left)&&f.hands.Current(InteractionHand::Left)->token.kind==HandClaimKind::AmmoObject);
    }return 0;
}
int TrackingStowAndSameOwnerRearmRetainDebt(){for(unsigned mode=0;mode<4;++mode){Fixture f;CHECK(f.Grip());f.Step(.01f);f.Advance(.02f);
    switch(mode){case 0:f.sample.source.tracked[0]=false;break;case 1:f.sample.source.focused=false;break;
    case 2:f.slide.CancelPhysical(f.sample.source,f.hands);break;case 3:f.sample.authority.feedSeated=false;break;}
    f.Run();CHECK(f.slide.Owed()&&!f.result.ownsMechanism&&!f.result.request);CHECK(!f.Begin());
    f.sample.source.tracked={true,true};f.sample.source.focused=true;f.sample.authority.feedSeated=true;
    f.Advance(0);CHECK(f.AcquireGun());f.Run();CHECK(!f.result.ownsMechanism);
    f.Step(0,false);f.Step(0);CHECK(f.result.ownsMechanism);f.Stroke();CHECK(f.submissions==1);
    }return 0;
}
int AuthorityIdentityCannotChangeMidStroke(){for(unsigned field=0;field<10;++field){Fixture f;CHECK(f.Grip());f.Advance(.01f);
    switch(field){case 0:++f.sample.authority.identity.magazine.id;break;case 1:++f.sample.authority.identity.ammunition.generation;break;
    case 2:++f.sample.authority.identity.item.generation;break;case 3:++f.sample.authority.identity.mechanism.generation;break;
    case 4:++f.sample.authority.identity.feedTransaction;break;case 5:++f.sample.authority.identity.cycle;break;
    case 6:++f.sample.authority.identity.grant;break;case 7:f.sample.authority.chamber=ChamberKnowledge::Unknown;break;
    case 8:f.sample.authority.chamber=ChamberKnowledge::Occupied;break;case 9:f.sample.authority.sequence=1;break;}
    f.Run();CHECK(f.result.failure==PistolSlideFailure::Authority&&!f.result.ownsMechanism&&f.slide.Owed()&&!f.submissions);
    }return 0;
}
int PendingRequestSurvivesInterruptionsAndDeadline(){for(unsigned mode=0;mode<5;++mode){Fixture f;CHECK(f.Grip());f.Stroke();CHECK(f.sent);
    const auto request=*f.sent;f.Advance(0,false,300000000);
    switch(mode){case 0:f.sample.source.tracked[0]=false;break;case 1:f.sample.source.focused=false;break;
    case 2:++f.sample.source.owner.equipGeneration;break;case 3:f.sample.authority={};break;
    case 4:f.slide.CancelPhysical(f.sample.source,f.hands);break;}
    f.Run();CHECK(f.result.requestExpired&&f.result.blocksFire&&f.slide.Phase()==PistolSlidePhase::AwaitingNative);
    CHECK(f.submissions==1&&!f.result.request&&!f.Begin());
    PistolSlideRetirement retired{request.authority.identity,request.authority.sequence+1,f.sample.source.nowNs,f.sample.source.deadlineNs,true};
    CHECK(!f.slide.Retire(retired,f.sample.source.nowNs));auto ready=f.Ready();CHECK(ready.request==request);
    CHECK(f.slide.ObserveReady(ready,ready.observedNs));CHECK(!f.slide.Owed());
    }return 0;
}
int AcknowledgementRequiresExactRequestAndChamberPoststate(){for(unsigned mutation=0;mutation<14;++mutation){Fixture f;CHECK(f.Grip());f.Stroke();auto ready=f.Ready();
    switch(mutation){case 0:++ready.request.request;break;case 1:++ready.request.authority.identity.feedTransaction;break;
    case 2:++ready.request.inputSequence;break;case 3:++ready.request.mechanism.id;break;
    case 4:++ready.poststate.identity.magazine.generation;break;case 5:++ready.poststate.identity.ammunition.id;break;
    case 6:++ready.poststate.identity.owner.actorGeneration;break;case 7:ready.poststate.chamber=ChamberKnowledge::Unknown;break;
    case 8:ready.poststate.chamber=ChamberKnowledge::Empty;break;case 9:ready.poststate.nativeReady=false;break;
    case 10:ready.poststate.feedSeated=false;break;case 11:ready.sequence=ready.request.authority.sequence;break;
    case 12:ready.deadlineNs=ready.observedNs;break;case 13:ready.observedNs=ready.request.observedNs-1;break;}
    CHECK(!f.slide.ObserveReady(ready,ready.observedNs));CHECK(f.slide.Owed()&&f.slide.Phase()==PistolSlidePhase::AwaitingNative);
    ready=f.Ready();CHECK(f.slide.ObserveReady(ready,ready.observedNs));
    }return 0;
}
int NonDispatchRequiresProofAndNewFullStroke(){Fixture f;CHECK(f.Grip());f.Stroke();const auto first=*f.sent;
    PistolSlideNonDispatch rejection{first,first.authority.sequence+1,f.sample.source.nowNs+1000,f.sample.source.deadlineNs,false};
    CHECK(!f.slide.RejectUndispatched(rejection,rejection.observedNs));rejection.neverDispatched=true;++rejection.request.request;
    CHECK(!f.slide.RejectUndispatched(rejection,rejection.observedNs));rejection.request=first;
    CHECK(f.slide.RejectUndispatched(rejection,rejection.observedNs));CHECK(f.slide.Owed());CHECK(!f.Begin());
    f.Step(0);CHECK(!f.result.ownsMechanism);f.Step(0,false);f.Step(0);f.Stroke();CHECK(f.submissions==2&&f.sent->request>first.request);
    auto wrong=f.Ready();wrong.request=first;CHECK(!f.slide.ObserveReady(wrong,wrong.observedNs));
    const auto right=f.Ready();CHECK(f.slide.ObserveReady(right,right.observedNs));return 0;
}
int SameOwnerNewFeedAndExplicitNewOwnerLifetime(){Fixture f;CHECK(f.Grip());f.Stroke();const auto ready=f.Ready();
    CHECK(f.slide.ObserveReady(ready,ready.observedNs));f.Advance(0,false);CHECK(f.AcquireGun());++f.sample.authority.identity.grant;
    CHECK(!f.Begin());++f.sample.authority.identity.feedTransaction;++f.sample.authority.identity.cycle;
    CHECK(f.Begin());f.Step(0,false);f.Step(0);CHECK(f.result.ownsMechanism);
    f.slide.CancelPhysical(f.sample.source,f.hands);const auto old=f.sample.authority.identity;
    PistolSlideRetirement retirement{old,f.sample.authority.sequence+1,f.sample.source.nowNs+1000,f.sample.source.deadlineNs,true};
    auto wrong=retirement;++wrong.identity.owner.equipGeneration;CHECK(!f.slide.Retire(wrong,wrong.observedNs));
    retirement.ownerLifetimeEnded=false;CHECK(!f.slide.Retire(retirement,retirement.observedNs));retirement.ownerLifetimeEnded=true;
    CHECK(f.slide.Retire(retirement,retirement.observedNs));CHECK(!f.slide.Owed());
    ++f.sample.source.owner.equipGeneration;f.sample.authority.identity.owner=f.sample.source.owner;
    f.sample.authority.identity.feedTransaction=1;f.sample.authority.identity.cycle=1;
    f.Advance(0,false);CHECK(f.AcquireGun());CHECK(!f.Begin()); // Global adapter grant cannot replay across lifetime.
    ++f.sample.authority.identity.grant;CHECK(f.Begin());f.Step(0,false);f.Step(0);f.Stroke();CHECK(f.submissions==2);
    return 0;
}
int InactiveAndCleanupCannotDisturbReplacementCustody(){for(unsigned state=0;state<3;++state){Fixture f;
    if(state){CHECK(f.Grip());if(state==2)f.Stroke();else f.slide.CancelPhysical(f.sample.source,f.hands);}
    f.Advance(0);CHECK(f.AcquireGun());const auto current=f.sample.source;
    const auto other=f.hands.Acquire(current,{current.owner,InteractionHand::Left,HandClaimKind::AmmoObject,
        {300,1},{{301,1},current.sequence,current.deadlineNs,true},++f.intent,0});CHECK(other.claim);
    auto stale=current;stale.nowNs-=1000;f.slide.CancelPhysical(stale,f.hands);
    CHECK(f.hands.Current(InteractionHand::Left)->token==other.claim->token);
    CHECK(f.hands.Current(InteractionHand::Right)->token==f.sample.gun);
    if(state!=1){f.sample.source=stale;f.Run();CHECK(f.hands.Current(InteractionHand::Left)->token==other.claim->token);}
    if(state==2)CHECK(f.slide.Phase()==PistolSlidePhase::AwaitingNative&&f.submissions==1);
    }return 0;
}
int PreconsumedReleaseIsNotInventedCompletion(){Fixture f;CHECK(f.Grip());f.Back();f.Forward();f.Advance(0,false);
    // A different caller already consumed the release. The slide cannot infer
    // that it released its own exact claim merely from the now-empty hand.
    f.hands.UpdateAndRenewGunHold(f.sample.source,f.sample.gun,f.sample.gunContact);
    f.Run();CHECK(!f.submissions&&f.slide.Owed()&&!f.result.ownsMechanism);
    f.Step(0,false);f.Step(0);f.Stroke();CHECK(f.submissions==1);return 0;
}
int MirroredHandAssignmentAndTranslatedFrame(){Fixture f;
    f.hands.Reset();f.profile.hands={InteractionHand::Right,InteractionHand::Left};
    f.profile.axis={1,0,0};f.profile.closedContact.values[3][0]=.2f;
    f.profile.closedContact.values[3][1]=.1f;f.profile.closedContact.values[3][2]=-.3f;
    f.Advance(0);CHECK(f.AcquireGun());CHECK(f.Grip());CHECK(f.result.target&&f.result.target->mechanism.hand==InteractionHand::Right);
    f.Stroke();CHECK(f.submissions==1&&f.sent->gun.hand==InteractionHand::Left&&f.sent->mechanism.hand==InteractionHand::Right);
    return 0;
}
}
int main(){
    if(MeasuredStrokeAndExactAcknowledgement()||AuthorityDefaultsAndSelectionFailClosed()||CanonicalMetresAndFiniteGeometry()||
       NeutralDwellAndHysteresis()||PartialJitterAndEarlyReleaseCannotComplete()||RawPoseRejectsJumpOffAxisAndBadFrames()||
       FreshContactAndGunCustodyRequired()||OtherHandRolesAreNeverStolen()||LostMechanismAndReplacedGunInvalidateProgress()||
       TrackingStowAndSameOwnerRearmRetainDebt()||AuthorityIdentityCannotChangeMidStroke()||PendingRequestSurvivesInterruptionsAndDeadline()||
       AcknowledgementRequiresExactRequestAndChamberPoststate()||NonDispatchRequiresProofAndNewFullStroke()||
       SameOwnerNewFeedAndExplicitNewOwnerLifetime()||InactiveAndCleanupCannotDisturbReplacementCustody()||
       PreconsumedReleaseIsNotInventedCompletion()||MirroredHandAssignmentAndTranslatedFrame())return 1;
    std::puts("18 physical pistol-slide groups passed; real arbiter/raw gesture policy, synthetic adapter chamber receipts only.");
}
