#include "fvr/interaction/FixedContainerReload.h"
#include "Test.h"
#include <memory>
#include <type_traits>
using namespace fvr::interaction;
namespace {
using Step=FixedContainerStep;using Status=FixedContainerStatus;
using Matrix=fvr::math::Matrix4;
static_assert(!std::is_copy_constructible_v<FixedContainerReload>);
Matrix Identity(){Matrix m{};for(unsigned i=0;i<4;++i)m.values[i][i]=1;return m;}
// Synthetic geometry tests policy, not weapon calibration or native ABI.
struct Fixture {
    FixedContainerProfile profile{};FixedContainerSample x{};AmmunitionLedger ledger{};HandInteraction hands{};
    std::unique_ptr<FixedContainerReload> reload;
    HandClaimToken gun{},claim{};std::uint64_t intent=0;
    AmmoResourceContext context{{1,2,3,4},5,6};FeedKey mechanism{7,8},binding{9,10};
    FeedSupplyKey supply{{11,12},{}};
    bool heldClaim=false;
    Fixture(FixedContainerKind kind=FixedContainerKind::Cylinder,int loaded=2,int reserve=4,bool guided=false){
        profile.kind=kind;profile.id=20;profile.revision=21;profile.ammunitionKind=22;
        profile.capacity=kind==FixedContainerKind::SingleRoundBreech||kind==FixedContainerKind::SingleRoundMuzzle?
            1:kind==FixedContainerKind::Cylinder?6:8;
        profile.roundsPerInsertion=kind==FixedContainerKind::EnBlocClip?8:1;profile.maxAgeNs=1000;
        if(kind==FixedContainerKind::EnBlocClip)supply.container={13,14};
        for(unsigned i=0;i<4;++i){auto& g=profile.geometry[i];g.contact={100+i,.02f,.1f};
            auto& m=g.motion;m.operation=fixed_container_detail::Operation(static_cast<Step>(i));
            m.kind=FeedMotionKind::Linear;m.measured=true;m.weaponFromMotion=m.motionFromContact=Identity();
            m.travel=.1f;m.completeFraction=.9f;m.releaseFraction=.8f;m.pathToleranceMeters=.005f;
            m.orientationToleranceRadians=.1f;m.maxStepMeters=.04f;m.maxStepRadians=.25f;
            m.endpointDwellNs=20;m.maximumGestureNs=1000;m.maxInputGapNs=40;
        }
        if(kind==FixedContainerKind::SingleRoundBreech)profile.geometry[1]={};
        if(kind==FixedContainerKind::SingleRoundMuzzle)for(unsigned i:{0u,1u,3u})profile.geometry[i]={};
        if(kind==FixedContainerKind::EnBlocClip||kind==FixedContainerKind::SingleRoundBreech||
           kind==FixedContainerKind::SingleRoundMuzzle){auto& m=profile.geometry[2].motion;
            m.kind=FeedMotionKind::FreePlacement;m.placement.weaponFromSource=Identity();
            m.placement.weaponFromDestination=Identity();m.placement.weaponFromDestination.values[3][0]=.1f;
            m.placement.sourceFromGrip=Identity();}
        if(guided){profile.insertionGuidance={.06f,.14f,.02f};profile.geometry[2].motion.retainWhileGripped=true;}
        x.ammunition={context,1,1000,2000,{loaded,reserve,profile.capacity},true};
        x.input={{1,2,5,6},1,1000,2000,1000,true,{true,true},{false,false}};
        x.held=true;x.weaponFromContact=Identity();x.interaction.hand={44,1};x.interaction.trackingGeneration=1;
        x.interaction.focused=true;x.interaction.contact.tracked=true;
        reload=std::make_unique<FixedContainerReload>(profile,context,mechanism,binding,supply,30,ledger);
        (void)reload->Bind(x.ammunition,x.input.nowNs);
        (void)AcquireGun();
    }
    bool AcquireGun(){
        HandClaimRequest q{x.input.owner,InteractionHand::Right,HandClaimKind::GunHold,{3,4},
            {{300,1},x.input.sequence,x.input.deadlineNs,true},++intent,0};
        const auto g=hands.Acquire(x.input,q);if(!g.claim)return false;
        gun=g.claim->token;return true;
    }
    void Tick(float progress=0,bool grip=true){
        ++x.ammunition.sequence;++x.input.sequence;x.input.nowNs+=10;
        x.input.observedNs=x.ammunition.observedNs=x.input.nowNs;
        x.input.deadlineNs=x.ammunition.deadlineNs=x.input.nowNs+1000;
        x.ammunition.counts=ledger.Snapshot().counts;x.input.released={false,false};
        auto& c=x.interaction.contact;c.owner={1,2,3,5,6};c.mechanism=mechanism;
        c.snapshotSequence=x.ammunition.sequence;c.inputSequence=x.input.sequence;c.observedNs=x.input.observedNs;
        c.weaponFromHandContact=Identity();c.weaponFromHandContact.values[3][0]=progress*.1f;
        x.interaction.grip=grip;x.claim=heldClaim?std::optional{claim}:std::nullopt;
    }
    void Renew(){
        hands.Update(x.input);
        hands.Renew(x.input,gun,{{300,1},x.input.sequence,x.input.deadlineNs,true});
        if(heldClaim)hands.Renew(x.input,claim,{claim.contact,x.input.sequence,x.input.deadlineNs,true});
    }
    FixedContainerResult Run(float progress=0,bool grip=true){Tick(progress,grip);Renew();return reload->Update(x,hands);}
    bool Acquire(float progress=0){
        if(heldClaim){Tick(progress,false);Renew();hands.Release(x.input,claim);heldClaim=false;x.claim.reset();}
        const auto neutral=Run(progress,false);
        if(neutral.interaction.status!=FeedInteractionStatus::Armed)return false;
        Tick(progress);Renew();const auto step=reload->Step();
        HandClaimRequest q{x.input.owner,InteractionHand::Left,
            step==Step::Insert?HandClaimKind::AmmoObject:HandClaimKind::Mechanism,
            step==Step::Insert?HandInteractionKey{11,12}:HandInteractionKey{3,4},
            {{profile.geometry[static_cast<unsigned>(step)].contact.role,profile.revision},x.input.sequence,x.input.deadlineNs,true},
            ++intent,step==Step::Insert?0:gun.id};
        const auto got=hands.Acquire(x.input,q);if(!got.claim)return false;
        claim=got.claim->token;heldClaim=true;x.claim=claim;
        return reload->Update(x,hands).interaction.status==FeedInteractionStatus::Acquired;
    }
    FixedContainerResult Finish(){
        FixedContainerResult r;
        const bool releaseToFinish=profile.geometry[static_cast<unsigned>(reload->Step())].motion.kind==FeedMotionKind::FreePlacement;
        for(float p:{.2f,.4f,.6f,.8f,1.f,1.f,1.f})r=Run(p);
        if(releaseToFinish){
            // Policy ticks the arbiter before a release consumes its claim.
            Tick(1,false);x.input.released[0]=true;r=reload->Update(x,hands);heldClaim=false;
        }
        return r;
    }
    AmmunitionReceipt Receipt(const FixedContainerRequest& r)const {
        return {r.ammunition,1,r.ammunition.requestedNs,r.ammunition.requestedNs+1,
            r.ammunition.before,r.ammunition.after,true,true};
    }
    bool Complete(const FixedContainerRequest& r){
        const auto receipt=Receipt(r);Tick();x.ammunition.counts=r.ammunition.after;
        return reload->Complete(r,receipt,x.ammunition,x.input.nowNs);
    }
};
int CylinderPreservesLiveRoundsAndClosesPartiallyLoaded(){
    Fixture f;CHECK(fixed_container_detail::Profile(f.profile));CHECK(!f.reload->BlocksFire());
    for(const auto expected:{Step::Open,Step::EjectSpent}){
        CHECK(f.reload->Step()==expected&&f.Acquire());CHECK(f.reload->BlocksFire());
        const auto r=f.Finish();CHECK(r.status==Status::PhysicalStep&&!r.dispatch);
        CHECK(f.ledger.Snapshot().counts.loaded==2&&f.ledger.Snapshot().counts.reserve==4);
    }
    CHECK(f.reload->Step()==Step::Insert&&f.Acquire());const auto r=f.Finish();CHECK(r.dispatch);
    CHECK(r.dispatch->ammunition.operation==AmmunitionOperation::InsertRounds);
    CHECK((r.dispatch->ammunition.before==AmmunitionCounts{2,4,6}));
    CHECK((r.dispatch->ammunition.after==AmmunitionCounts{3,3,6}));
    CHECK(f.ledger.Snapshot().counts.loaded==2&&f.reload->Pending()==r.dispatch);
    CHECK(!f.Run().dispatch&&f.Complete(*r.dispatch));CHECK(f.ledger.Snapshot().counts.loaded==3);
    f.x.finishLoading=true;CHECK(f.Run().step==Step::Close);f.x.finishLoading=false;
    CHECK(f.Acquire());CHECK(f.Finish().step==Step::Complete&&!f.reload->BlocksFire());
    CHECK((f.ledger.Snapshot().counts==AmmunitionCounts{3,3,6}));return 0;
}
int ClipOneAtomicBoundedTransferAndNoExtraChamber(){
    for(const auto counts:{AmmunitionCounts{0,3,8},AmmunitionCounts{5,20,8}}){
        Fixture f(FixedContainerKind::EnBlocClip,counts.loaded,counts.reserve);CHECK(f.Acquire());
        const auto r=f.Finish();CHECK(r.dispatch&&r.dispatch->supply==f.supply);
        const int n=std::min(counts.capacity-counts.loaded,counts.reserve);
        CHECK((r.dispatch->ammunition.after==AmmunitionCounts{counts.loaded+n,counts.reserve-n,8}));
        CHECK(f.reload->Step()==Step::Insert&&f.Complete(*r.dispatch));
        CHECK(f.reload->Step()==Step::Complete&&!f.reload->BlocksFire());
        CHECK(!f.Run().dispatch&&f.ledger.Snapshot().counts.loaded<=8);
    }return 0;
}
int SingleRoundProfileAndOpeningPreserveLiveAmmunition(){
    Fixture f(FixedContainerKind::SingleRoundBreech,1,2);
    CHECK(fixed_container_detail::Profile(f.profile));
    CHECK(!f.profile.geometry[static_cast<unsigned>(Step::EjectSpent)].contact.role);
    auto invalid=f.profile;invalid.capacity=2;CHECK(!fixed_container_detail::Profile(invalid));
    invalid=f.profile;invalid.roundsPerInsertion=2;CHECK(!fixed_container_detail::Profile(invalid));
    for(const auto step:{Step::Open,Step::Insert,Step::Close}){
        invalid=f.profile;invalid.geometry[static_cast<unsigned>(step)].motion.measured=false;
        CHECK(!fixed_container_detail::Profile(invalid));
    }
    CHECK(f.Acquire());const auto opened=f.Finish();
    CHECK(opened.status==Status::PhysicalStep&&opened.step==Step::Insert&&!opened.dispatch);
    CHECK((f.ledger.Snapshot().counts==AmmunitionCounts{1,2,1}));
    CHECK(!f.reload->Pending()&&!f.ledger.Pending()&&f.reload->BlocksFire());
    CHECK(f.Run().step==Step::Close&&f.reload->BlocksFire());
    CHECK(f.Acquire());CHECK(f.Finish().step==Step::Complete&&!f.reload->BlocksFire());
    CHECK((f.ledger.Snapshot().counts==AmmunitionCounts{1,2,1}));return 0;
}
int SingleRoundCycleRequiresCloseAndRejectsDuplicateAndForeignAuthority(){
    Fixture f(FixedContainerKind::SingleRoundBreech,0,3);CHECK(!f.reload->BlocksFire());
    CHECK(f.Acquire()&&f.reload->BlocksFire());
    CHECK(f.Finish().step==Step::Insert&&f.reload->BlocksFire());
    CHECK(!f.reload->RecoveryMotion());
    CHECK(f.Acquire());auto foreign=f.claim;++foreign.owner.actor;f.x.claim=foreign;
    CHECK(f.reload->Update(f.x,f.hands).status==Status::Suspended&&!f.reload->Pending());
    CHECK((f.ledger.Snapshot().counts==AmmunitionCounts{0,3,1}));
    CHECK(f.Acquire());const auto sent=f.Finish();CHECK(sent.dispatch&&sent.dispatch->supply==f.supply);
    CHECK(sent.dispatch->ammunition.operation==AmmunitionOperation::InsertRounds);
    CHECK((sent.dispatch->ammunition.after==AmmunitionCounts{1,2,1}));
    CHECK(f.reload->Step()==Step::Insert&&f.reload->BlocksFire());
    CHECK(f.reload->RecoveryMotion());
    CHECK(!f.reload->Update(f.x,f.hands).dispatch&&!f.Run().dispatch);
    CHECK((f.ledger.Snapshot().counts==AmmunitionCounts{0,3,1}));
    auto foreignPost=f.x.ammunition;foreignPost.counts=sent.dispatch->ammunition.after;
    ++foreignPost.context.resource.weapon;
    CHECK(!f.reload->Complete(*sent.dispatch,f.Receipt(*sent.dispatch),foreignPost,f.x.input.nowNs));
    CHECK(f.reload->Pending()==sent.dispatch&&f.reload->BlocksFire());
    CHECK(f.reload->RecoveryMotion());
    CHECK(f.Complete(*sent.dispatch));
    CHECK(f.reload->Step()==Step::Close&&f.reload->BlocksFire());
    CHECK(!f.reload->RecoveryMotion());
    CHECK(f.reload->Complete(*sent.dispatch,f.Receipt(*sent.dispatch),f.x.ammunition,f.x.input.nowNs));
    CHECK(f.reload->Step()==Step::Close&&!f.Run().dispatch&&f.reload->BlocksFire());
    CHECK(f.Acquire()&&f.reload->BlocksFire());
    const auto closed=f.Finish();CHECK(closed.step==Step::Complete&&!closed.dispatch&&!f.reload->BlocksFire());
    CHECK(!f.Run().dispatch&&(f.ledger.Snapshot().counts==AmmunitionCounts{1,2,1}));return 0;
}
int SingleRoundPendingAndPartialCloseResumeAfterStowAndTrackingLoss(){
    Fixture f(FixedContainerKind::SingleRoundBreech,0,2);
    CHECK(f.Acquire());CHECK(f.Finish().step==Step::Insert);CHECK(f.Acquire());
    const auto sent=f.Finish();CHECK(sent.dispatch);
    f.x.held=false;f.Tick();f.x.input.released={true,true};
    CHECK(f.reload->Update(f.x,f.hands).status==Status::AwaitingReceipt);
    CHECK(!f.hands.Current(InteractionHand::Left)&&!f.hands.Current(InteractionHand::Right));
    CHECK(f.reload->Pending()==sent.dispatch&&f.ledger.Pending()==sent.dispatch->ammunition);
    CHECK((f.ledger.Snapshot().counts==AmmunitionCounts{0,2,1})&&f.reload->BlocksFire());
    CHECK(f.Complete(*sent.dispatch));
    CHECK(f.reload->Step()==Step::Close&&f.reload->BlocksFire());
    CHECK(f.Run().status==Status::Suspended&&!f.reload->Pending());
    f.x.held=true;f.Tick(0,false);CHECK(f.AcquireGun());
    CHECK(f.Acquire());CHECK(f.Run(.2f).interaction.status==FeedInteractionStatus::Moving);
    const auto partial=f.reload->RecoveryMotion();CHECK(partial);
    f.x.input.tracked={false,false};CHECK(f.Run(.4f).status==Status::Suspended);
    CHECK(!f.hands.Current(InteractionHand::Left)&&!f.hands.Current(InteractionHand::Right));
    CHECK(f.reload->Step()==Step::Close&&f.reload->BlocksFire()&&f.reload->RecoveryMotion());
    CHECK((f.ledger.Snapshot().counts==AmmunitionCounts{1,1,1}));
    f.heldClaim=false;f.x.claim.reset();f.x.input.tracked={true,true};f.Tick(.2f,false);
    f.x.weaponFromContact=partial->WeaponFromHeld();
    CHECK(f.reload->RecoverMotion(f.x,*partial));CHECK(f.AcquireGun());CHECK(f.Acquire(.2f));
    CHECK(f.reload->BlocksFire());
    FixedContainerResult closed;for(float p:{.4f,.6f,.8f,1.f,1.f,1.f})closed=f.Run(p);
    CHECK(!closed.dispatch&&closed.step==Step::Close&&f.reload->BlocksFire());
    f.Tick(1,false);f.x.input.released[0]=true;closed=f.reload->Update(f.x,f.hands);f.heldClaim=false;
    CHECK(closed.step==Step::Complete&&!closed.dispatch&&!f.reload->BlocksFire());
    CHECK((f.ledger.Snapshot().counts==AmmunitionCounts{1,1,1}));return 0;
}
int MuzzleRequiresOnlyMeasuredInsertionAndRejectsClipOrBreechPolicy(){
    Fixture f(FixedContainerKind::SingleRoundMuzzle,0,2);
    CHECK(fixed_container_detail::Profile(f.profile)&&f.reload->Step()==Step::Insert&&!f.reload->BlocksFire());
    for(const auto step:{Step::Open,Step::EjectSpent,Step::Close}){
        const auto& unused=f.profile.geometry[static_cast<unsigned>(step)];
        CHECK(!unused.contact.role&&!unused.motion.measured);
    }
    auto invalid=f.profile;invalid.kind=FixedContainerKind::SingleRoundBreech;
    CHECK(!fixed_container_detail::Profile(invalid));
    invalid=f.profile;invalid.capacity=2;CHECK(!fixed_container_detail::Profile(invalid));
    invalid=f.profile;invalid.roundsPerInsertion=2;CHECK(!fixed_container_detail::Profile(invalid));
    invalid=f.profile;invalid.geometry[2].motion.measured=false;CHECK(!fixed_container_detail::Profile(invalid));
    auto clip=f.supply;clip.container={13,14};AmmunitionLedger other;
    FixedContainerReload fakeClip(f.profile,f.context,f.mechanism,f.binding,clip,31,other);
    CHECK(!fakeClip.Bind(f.x.ammunition,f.x.input.nowNs));
    for(const auto counts:{AmmunitionCounts{1,2,1},AmmunitionCounts{0,0,1}}){
        Fixture empty(FixedContainerKind::SingleRoundMuzzle,counts.loaded,counts.reserve);
        empty.x.finishLoading=true;const auto stopped=empty.Run();
        CHECK(stopped.status==Status::NoAmmunition&&stopped.step==Step::Insert&&!stopped.dispatch);
        CHECK(!empty.reload->Pending()&&empty.ledger.Snapshot().counts==counts);
    }return 0;
}
FixedContainerResult GuidedPose(Fixture& f,float progress,float angle,bool grip=true){
    f.Tick(progress,grip);auto& pose=f.x.interaction.contact.weaponFromHandContact;
    const auto c=std::cos(angle),s=std::sin(angle);
    pose.values[0][0]=pose.values[1][1]=c;pose.values[0][1]=s;pose.values[1][0]=-s;
    if(!grip){f.x.input.released[0]=true;const auto r=f.reload->Update(f.x,f.hands);f.heldClaim=false;return r;}
    f.Renew();return f.reload->Update(f.x,f.hands);
}
int GuidedFixedRoundsRequireFreshInwardTravelAndRelease(){
    for(const auto kind:{FixedContainerKind::SingleRoundBreech,FixedContainerKind::SingleRoundMuzzle}){
        Fixture f(kind,0,2,true);CHECK(fixed_container_detail::Profile(f.profile));
        if(kind==FixedContainerKind::SingleRoundBreech){CHECK(f.reload->Step()==Step::Open&&f.Acquire());CHECK(f.Finish().step==Step::Insert);}
        CHECK(f.Acquire());const auto token=f.claim;
        // A natural wrist can be far outside the old exact alignment cone.
        for(unsigned n=1;n<=7;++n)CHECK(!GuidedPose(f,.2f,float(n)*.2f).dispatch);
        const auto captured=GuidedPose(f,.5f,1.4f);CHECK(captured.interaction.target&&!captured.dispatch);
        for(unsigned n=0;n<8;++n){const auto held=GuidedPose(f,.5f,1.4f+float(n)*.1f);
            CHECK(held.interaction.status==FeedInteractionStatus::Moving&&!held.dispatch);
            CHECK(feed_interaction_detail::Distance(*held.interaction.target,f.profile.geometry[2].motion.placement.weaponFromDestination)>.04);}
        CHECK(!f.reload->Pending()&&f.hands.Current(InteractionHand::Left)->token==token);
        CHECK(GuidedPose(f,.6f,2.1f).interaction.status==FeedInteractionStatus::Moving);
        for(unsigned n=0;n<4;++n){const auto held=GuidedPose(f,.7f,2.1f);
            CHECK(held.interaction.status==FeedInteractionStatus::Endpoint&&!held.dispatch);
            CHECK(held.interaction.target->values==f.profile.geometry[2].motion.placement.weaponFromDestination.values);}
        CHECK(!f.reload->Update(f.x,f.hands).dispatch); // Duplicate pose cannot dispatch.
        const auto sent=GuidedPose(f,.7f,2.1f,false);CHECK(sent.dispatch&&f.reload->Pending()==sent.dispatch);
        CHECK((sent.dispatch->ammunition.before==AmmunitionCounts{0,2,1}));
        CHECK((sent.dispatch->ammunition.after==AmmunitionCounts{1,1,1}));
        CHECK(!f.Run().dispatch&&f.Complete(*sent.dispatch));
        CHECK(f.reload->Step()==(kind==FixedContainerKind::SingleRoundBreech?Step::Close:Step::Complete));
    }return 0;
}
int GuidedFixedRoundsWithdrawWithoutDroppingCustody(){
    for(const auto kind:{FixedContainerKind::SingleRoundBreech,FixedContainerKind::SingleRoundMuzzle}){
        Fixture f(kind,0,2,true);
        if(kind==FixedContainerKind::SingleRoundBreech){CHECK(f.Acquire());CHECK(f.Finish().step==Step::Insert);}
        CHECK(f.Acquire());const auto token=f.claim;
        GuidedPose(f,.2f,0);GuidedPose(f,.5f,0);GuidedPose(f,.7f,0);
        for(unsigned n=0;n<4;++n)GuidedPose(f,.7f,0);
        const auto withdrawn=GuidedPose(f,.4f,0);
        CHECK(withdrawn.interaction.status==FeedInteractionStatus::Moving&&!withdrawn.dispatch);
        CHECK(f.hands.Current(InteractionHand::Left)->token==token&&!f.reload->Pending());
        CHECK(!GuidedPose(f,.4f,0,false).dispatch);
        CHECK((f.ledger.Snapshot().counts==AmmunitionCounts{0,2,1}));
    }
    Fixture early(FixedContainerKind::SingleRoundMuzzle,0,2,true);CHECK(early.Acquire());
    GuidedPose(early,.2f,0);GuidedPose(early,.5f,0);
    for(unsigned n=0;n<8;++n)GuidedPose(early,.5f,0);
    CHECK(!GuidedPose(early,.5f,0,false).dispatch&&!early.reload->Pending());
    auto invalid=early.profile;invalid.kind=FixedContainerKind::EnBlocClip;CHECK(!fixed_container_detail::Profile(invalid));
    invalid=early.profile;invalid.insertionGuidance.inwardTravelMeters=0;CHECK(!fixed_container_detail::Profile(invalid));
    invalid=early.profile;invalid.insertionGuidance.retentionRadiusMeters=.01f;CHECK(!fixed_container_detail::Profile(invalid));
    return 0;
}
int GuidedFixedRoundSafetyCannotCompleteContact(){
    for(const bool foreign:{false,true}){
        Fixture f(FixedContainerKind::SingleRoundMuzzle,0,2,true);CHECK(f.Acquire());
        GuidedPose(f,.2f,0);GuidedPose(f,.5f,0);GuidedPose(f,.7f,0);
        for(unsigned n=0;n<4;++n)GuidedPose(f,.7f,0);
        f.Tick(.7f,false);f.x.input.released[0]=true;
        if(foreign)++f.x.input.owner.equipGeneration;else f.x.input.tracked[0]=false;
        const auto lost=f.reload->Update(f.x,f.hands);CHECK(!lost.dispatch&&!f.reload->Pending());
        CHECK((f.ledger.Snapshot().counts==AmmunitionCounts{0,2,1}));
        CHECK(!f.hands.Current(InteractionHand::Left)&&f.reload->Step()==Step::Insert);
    }return 0;
}
int MuzzlePartialInsertionAndPendingStowRequireExactReceipt(){
    Fixture f(FixedContainerKind::SingleRoundMuzzle,0,2);f.x.finishLoading=true;
    CHECK(f.Acquire()&&f.reload->BlocksFire());CHECK(f.Run(.2f).interaction.status==FeedInteractionStatus::Moving);
    const auto partial=f.reload->RecoveryMotion();CHECK(partial);
    f.x.held=false;f.Tick(.4f);f.x.input.released={true,true};
    CHECK(f.reload->Update(f.x,f.hands).status==Status::Suspended);
    CHECK(!f.hands.Current(InteractionHand::Left)&&!f.hands.Current(InteractionHand::Right));
    CHECK(f.reload->Step()==Step::Insert&&f.reload->BlocksFire()&&f.reload->RecoveryMotion());
    CHECK(!f.reload->Pending()&&(f.ledger.Snapshot().counts==AmmunitionCounts{0,2,1}));
    f.heldClaim=false;f.x.claim.reset();f.x.held=true;f.Tick(.2f,false);
    f.x.weaponFromContact=partial->WeaponFromHeld();
    CHECK(f.reload->RecoverMotion(f.x,*partial)&&f.AcquireGun()&&f.Acquire(.2f));
    const auto sent=f.Finish();CHECK(sent.dispatch&&sent.dispatch->supply==f.supply);
    CHECK(sent.dispatch->ammunition.operation==AmmunitionOperation::InsertRounds);
    CHECK((sent.dispatch->ammunition.after==AmmunitionCounts{1,1,1}));
    CHECK(f.reload->Step()==Step::Insert&&f.reload->BlocksFire());
    f.x.held=false;f.Tick();f.x.input.released={true,true};
    CHECK(f.reload->Update(f.x,f.hands).status==Status::AwaitingReceipt);
    CHECK(!f.hands.Current(InteractionHand::Left)&&!f.hands.Current(InteractionHand::Right));
    CHECK(f.reload->Pending()==sent.dispatch&&f.ledger.Pending()==sent.dispatch->ammunition);
    CHECK(!f.Run().dispatch&&(f.ledger.Snapshot().counts==AmmunitionCounts{0,2,1}));
    auto post=f.x.ammunition;post.counts=sent.dispatch->ammunition.after;
    auto foreign=post;++foreign.context.resource.weapon;
    CHECK(!f.reload->Complete(*sent.dispatch,f.Receipt(*sent.dispatch),foreign,f.x.input.nowNs));
    auto bad=f.Receipt(*sent.dispatch);++bad.command.id;
    CHECK(!f.reload->Complete(*sent.dispatch,bad,post,f.x.input.nowNs));
    bad=f.Receipt(*sent.dispatch);bad.copiesVerified=false;
    CHECK(!f.reload->Complete(*sent.dispatch,bad,post,f.x.input.nowNs));
    CHECK(f.reload->Pending()==sent.dispatch&&f.reload->Step()==Step::Insert&&f.reload->BlocksFire());
    CHECK(f.ledger.Phase()==AmmunitionLedgerPhase::NeedsReconciliation&&f.Complete(*sent.dispatch));
    CHECK(f.reload->Step()==Step::Complete&&!f.reload->BlocksFire());
    CHECK(f.reload->Complete(*sent.dispatch,f.Receipt(*sent.dispatch),f.x.ammunition,f.x.input.nowNs));
    CHECK(!f.Run().dispatch&&(f.ledger.Snapshot().counts==AmmunitionCounts{1,1,1}));return 0;
}
int TrackingAndStowRetainMechanicalDebtAndPendingReceipt(){
    Fixture f;CHECK(f.Acquire());f.Run(.2f);f.x.interaction.contact.tracked=false;
    CHECK(f.Run(.4f).status==Status::Suspended&&f.reload->BlocksFire());
    f.x.interaction.contact.tracked=true;CHECK(f.Run().interaction.status==FeedInteractionStatus::NeedNeutral);
    CHECK(f.Acquire());CHECK(f.Finish().step==Step::EjectSpent);CHECK(f.Acquire());f.Finish();
    CHECK(f.Acquire());const auto sent=f.Finish();CHECK(sent.dispatch);
    f.x.held=false;CHECK(f.Run().status==Status::AwaitingReceipt&&f.reload->Pending()==sent.dispatch);
    CHECK(f.Complete(*sent.dispatch));CHECK((f.ledger.Snapshot().counts==AmmunitionCounts{3,3,6}));
    CHECK(f.reload->Step()==Step::Insert&&f.reload->BlocksFire());
    f.x.held=true;CHECK(!f.Run().dispatch);return 0;
}
int WrongClaimsAndTeleportCannotLoad(){
    for(const auto kind:{FixedContainerKind::EnBlocClip,FixedContainerKind::SingleRoundMuzzle}){
        Fixture f(kind,0,8);CHECK(f.Acquire());
        CHECK(f.reload->Update(f.x,f.hands).interaction.status==FeedInteractionStatus::Duplicate);
        const auto jumped=f.Run(1);
        CHECK(jumped.status==Status::Suspended&&jumped.interaction.status==FeedInteractionStatus::MotionJump);
        CHECK(!jumped.dispatch&&f.ledger.Snapshot().counts.loaded==0);
        CHECK(f.Acquire());auto wrong=f.claim;++wrong.id;f.x.claim=wrong;
        CHECK(f.reload->Update(f.x,f.hands).status==Status::Suspended&&!f.reload->Pending());
        CHECK(f.Acquire());wrong=f.claim;++wrong.owner.actor;f.x.claim=wrong;
        CHECK(f.reload->Update(f.x,f.hands).status==Status::Suspended&&!f.reload->Pending());
        CHECK(f.Acquire());f.x.input.focused=false;
        CHECK(f.Run(.2f).status==Status::Suspended&&!f.reload->Pending());
    }return 0;
}
int ExactTypedReceiptAndDuplicateCannotFinishOtherTransfer(){
    Fixture f;CHECK(f.Acquire());f.Finish();CHECK(f.Acquire());f.Finish();CHECK(f.Acquire());
    const auto first=f.Finish();CHECK(first.dispatch);auto bad=f.Receipt(*first.dispatch);++bad.command.id;
    f.Tick();f.x.ammunition.counts=first.dispatch->ammunition.after;
    CHECK(!f.reload->Complete(*first.dispatch,bad,f.x.ammunition,f.x.input.nowNs));
    bad=f.Receipt(*first.dispatch);bad.copiesVerified=false;
    CHECK(!f.reload->Complete(*first.dispatch,bad,f.x.ammunition,f.x.input.nowNs));
    CHECK(f.reload->Pending()==first.dispatch&&f.ledger.Phase()==AmmunitionLedgerPhase::NeedsReconciliation);
    CHECK(f.Complete(*first.dispatch));CHECK(f.Acquire());const auto second=f.Finish();CHECK(second.dispatch);
    CHECK(!f.reload->Complete(*first.dispatch,f.Receipt(*first.dispatch),f.x.ammunition,f.x.input.nowNs));
    CHECK(f.reload->Pending()==second.dispatch&&f.Complete(*second.dispatch));
    CHECK((f.ledger.Snapshot().counts==AmmunitionCounts{4,2,6}));return 0;
}
int LedgerBatchRejectsInvalidAndPreservesOriginalStore(){
    Fixture f;const auto s=f.x.ammunition;
    CHECK(!f.ledger.Queue(AmmunitionOperation::InsertRounds,s,1000,{},0));
    CHECK(!f.ledger.Queue(AmmunitionOperation::InsertRounds,s,1000,{},-1));
    const auto queued=f.ledger.Queue(AmmunitionOperation::InsertRounds,s,1000,{},999999);
    CHECK((queued&&queued->after==AmmunitionCounts{6,0,6}));CHECK(!f.ledger.Dispatch(s,2000));
    CHECK(f.ledger.Phase()==AmmunitionLedgerPhase::Ready&&f.ledger.Snapshot().counts==s.counts);
    const auto removed=f.ledger.Queue(AmmunitionOperation::RemoveMagazine,s,1000);CHECK(removed);
    CHECK(f.ledger.Dispatch(s,1000));AmmunitionReceipt receipt{*removed,1,1000,1001,removed->before,removed->after,true,true};
    CHECK(f.ledger.Complete(receipt,1001));auto empty=s;empty.counts=removed->after;empty.sequence=2;empty.observedNs=1002;
    CHECK(!f.ledger.Queue(AmmunitionOperation::InsertRounds,empty,1002,{},4));
    CHECK(f.ledger.Original()&&f.ledger.Original()->rounds==2);return 0;
}
int InvalidProfileOwnerAndUnverifiedGeometryFailClosed(){
    Fixture f;auto p=f.profile;p.geometry[0].motion.measured=false;
    CHECK(!fixed_container_detail::Profile(p));p=f.profile;p.capacity=7;
    AmmunitionLedger ledger;FixedContainerReload wrongCapacity(p,f.context,f.mechanism,f.binding,f.supply,31,ledger);
    CHECK(!wrongCapacity.Bind(f.x.ammunition,1000));
    f.Tick(0,false);++f.x.ammunition.context.resource.weapon;CHECK(f.reload->Update(f.x,f.hands).status==Status::Invalid);
    CHECK((f.ledger.Snapshot().counts==AmmunitionCounts{2,4,6}));return 0;
}
int PlacementRebasePinsDestinationAndRejectsHeldMutation(){
    Fixture f(FixedContainerKind::EnBlocClip,0,8);auto motion=f.profile.geometry[2].motion;
    motion.placement.weaponFromSource.values[3][0]=.02f;
    CHECK(f.reload->RebaseInsertionSource(f.x.ammunition,motion,f.x.input.nowNs));
    motion.placement.weaponFromDestination.values[3][1]=.02f;
    CHECK(!f.reload->RebaseInsertionSource(f.x.ammunition,motion,f.x.input.nowNs));
    CHECK(f.reload->RebaseInsertionSource(f.x.ammunition,f.profile.geometry[2].motion,f.x.input.nowNs));
    CHECK(f.Acquire());CHECK(!f.reload->RebaseInsertionSource(f.x.ammunition,f.profile.geometry[2].motion,f.x.input.nowNs));
    CHECK(!f.reload->Pending()&&f.ledger.Snapshot().counts.loaded==0);return 0;
}
}
int FixedPolicyRejectionDiagnostics(){
    for(unsigned branch=0;branch<4;++branch){
        Fixture f(FixedContainerKind::SingleRoundMuzzle,0,1);CHECK(f.Acquire());
        FixedContainerResult result;FixedContainerRejection expected=FixedContainerRejection::Unknown;
        if(branch==0){f.x.weaponFromContact.values[3][3]=0;expected=FixedContainerRejection::Admission;result=f.reload->Update(f.x,f.hands);}
        if(branch==1){f.x.held=false;expected=FixedContainerRejection::Safety;result=f.reload->Update(f.x,f.hands);}
        if(branch==2){++f.x.claim->id;expected=FixedContainerRejection::Hand;result=f.reload->Update(f.x,f.hands);}
        if(branch==3){expected=FixedContainerRejection::Gesture;result=f.Run(1);}
        CHECK(result.rejection==expected&&!result.dispatch&&!f.reload->Pending());
        CHECK(result.status==(branch==0?Status::Invalid:Status::Suspended));
        if(branch==3)CHECK(result.interaction.status==FeedInteractionStatus::MotionJump);
        CHECK(f.ledger.Snapshot().counts.loaded==0&&f.ledger.Snapshot().counts.reserve==1);
    }
    return 0;
}

