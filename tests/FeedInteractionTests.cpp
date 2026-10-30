#include "fvr/interaction/FeedInteraction.h"
#include "Test.h"
#include <limits>
using namespace fvr::interaction;
namespace {
using Op=FeedPhysicalOperation;using Status=FeedInteractionStatus;
using Matrix=fvr::math::Matrix4;
Matrix Identity(){Matrix m{};for(unsigned i=0;i<4;++i)m.values[i][i]=1;return m;}
Matrix Turn(float angle){auto m=Identity();const auto c=std::cos(angle),s=std::sin(angle);
    m.values[0][0]=m.values[1][1]=c;m.values[0][1]=s;m.values[1][0]=-s;return m;}
Matrix Product(const Matrix& a,const Matrix& b){Matrix m{};for(unsigned i=0;i<4;++i)for(unsigned j=0;j<4;++j)
    for(unsigned k=0;k<4;++k)m.values[i][j]+=a.values[i][k]*b.values[k][j];return m;}
struct Fixture {
    FeedMechanismProfile p{};FeedInteractionProfile geometry{};FeedMechanismSnapshot s{};
    FeedSupplyKey old{{10,1},{20,1}},desired{{30,2},{40,2}};
    FeedInteraction controller{};FeedInteractionSample sample{};FeedInteractionResult result{};
    Op operation=Op::ReleaseLatch;std::int64_t now=1000;
    explicit Fixture(Op op=Op::ReleaseLatch){
        p.family=FeedFamily::Belt;p.id=1;p.revision=2;p.ammunitionKind=3;p.maxAgeNs=100;
        p.cover=p.latch=p.container=p.charge=true;
        p.containerNeedsOpen=p.feedNeedsOpen=p.detachNeedsUnseated=true;
        s.owner={1,2,3,4,5};s.mechanism={6,7};s.profileId=p.id;s.profileRevision=p.revision;
        s.sequence=10;s.inputSequence=20;s.observedNs=now;s.reconciled=true;
        s.cover=FeedCoverState::Closed;s.latch=FeedLatchState::Engaged;
        s.container=FeedContainerState::Attached;s.feed=FeedSeatState::Seated;s.cycle=FeedCycleState::Required;
        s.attached=s.seated=old;s.supply={desired,p.ammunitionKind,FeedSupplyState::Usable};
        for(unsigned i=0;i<p.contacts.size();++i){p.contacts[i]={100+i,.025f,.1f};s.contacts[i]={100+i,Identity()};}
        geometry.profileId=p.id;geometry.profileRevision=p.revision;
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
        constexpr Op order[]={Op::ReleaseLatch,Op::OpenCover,Op::WithdrawFeed,Op::DetachContainer,Op::AttachContainer,
            Op::SeatFeed,Op::CloseCover,Op::EngageLatch,Op::CycleAction};
        for(const auto previous:order){if(previous==op)break;Observe(previous);}
        operation=op;SetContact();sample.hand={90,1};sample.trackingGeneration=1;sample.focused=sample.operationPermitted=true;
        Prepare(0,false);
    }
    FeedMotionProfile& Motion(){return geometry.operations[static_cast<unsigned>(operation)];}
    unsigned Part()const{return static_cast<unsigned>(feed_mechanism_detail::OperationPart(operation));}
    // Explicit synthetic adapter observations. The controller never writes them.
    void Observe(Op op){switch(op){
        case Op::ReleaseLatch:s.latch=FeedLatchState::Released;break;
        case Op::OpenCover:s.cover=FeedCoverState::Open;break;
        case Op::WithdrawFeed:s.feed=FeedSeatState::Unseated;s.seated={};break;
        case Op::DetachContainer:s.container=FeedContainerState::Detached;s.attached={};break;
        case Op::AttachContainer:s.container=FeedContainerState::Attached;s.attached=desired;break;
        case Op::SeatFeed:s.feed=FeedSeatState::Seated;s.seated=desired;break;
        case Op::CloseCover:s.cover=FeedCoverState::Closed;break;
        case Op::EngageLatch:s.latch=FeedLatchState::Engaged;break;
        case Op::CycleAction:s.cycle=FeedCycleState::NativeReady;break;
        default:break;
    }}
    Matrix Pose(float progress){auto local=Motion().motionFromContact;
        if(Motion().kind==FeedMotionKind::Linear)local.values[3][0]+=progress*Motion().travel;
        else local=Product(local,Turn(progress*Motion().travel));
        return Product(local,Motion().weaponFromMotion);
    }
    void SetContact(){s.contacts[Part()].weaponFromContact=Pose(0);}
    void Prepare(float progress,bool grip=true,std::int64_t advance=10){
        now+=advance;++s.sequence;++s.inputSequence;s.observedNs=now;
        sample.contact={s.owner,s.mechanism,s.sequence,s.inputSequence,now,true,true,Pose(progress)};sample.grip=grip;
    }
    FeedInteractionResult Run(){result=controller.Update(p,geometry,s,desired,sample,now);return result;}
    FeedInteractionResult Step(float progress,bool grip=true,std::int64_t advance=10){Prepare(progress,grip,advance);return Run();}
    bool Acquire(){return Step(0,false).status==Status::Armed&&Step(0).status==Status::Acquired;}
    FeedInteractionResult Outbound(){for(float point:{.2f,.4f,.6f,.8f,.95f})Step(point);
        Step(.95f);return Step(.95f);}
};
int LinearReceiptAndNoStateMutation(){
    Fixture f(Op::SeatFeed);CHECK(f.Acquire());const auto before=f.s;auto r=f.Outbound();
    CHECK(r.status==Status::Completed&&r.completion&&r.completion->id==1);
    CHECK(r.completion->candidate.operation==Op::SeatFeed&&r.completion->candidate.supply==f.desired);
    CHECK(r.completion->candidate.goal==f.desired&&r.completion->candidate.owner==f.s.owner);
    CHECK(r.completion->candidate.mechanism==f.s.mechanism&&r.completion->candidate.snapshotSequence==f.s.sequence);
    CHECK(r.completion->inputSequence==f.sample.contact.inputSequence&&r.completion->completedNs==f.sample.contact.observedNs);
    CHECK(f.s.feed==before.feed&&f.s.seated==before.seated&&f.s.attached==before.attached&&f.s.cycle==before.cycle);
    CHECK(f.s.feed==FeedSeatState::Unseated&&f.s.cycle==FeedCycleState::Required);
    CHECK(EvaluateFeedMechanism(f.p,f.s,f.desired,f.now).candidate->operation==Op::SeatFeed);
    CHECK(!f.Run().completion&&f.result.status==Status::Duplicate);CHECK(!f.Step(.95f).completion);return 0;
}
int EveryOperationKeepsItsTypeAndResource(){
    for(unsigned i=1;i<=9;++i){Fixture f(static_cast<Op>(i));CHECK(f.Acquire());auto r=f.Outbound();
        if(f.operation==Op::CycleAction){CHECK(r.status==Status::Returning&&!r.completion);
            for(float x:{.7f,.5f,.3f,.05f,.05f,.05f})r=f.Step(x);}
        CHECK(r.completion&&r.completion->candidate.operation==static_cast<Op>(i));
        const auto resource=(i==3||i==4)?f.old:(i==5||i==6)?f.desired:FeedSupplyKey{};
        CHECK(r.completion->candidate.supply==resource&&r.completion->candidate.goal==f.desired);
    }return 0;
}
int RotatedTranslatedMetreFrame(){
    Fixture f(Op::OpenCover);f.Motion().weaponFromMotion=Turn(.7f);
    f.Motion().weaponFromMotion.values[3]={.35f,-.25f,.4f,1};f.SetContact();CHECK(f.Acquire());
    CHECK(f.Outbound().completion);const auto target=*f.result.target;
    CHECK(Near(target.values[3][0],.35f+.2f*std::cos(1.65f)));
    CHECK(Near(target.values[3][1],-.25f+.2f*std::sin(1.65f))&&Near(target.values[3][2],.4f));return 0;
}
int LinearRotatedAxisAndNegativeTravel(){
    Fixture f;f.Motion().weaponFromMotion=Turn(1.57079632679f);f.Motion().weaponFromMotion.values[3][2]=.4f;
    f.Motion().travel=-.1f;f.SetContact();CHECK(f.Acquire());CHECK(f.Outbound().completion);
    CHECK(Near(f.result.target->values[3][0],0)&&Near(f.result.target->values[3][1],-.095f));return 0;
}
int InitialHeldAndMissedPressNeedNeutral(){
    Fixture f;CHECK(f.Step(0).status==Status::NeedNeutral);CHECK(f.Step(.2f).status==Status::NeedNeutral);
    CHECK(f.Step(0,false).status==Status::Armed);CHECK(f.Step(.5f).status==Status::ContactMiss);
    CHECK(f.Step(0).status==Status::NeedNeutral);CHECK(f.Acquire());return 0;
}
int FreshNeutralAndNewPressForEveryReceipt(){
    Fixture f;CHECK(f.Acquire());CHECK(f.Outbound().completion);const auto first=*f.result.completion;
    CHECK(!f.Step(0).completion&&f.result.status==Status::NeedNeutral);
    f.sample.grip=false;CHECK(f.Run().status==Status::InvalidInput); // Mutating duplicate packet is no neutral.
    CHECK(f.Step(0).status==Status::NeedNeutral);CHECK(f.Acquire());CHECK(f.Outbound().completion);
    CHECK(f.result.completion->id==first.id+1);return 0;
}
int HysteresisPreservesDwellWithinBand(){
    Fixture f;CHECK(f.Acquire());for(float x:{.2f,.4f,.6f,.8f})CHECK(!f.Step(x).completion);
    CHECK(f.Step(.95f).status==Status::Endpoint);CHECK(!f.Step(.85f).completion);
    CHECK(f.Step(.86f).completion);return 0;
}
int HysteresisResetsBelowRelease(){
    Fixture f;CHECK(f.Acquire());for(float x:{.2f,.4f,.6f,.8f,.95f})f.Step(x);
    CHECK(f.Step(.79f).status==Status::Moving);CHECK(!f.Step(.85f).completion);
    CHECK(f.Step(.95f).status==Status::Endpoint);CHECK(!f.Step(.95f).completion);CHECK(f.Step(.95f).completion);return 0;
}
int DuplicateCannotAccrueDwell(){
    Fixture f;CHECK(f.Acquire());for(float x:{.2f,.4f,.6f,.8f,.95f})f.Step(x);
    f.now+=20;CHECK(f.Run().status==Status::Duplicate&&!f.result.completion);
    CHECK(f.Step(.95f,true,1).completion);return 0; // Only the NEW endpoint observation finishes dwell.
}
int ReturnStrokeRequiresRearAndFreshFrontDwell(){
    Fixture f(Op::CycleAction);CHECK(f.Acquire());CHECK(f.Outbound().status==Status::Returning);
    for(float x:{.7f,.5f,.3f})CHECK(!f.Step(x).completion);
    CHECK(f.Step(.05f).status==Status::Endpoint);CHECK(!f.Step(.15f).completion);
    CHECK(f.Step(.14f).completion);CHECK(f.s.cycle==FeedCycleState::Required);return 0;
}
int TrackingLossCannotFinishOrResumeHeld(){
    Fixture f;CHECK(f.Acquire());f.Step(.3f);f.Prepare(.6f);f.sample.contact.tracked=false;
    CHECK(!f.Run().completion&&!f.result.target);f.sample.contact.tracked=true;
    CHECK(f.Run().status==Status::InvalidInput);CHECK(f.Step(.6f).status==Status::NeedNeutral);
    CHECK(f.Acquire());CHECK(f.Outbound().completion);return 0;
}
int InputGapCannotBridgeGesture(){
    Fixture f;CHECK(f.Acquire());f.Step(.3f);CHECK(f.Step(.6f,true,41).status==Status::Interrupted);
    CHECK(f.Step(.95f).status==Status::NeedNeutral);CHECK(f.Acquire());return 0;
}
int ReleaseFocusCancelAndExplicitInterrupt(){
    for(unsigned i=0;i<4;++i){Fixture f;CHECK(f.Acquire());f.Step(.3f);f.Prepare(.5f);
        if(i==0)f.sample.grip=false;if(i==1)f.sample.focused=false;if(i==2)f.sample.cancel=true;if(i==3)f.controller.Interrupt();
        CHECK(!f.Run().completion&&!f.result.target);f.sample.focused=true;f.sample.cancel=false;
        CHECK(f.Step(0).status==Status::NeedNeutral);CHECK(f.Acquire());
    }return 0;
}
int LifetimeAndResourcesCannotInheritGrasp(){
    for(unsigned i=0;i<15;++i){Fixture f(Op::SeatFeed);CHECK(f.Acquire());f.Step(.3f);
        switch(i){case 0:++f.s.owner.actor;break;case 1:++f.s.owner.actorGeneration;break;case 2:++f.s.owner.weapon;break;
        case 3:++f.s.owner.equipGeneration;break;case 4:++f.s.owner.space;break;case 5:++f.s.mechanism.id;break;
        case 6:++f.s.mechanism.generation;break;case 7:++f.p.id;f.s.profileId=f.geometry.profileId=f.p.id;break;
        case 8:++f.p.revision;f.s.profileRevision=f.geometry.profileRevision=f.p.revision;break;
        case 9:++f.desired.resource.generation;f.s.supply.key=f.s.attached=f.desired;break;
        case 10:++f.desired.container.generation;f.s.supply.key=f.s.attached=f.desired;break;
        case 11:++f.sample.hand.id;break;case 12:++f.sample.hand.generation;break;
        case 13:++f.sample.trackingGeneration;break;default:f.Motion().maxStepMeters=.035f;break;}
        CHECK(f.Step(.5f).status==Status::CandidateChanged&&!f.result.completion&&!f.result.target);
        CHECK(f.Step(0).status==Status::NeedNeutral);CHECK(f.Acquire());
    }return 0;
}
int OperationChangeDoesNotReuseHeldMotion(){
    Fixture f;CHECK(f.Acquire());f.Step(.3f);f.Observe(Op::ReleaseLatch);f.operation=Op::OpenCover;f.SetContact();
    CHECK(f.Step(.5f).status==Status::CandidateChanged);CHECK(f.Step(0).status==Status::NeedNeutral);
    CHECK(f.Acquire());CHECK(f.Outbound().completion&&f.result.completion->candidate.operation==Op::OpenCover);return 0;
}
int MalformedOrUnknownGeometryFailsClosed(){
    for(unsigned i=0;i<20;++i){Fixture f;auto& m=f.Motion();switch(i){
        case 0:m.measured=false;break;case 1:m.kind=FeedMotionKind::Unknown;break;case 2:m.weaponFromMotion={};break;
        case 3:m.weaponFromMotion.values[0][0]=-1;break;case 4:m.weaponFromMotion.values[0][1]=.1f;break;
        case 5:m.motionFromContact.values[1][1]=2;break;case 6:m.travel=0;break;
        case 7:m.travel=std::numeric_limits<float>::infinity();break;case 8:m.completeFraction=.4f;break;
        case 9:m.releaseFraction=m.completeFraction;break;case 10:m.pathToleranceMeters=.1f;break;
        case 11:m.maxStepMeters=.1f;break;case 12:m.maxStepRadians=4;break;case 13:m.endpointDwellNs=0;break;
        case 14:m.maximumGestureNs=m.endpointDwellNs;break;case 15:m.maxInputGapNs=0;break;
        case 16:m.orientationToleranceRadians=std::numeric_limits<float>::quiet_NaN();break;
        case 17:m.operation=Op::SeatFeed;break;case 18:++f.geometry.profileRevision;break;
        default:f.geometry={};break;}
        CHECK(f.Step(0,false).status==Status::InvalidProfile&&!f.result.completion);
    }
    Fixture hinge(Op::OpenCover);hinge.Motion().motionFromContact.values[3][0]=0;
    CHECK(hinge.Step(0,false).status==Status::InvalidProfile);
    Fixture unknown;unknown.p.family=FeedFamily::Unknown;CHECK(!unknown.Step(0,false).completion);
    FeedInteraction empty;CHECK(!empty.Update({}, {}, {}, {}, {}, 1).completion);return 0;
}
int RawGeometryOffAxisAndJumpRejected(){
    for(unsigned i=0;i<6;++i){Fixture f;CHECK(f.Acquire());f.Prepare(.2f);
        switch(i){case 0:f.sample.contact.weaponFromHandContact.values[0][0]=-1;break;
        case 1:f.sample.contact.weaponFromHandContact.values[3][1]=.006f;break;
        case 2:f.sample.contact.weaponFromHandContact=Product(Turn(.15f),f.sample.contact.weaponFromHandContact);break;
        case 3:f.sample.contact.weaponFromHandContact.values[3][0]=.08f;break;
        case 4:f.sample.contact.weaponFromHandContact.values[3][0]=-.025f;break;
        default:f.sample.contact.weaponFromHandContact.values[3][0]=std::numeric_limits<float>::quiet_NaN();break;}
        CHECK(!f.Run().completion&&!f.result.target);CHECK(f.result.status==Status::InvalidInput||f.result.status==Status::OffPath||f.result.status==Status::MotionJump);
    }return 0;
}
int HingeRequiresArcAndMatchingOrientation(){
    for(unsigned i=0;i<3;++i){Fixture f(Op::OpenCover);CHECK(f.Acquire());f.Prepare(.2f);
        if(i==0){auto fake=f.Pose(0);fake.values[0]=f.sample.contact.weaponFromHandContact.values[0];
            fake.values[1]=f.sample.contact.weaponFromHandContact.values[1];f.sample.contact.weaponFromHandContact=fake;}
        if(i==1){f.sample.contact.weaponFromHandContact.values[0]=Identity().values[0];
            f.sample.contact.weaponFromHandContact.values[1]=Identity().values[1];}
        if(i==2)f.sample.contact.weaponFromHandContact.values[3][2]=.006f;
        CHECK(f.Run().status==Status::OffPath&&!f.result.completion);
    }return 0;
}
int MeasuredStartMustMatchObservedContact(){
    Fixture f;f.s.contacts[f.Part()].weaponFromContact.values[3][0]=.02f;
    CHECK(f.Step(0,false).status==Status::Armed);CHECK(f.Step(0).status==Status::OffPath);return 0;
}
int StaleIncoherentAndUnavailableEvidence(){
    for(unsigned i=0;i<10;++i){Fixture f;CHECK(f.Acquire());f.Prepare(.2f);
        switch(i){case 0:f.now+=101;break;case 1:++f.sample.contact.snapshotSequence;break;
        case 2:++f.sample.contact.inputSequence;break;case 3:++f.sample.contact.owner.space;break;
        case 4:f.sample.contact.handAvailable=false;break;case 5:f.s.reconciled=false;break;
        case 6:f.s.supply.state=FeedSupplyState::Depleted;break;case 7:f.sample.contact.observedNs=f.now+1;break;
        case 8:f.s.contacts[f.Part()].role=999;break;default:--f.sample.contact.observedNs;break;}
        CHECK(f.Run().status==Status::InvalidInput&&!f.result.completion&&!f.result.target);
    }return 0;
}
int ReversedSequenceAndTimeCannotComplete(){
    for(unsigned i=0;i<3;++i){Fixture f;CHECK(f.Acquire());f.Step(.2f);f.Prepare(.4f);
        if(i==0){f.s.inputSequence-=2;f.sample.contact.inputSequence=f.s.inputSequence;}
        if(i==1){f.s.sequence-=2;f.sample.contact.snapshotSequence=f.s.sequence;}
        if(i==2){f.now-=11;f.s.observedNs=f.sample.contact.observedNs=f.now;}
        CHECK(f.Run().status==Status::InvalidInput&&!f.result.completion);
    }return 0;
}
int GestureDeadlineAlsoRejectsDuplicate(){
    Fixture f;f.Motion().maximumGestureNs=50;CHECK(f.Acquire());f.Step(.2f);f.now+=41;
    CHECK(f.Run().status==Status::Expired&&!f.result.completion);return 0;
}
int SnapshotCannotMutateStableProfileDuringMotion(){
    Fixture f;CHECK(f.Acquire());f.p.contacts[f.Part()].radiusMeters=.03f;
    CHECK(f.Step(.2f).status==Status::CandidateChanged);return 0;
}
int PendingExternalWorkSuppressesGestureUntilFreshNeutral(){
    Fixture f;CHECK(f.Acquire());f.Step(.3f);f.sample.operationPermitted=false;
    CHECK(f.Step(.5f).status==Status::Interrupted&&!f.result.completion);
    f.sample.operationPermitted=true;CHECK(f.Run().status==Status::InvalidInput);
    CHECK(f.Step(0).status==Status::NeedNeutral);CHECK(f.Acquire());CHECK(f.Outbound().completion);return 0;
}
int ImmutableInputAndSnapshotSequences(){
    for(unsigned i=0;i<2;++i){Fixture f;CHECK(f.Acquire());
        if(i==0){f.Prepare(.2f);--f.s.sequence;f.sample.contact.snapshotSequence=f.s.sequence;}
        else {++f.s.sequence;f.sample.contact.snapshotSequence=f.s.sequence;}
        CHECK(f.Run().status==Status::InvalidInput&&!f.result.completion);
    }return 0;
}
}
int main(){
    if(LinearReceiptAndNoStateMutation()||EveryOperationKeepsItsTypeAndResource()||RotatedTranslatedMetreFrame()||
       LinearRotatedAxisAndNegativeTravel()||InitialHeldAndMissedPressNeedNeutral()||FreshNeutralAndNewPressForEveryReceipt()||
       HysteresisPreservesDwellWithinBand()||HysteresisResetsBelowRelease()||DuplicateCannotAccrueDwell()||
       ReturnStrokeRequiresRearAndFreshFrontDwell()||TrackingLossCannotFinishOrResumeHeld()||InputGapCannotBridgeGesture()||
       ReleaseFocusCancelAndExplicitInterrupt()||LifetimeAndResourcesCannotInheritGrasp()||OperationChangeDoesNotReuseHeldMotion()||
       MalformedOrUnknownGeometryFailsClosed()||RawGeometryOffAxisAndJumpRejected()||HingeRequiresArcAndMatchingOrientation()||
       MeasuredStartMustMatchObservedContact()||StaleIncoherentAndUnavailableEvidence()||ReversedSequenceAndTimeCannotComplete()||
       GestureDeadlineAlsoRejectsDuplicate()||SnapshotCannotMutateStableProfileDuringMotion()||
       PendingExternalWorkSuppressesGestureUntilFreshNeutral()||ImmutableInputAndSnapshotSequences())return 1;
    std::puts("FeedInteraction: 25 deterministic cases passed (synthetic measured geometry; no native acceptance).");return 0;
}
