#include "fvr/interaction/FeedReload.h"
#include "Test.h"
#include <type_traits>
using namespace fvr::interaction;
namespace {
using Op=FeedPhysicalOperation;
using Status=FeedReloadStatus;
static_assert(!std::is_copy_constructible_v<FeedReload>);
static_assert(!std::is_copy_assignable_v<FeedReload>);
fvr::math::Matrix4 Identity(){fvr::math::Matrix4 m{};for(unsigned i=0;i<4;++i)m.values[i][i]=1;return m;}
struct Fixture {
    FeedMechanismProfile p{};
    FeedReloadSample x{};
    FeedSupplyKey old{{10,1},{20,1}},goal{{30,2},{40,2}};
    std::optional<FeedReload> reload{};
    std::uint64_t intent=0;
    Fixture(){
        p.family=FeedFamily::Belt;p.id=1;p.revision=2;p.ammunitionKind=3;p.maxAgeNs=100;
        p.cover=p.latch=p.container=p.charge=true;
        p.containerNeedsOpen=p.feedNeedsOpen=p.detachNeedsUnseated=true;
        auto& s=x.snapshot;
        s.owner={1,2,3,4,5};s.mechanism={6,7};s.profileId=p.id;s.profileRevision=p.revision;
        s.sequence=10;s.inputSequence=20;s.observedNs=x.nowNs=1000;s.reconciled=true;
        s.cover=FeedCoverState::Closed;s.latch=FeedLatchState::Engaged;
        s.container=FeedContainerState::Attached;s.feed=FeedSeatState::Seated;s.cycle=FeedCycleState::Required;
        s.attached=s.seated=old;s.supply={goal,p.ammunitionKind,FeedSupplyState::Usable};
        for(unsigned i=0;i<p.contacts.size();++i){p.contacts[i]={100+i,.025f,.2f};s.contacts[i]={100+i,Identity()};}
        x.focused=x.tracked=x.held=true;
        x.binding.key={90,1};x.binding.owner=s.owner;x.binding.mechanism=s.mechanism;
        x.binding.profileId=p.id;x.binding.profileRevision=p.revision;x.binding.verified=true;
        x.binding.operations.fill(true);x.binding.operations[0]=false;
    }
    void Start(){reload.emplace(p,x.snapshot.owner,x.snapshot.mechanism,goal,500);}
    void Tick(){
        ++x.snapshot.sequence;++x.snapshot.inputSequence;x.snapshot.observedNs=++x.nowNs;
        x.neutral=false;x.completion.reset();x.acknowledgement.reset();
    }
    FeedReloadResult Update(){return reload->Update(x);}
    FeedReloadResult Neutral(){Tick();x.neutral=true;return Update();}
    void Intent(){
        Tick();const auto c=EvaluateFeedMechanism(p,x.snapshot,goal,x.nowNs).candidate;
        if(c)x.completion=FeedPhysicalCompletion{++intent,x.snapshot.inputSequence,x.nowNs,*c};
    }
    void Effect(Op op){ // Synthetic adapter observations, never coordinator mutations.
        auto& s=x.snapshot;
        switch(op){
        case Op::ReleaseLatch:s.latch=FeedLatchState::Released;break;
        case Op::OpenCover:s.cover=FeedCoverState::Open;break;
        case Op::WithdrawFeed:s.feed=FeedSeatState::Unseated;s.seated={};break;
        case Op::DetachContainer:s.container=FeedContainerState::Detached;s.attached={};break;
        case Op::AttachContainer:s.container=FeedContainerState::Attached;s.attached=goal;break;
        case Op::SeatFeed:s.feed=FeedSeatState::Seated;s.seated=goal;break;
        case Op::CloseCover:s.cover=FeedCoverState::Closed;break;
        case Op::EngageLatch:s.latch=FeedLatchState::Engaged;break;
        case Op::CycleAction:s.cycle=FeedCycleState::NativeReady;break;
        default:break;
        }
    }
    void Ack(const FeedReloadRequest& request,FeedReloadAcknowledgement status=FeedReloadAcknowledgement::Applied){
        x.acknowledgement=FeedReloadAck{request,status,x.snapshot.sequence,x.nowNs};
    }
    FeedReloadResult Apply(const FeedReloadRequest& request){
        Tick();Effect(request.completion.candidate.operation);Ack(request);return Update();
    }
    void Ready(){
        x.snapshot.cover=FeedCoverState::Closed;x.snapshot.latch=FeedLatchState::Engaged;
        x.snapshot.container=FeedContainerState::Attached;x.snapshot.feed=FeedSeatState::Seated;
        x.snapshot.attached=x.snapshot.seated=goal;x.snapshot.cycle=FeedCycleState::NativeReady;
    }
};
int CompleteReplacement(){
    Fixture f;f.Start();
    constexpr Op order[]={Op::ReleaseLatch,Op::OpenCover,Op::WithdrawFeed,Op::DetachContainer,
        Op::AttachContainer,Op::SeatFeed,Op::CloseCover,Op::EngageLatch,Op::CycleAction};
    unsigned count=0;
    for(const auto op:order){
        CHECK(f.Neutral().status==Status::Armed);f.Intent();auto r=f.Update();
        CHECK(r.dispatch&&r.pending==r.dispatch&&r.dispatch->completion.candidate.operation==op);
        const auto request=*r.dispatch;
        CHECK(request.id==count+1&&request.lifetime==500&&request.binding==f.x.binding.key);
        if(op==Op::WithdrawFeed||op==Op::DetachContainer)CHECK(request.completion.candidate.supply==f.old);
        if(op==Op::AttachContainer||op==Op::SeatFeed)CHECK(request.completion.candidate.supply==f.goal);
        for(unsigned repeat=0;repeat<3;++repeat){r=f.Update();CHECK(!r.dispatch&&r.pending==request&&r.acceptedOperations==count);}
        r=f.Apply(request);++count;
        CHECK(!r.dispatch&&!r.pending&&r.acceptedOperations==count&&r.lastApplied==request);
        CHECK(r.completed==(op==Op::CycleAction));
    }
    CHECK(f.reload->State().phase==FeedReloadPhase::Complete);
    CHECK(f.Neutral().completed&&!f.Update().dispatch);return 0;
}
int PartialAndEmptyProfiles(){
    for(const bool empty:{false,true}){
        Fixture f;
        if(empty){f.x.snapshot.feed=FeedSeatState::Unseated;f.x.snapshot.seated={};
            f.x.snapshot.container=FeedContainerState::Detached;f.x.snapshot.attached={};}
        else {f.Ready();f.x.snapshot.feed=FeedSeatState::Unseated;f.x.snapshot.seated={};}
        f.x.snapshot.cover=FeedCoverState::Open;f.x.snapshot.latch=FeedLatchState::Released;
        f.Start();
        const Op emptyOrder[]={Op::AttachContainer,Op::SeatFeed,Op::CloseCover,Op::EngageLatch,Op::CycleAction};
        const Op partialOrder[]={Op::SeatFeed,Op::CloseCover,Op::EngageLatch};
        const auto* order=empty?emptyOrder:partialOrder;
        const unsigned steps=empty?5u:3u;
        for(unsigned i=0;i<steps;++i){CHECK(f.Neutral().status==Status::Armed);f.Intent();const auto r=f.Update();
            CHECK(r.dispatch&&r.dispatch->completion.candidate.operation==order[i]);
            CHECK(f.Apply(*r.dispatch).completed==(i+1==steps));}
        CHECK(f.reload->State().acceptedOperations==steps);
    }return 0;
}
int OptionalPartsDoNotInventOperations(){
    Fixture f;f.p.cover=f.p.latch=f.p.container=f.p.charge=false;
    f.p.containerNeedsOpen=f.p.feedNeedsOpen=f.p.detachNeedsUnseated=false;
    f.p.contacts[0]=f.p.contacts[1]=f.p.contacts[2]=f.p.contacts[4]={};
    f.x.snapshot.cover=FeedCoverState::Absent;f.x.snapshot.latch=FeedLatchState::Absent;
    f.x.snapshot.container=FeedContainerState::Absent;f.x.snapshot.cycle=FeedCycleState::Absent;
    f.x.snapshot.attached={};f.old.container={};f.goal.container={};
    f.x.snapshot.seated=f.old;f.x.snapshot.supply.key=f.goal;f.Start();
    for(const auto op:{Op::WithdrawFeed,Op::SeatFeed}){
        CHECK(f.Neutral().status==Status::Armed);f.Intent();const auto r=f.Update();
        CHECK(r.dispatch&&r.dispatch->completion.candidate.operation==op);
        CHECK(f.Apply(*r.dispatch).completed==(op==Op::SeatFeed));
    }return 0;
}
int ReceiptIsConsumedAcrossPackets(){
    Fixture f;f.Start();CHECK(f.Neutral().status==Status::Armed);f.Intent();auto r=f.Update();CHECK(r.dispatch);
    const auto request=*r.dispatch;
    f.Intent();const auto discarded=*f.x.completion;
    CHECK(!f.Update().dispatch);CHECK(!f.Apply(request).pending);
    CHECK(f.Neutral().status==Status::Armed);f.Intent();f.x.completion->id=discarded.id;
    CHECK(f.Update().status==Status::DuplicateCompletion&&!f.reload->State().pending);
    CHECK(f.Neutral().status==Status::Armed);f.Intent();r=f.Update();CHECK(r.dispatch&&r.dispatch->id==2);
    return 0;
}
int SamePacketCannotDispatchOrRearm(){
    Fixture f;f.Start();CHECK(f.Neutral().status==Status::Armed);
    f.x.neutral=false;
    const auto candidate=*EvaluateFeedMechanism(f.p,f.x.snapshot,f.goal,f.x.nowNs).candidate;
    f.x.completion=FeedPhysicalCompletion{++f.intent,f.x.snapshot.inputSequence,f.x.nowNs,candidate};
    CHECK(f.Update().status==Status::DuplicatePacket);
    f.Intent();f.x.completion->id=1;
    CHECK(f.Update().status==Status::DuplicateCompletion);
    CHECK(f.Neutral().status==Status::Armed);f.Intent();const auto r=f.Update();CHECK(r.dispatch);
    CHECK(!f.Apply(*r.dispatch).pending);
    f.x.acknowledgement.reset();f.x.neutral=true;
    CHECK(f.Update().status==Status::DuplicatePacket);return 0;
}
int CompletionMustMatchExactCandidate(){
    for(unsigned mismatch=0;mismatch<16;++mismatch){
        Fixture f;f.Start();CHECK(f.Neutral().status==Status::Armed);f.Intent();auto& c=*f.x.completion;
        switch(mismatch){
        case 0:++c.candidate.owner.actor;break;case 1:++c.candidate.owner.actorGeneration;break;
        case 2:++c.candidate.owner.weapon;break;case 3:++c.candidate.owner.equipGeneration;break;
        case 4:++c.candidate.owner.space;break;case 5:++c.candidate.mechanism.generation;break;
        case 6:++c.candidate.profileId;break;case 7:++c.candidate.profileRevision;break;
        case 8:++c.candidate.supply.resource.id;break;case 9:++c.candidate.goal.resource.generation;break;
        case 10:c.candidate.operation=Op::OpenCover;break;case 11:++c.candidate.snapshotSequence;break;
        case 12:++c.inputSequence;break;case 13:--c.completedNs;break;
        case 14:++c.completedNs;break;default:++c.candidate.role;break;
        }
        CHECK(f.Update().status==Status::InvalidCompletion&&!f.reload->State().pending);
        CHECK(f.Neutral().status==Status::Armed);f.Intent();f.x.completion->id=1;
        CHECK(f.Update().status==Status::DuplicateCompletion);
    }return 0;
}
int AckMustEchoExactRequest(){
    for(unsigned mismatch=0;mismatch<15;++mismatch){
        Fixture f;f.Start();CHECK(f.Neutral().status==Status::Armed);f.Intent();const auto sent=f.Update();CHECK(sent.dispatch);
        const auto request=*sent.dispatch;f.Tick();f.Effect(Op::ReleaseLatch);f.Ack(request);
        auto& r=f.x.acknowledgement->request;
        switch(mismatch){
        case 0:++r.id;break;case 1:++r.lifetime;break;case 2:++r.binding.generation;break;
        case 3:++r.completion.candidate.owner.actorGeneration;break;case 4:++r.completion.candidate.mechanism.id;break;
        case 5:++r.completion.candidate.profileRevision;break;case 6:++r.completion.candidate.supply.container.id;break;
        case 7:++r.completion.candidate.goal.resource.id;break;case 8:r.completion.candidate.operation=Op::OpenCover;break;
        case 9:++r.completion.id;break;case 10:++r.completion.inputSequence;break;
        case 11:++r.completion.completedNs;break;case 12:++r.requestedNs;break;
        case 13:++r.completion.candidate.snapshotSequence;break;default:++r.completion.candidate.role;break;
        }
        const auto rejected=f.Update();CHECK(rejected.status==Status::AcknowledgementMismatch);
        CHECK(rejected.pending==request&&rejected.acceptedOperations==0&&!rejected.dispatch);
        f.Ack(request);CHECK(f.Update().acceptedOperations==1);
    }return 0;
}
int AppliedNeedsFreshObservedEffect(){
    Fixture f;f.Start();CHECK(f.Neutral().status==Status::Armed);f.Intent();auto r=f.Update();CHECK(r.dispatch);
    const auto request=*r.dispatch;
    f.Tick();f.Ack(request);r=f.Update();
    CHECK(r.status==Status::AwaitingSnapshot&&r.pending==request&&r.acceptedOperations==1&&!r.completed);
    for(unsigned i=0;i<3;++i){r=f.Update();CHECK(r.pending==request&&r.acceptedOperations==1&&!r.dispatch);}
    f.Tick();f.x.snapshot.reconciled=false;f.Effect(Op::ReleaseLatch);r=f.Update();
    CHECK(r.pending==request&&r.acceptedOperations==1&&r.mechanismStatus==FeedMechanismStatus::Unreconciled);
    f.Tick();f.x.snapshot.reconciled=true;r=f.Update();
    CHECK(!r.pending&&r.acceptedOperations==1&&r.status==Status::Reconciled&&!r.dispatch);
    f.Intent();CHECK(f.Update().status==Status::NeutralRequired);return 0;
}
int SnapshotWithoutAckDoesNotAdvance(){
    Fixture f;f.Start();CHECK(f.Neutral().status==Status::Armed);f.Intent();auto r=f.Update();CHECK(r.dispatch);
    const auto request=*r.dispatch;f.Tick();f.Effect(Op::ReleaseLatch);r=f.Update();
    CHECK(r.status==Status::AwaitingAcknowledgement&&r.pending==request&&r.acceptedOperations==0);
    f.x.nowNs+=10000;r=f.Update();CHECK(r.pending==request&&!r.completed&&!r.dispatch);
    f.Tick();f.Ack(request);r=f.Update();CHECK(!r.pending&&r.acceptedOperations==1);return 0;
}
int AckEvidenceCannotBeOldFutureOrImplicit(){
    for(unsigned invalid=0;invalid<5;++invalid){
        Fixture f;f.Start();CHECK(f.Neutral().status==Status::Armed);f.Intent();const auto r=f.Update();CHECK(r.dispatch);
        f.Tick();f.Effect(Op::ReleaseLatch);f.Ack(*r.dispatch);auto& a=*f.x.acknowledgement;
        switch(invalid){case 0:a.snapshotSequence=r.dispatch->completion.candidate.snapshotSequence;break;
        case 1:a.observedNs=r.dispatch->requestedNs;break;case 2:++a.observedNs;break;
        case 3:a.status=FeedReloadAcknowledgement::None;break;default:f.x.nowNs+=101;break;}
        const auto result=f.Update();CHECK(result.status==Status::InvalidAcknowledgement);
        CHECK(result.pending==r.dispatch&&result.acceptedOperations==0);
    }return 0;
}
int CiteLaterSnapshotAndRejectContradictoryAck(){
    Fixture f;f.Start();CHECK(f.Neutral().status==Status::Armed);f.Intent();const auto sent=f.Update();CHECK(sent.dispatch);
    f.Tick();f.Effect(Op::ReleaseLatch);f.Ack(*sent.dispatch);++f.x.acknowledgement->snapshotSequence;
    auto r=f.Update();CHECK(r.pending&&r.acceptedOperations==1);
    f.x.acknowledgement->status=FeedReloadAcknowledgement::Rejected;r=f.Update();
    CHECK(r.status==Status::AcknowledgementMismatch&&r.pending&&r.acceptedOperations==1);
    f.Tick();r=f.Update();CHECK(!r.pending&&r.acceptedOperations==1);return 0;
}
int TrackingAndStowRetainPendingAndApplied(){
    for(const bool stow:{false,true}){
        Fixture f;f.Start();CHECK(f.Neutral().status==Status::Armed);f.Intent();const auto sent=f.Update();CHECK(sent.dispatch);
        f.Tick();if(stow)f.x.held=false;else f.x.tracked=false;
        auto r=f.Update();CHECK(r.pending==sent.dispatch&&r.suspended&&!r.dispatch);
        f.Tick();f.Ack(*sent.dispatch);r=f.Update();CHECK(r.pending==sent.dispatch&&r.acceptedOperations==1&&r.suspended);
        f.Tick();f.Effect(Op::ReleaseLatch);r=f.Update();
        CHECK(!r.pending&&r.lastApplied==sent.dispatch&&r.acceptedOperations==1&&r.suspended);
        f.x.held=f.x.tracked=true;f.Intent();CHECK(f.Update().status==Status::NeutralRequired);
        CHECK(f.Neutral().status==Status::Armed);f.Intent();r=f.Update();
        CHECK(r.dispatch&&r.dispatch->completion.candidate.operation==Op::OpenCover&&r.acceptedOperations==1);
    }return 0;
}
int TrackingLossWithBadAckStillSuspends(){
    Fixture f;f.Start();CHECK(f.Neutral().status==Status::Armed);
    f.Tick();f.x.tracked=false;f.x.acknowledgement=FeedReloadAck{};
    CHECK(f.Update().suspended);f.x.tracked=true;f.Intent();CHECK(!f.Update().dispatch);return 0;
}
int WrongOwnerDoesNotRetargetAcceptedEffect(){
    Fixture f;f.Start();CHECK(f.Neutral().status==Status::Armed);f.Intent();const auto sent=f.Update();CHECK(sent.dispatch);
    const auto owner=f.x.snapshot.owner;f.Tick();++f.x.snapshot.owner.actorGeneration;f.Ack(*sent.dispatch);
    auto r=f.Update();CHECK(r.status==Status::LifetimeMismatch&&r.pending==sent.dispatch&&r.acceptedOperations==1);
    f.Tick();f.x.snapshot.owner=owner;f.Effect(Op::ReleaseLatch);r=f.Update();
    CHECK(!r.pending&&r.lastApplied==sent.dispatch&&r.suspended);return 0;
}
int UnknownAndWrongResourceCannotDispatch(){
    for(unsigned invalid=0;invalid<8;++invalid){
        Fixture f;f.Start();
        switch(invalid){case 0:f.x.snapshot.feed=FeedSeatState::Unknown;break;
        case 1:f.x.snapshot.cycle=FeedCycleState::Unknown;break;case 2:f.x.snapshot.supply.state=FeedSupplyState::Unknown;break;
        case 3:++f.x.snapshot.supply.key.resource.generation;break;case 4:++f.x.snapshot.supply.key.container.generation;break;
        case 5:f.x.snapshot.reconciled=false;break;case 6:f.x.snapshot.cycle=FeedCycleState::Pending;break;
        default:f.x.snapshot.supply.state=FeedSupplyState::Depleted;break;}
        const auto r=f.Neutral();CHECK(!r.dispatch&&!r.completed&&r.status!=Status::Armed);
    }return 0;
}
int PerOperationBindingIsIndependent(){
    for(unsigned invalid=0;invalid<8;++invalid){
        Fixture f;f.Start();
        switch(invalid){case 0:f.x.binding.verified=false;break;case 1:f.x.binding.key={};break;
        case 2:++f.x.binding.owner.space;break;case 3:++f.x.binding.mechanism.generation;break;
        case 4:++f.x.binding.profileId;break;case 5:++f.x.binding.profileRevision;break;
        case 6:f.x.binding.operations.fill(false);break;
        default:f.x.binding.operations[static_cast<unsigned>(Op::ReleaseLatch)]=false;break;}
        const auto r=f.Neutral();CHECK(r.status==Status::UnverifiedBinding&&!r.dispatch);
    }
    Fixture f;f.Start();CHECK(f.Neutral().status==Status::Armed);f.Intent();const auto sent=f.Update();CHECK(sent.dispatch);
    CHECK(!f.Apply(*sent.dispatch).pending);++f.x.binding.key.generation;
    CHECK(f.Neutral().status==Status::UnverifiedBinding);return 0;
}
int SnapshotIdentityAndImmutability(){
    Fixture f;f.Start();CHECK(f.Neutral().status==Status::Armed);
    f.x.snapshot.latch=FeedLatchState::Released;
    CHECK(f.Update().status==Status::SnapshotMismatch);
    f.Tick();--f.x.snapshot.inputSequence; // Equal input cannot rearm.
    f.x.neutral=true;CHECK(f.Update().status==Status::DuplicatePacket);
    f.Tick();f.x.neutral=true;CHECK(f.Update().status==Status::Armed);
    f.Tick();f.x.snapshot.sequence=1;CHECK(f.Update().status==Status::SequenceRollback);
    return 0;
}
int GapAndClockRollbackRequireNewIntent(){
    Fixture f;f.Start();CHECK(f.Neutral().status==Status::Armed);f.x.nowNs+=101;f.Intent();
    CHECK(f.Update().status==Status::NeutralRequired&&!f.reload->State().pending);
    CHECK(f.Neutral().status==Status::Armed);f.Intent();const auto sent=f.Update();CHECK(sent.dispatch);
    --f.x.nowNs;auto r=f.Update();CHECK(r.status==Status::ClockDiscontinuity&&r.pending==sent.dispatch);
    f.x.nowNs+=2;f.Tick();f.Effect(Op::ReleaseLatch);f.Ack(*sent.dispatch);r=f.Update();
    CHECK(!r.pending&&r.acceptedOperations==1&&r.suspended);return 0;
}
int RejectionNeedsEvidenceAndFreshNeutral(){
    Fixture f;f.Start();CHECK(f.Neutral().status==Status::Armed);f.Intent();const auto sent=f.Update();CHECK(sent.dispatch);
    f.Tick();f.Ack(*sent.dispatch,FeedReloadAcknowledgement::Rejected);auto r=f.Update();
    CHECK(!r.pending&&!r.completed&&r.acceptedOperations==0&&r.status==Status::Reconciled);
    f.Intent();CHECK(f.Update().status==Status::NeutralRequired);
    CHECK(f.Neutral().status==Status::Armed);f.Intent();r=f.Update();
    CHECK(r.dispatch&&r.dispatch->id==sent.dispatch->id+1&&r.dispatch->completion.candidate.operation==Op::ReleaseLatch);
    return 0;
}
int CompletionNeedsExplicitFinalEvidence(){
    Fixture already;already.Ready();already.Start();const auto ready=already.Neutral();
    CHECK(ready.status==Status::NoPhysicalStep&&!ready.completed&&ready.acceptedOperations==0);
    Fixture f;f.Ready();f.x.snapshot.cycle=FeedCycleState::Required;f.Start();
    CHECK(f.Neutral().status==Status::Armed);f.Intent();const auto sent=f.Update();CHECK(sent.dispatch);
    f.Tick();f.x.snapshot.cycle=FeedCycleState::Pending;f.Ack(*sent.dispatch);auto r=f.Update();
    CHECK(r.pending&&r.acceptedOperations==1&&!r.completed);
    f.Tick();f.x.snapshot.cycle=FeedCycleState::Unknown;r=f.Update();CHECK(r.pending&&!r.completed);
    f.Tick();f.x.snapshot.cycle=FeedCycleState::NativeReady;f.x.snapshot.supply.state=FeedSupplyState::Unknown;
    r=f.Update();CHECK(r.pending&&!r.completed);
    f.Tick();f.x.snapshot.supply.state=FeedSupplyState::Usable;r=f.Update();
    CHECK(r.completed&&!r.pending&&r.acceptedOperations==1);return 0;
}
int RetirementRequiresOldLifetimeReconciliation(){
    Fixture f;f.Start();CHECK(f.Neutral().status==Status::Armed);f.Intent();const auto sent=f.Update();CHECK(sent.dispatch);
    f.Tick();auto r=f.reload->Retire(f.x.snapshot,f.x.nowNs);
    CHECK(r.status==Status::RetireNeedsReconciliation&&r.pending==sent.dispatch);
    CHECK(!f.Apply(*sent.dispatch).pending);
    CHECK(f.reload->Retire(f.x.snapshot,f.x.nowNs).status==Status::RetireNeedsReconciliation);
    f.Tick();f.x.snapshot.reconciled=false;
    CHECK(f.reload->Retire(f.x.snapshot,f.x.nowNs).status==Status::RetireNeedsReconciliation);
    f.Tick();f.x.snapshot.reconciled=true;r=f.reload->Retire(f.x.snapshot,f.x.nowNs);
    CHECK(r.status==Status::Retired&&r.lastApplied==sent.dispatch&&r.acceptedOperations==1);
    f.Intent();CHECK(f.Update().status==Status::Retired&&!f.Update().dispatch);
    FeedReload next(f.p,f.x.snapshot.owner,f.x.snapshot.mechanism,f.goal,501);
    f.Ack(*sent.dispatch);r=next.Update(f.x);
    CHECK(r.status==Status::AcknowledgementMismatch&&r.acceptedOperations==0);return 0;
}
int InvalidLifetimeCannotStart(){
    Fixture f;FeedReload invalid(f.p,f.x.snapshot.owner,f.x.snapshot.mechanism,f.goal,0);
    CHECK(invalid.Update(f.x).status==Status::InvalidLifetime);
    f.p.family=FeedFamily::Magazine;f.Start();CHECK(f.Neutral().status==Status::InvalidLifetime);return 0;
}
}
int main(){
    if(CompleteReplacement()||PartialAndEmptyProfiles()||OptionalPartsDoNotInventOperations()||ReceiptIsConsumedAcrossPackets()||
       SamePacketCannotDispatchOrRearm()||CompletionMustMatchExactCandidate()||AckMustEchoExactRequest()||
       AppliedNeedsFreshObservedEffect()||SnapshotWithoutAckDoesNotAdvance()||AckEvidenceCannotBeOldFutureOrImplicit()||
       CiteLaterSnapshotAndRejectContradictoryAck()||TrackingAndStowRetainPendingAndApplied()||TrackingLossWithBadAckStillSuspends()||
       WrongOwnerDoesNotRetargetAcceptedEffect()||UnknownAndWrongResourceCannotDispatch()||PerOperationBindingIsIndependent()||
       SnapshotIdentityAndImmutability()||GapAndClockRollbackRequireNewIntent()||RejectionNeedsEvidenceAndFreshNeutral()||
       CompletionNeedsExplicitFinalEvidence()||RetirementRequiresOldLifetimeReconciliation()||InvalidLifetimeCannotStart())return 1;
    std::puts("FeedReloadTests: 22 deterministic synthetic coordinator cases passed (no native/headset claim)");return 0;
}