int HeldContactGapRetainsOnlyCustody(){
    for(bool establishedDwell:{false,true}){
        Fixture f(FixedContainerKind::SingleRoundMuzzle,0,1);CHECK(f.Acquire());
        for(float p:{.2f,.4f,.6f,.8f,1.f})CHECK(!f.Run(p).dispatch);
        if(establishedDwell)for(unsigned n=0;n<2;++n)CHECK(!f.Run(1).dispatch);
        const auto before=f.ledger.Snapshot();const auto token=f.claim;
        const auto held=*f.hands.Current(InteractionHand::Left);
        const auto recovery=f.reload->RecoveryMotion();CHECK(recovery);
        f.Tick(1);f.hands.Update(f.x.input);
        CHECK(f.hands.Renew(f.x.input,f.gun,{{300,1},f.x.input.sequence,f.x.input.deadlineNs,true}).claim);
        const auto retained=f.reload->RetainHeld(f.x.input,token,true,f.hands);
        CHECK(retained&&retained->token==token&&retained->inputSequence==f.x.input.sequence&&retained->deadlineNs==f.x.input.deadlineNs);
        CHECK(retained->deadlineNs>held.deadlineNs&&!f.reload->Pending()&&f.reload->Step()==Step::Insert);
        CHECK(f.ledger.Snapshot().sequence==before.sequence&&f.ledger.Snapshot().deadlineNs==before.deadlineNs&&f.ledger.Snapshot().counts==before.counts);
        CHECK(f.reload->RecoveryMotion()->WeaponFromHeld().values==recovery->WeaponFromHeld().values);
        const auto duplicate=f.reload->RetainHeld(f.x.input,token,true,f.hands);
        CHECK(duplicate&&duplicate->deadlineNs==retained->deadlineNs&&duplicate->inputSequence==retained->inputSequence);
        // Missing publication cannot establish dwell, and cannot erase dwell
        // already proved by fresh geometry. Release needs actual contact.
        f.Tick(1,false);f.x.input.released[0]=true;
        const auto released=f.reload->Update(f.x,f.hands);
        if(!establishedDwell)CHECK(released.status==Status::Suspended&&!released.dispatch&&!f.reload->Pending());
        else CHECK(released.dispatch&&f.reload->Pending()&&released.dispatch->supply==f.supply);
        CHECK(f.ledger.Snapshot().counts==before.counts);
    }
    // A short original geometry lease expires while genuine newer safety
    // packets retain custody. That expired endpoint cannot supply dwell.
    // One missing tick leaves the prior lease fresh at T+10; the fresh release
    // at T+20 must expire it. Two missing ticks also cover RetainHeld expiry.
    for(unsigned missingTicks:{1u,2u}){
        Fixture expired(FixedContainerKind::SingleRoundMuzzle,0,1);CHECK(expired.Acquire());
        for(float p:{.2f,.4f,.6f,.8f,1.f,1.f,1.f})CHECK(!expired.Run(p).dispatch);
        expired.Tick(1);expired.x.input.deadlineNs=expired.x.input.observedNs+15;expired.Renew();
        const auto sourceDeadline=expired.x.input.deadlineNs;
        CHECK(!expired.reload->Update(expired.x,expired.hands).dispatch);
        for(unsigned n=0;n<missingTicks;++n){
            expired.Tick(1);expired.hands.Update(expired.x.input);
            CHECK(expired.hands.Renew(expired.x.input,expired.gun,{{300,1},expired.x.input.sequence,expired.x.input.deadlineNs,true}).claim);
            CHECK(expired.reload->RetainHeld(expired.x.input,expired.claim,true,expired.hands));
            if(n==0)CHECK(expired.x.input.nowNs<sourceDeadline);
        }
        expired.Tick(1,false);expired.x.input.released[0]=true;
        CHECK(expired.x.input.nowNs>=sourceDeadline&&expired.x.input.nowNs<expired.x.input.deadlineNs);
        const auto noDwell=expired.reload->Update(expired.x,expired.hands);
        CHECK(noDwell.status==Status::Suspended&&noDwell.interaction.status==FeedInteractionStatus::Interrupted&&!noDwell.dispatch);
        CHECK(!expired.reload->Pending()&&expired.ledger.Snapshot().counts.loaded==0&&expired.ledger.Snapshot().counts.reserve==1);
    }
    // Real input can retain the object across a longer contact gap, but cannot
    // extend the original gesture's input-gap allowance.
    Fixture f(FixedContainerKind::SingleRoundMuzzle,0,1);CHECK(f.Acquire());CHECK(!f.Run(.2f).dispatch);
    for(unsigned n=0;n<5;++n){
        f.Tick(.2f);f.hands.Update(f.x.input);
        CHECK(f.hands.Renew(f.x.input,f.gun,{{300,1},f.x.input.sequence,f.x.input.deadlineNs,true}).claim);
        CHECK(f.reload->RetainHeld(f.x.input,f.claim,true,f.hands));
    }
    const auto resumed=f.Run(.2f);
    CHECK(resumed.status==Status::Suspended&&resumed.interaction.status==FeedInteractionStatus::Interrupted&&!resumed.dispatch);
    CHECK(!f.reload->Pending()&&f.ledger.Snapshot().counts.loaded==0);return 0;
}
int HeldContactGapRejectsExpiredOrForeignClaims(){
    for(unsigned branch=0;branch<10;++branch){
        Fixture f(FixedContainerKind::SingleRoundMuzzle,0,1);CHECK(f.Acquire());
        const auto held=*f.hands.Current(InteractionHand::Left);auto token=held.token;
        f.Tick();
        if(branch==0)f.x.input.released[0]=true;
        if(branch==1)f.x.input.tracked[0]=false;
        if(branch==2)f.x.input.focused=false;
        if(branch==3){f.x.input.observedNs=f.x.input.nowNs=held.deadlineNs;f.x.input.deadlineNs=held.deadlineNs+1000;}
        if(branch==4)++f.x.input.owner.equipGeneration;
        if(branch==5)++token.id;
        if(branch==6)++token.item.generation;
        f.hands.Update(f.x.input);
        (void)f.hands.Renew(f.x.input,f.gun,{{300,1},f.x.input.sequence,f.x.input.deadlineNs,true});
        if(branch==7)(void)f.hands.Release(f.x.input,f.gun);
        if(branch==8)(void)f.hands.Release(f.x.input,f.claim);
        CHECK(!f.reload->RetainHeld(f.x.input,token,branch!=9,f.hands));
        CHECK(!f.reload->Pending()&&f.ledger.Snapshot().counts.loaded==0&&f.ledger.Snapshot().counts.reserve==1);
        if(branch==3||branch==8)CHECK(!f.reload->RetainHeld(f.x.input,f.claim,true,f.hands)&&!f.hands.Current(InteractionHand::Left));
    }
    Fixture mechanism;CHECK(mechanism.Acquire());
    CHECK(!mechanism.reload->RetainHeld(mechanism.x.input,mechanism.claim,true,mechanism.hands));
    Fixture pending(FixedContainerKind::SingleRoundMuzzle,0,1);CHECK(pending.Acquire());
    CHECK(pending.Finish().dispatch&&pending.reload->Pending());
    CHECK(!pending.reload->RetainHeld(pending.x.input,pending.claim,true,pending.hands));
    return 0;
}

