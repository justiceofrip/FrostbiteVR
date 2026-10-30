#include "fvr/interaction/FeedReloadSession.h"
#include "Test.h"
#include <memory>
#include <type_traits>
using namespace fvr::interaction;
namespace {
using Op=FeedPhysicalOperation;using Gesture=FeedInteractionStatus;using Status=FeedReloadStatus;
using Matrix=fvr::math::Matrix4;
constexpr Op order[]={Op::ReleaseLatch,Op::OpenCover,Op::WithdrawFeed,Op::DetachContainer,
    Op::AttachContainer,Op::SeatFeed,Op::CloseCover,Op::EngageLatch,Op::CycleAction};
Matrix Identity(){Matrix m{};for(unsigned i=0;i<4;++i)m.values[i][i]=1;return m;}
Matrix Turn(float angle){auto m=Identity();const auto c=std::cos(angle),s=std::sin(angle);
    m.values[0][0]=m.values[1][1]=c;m.values[0][1]=s;m.values[1][0]=-s;return m;}
Matrix Product(const Matrix& a,const Matrix& b){Matrix m{};for(unsigned i=0;i<4;++i)for(unsigned j=0;j<4;++j)
    for(unsigned k=0;k<4;++k)m.values[i][j]+=a.values[i][k]*b.values[k][j];return m;}
struct Fixture {
    FeedMechanismProfile profile{};FeedInteractionProfile geometry{};FeedReloadSessionSample x{};
    FeedSupplyKey old{{10,1},{20,1}},goal{{30,2},{40,2}};
    std::unique_ptr<FeedReloadSession> session{};FeedReloadSessionResult result{};
    std::uint64_t lifetime=500;
    explicit Fixture(Op first=Op::ReleaseLatch){
        profile.family=FeedFamily::Belt;profile.id=1;profile.revision=2;profile.ammunitionKind=3;profile.maxAgeNs=100;
        profile.cover=profile.latch=profile.container=profile.charge=true;
        profile.containerNeedsOpen=profile.feedNeedsOpen=profile.detachNeedsUnseated=true;
        auto& s=x.snapshot;s.owner={1,2,3,4,5};s.mechanism={6,7};s.profileId=profile.id;s.profileRevision=profile.revision;
        x.nowNs=s.observedNs=1000;s.sequence=10;s.inputSequence=20;s.reconciled=true;
        s.cover=FeedCoverState::Closed;s.latch=FeedLatchState::Engaged;s.container=FeedContainerState::Attached;
        s.feed=FeedSeatState::Seated;s.cycle=FeedCycleState::Required;s.attached=s.seated=old;
        s.supply={goal,profile.ammunitionKind,FeedSupplyState::Usable};
        for(unsigned i=0;i<profile.contacts.size();++i){profile.contacts[i]={100+i,.025f,.1f};s.contacts[i]={100+i,Identity()};}
        s.contacts[static_cast<unsigned>(FeedPart::Cover)].weaponFromContact.values[3][0]=.2f;
        geometry.profileId=profile.id;geometry.profileRevision=profile.revision;
        for(unsigned i=1;i<geometry.operations.size();++i){
            auto& m=geometry.operations[i];m.operation=static_cast<Op>(i);m.kind=FeedMotionKind::Linear;m.measured=true;
            m.weaponFromMotion=m.motionFromContact=Identity();m.travel=.1f;
            m.completeFraction=.9f;m.releaseFraction=.8f;m.pathToleranceMeters=.005f;
            m.orientationToleranceRadians=.1f;m.maxStepMeters=.04f;m.maxStepRadians=.25f;
            m.endpointDwellNs=20;m.maximumGestureNs=1000;m.maxInputGapNs=40;
            if(m.operation==Op::OpenCover||m.operation==Op::CloseCover){
                m.kind=FeedMotionKind::Hinge;m.motionFromContact.values[3][0]=.2f;
                m.travel=m.operation==Op::OpenCover?1.f:-1.f;m.maxStepMeters=.06f;
            }
            if(m.operation==Op::CycleAction)m.returnToStart=true;
        }
        for(const auto op:order){if(op==first)break;Observe(op);}
        auto& b=x.binding;b.key={80,1};b.owner=s.owner;b.mechanism=s.mechanism;
        b.profileId=profile.id;b.profileRevision=profile.revision;b.verified=true;b.operations.fill(true);
        x.held=true;x.interaction.hand={90,1};x.interaction.trackingGeneration=1;x.interaction.focused=true;
        // operationPermitted is deliberately left false: the session derives
        // it from retained native state and exact current capability evidence.
        x.interaction.contact.tracked=x.interaction.contact.handAvailable=true;
        Start();
    }
    void Start(){session=std::make_unique<FeedReloadSession>(profile,geometry,x.snapshot.owner,x.snapshot.mechanism,goal,lifetime);}
    Op Next()const {const auto q=EvaluateFeedMechanism(profile,x.snapshot,goal,x.nowNs);
        return q.candidate?q.candidate->operation:Op::CycleAction;}
    Matrix Pose(Op op,float progress)const {
        const auto& m=geometry.operations[static_cast<unsigned>(op)];auto local=m.motionFromContact;
        if(m.kind==FeedMotionKind::FreePlacement){auto object=m.placement.weaponFromSource;
            for(unsigned i=0;i<3;++i)object.values[3][i]+=
                progress*(m.placement.weaponFromDestination.values[3][i]-object.values[3][i]);
            return Product(m.placement.sourceFromGrip,object);
        }
        if(m.kind==FeedMotionKind::Linear)local.values[3][0]+=progress*m.travel;
        else local=Product(local,Turn(progress*m.travel));
        return Product(local,m.weaponFromMotion);
    }
    void Prepare(float progress=0,bool grip=true,std::int64_t advance=10){
        x.nowNs+=advance;auto& s=x.snapshot;++s.sequence;++s.inputSequence;s.observedNs=x.nowNs;
        auto& c=x.interaction.contact;c.owner=s.owner;c.mechanism=s.mechanism;c.snapshotSequence=s.sequence;
        c.inputSequence=s.inputSequence;c.observedNs=x.nowNs;c.weaponFromHandContact=Pose(Next(),progress);
        x.interaction.grip=grip;
    }
    FeedReloadSessionResult Run(){result=session->Update(x);return result;}
    FeedReloadSessionResult Step(float progress=0,bool grip=true,std::int64_t advance=10){Prepare(progress,grip,advance);return Run();}
    bool Acquire(){return Step(0,false).reload.status==Status::Armed&&Step().interaction.status==Gesture::Acquired;}
    FeedReloadSessionResult Outbound(){for(float p:{.2f,.4f,.6f,.8f,.95f,.95f,.95f})Step(p);return result;}
    FeedReloadSessionResult Finish(){
        if(geometry.operations[static_cast<unsigned>(Next())].kind==FeedMotionKind::FreePlacement){
            for(float p:{.2f,.4f,.6f,.8f,1.f,1.f,1.f})Step(p);return Step(1,false);
        }
        auto r=Outbound();if(Next()==Op::CycleAction)
        for(float p:{.7f,.5f,.3f,.05f,.05f,.05f})r=Step(p);return r;}
    // Test adapter evidence, injected explicitly only after physical dispatch.
    // The session has no reference through which it could mutate native state.
    void Observe(Op op){auto& s=x.snapshot;switch(op){
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
    }}
    FeedReloadSessionResult Native(const FeedReloadRequest& request,bool effect=true,
        FeedReloadAcknowledgement status=FeedReloadAcknowledgement::Applied,bool neutral=false){
        if(effect)Observe(request.completion.candidate.operation);
        Prepare(0,!neutral);x.acknowledgement=FeedReloadAck{request,status,x.snapshot.sequence,x.nowNs};
        const auto r=Run();x.acknowledgement.reset();return r;
    }
};
int CompleteNineMeasuredOperations(){
    Fixture f;unsigned count=0;
    for(const auto op:order){
        CHECK(f.Next()==op&&f.Acquire());const auto before=f.x.snapshot;const auto sent=f.Finish();
        CHECK(sent.interaction.status==Gesture::Completed&&sent.interaction.completion&&sent.reload.dispatch);
        const auto request=*sent.reload.dispatch;const auto& c=request.completion.candidate;
        CHECK(request.id==count+1&&request.lifetime==500&&request.binding==f.x.binding.key);
        CHECK(c.operation==op&&c.goal==f.goal&&c.owner==before.owner&&c.mechanism==before.mechanism);
        CHECK(c.supply==((op==Op::WithdrawFeed||op==Op::DetachContainer)?f.old:
            (op==Op::AttachContainer||op==Op::SeatFeed)?f.goal:FeedSupplyKey{}));
        CHECK(c.snapshotSequence==f.x.snapshot.sequence&&request.completion.inputSequence==f.x.snapshot.inputSequence);
        CHECK(f.x.snapshot.cover==before.cover&&f.x.snapshot.latch==before.latch&&f.x.snapshot.container==before.container&&
            f.x.snapshot.feed==before.feed&&f.x.snapshot.cycle==before.cycle&&f.x.snapshot.attached==before.attached&&
            f.x.snapshot.seated==before.seated); // Physical motion is not native effect.
        CHECK(f.Run().reload.pending==request&&!f.result.reload.dispatch&&!f.result.recognitionEnabled);
        const auto applied=f.Native(request);++count;
        CHECK(applied.reload.acceptedOperations==count&&applied.reload.lastApplied==request&&!applied.reload.pending);
        CHECK(!applied.recognitionEnabled&&!applied.interaction.completion&&!applied.reload.dispatch);
        CHECK(applied.reload.completed==(op==Op::CycleAction));
    }
    CHECK(f.session->State().phase==FeedReloadPhase::Complete);
    for(unsigned i=0;i<3;++i){const auto r=f.Step(0,false);CHECK(r.reload.completed&&!r.recognitionEnabled&&!r.reload.dispatch);}
    // Even a later native change cannot reopen a completed transaction.
    f.x.snapshot.cycle=FeedCycleState::Required;CHECK(!f.Step().recognitionEnabled&&f.result.reload.completed);return 0;
}
int EndpointNeedsFreshRawSamplesAndDispatchIsOnce(){
    Fixture f;CHECK(f.Acquire());for(float p:{.2f,.4f,.6f,.8f,.95f})CHECK(!f.Step(p).reload.dispatch);
    f.x.nowNs+=20;CHECK(f.Run().interaction.status==Gesture::Duplicate&&!f.result.reload.dispatch);
    const auto sent=f.Step(.95f,true,1);CHECK(sent.reload.dispatch);const auto request=*sent.reload.dispatch;
    for(unsigned i=0;i<4;++i){CHECK(!f.Run().reload.dispatch&&f.result.reload.pending==request);}
    for(float p:{0.f,.2f,.4f,.8f,1.f}){const auto r=f.Step(p);CHECK(!r.recognitionEnabled&&!r.interaction.completion&&!r.reload.dispatch);}
    CHECK(f.session->State().acceptedOperations==0);return 0;
}
int SameFrameResolutionCannotRearmOrComplete(){
    for(const bool neutral:{false,true}){
        Fixture f;CHECK(f.Acquire());const auto sent=f.Finish();CHECK(sent.reload.dispatch);
        const auto resolved=f.Native(*sent.reload.dispatch,true,FeedReloadAcknowledgement::Applied,neutral);
        CHECK(resolved.reload.status==Status::Reconciled&&!resolved.recognitionEnabled&&!resolved.interaction.completion);
        CHECK(f.Run().reload.status==Status::DuplicatePacket&&!f.result.reload.dispatch);
        CHECK(!f.Step().reload.dispatch&&f.result.interaction.status==Gesture::NeedNeutral);
        CHECK(f.Acquire());const auto next=f.Finish();CHECK(next.reload.dispatch);
        CHECK(next.reload.dispatch->completion.id==sent.reload.dispatch->completion.id+1);
        CHECK(next.reload.dispatch->completion.candidate.operation==Op::OpenCover);
    }return 0;
}
int ExactCapabilityRequiredForEachOperation(){
    for(const auto op:order){Fixture f(op);f.x.binding.operations[static_cast<unsigned>(op)]=false;
        CHECK(!f.Step(0,false).recognitionEnabled&&!f.result.reload.dispatch);
        // No validated neutral is forwarded through a disabled capability;
        // the initially suspended coordinator therefore remains unarmed.
        CHECK(f.result.reload.status==Status::NeutralRequired&&!f.result.reload.pending);
        f.x.binding.operations[static_cast<unsigned>(op)]=true;CHECK(f.Acquire());CHECK(f.Finish().reload.dispatch);
    }return 0;
}
int BindingChangeCannotInheritGesture(){
    for(unsigned kind=0;kind<13;++kind){
        Fixture f;CHECK(f.Acquire());f.Step(.3f);const auto original=f.x.binding;
        switch(kind){case 0:++f.x.binding.key.id;break;case 1:++f.x.binding.key.generation;break;
        case 2:++f.x.binding.owner.actor;break;case 3:++f.x.binding.owner.actorGeneration;break;
        case 4:++f.x.binding.owner.weapon;break;case 5:++f.x.binding.owner.equipGeneration;break;
        case 6:++f.x.binding.owner.space;break;case 7:++f.x.binding.mechanism.id;break;
        case 8:++f.x.binding.mechanism.generation;break;case 9:++f.x.binding.profileId;break;
        case 10:++f.x.binding.profileRevision;break;case 11:f.x.binding.verified=false;break;
        default:f.x.binding.operations[static_cast<unsigned>(Op::ReleaseLatch)]=false;break;}
        CHECK(!f.Step(.5f).recognitionEnabled&&!f.result.interaction.completion&&!f.result.reload.dispatch);
        f.x.binding=original;CHECK(!f.Run().reload.dispatch); // Restoring the same packet is not a new gesture.
        CHECK(f.Step().interaction.status==Gesture::NeedNeutral);CHECK(f.Acquire());CHECK(f.Finish().reload.dispatch);
    }return 0;
}
int BindingCannotChangeBetweenNeutralAndPressOrAfterDispatch(){
    Fixture f;CHECK(f.Step(0,false).reload.status==Status::Armed);++f.x.binding.key.generation;
    CHECK(!f.Step().recognitionEnabled&&!f.result.reload.dispatch);
    CHECK(f.Step().interaction.status==Gesture::NeedNeutral);CHECK(f.Acquire());
    const auto sent=f.Finish();CHECK(sent.reload.dispatch);const auto key=sent.reload.dispatch->binding;
    CHECK(!f.Native(*sent.reload.dispatch).reload.pending);++f.x.binding.key.generation;
    CHECK(!f.Step(0,false).recognitionEnabled&&f.result.reload.status==Status::UnverifiedBinding);
    f.x.binding.key=key;CHECK(f.Acquire());CHECK(f.Finish().reload.dispatch);return 0;
}
int TrackingAndStowRetainAcceptedEffectPending(){
    for(const bool stow:{false,true}){
        Fixture f;CHECK(f.Acquire());const auto sent=f.Finish();CHECK(sent.reload.dispatch);const auto request=*sent.reload.dispatch;
        if(stow)f.x.held=false;else f.x.interaction.contact.tracked=false;
        const auto waiting=f.Native(request,false);CHECK(waiting.reload.phase==FeedReloadPhase::AwaitingSnapshot);
        CHECK(waiting.reload.pending==request&&waiting.reload.lastApplied==request&&waiting.reload.acceptedOperations==1);
        CHECK(waiting.reload.suspended&&!waiting.recognitionEnabled&&!waiting.reload.dispatch);
        f.x.nowNs+=10000;CHECK(f.Run().reload.pending==request&&f.result.reload.acceptedOperations==1);
        f.Observe(Op::ReleaseLatch);const auto effect=f.Step(0,false);
        CHECK(effect.reload.status==Status::Reconciled&&!effect.reload.pending&&effect.reload.suspended);
        CHECK(effect.reload.acceptedOperations==1&&!effect.recognitionEnabled);
        f.x.held=f.x.interaction.contact.tracked=true;
        CHECK(!f.Step().reload.dispatch&&f.result.interaction.status==Gesture::NeedNeutral);
        CHECK(f.Acquire());CHECK(f.Finish().reload.dispatch&&f.result.reload.acceptedOperations==1);
    }return 0;
}
int TrackingFocusHandAndStowInterruptMeasuredMotion(){
    for(unsigned kind=0;kind<6;++kind){Fixture f;CHECK(f.Acquire());f.Step(.3f);
        switch(kind){case 0:f.x.interaction.contact.tracked=false;break;case 1:f.x.held=false;break;
        case 2:f.x.interaction.focused=false;break;case 3:f.x.interaction.contact.handAvailable=false;break;
        case 4:f.x.interaction.cancel=true;break;default:++f.x.interaction.trackingGeneration;break;}
        CHECK(!f.Step(.5f).reload.dispatch&&!f.result.interaction.completion);
        f.x.interaction.contact.tracked=f.x.held=f.x.interaction.focused=f.x.interaction.contact.handAvailable=true;
        f.x.interaction.cancel=false;CHECK(!f.Step().reload.dispatch&&f.result.interaction.status==Gesture::NeedNeutral);
        CHECK(f.Acquire());CHECK(f.Finish().reload.dispatch);
    }return 0;
}
int ExactAcknowledgementAndSeparatePostStateRequired(){
    Fixture f;CHECK(f.Acquire());const auto sent=f.Finish();CHECK(sent.reload.dispatch);const auto request=*sent.reload.dispatch;
    f.Prepare();f.x.acknowledgement=FeedReloadAck{request,FeedReloadAcknowledgement::Applied,f.x.snapshot.sequence,f.x.nowNs};
    ++f.x.acknowledgement->request.binding.generation;
    CHECK(f.Run().reload.status==Status::AcknowledgementMismatch&&f.result.reload.acceptedOperations==0);
    f.x.acknowledgement->request=request;
    CHECK(f.Run().reload.phase==FeedReloadPhase::AwaitingSnapshot&&f.result.reload.acceptedOperations==1);
    CHECK(!f.result.recognitionEnabled&&!f.result.reload.dispatch&&f.result.reload.pending==request);
    f.x.acknowledgement.reset();f.Observe(Op::ReleaseLatch);f.x.snapshot.reconciled=false;
    CHECK(f.Step().reload.pending==request);f.x.snapshot.reconciled=true;
    CHECK(f.Step().reload.status==Status::Reconciled&&!f.result.reload.pending&&f.result.reload.acceptedOperations==1);
    return 0;
}
int PostStateWithoutReceiptDoesNotReleasePending(){
    Fixture f;CHECK(f.Acquire());const auto sent=f.Finish();CHECK(sent.reload.dispatch);const auto request=*sent.reload.dispatch;
    f.Observe(Op::ReleaseLatch);CHECK(f.Step(0,false).reload.phase==FeedReloadPhase::AwaitingAcknowledgement);
    CHECK(!f.result.recognitionEnabled&&f.result.reload.acceptedOperations==0&&!f.result.reload.completed);
    CHECK(f.Native(request).reload.status==Status::Reconciled);return 0;
}
int RejectedOperationNeedsNewPhysicalGesture(){
    Fixture f;CHECK(f.Acquire());const auto sent=f.Finish();CHECK(sent.reload.dispatch);
    const auto rejected=f.Native(*sent.reload.dispatch,false,FeedReloadAcknowledgement::Rejected,true);
    CHECK(rejected.reload.status==Status::Reconciled&&rejected.reload.acceptedOperations==0&&!rejected.reload.pending);
    CHECK(!rejected.recognitionEnabled&&f.Next()==Op::ReleaseLatch);
    CHECK(f.Step().interaction.status==Gesture::NeedNeutral);CHECK(f.Acquire());
    const auto retry=f.Finish();CHECK(retry.reload.dispatch);
    CHECK(retry.reload.dispatch->id==sent.reload.dispatch->id+1&&
        retry.reload.dispatch->completion.id==sent.reload.dispatch->completion.id+1);return 0;
}
int StaleCompletionPacketCannotReappearAfterResolution(){
    Fixture f;CHECK(f.Acquire());const auto sent=f.Finish();CHECK(sent.reload.dispatch);const auto old=f.x;
    CHECK(!f.Native(*sent.reload.dispatch).reload.pending);const auto resolved=f.x;
    f.x=old;f.x.nowNs=resolved.nowNs;const auto stale=f.Run();
    CHECK(!stale.reload.dispatch&&!stale.interaction.completion&&stale.reload.status==Status::SequenceRollback);
    f.x=resolved;CHECK(f.Step().interaction.status==Gesture::NeedNeutral);CHECK(f.Acquire());
    CHECK(f.Finish().reload.dispatch&&f.result.reload.dispatch->completion.id==sent.reload.dispatch->completion.id+1);return 0;
}
int InvalidCurrentEvidenceCannotFinishMotion(){
    for(unsigned kind=0;kind<5;++kind){Fixture f;CHECK(f.Acquire());f.Step(.3f);f.Prepare(.5f);
        switch(kind){case 0:++f.x.interaction.contact.inputSequence;break;
        case 1:f.x.interaction.contact.observedNs=f.x.nowNs-1;break;
        case 2:f.x.interaction.contact.weaponFromHandContact.values[3][1]=.1f;break;
        case 3:f.x.snapshot.reconciled=false;break;default:f.x.nowNs+=101;break;}
        CHECK(!f.Run().reload.dispatch&&!f.result.interaction.completion);
        f.x.snapshot.reconciled=true;
        if(kind==0){
            // The incoherent contact claimed the following input sequence.
            // That rejected raw watermark cannot later be reused as neutral.
            CHECK(f.Step().interaction.status==Gesture::InvalidInput);
            CHECK(!f.result.interaction.completion&&!f.result.reload.dispatch);
        }
        CHECK(f.Step().interaction.status==Gesture::NeedNeutral);
        CHECK(f.Acquire());CHECK(f.Finish().reload.dispatch);
    }return 0;
}
int NewOwnerCannotInheritPendingOrAcceptedWork(){
    Fixture f;CHECK(f.Acquire());const auto sent=f.Finish();CHECK(sent.reload.dispatch);const auto owner=f.x.snapshot.owner;
    ++f.x.snapshot.owner.actorGeneration;f.x.binding.owner=f.x.snapshot.owner;
    const auto foreign=f.Native(*sent.reload.dispatch,false);
    CHECK(foreign.reload.status==Status::LifetimeMismatch&&foreign.reload.pending==sent.reload.dispatch);
    CHECK(foreign.reload.acceptedOperations==1&&!foreign.recognitionEnabled&&!foreign.reload.dispatch);
    FeedReloadSession next(f.profile,f.geometry,f.x.snapshot.owner,f.x.snapshot.mechanism,f.goal,501);
    f.Prepare(0,false);f.x.acknowledgement=FeedReloadAck{*sent.reload.dispatch,FeedReloadAcknowledgement::Applied,
        f.x.snapshot.sequence,f.x.nowNs};
    CHECK(next.Update(f.x).reload.status==Status::AcknowledgementMismatch&&next.State().acceptedOperations==0);
    f.x.acknowledgement.reset();f.Prepare(0,false);CHECK(next.Update(f.x).reload.status==Status::Armed);
    f.x.snapshot.owner=owner;f.x.binding.owner=owner;f.Observe(Op::ReleaseLatch);
    CHECK(f.Step().reload.status==Status::Reconciled&&f.result.reload.acceptedOperations==1&&!f.result.reload.pending);
    CHECK(f.session->Retire(f.x.snapshot,f.x.nowNs).status==Status::RetireNeedsReconciliation);
    f.Prepare();CHECK(f.session->Retire(f.x.snapshot,f.x.nowNs).status==Status::Retired);
    CHECK(f.session->State().lastApplied==sent.reload.dispatch&&f.session->State().acceptedOperations==1);
    CHECK(!f.Step(0,false).recognitionEnabled&&f.result.reload.status==Status::Retired);return 0;
}
int RetirementCannotDiscardPendingTransaction(){
    Fixture f;CHECK(f.Acquire());const auto sent=f.Finish();CHECK(sent.reload.dispatch);f.Prepare();
    CHECK(f.session->Retire(f.x.snapshot,f.x.nowNs).status==Status::RetireNeedsReconciliation);
    CHECK(f.session->State().pending==sent.reload.dispatch);
    CHECK(!f.Native(*sent.reload.dispatch).reload.pending);
    f.Prepare();CHECK(f.session->Retire(f.x.snapshot,f.x.nowNs).status==Status::Retired);
    CHECK(!f.Step().recognitionEnabled&&!f.result.reload.dispatch);return 0;
}
int OptionalMechanismsAndAlreadyReadyDoNotInventNativeWork(){
    Fixture ready;for(const auto op:order)ready.Observe(op);ready.Start();
    CHECK(ready.Step(0,false).reload.mechanismStatus==FeedMechanismStatus::NoPhysicalStep&&!ready.result.reload.completed);
    CHECK(!ready.result.recognitionEnabled&&ready.result.reload.acceptedOperations==0);
    Fixture f;f.profile.cover=f.profile.latch=f.profile.container=f.profile.charge=false;
    f.profile.containerNeedsOpen=f.profile.feedNeedsOpen=f.profile.detachNeedsUnseated=false;
    f.profile.contacts[0]=f.profile.contacts[1]=f.profile.contacts[2]=f.profile.contacts[4]={};
    auto& s=f.x.snapshot;s.cover=FeedCoverState::Absent;s.latch=FeedLatchState::Absent;
    s.container=FeedContainerState::Absent;s.cycle=FeedCycleState::Absent;s.attached={};
    f.old.container={};f.goal.container={};s.seated=f.old;s.supply.key=f.goal;f.Start();
    for(const auto op:{Op::WithdrawFeed,Op::SeatFeed}){CHECK(f.Next()==op&&f.Acquire());const auto sent=f.Finish();
        CHECK(sent.reload.dispatch&&sent.reload.dispatch->completion.candidate.operation==op);
        CHECK(f.Native(*sent.reload.dispatch).reload.completed==(op==Op::SeatFeed));}
    CHECK(f.session->State().acceptedOperations==2);return 0;
}
int UnsupportedAndUnmeasuredStayDisabled(){
    Fixture magazine;magazine.profile.family=FeedFamily::Magazine;magazine.Start();
    CHECK(!magazine.Step(0,false).recognitionEnabled&&magazine.result.reload.status==Status::InvalidLifetime);
    Fixture unmeasured;unmeasured.geometry.operations[1].measured=false;unmeasured.Start();
    CHECK(unmeasured.Step(0,false).interaction.status==Gesture::InvalidProfile&&!unmeasured.result.reload.dispatch);
    Fixture invalid;invalid.lifetime=0;invalid.Start();
    CHECK(!invalid.Step(0,false).recognitionEnabled&&invalid.result.reload.status==Status::InvalidLifetime);return 0;
}
void Placement(Fixture& f,Op op){
    auto& motion=f.geometry.operations[static_cast<unsigned>(op)];motion.kind=FeedMotionKind::FreePlacement;
    motion.placement={Identity(),Identity(),Identity()};motion.placement.weaponFromDestination.values[3][0]=.1f;
    f.x.snapshot.contacts[static_cast<unsigned>(feed_mechanism_detail::OperationPart(op))].weaponFromContact=Identity();
}
int SourceRebaseAfterAcknowledgementRetainsLifetimeAndRequiresNeutral(){
    Fixture f(Op::DetachContainer);Placement(f,Op::DetachContainer);Placement(f,Op::AttachContainer);f.Start();
    CHECK(f.Step(0,false).reload.status==Status::Armed);
    auto candidate=*EvaluateFeedMechanism(f.profile,f.x.snapshot,f.goal,f.x.nowNs).candidate;
    auto motion=f.geometry.operations[static_cast<unsigned>(candidate.operation)];
    CHECK(!f.session->UpdateMotion(f.x.snapshot,candidate,motion,f.x.nowNs)); // Armed is not idle.
    CHECK(f.Step().interaction.status==Gesture::Acquired);
    candidate=*EvaluateFeedMechanism(f.profile,f.x.snapshot,f.goal,f.x.nowNs).candidate;
    CHECK(!f.session->UpdateMotion(f.x.snapshot,candidate,motion,f.x.nowNs));
    const auto sent=f.Finish();CHECK(sent.reload.dispatch);candidate=sent.reload.dispatch->completion.candidate;
    CHECK(!f.session->UpdateMotion(f.x.snapshot,candidate,motion,f.x.nowNs));
    CHECK(!f.Native(*sent.reload.dispatch).reload.pending);
    auto& rebased=f.geometry.operations[static_cast<unsigned>(Op::AttachContainer)];
    rebased.placement.weaponFromSource.values[3][1]=.003f;
    f.x.snapshot.contacts[static_cast<unsigned>(FeedPart::Container)].weaponFromContact=rebased.placement.weaponFromSource;
    f.Prepare();candidate=*EvaluateFeedMechanism(f.profile,f.x.snapshot,f.goal,f.x.nowNs).candidate;
    const auto retained=f.session->State();
    CHECK(f.session->UpdateMotion(f.x.snapshot,candidate,rebased,f.x.nowNs));
    CHECK(f.session->State().acceptedOperations==retained.acceptedOperations&&f.session->State().lastApplied==retained.lastApplied);
    CHECK(f.Run().interaction.status==Gesture::NeedNeutral); // Rebase did not consume this packet.
    CHECK(f.Acquire()&&f.result.interaction.target&&Near(f.result.interaction.target->values[3][1],.003f));
    const auto next=f.Finish();CHECK(next.reload.dispatch&&next.reload.dispatch->lifetime==500);
    CHECK(next.reload.dispatch->id==sent.reload.dispatch->id+1&&
        next.reload.dispatch->completion.id==sent.reload.dispatch->completion.id+1);return 0;
}
int SourceRebaseCannotChangeDestinationPolicyOrCurrentEvidence(){
    for(unsigned kind=0;kind<8;++kind){Fixture f(Op::AttachContainer);Placement(f,Op::AttachContainer);f.Start();
        f.Prepare(0,false);auto snapshot=f.x.snapshot;auto candidate=*EvaluateFeedMechanism(f.profile,snapshot,f.goal,f.x.nowNs).candidate;
        auto motion=f.geometry.operations[static_cast<unsigned>(Op::AttachContainer)];auto now=f.x.nowNs;
        switch(kind){case 0:motion.placement.weaponFromDestination.values[3][0]+=.001f;break;
        case 1:motion.maxStepMeters+=.001f;break;case 2:motion.placement.sourceFromGrip.values[3][0]+=.001f;break;
        case 3:++candidate.snapshotSequence;break;case 4:now+=101;break;
        case 5:motion.placement.weaponFromSource={};break;case 6:++snapshot.owner.actorGeneration;break;
        default:motion.placement.weaponFromSource.values[3][1]+=.02f;break;}
        CHECK(!f.session->UpdateMotion(snapshot,candidate,motion,now));
        CHECK(f.Acquire()&&f.Finish().reload.dispatch);
    }return 0;
}
}
int main(){
    static_assert(!std::is_copy_constructible_v<FeedReloadSession> && !std::is_move_constructible_v<FeedReloadSession>);
    if(CompleteNineMeasuredOperations()||EndpointNeedsFreshRawSamplesAndDispatchIsOnce()||SameFrameResolutionCannotRearmOrComplete()||
       ExactCapabilityRequiredForEachOperation()||BindingChangeCannotInheritGesture()||
       BindingCannotChangeBetweenNeutralAndPressOrAfterDispatch()||TrackingAndStowRetainAcceptedEffectPending()||
       TrackingFocusHandAndStowInterruptMeasuredMotion()||ExactAcknowledgementAndSeparatePostStateRequired()||
       PostStateWithoutReceiptDoesNotReleasePending()||RejectedOperationNeedsNewPhysicalGesture()||
       StaleCompletionPacketCannotReappearAfterResolution()||InvalidCurrentEvidenceCannotFinishMotion()||
       NewOwnerCannotInheritPendingOrAcceptedWork()||RetirementCannotDiscardPendingTransaction()||
       OptionalMechanismsAndAlreadyReadyDoNotInventNativeWork()||UnsupportedAndUnmeasuredStayDisabled()||
       SourceRebaseAfterAcknowledgementRetainsLifetimeAndRequiresNeutral()||
       SourceRebaseCannotChangeDestinationPolicyOrCurrentEvidence())return 1;
    std::puts("FeedReloadSessionTests: 19 deterministic measured-gesture/native-evidence composition cases passed (no native/headset claim)");
    return 0;
}
