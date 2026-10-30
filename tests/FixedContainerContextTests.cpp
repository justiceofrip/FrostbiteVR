#include "fvr/interaction/FixedContainerReload.h"
#include "Test.h"
#include <memory>
using namespace fvr::interaction;
namespace {
using Matrix=fvr::math::Matrix4;
Matrix Identity(){Matrix m{};for(unsigned n=0;n<4;++n)m.values[n][n]=1;return m;}
// Synthetic cylinder geometry isolates retained mechanical debt and fresh
// custody. Native service receipt creation is covered by the BC2 service suite.
struct Fixture {
    FixedContainerProfile profile{};FixedContainerSample x{};AmmunitionLedger ledger;HandInteraction hands;
    std::unique_ptr<FixedContainerReload> policy;HandClaimToken gun{},claim{};
    std::uint64_t intent=0;bool held=false;
    Fixture(){
        profile.kind=FixedContainerKind::Cylinder;profile.id=20;profile.revision=21;profile.ammunitionKind=22;
        profile.capacity=6;profile.roundsPerInsertion=1;profile.maxAgeNs=1000;
        for(unsigned n=0;n<4;++n){
            auto& g=profile.geometry[n];g.contact={100+n,.02f,.1f};auto& m=g.motion;
            m.operation=fixed_container_detail::Operation(static_cast<FixedContainerStep>(n));m.kind=FeedMotionKind::Linear;
            m.measured=true;m.weaponFromMotion=m.motionFromContact=Identity();m.travel=.1f;
            m.completeFraction=.9f;m.releaseFraction=.8f;m.pathToleranceMeters=.005f;m.orientationToleranceRadians=.1f;
            m.maxStepMeters=.04f;m.maxStepRadians=.25f;m.endpointDwellNs=20;m.maximumGestureNs=1000;m.maxInputGapNs=40;
        }
        x.ammunition={{{1,2,3,4},5,6},1,1000,2000,{2,4,6},true};
        x.input={{101,2,71,6},1,1000,2000,1000,true,{true,true},{false,false}};
        x.held=true;x.weaponFromContact=Identity();x.interaction.hand={44,1};x.interaction.trackingGeneration=6;
        x.interaction.focused=x.interaction.contact.tracked=true;
        policy=std::make_unique<FixedContainerReload>(profile,x.ammunition.context,FeedKey{7,8},FeedKey{9,10},
            FeedSupplyKey{{11,12},{}},30,ledger,true,x.input.owner);
        (void)policy->Bind(x.ammunition,x.input.nowNs);AcquireGun();
    }
    void AcquireGun(){
        const auto got=hands.Acquire(x.input,{x.input.owner,InteractionHand::Right,HandClaimKind::GunHold,
            {3,x.input.owner.equipGeneration},{{300,1},x.input.sequence,x.input.deadlineNs,true},++intent,0});
        if(got.claim)gun=got.claim->token;
    }
    void Tick(float progress,bool grip){
        ++x.ammunition.sequence;++x.input.sequence;x.input.nowNs+=10;
        x.ammunition.observedNs=x.input.observedNs=x.input.nowNs;
        x.ammunition.deadlineNs=x.input.deadlineNs=x.input.nowNs+1000;
        x.ammunition.counts=ledger.Snapshot().counts;x.input.released={false,false};
        const auto& c=x.ammunition.context;
        x.interaction.contact={{c.resource.actor,c.resource.actorGeneration,c.resource.weapon,c.equipGeneration,c.space},
            {7,8},x.ammunition.sequence,x.input.sequence,x.input.observedNs,true,true,Identity()};
        x.interaction.contact.weaponFromHandContact.values[3][0]=progress*.1f;
        x.interaction.grip=grip;x.claim=held?std::optional{claim}:std::nullopt;
        if(ledger.Phase()==AmmunitionLedgerPhase::Ready)(void)ledger.Bind(x.ammunition,x.input.nowNs);
    }
    void Renew(){
        hands.Update(x.input);hands.Renew(x.input,gun,{{300,1},x.input.sequence,x.input.deadlineNs,true});
        if(held)hands.Renew(x.input,claim,{claim.contact,x.input.sequence,x.input.deadlineNs,true});
    }
    FixedContainerResult Run(float p,bool grip){Tick(p,grip);Renew();return policy->Update(x,hands);}
    bool Acquire(float p=0){
        Tick(p,true);Renew();const bool insert=policy->Step()==FixedContainerStep::Insert;
        const auto role=profile.geometry[static_cast<unsigned>(policy->Step())].contact.role;
        const auto got=hands.Acquire(x.input,{x.input.owner,InteractionHand::Left,
            insert?HandClaimKind::AmmoObject:HandClaimKind::Mechanism,
            insert?HandInteractionKey{11,12}:HandInteractionKey{3,x.input.owner.equipGeneration},
            {{role,21},x.input.sequence,x.input.deadlineNs,true},++intent,insert?0:gun.id});
        if(!got.claim)return false;claim=got.claim->token;held=true;x.claim=claim;
        return policy->Update(x,hands).interaction.status==FeedInteractionStatus::Acquired;
    }
    FixedContainerResult Finish(){FixedContainerResult r;
        for(float p:{.2f,.4f,.6f,.8f,1.f,1.f,1.f})r=Run(p,true);return r;}
    void Release(){
        if(held){Tick(0,false);Renew();(void)hands.Release(x.input,claim);held=false;x.claim.reset();}
    }
    bool Migrate(){
        policy->Suspend();++x.ammunition.context.equipGeneration;++x.ammunition.context.space;
        ++x.input.owner.equipGeneration;++x.input.owner.space;x.interaction.trackingGeneration=x.input.owner.space;
        Tick(.2f,false);hands.Update(x.input);held=false;x.claim.reset();AcquireGun();
        return policy->RebindContext(x.ammunition,x.input.owner,x.input.nowNs);
    }
};
int MigrationPreservesRecoveryAndRequiresNewHandContact(){
    Fixture f;CHECK(f.Run(0,false).interaction.status==FeedInteractionStatus::Armed);CHECK(f.Acquire());
    CHECK(f.Run(.2f,true).interaction.status==FeedInteractionStatus::Moving);
    const auto before=f.policy->RecoveryMotion();const auto oldClaim=f.claim;CHECK(before&&f.policy->BlocksFire());
    CHECK(f.Migrate());const auto migrated=f.policy->RecoveryMotion();CHECK(migrated);
    CHECK(migrated->WeaponFromHeld().values==before->WeaponFromHeld().values);
    CHECK(f.policy->Step()==FixedContainerStep::Open&&f.policy->BlocksFire());
    CHECK(!f.hands.Renew(f.x.input,oldClaim,{oldClaim.contact,f.x.input.sequence,f.x.input.deadlineNs,true}).accepted);
    f.x.claim=oldClaim;f.x.interaction.grip=true;
    CHECK(f.policy->Update(f.x,f.hands).status==FixedContainerStatus::Suspended);
    f.x.claim.reset();f.x.interaction.grip=false;f.x.weaponFromContact=migrated->WeaponFromHeld();
    CHECK(!f.policy->RecoverMotion(f.x,*before)); // Old context proof is revoked.
    CHECK(f.policy->RecoverMotion(f.x,*migrated));
    CHECK(f.Run(.2f,false).interaction.status==FeedInteractionStatus::Armed);
    CHECK(f.Acquire(.2f));CHECK(f.claim.owner!=oldClaim.owner&&f.claim.id!=oldClaim.id);
    CHECK(f.Run(.4f,true).interaction.status==FeedInteractionStatus::Moving);
    for(float p:{.6f,.8f,1.f,1.f,1.f})f.Run(p,true);
    CHECK(f.policy->Step()==FixedContainerStep::Open); // Recovery requires fresh release after dwell.
    f.Tick(1,false);f.x.input.released[0]=true;
    CHECK(f.policy->Update(f.x,f.hands).status==FixedContainerStatus::PhysicalStep);
    CHECK(f.policy->Step()==FixedContainerStep::EjectSpent);
    CHECK((f.ledger.Snapshot().counts==AmmunitionCounts{2,4,6}));return 0;
}
int MigrationCannotAcquireWithHeldGripOrMissedCurrentContact(){
    Fixture f;CHECK(f.Run(0,false).interaction.status==FeedInteractionStatus::Armed);CHECK(f.Acquire());
    f.Run(.2f,true);CHECK(f.Migrate());
    const auto proof=f.policy->RecoveryMotion();CHECK(proof);f.x.weaponFromContact=proof->WeaponFromHeld();
    CHECK(f.policy->RecoverMotion(f.x,*proof));
    CHECK(!f.Acquire(.2f)); // A new claim alone does not supply neutral.
    CHECK(!f.policy->Pending());f.Release();
    const auto retained=f.policy->RecoveryMotion();CHECK(retained);
    f.Tick(.2f,false);f.Renew();CHECK(f.policy->RecoverMotion(f.x,*retained));
    CHECK(f.Run(.2f,false).interaction.status==FeedInteractionStatus::Armed);
    CHECK(!f.Acquire(.8f)); // Correct owner, wrong actual contact.
    CHECK(!f.policy->Pending()&&f.policy->Step()==FixedContainerStep::Open);
    CHECK((f.ledger.Snapshot().counts==AmmunitionCounts{2,4,6}));return 0;
}
int RetainedInsertCompletionLeavesCylinderOpenAndCountsConserved(){
    Fixture f;
    for(unsigned n=0;n<2;++n){
        CHECK(f.Run(0,false).interaction.status==FeedInteractionStatus::Armed);CHECK(f.Acquire());
        CHECK(f.Finish().status==FixedContainerStatus::PhysicalStep);f.Release();
    }
    CHECK(f.policy->Step()==FixedContainerStep::Insert);
    CHECK(f.Run(0,false).interaction.status==FeedInteractionStatus::Armed);CHECK(f.Acquire());
    const auto queued=f.Finish().dispatch;CHECK(queued&&f.ledger.Phase()==AmmunitionLedgerPhase::Queued);
    CHECK(f.ledger.Dispatch(f.x.ammunition,f.x.input.nowNs));
    const AmmunitionReceipt receipt{queued->ammunition,1,queued->ammunition.requestedNs,
        queued->ammunition.requestedNs+1,queued->ammunition.before,queued->ammunition.after,true,true};
    CHECK(f.ledger.Complete(receipt,receipt.completedNs));
    CHECK(!f.Migrate()); // The policy still owes this exact receipt.
    CHECK(f.policy->CompleteAfterContextChange(*queued,receipt,f.x.ammunition,f.x.input.nowNs));
    CHECK(f.policy->RebindContext(f.x.ammunition,f.x.input.owner,f.x.input.nowNs));
    CHECK(f.policy->Step()==FixedContainerStep::Insert&&f.policy->BlocksFire()&&!f.policy->Pending());
    CHECK((f.ledger.Snapshot().counts==AmmunitionCounts{3,3,6}));
    CHECK(f.policy->CompleteAfterContextChange(*queued,receipt,f.x.ammunition,f.x.input.nowNs));
    CHECK((f.ledger.Snapshot().counts==AmmunitionCounts{3,3,6}));return 0;
}
}
int main(){return MigrationPreservesRecoveryAndRequiresNewHandContact()||MigrationCannotAcquireWithHeldGripOrMissedCurrentContact()||
    RetainedInsertCompletionLeavesCylinderOpenAndCountsConserved();}