int DelayedHeldGeometryKeepsItsOwnLease(){
    for(unsigned branch=0;branch<4;++branch){
        Fixture f(FixedContainerKind::SingleRoundMuzzle,0,1);CHECK(f.Acquire());CHECK(!f.Run(.2f).dispatch);
        f.Tick(.4f);f.hands.Update(f.x.input);
        CHECK(f.hands.Renew(f.x.input,f.gun,{{300,1},f.x.input.sequence,f.x.input.deadlineNs,true}).claim);
        auto geometry=f.x;const auto source=geometry.input;const auto counts=f.ledger.Snapshot().counts;
        f.Tick(.4f);f.hands.Update(f.x.input);
        CHECK(f.hands.Renew(f.x.input,f.gun,{{300,1},f.x.input.sequence,f.x.input.deadlineNs,true}).claim);
        auto retained=f.reload->RetainHeld(f.x.input,f.claim,true,f.hands);CHECK(retained);
        CHECK(retained->inputSequence>source.sequence&&retained->deadlineNs>source.deadlineNs);
        if(branch==1){
            // Keep current custody live while the original renderer lease
            // expires. Its deadline must never inherit the safety renewal.
            f.x.input.nowNs=source.deadlineNs-10;f.Tick(.4f);f.hands.Update(f.x.input);
            CHECK(f.hands.Renew(f.x.input,f.gun,{{300,1},f.x.input.sequence,f.x.input.deadlineNs,true}).claim);
            retained=f.reload->RetainHeld(f.x.input,f.claim,true,f.hands);CHECK(retained);
        }
        if(branch==2){++geometry.input.observedNs;++geometry.interaction.contact.observedNs;}
        if(branch==3){
            // Real release with only an intermediate pose cannot complete,
            // even though the current release and delayed pose are recorded.
            const auto before=*f.hands.Current(InteractionHand::Left);
            f.Tick(.4f,false);f.x.input.released[0]=true;const auto released=f.hands.Update(f.x.input);
            CHECK(released.released[0]);
            geometry.releaseEvidence=FixedContainerReleaseEvidence{before,*released.released[0],f.x.input};
            CHECK(f.hands.Renew(f.x.input,f.gun,{{300,1},f.x.input.sequence,f.x.input.deadlineNs,true}).claim);
            geometry.interaction.grip=false;
        }
        geometry.ammunition=f.x.ammunition;geometry.interaction.contact.snapshotSequence=geometry.ammunition.sequence;
        geometry.currentSafety=f.x.input;geometry.input.nowNs=f.x.input.nowNs;
        const auto result=f.reload->Update(geometry,f.hands);
        CHECK(!result.dispatch&&!f.reload->Pending()&&f.ledger.Snapshot().counts==counts);
        if(branch==0){
            CHECK(result.interaction.status==FeedInteractionStatus::Moving&&result.interaction.target);
            CHECK(result.interaction.target->values==geometry.interaction.contact.weaponFromHandContact.values);
            const auto held=f.hands.Current(InteractionHand::Left);CHECK(held&&held->token==retained->token);
            CHECK(held->inputSequence==retained->inputSequence&&held->deadlineNs==retained->deadlineNs);
        }else if(branch==1)CHECK(result.rejection==FixedContainerRejection::Admission&&retained->deadlineNs>geometry.input.nowNs);
        else if(branch==2)CHECK(result.rejection==FixedContainerRejection::Hand);
        else CHECK(result.rejection==FixedContainerRejection::Gesture&&!f.hands.Current(InteractionHand::Left));
    }
    return 0;
}

int FreshArmedInsertionKeepsExactSourceAndNeutral(){
    // Arm with an already held object, as the pouch adapter does, but do not
    // acquire the policy gesture until after two custody-only safety renewals.
    for(unsigned branch=0;branch<11;++branch){
        Fixture f(FixedContainerKind::SingleRoundMuzzle,0,1);
        f.Tick(0,branch==4);f.Renew();
        const auto acquire=[&](){
            const auto got=f.hands.Acquire(f.x.input,{f.x.input.owner,InteractionHand::Left,HandClaimKind::AmmoObject,{11,12},
                {{f.profile.geometry[2].contact.role,f.profile.revision},f.x.input.sequence,f.x.input.deadlineNs,true},++f.intent,0});
            if(!got.claim)return false;f.claim=got.claim->token;f.heldClaim=true;f.x.claim=f.claim;return true;
        };
        CHECK(acquire());const auto token=f.claim;const auto counts=f.ledger.Snapshot().counts;
        if(branch==10){f.x.currentSafety=f.x.input;--f.x.input.observedNs;--f.x.interaction.contact.observedNs;}
        const auto neutral=f.reload->Update(f.x,f.hands);
        CHECK(neutral.interaction.status==(branch==4?FeedInteractionStatus::NeedNeutral:FeedInteractionStatus::Armed));
        f.x.currentSafety.reset();
        CHECK(!f.reload->BlocksFire()); // Arming has not acquired motion.
        if(branch==5)f.reload->Suspend();
        if(branch==6)CHECK(f.Run(.5f).interaction.status==FeedInteractionStatus::ContactMiss);
        if(branch==8)f.x.input.nowNs+=40; // Original neutral-to-grip gap must still reject.
        const auto safety=[&](){
            f.hands.Update(f.x.input);
            return bool(f.hands.Renew(f.x.input,f.gun,{{300,1},f.x.input.sequence,f.x.input.deadlineNs,true}).claim);
        };
        if(branch==9){
            // A genuine released neutral without a claim clears the old arming
            // identity; an ordinary new grasp must still be possible.
            f.Tick(0,false);f.x.input.released[0]=true;f.heldClaim=false;f.x.claim.reset();CHECK(safety());
            CHECK(!f.hands.Current(InteractionHand::Left));
            CHECK(f.reload->Update(f.x,f.hands).interaction.status==FeedInteractionStatus::Armed);
            CHECK(f.Acquire()&&f.claim!=token&&!f.reload->Pending()&&f.ledger.Snapshot().counts==counts);continue;
        }
        f.Tick();CHECK(safety());auto geometry=f.x;const auto source=geometry.input;
        std::optional<HandClaim> retained;
        for(unsigned n=0;n<2;++n){f.Tick();CHECK(safety());retained=f.reload->RetainHeld(f.x.input,token,true,f.hands);CHECK(retained);}
        CHECK(f.x.input.sequence==source.sequence+2&&retained->inputSequence>source.sequence);
        if(branch==1){++geometry.input.observedNs;++geometry.interaction.contact.observedNs;}
        if(branch==2){
            f.x.input.nowNs=source.deadlineNs-10;f.Tick();CHECK(safety());
            retained=f.reload->RetainHeld(f.x.input,token,true,f.hands);CHECK(retained&&retained->deadlineNs>source.deadlineNs);
        }
        if(branch==3){
            const auto before=*f.hands.Current(InteractionHand::Left);
            f.Tick(0,false);f.x.input.released[0]=true;const auto released=f.hands.Update(f.x.input);CHECK(released.released[0]);
            geometry.releaseEvidence=FixedContainerReleaseEvidence{before,*released.released[0],f.x.input};
            CHECK(safety());geometry.interaction.grip=false;
        }
        if(branch==7){
            CHECK(f.hands.Release(f.x.input,token).accepted);f.Tick();CHECK(safety());CHECK(acquire());
            CHECK(f.claim!=token&&!f.reload->RetainHeld(f.x.input,f.claim,true,f.hands));geometry.claim=f.claim;
        }
        geometry.ammunition=f.x.ammunition;geometry.interaction.contact.snapshotSequence=geometry.ammunition.sequence;
        geometry.currentSafety=f.x.input;geometry.input.nowNs=f.x.input.nowNs;
        const auto result=f.reload->Update(geometry,f.hands);
        CHECK(!result.dispatch&&!f.reload->Pending()&&f.ledger.Snapshot().counts==counts);
        if(branch==0){
            CHECK(result.interaction.status==FeedInteractionStatus::Acquired&&result.interaction.target);
            const auto held=f.hands.Current(InteractionHand::Left);CHECK(held&&held->token==token);
            CHECK(held->inputSequence==retained->inputSequence&&held->deadlineNs==retained->deadlineNs);
        }else{
            CHECK(!result.interaction.target);
            if(branch==2)CHECK(result.rejection==FixedContainerRejection::Admission);
            else if(branch==8)CHECK(result.rejection==FixedContainerRejection::Gesture&&result.interaction.status==FeedInteractionStatus::Interrupted);
            else CHECK(result.rejection==FixedContainerRejection::Hand);
            if(branch==3)CHECK(!f.hands.Current(InteractionHand::Left));
            if(branch>=4&&branch<=6)CHECK(f.Run().interaction.status==FeedInteractionStatus::NeedNeutral);
        }
    }
    return 0;
}

int main(){if(FixedPolicyRejectionDiagnostics())return 1;
    CHECK(GuidedFixedRoundsRequireFreshInwardTravelAndRelease()==0);
    CHECK(GuidedFixedRoundsWithdrawWithoutDroppingCustody()==0);
    CHECK(GuidedFixedRoundSafetyCannotCompleteContact()==0);
    CHECK(FreshArmedInsertionKeepsExactSourceAndNeutral()==0);
    CHECK(HeldContactGapRetainsOnlyCustody()==0);
    CHECK(HeldContactGapRejectsExpiredOrForeignClaims()==0);
    CHECK(DelayedHeldGeometryKeepsItsOwnLease()==0);
    CHECK(CylinderPreservesLiveRoundsAndClosesPartiallyLoaded()==0);
    CHECK(ClipOneAtomicBoundedTransferAndNoExtraChamber()==0);
    CHECK(SingleRoundProfileAndOpeningPreserveLiveAmmunition()==0);
    CHECK(SingleRoundCycleRequiresCloseAndRejectsDuplicateAndForeignAuthority()==0);
    CHECK(SingleRoundPendingAndPartialCloseResumeAfterStowAndTrackingLoss()==0);
    CHECK(MuzzleRequiresOnlyMeasuredInsertionAndRejectsClipOrBreechPolicy()==0);
    CHECK(MuzzlePartialInsertionAndPendingStowRequireExactReceipt()==0);
    CHECK(TrackingAndStowRetainMechanicalDebtAndPendingReceipt()==0);
    CHECK(WrongClaimsAndTeleportCannotLoad()==0);
    CHECK(ExactTypedReceiptAndDuplicateCannotFinishOtherTransfer()==0);
    CHECK(LedgerBatchRejectsInvalidAndPreservesOriginalStore()==0);
    CHECK(InvalidProfileOwnerAndUnverifiedGeometryFailClosed()==0);
    CHECK(PlacementRebasePinsDestinationAndRejectsHeldMutation()==0);
    return 0;
}
