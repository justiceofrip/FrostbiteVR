#include "fvr/interaction/FeedMechanism.h"
#include "Test.h"
#include <limits>
using namespace fvr::interaction;
namespace {
using Op=FeedPhysicalOperation;using Status=FeedMechanismStatus;
fvr::math::Matrix4 Identity(){fvr::math::Matrix4 m{};for(unsigned i=0;i<4;++i)m.values[i][i]=1;return m;}
struct Fixture {
    FeedMechanismProfile p{};FeedMechanismSnapshot s{};
    FeedSupplyKey old{{10,1},{20,1}},desired{{30,2},{40,2}};
    std::int64_t now=1000;
    Fixture(){
        p.family=FeedFamily::Belt;p.id=1;p.revision=2;p.ammunitionKind=3;p.maxAgeNs=100;
        p.cover=p.latch=p.container=p.charge=true;
        p.containerNeedsOpen=p.feedNeedsOpen=p.detachNeedsUnseated=true;
        s.owner={1,2,3,4,5};s.mechanism={6,7};s.profileId=p.id;s.profileRevision=p.revision;
        s.sequence=10;s.inputSequence=20;s.observedNs=now;s.reconciled=true;
        s.cover=FeedCoverState::Closed;s.latch=FeedLatchState::Engaged;
        s.container=FeedContainerState::Attached;s.feed=FeedSeatState::Seated;s.cycle=FeedCycleState::Required;
        s.attached=s.seated=old;s.supply={desired,p.ammunitionKind,FeedSupplyState::Usable};
        for(unsigned i=0;i<p.contacts.size();++i){p.contacts[i]={100+i,.025f,.2f};s.contacts[i]={100+i,Identity()};}
    }
    FeedMechanismResult Query()const{return EvaluateFeedMechanism(p,s,desired,now);}
    void Observe(Op op){ // Synthetic observations, NOT policy-generated acknowledgements.
        switch(op){
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
        }
        ++s.sequence;++s.inputSequence;s.observedNs=++now;
    }
    FeedContactSample Hand(const FeedPhysicalCandidate& c)const {
        return {s.owner,s.mechanism,s.sequence,s.inputSequence,now,true,true,s.contacts[static_cast<unsigned>(c.part)].weaponFromContact};
    }
    FeedContactResult Contact(const FeedPhysicalCandidate& c,const FeedContactSample& h)const {
        return EvaluateFeedContact(p,s,desired,c,h,now);
    }
    void FinishObserved(){s.cover=FeedCoverState::Closed;s.latch=FeedLatchState::Engaged;s.attached=s.seated=desired;s.cycle=FeedCycleState::NativeReady;}
};
int SyntheticReplacement(){
    Fixture f;
    constexpr Op order[]={Op::ReleaseLatch,Op::OpenCover,Op::WithdrawFeed,Op::DetachContainer,Op::AttachContainer,Op::SeatFeed,Op::CloseCover,Op::EngageLatch,Op::CycleAction};
    for(const auto op:order){auto r=f.Query();CHECK(r.status==Status::Candidate&&r.candidate&&r.candidate->operation==op);
        const auto again=f.Query();CHECK(again.candidate==r.candidate); // Query never advances observed state.
        CHECK(f.Contact(*r.candidate,f.Hand(*r.candidate)).status==Status::ContactMatch);
        if(op==Op::WithdrawFeed||op==Op::DetachContainer)CHECK(r.candidate->supply==f.old);
        if(op==Op::SeatFeed||op==Op::AttachContainer)CHECK(r.candidate->supply==f.desired);
        f.Observe(op);
    }
    CHECK(f.Query().status==Status::NoPhysicalStep&&!f.Query().candidate);return 0;
}
int PartialContinuation(){
    Fixture f;f.s.cover=FeedCoverState::Open;f.s.latch=FeedLatchState::Released;
    f.s.attached=f.desired;f.s.feed=FeedSeatState::Unseated;f.s.seated={};
    CHECK(f.Query().candidate->operation==Op::SeatFeed);f.Observe(Op::SeatFeed);
    CHECK(f.Query().candidate->operation==Op::CloseCover);return 0;
}
int RetainedFeed(){
    Fixture f;f.s.cover=FeedCoverState::Open;f.s.latch=FeedLatchState::Released;
    f.s.seated=f.desired;f.s.container=FeedContainerState::Detached;f.s.attached={};
    CHECK(f.Query().candidate->operation==Op::AttachContainer); // No needless withdrawal before ATTACH.
    f.Observe(Op::AttachContainer);CHECK(f.Query().candidate->operation==Op::CloseCover);return 0;
}
int NoContainerFamily(){
    Fixture f;f.p.container=false;f.p.containerNeedsOpen=f.p.detachNeedsUnseated=false;f.p.contacts[2]={};
    f.s.container=FeedContainerState::Absent;f.s.attached={};f.old.container={};f.desired.container={};
    f.s.seated=f.old;f.s.supply.key=f.desired;
    f.Observe(Op::ReleaseLatch);f.Observe(Op::OpenCover);
    CHECK(f.Query().candidate->operation==Op::WithdrawFeed);f.Observe(Op::WithdrawFeed);
    CHECK(f.Query().candidate->operation==Op::SeatFeed);return 0;
}
int AccessFreeFamily(){
    Fixture f;f.p.cover=f.p.latch=f.p.charge=false;
    f.p.containerNeedsOpen=f.p.feedNeedsOpen=false;
    f.p.contacts[0]=f.p.contacts[1]=f.p.contacts[4]={};
    f.s.cover=FeedCoverState::Absent;f.s.latch=FeedLatchState::Absent;f.s.cycle=FeedCycleState::Absent;
    CHECK(f.Query().candidate->operation==Op::WithdrawFeed);
    f.Observe(Op::WithdrawFeed);CHECK(f.Query().candidate->operation==Op::DetachContainer);
    f.s.attached=f.s.seated=f.desired;f.s.feed=FeedSeatState::Seated;
    CHECK(f.Query().status==Status::NoPhysicalStep);return 0;
}
int ConflictingFeedAndContainer(){
    Fixture f;f.s.seated=f.desired;CHECK(f.Query().status==Status::InvalidSnapshot);return 0;
}
int ClosureBeforeCharge(){
    Fixture f;f.FinishObserved();f.s.cover=FeedCoverState::Open;f.s.latch=FeedLatchState::Released;f.s.cycle=FeedCycleState::Required;
    CHECK(f.Query().candidate->operation==Op::CloseCover);f.Observe(Op::CloseCover);
    CHECK(f.Query().candidate->operation==Op::EngageLatch);f.Observe(Op::EngageLatch);
    CHECK(f.Query().candidate->operation==Op::CycleAction);return 0;
}
int AlreadyNativeReady(){Fixture f;f.FinishObserved();CHECK(f.Query().status==Status::NoPhysicalStep);return 0;}
int PendingNativeCycle(){Fixture f;f.FinishObserved();f.s.cycle=FeedCycleState::Pending;CHECK(f.Query().status==Status::CyclePending);return 0;}
int MagazineExcluded(){Fixture f;f.p.family=FeedFamily::Magazine;CHECK(!ValidateFeedMechanismProfile(f.p));CHECK(f.Query().status==Status::UnsupportedFamily);return 0;}
int DefaultDisabled(){CHECK(!ValidateFeedMechanismProfile({}));CHECK(!EvaluateFeedMechanism({}, {}, {},100).candidate);return 0;}
int WrongSupply(){
    for(unsigned i=0;i<5;++i){Fixture f;switch(i){case 0:++f.desired.resource.id;break;case 1:++f.desired.resource.generation;break;
        case 2:++f.desired.container.id;break;case 3:++f.desired.container.generation;break;default:++f.s.supply.ammunitionKind;break;}
        CHECK(f.Query().status==Status::WrongResource&&!f.Query().candidate);
    }return 0;
}
int UnavailableSupply(){
    for(const auto state:{FeedSupplyState::Unknown,FeedSupplyState::Depleted,FeedSupplyState::Unavailable,static_cast<FeedSupplyState>(200)}){
        Fixture f;f.s.supply.state=state;CHECK(f.Query().status==Status::ResourceUnavailable);
    }return 0;
}
int UnknownStates(){
    for(unsigned i=0;i<5;++i){Fixture f;switch(i){case 0:f.s.cover=FeedCoverState::Unknown;break;case 1:f.s.latch=FeedLatchState::Unknown;break;
        case 2:f.s.container=FeedContainerState::Unknown;break;case 3:f.s.feed=FeedSeatState::Unknown;break;default:f.s.cycle=FeedCycleState::Unknown;break;}
        CHECK(f.Query().status==Status::UnknownState);
    }return 0;
}
int ContradictoryProfile(){
    for(unsigned i=0;i<12;++i){Fixture f;switch(i){case 0:f.p.cover=false;break;case 1:f.p.container=false;break;case 2:f.p.charge=false;break;
        case 3:f.p.latch=false;break;case 4:f.p.id=0;break;case 5:f.p.revision=0;break;case 6:f.p.maxAgeNs=0;break;
        case 7:f.p.contacts[0].radiusMeters=-1;break;case 8:f.p.contacts[0].maxAngleRadians=4;break;
        case 9:f.p.contacts[0].radiusMeters=std::numeric_limits<float>::quiet_NaN();break;
        case 10:f.p.contacts[0].role=f.p.contacts[1].role;break;default:f.p.ammunitionKind=0;break;}
        CHECK(!ValidateFeedMechanismProfile(f.p));CHECK(f.Query().status==Status::InvalidProfile);
    }return 0;
}
int MalformedSnapshots(){
    for(unsigned i=0;i<9;++i){Fixture f;switch(i){case 0:f.s.cover=FeedCoverState::Open;break;case 1:f.s.container=FeedContainerState::Absent;break;
        case 2:f.s.feed=static_cast<FeedSeatState>(50);break;case 3:f.s.cycle=static_cast<FeedCycleState>(50);break;
        case 4:f.s.attached.resource.generation=0;break;case 5:f.s.feed=FeedSeatState::Unseated;break;
        case 6:f.s.container=FeedContainerState::Detached;break;case 7:f.s.mechanism.id=0;break;default:f.s.inputSequence=0;break;}
        CHECK(f.Query().status==Status::InvalidSnapshot&&!f.Query().candidate);
    }return 0;
}
int InterruptedReconciliation(){
    Fixture f;f.Observe(Op::ReleaseLatch);f.Observe(Op::OpenCover);f.Observe(Op::WithdrawFeed);
    f.s.reconciled=false;CHECK(f.Query().status==Status::Unreconciled);f.s.reconciled=true;
    CHECK(f.Query().candidate->operation==Op::DetachContainer);return 0;
}
int Freshness(){
    Fixture f;f.now+=100;CHECK(f.Query().candidate);++f.now;CHECK(f.Query().status==Status::Stale);
    f.now=f.s.observedNs-1;CHECK(f.Query().status==Status::Stale);return 0;
}
int ContactTranslationAndAngle(){
    Fixture f;const auto c=*f.Query().candidate;auto h=f.Hand(c);
    h.weaponFromHandContact.values[3][0]=.020f;auto r=f.Contact(c,h);CHECK(r.status==Status::ContactMatch&&r.target);CHECK(std::abs(r.distanceMeters-.02)<1e-6);
    h.weaponFromHandContact.values[3][0]=.026f;CHECK(f.Contact(c,h).status==Status::ContactMiss);
    h=f.Hand(c);const float angle=.3f;h.weaponFromHandContact.values[0][0]=std::cos(angle);h.weaponFromHandContact.values[0][1]=std::sin(angle);
    h.weaponFromHandContact.values[1][0]=-std::sin(angle);h.weaponFromHandContact.values[1][1]=std::cos(angle);
    r=f.Contact(c,h);CHECK(r.status==Status::ContactMiss&&Near(r.angleRadians,angle));return 0;
}
int IdentityInvalidatesCandidate(){
    for(unsigned i=0;i<10;++i){Fixture f;const auto c=*f.Query().candidate;switch(i){case 0:++f.s.owner.actor;break;case 1:++f.s.owner.actorGeneration;break;
        case 2:++f.s.owner.weapon;break;case 3:++f.s.owner.equipGeneration;break;case 4:++f.s.owner.space;break;
        case 5:++f.s.mechanism.id;break;case 6:++f.s.mechanism.generation;break;case 7:++f.s.sequence;break;
        case 8:++f.p.id;f.s.profileId=f.p.id;break;default:++f.p.revision;f.s.profileRevision=f.p.revision;break;}
        CHECK(f.Contact(c,f.Hand(c)).status==Status::CandidateChanged);
    }return 0;
}
int StateChangeInvalidatesCandidate(){
    Fixture f;const auto c=*f.Query().candidate;f.Observe(Op::ReleaseLatch);
    CHECK(f.Contact(c,f.Hand(c)).status==Status::CandidateChanged);return 0;
}
int CoherentRawContact(){
    for(unsigned i=0;i<6;++i){Fixture f;const auto c=*f.Query().candidate;auto h=f.Hand(c);switch(i){case 0:++h.inputSequence;break;
        case 1:++h.snapshotSequence;break;case 2:++h.owner.space;break;case 3:++h.mechanism.generation;break;
        case 4:h.observedNs=f.now-101;break;default:h.observedNs=f.now+1;break;}
        CHECK(f.Contact(c,h).status==Status::IncoherentContact);
    }return 0;
}
int HandOwnership(){
    Fixture f;const auto c=*f.Query().candidate;auto h=f.Hand(c);h.handAvailable=false;
    CHECK(f.Contact(c,h).status==Status::HandUnavailable);h.handAvailable=true;h.tracked=false;
    CHECK(f.Contact(c,h).status==Status::TrackingUnavailable);return 0;
}
int GeometryValidation(){
    for(unsigned i=0;i<6;++i){Fixture f;const auto c=*f.Query().candidate;auto h=f.Hand(c);auto& m=h.weaponFromHandContact;
        switch(i){case 0:m.values[0][0]=-1;break;case 1:m.values[0][1]=.1f;break;case 2:m={};break;
        case 3:m.values[3][0]=std::numeric_limits<float>::infinity();break;case 4:m.values[3][3]=2;break;
        default:m.values[1][1]=std::numeric_limits<float>::quiet_NaN();break;}
        CHECK(f.Contact(c,h).status==Status::InvalidGeometry);
        f.s.contacts[1].weaponFromContact=m;CHECK(f.Query().status==Status::InvalidGeometry);
    }return 0;
}
int ChangedContactRole(){Fixture f;++f.s.contacts[0].role;CHECK(f.Query().status==Status::InvalidGeometry);return 0;}
int ForgedCandidate(){
    Fixture f;auto c=*f.Query().candidate;const auto h=f.Hand(c);c.part=static_cast<FeedPart>(200);
    CHECK(f.Contact(c,h).status==Status::CandidateChanged);return 0;
}
int NoImpliedCompletion(){
    Fixture f;const auto c=*f.Query().candidate;const auto h=f.Hand(c);
    for(unsigned n=0;n<100;++n)CHECK(f.Contact(c,h).status==Status::ContactMatch);
    CHECK(f.Query().candidate==c&&f.s.latch==FeedLatchState::Engaged&&f.s.cycle==FeedCycleState::Required);return 0;
}
}
int main(){
    if(SyntheticReplacement()||PartialContinuation()||RetainedFeed()||NoContainerFamily()||AccessFreeFamily()||
       ConflictingFeedAndContainer()||ClosureBeforeCharge()||AlreadyNativeReady()||PendingNativeCycle()||MagazineExcluded()||DefaultDisabled()||WrongSupply()||UnavailableSupply()||
       UnknownStates()||ContradictoryProfile()||MalformedSnapshots()||InterruptedReconciliation()||Freshness()||
       ContactTranslationAndAngle()||IdentityInvalidatesCandidate()||StateChangeInvalidatesCandidate()||CoherentRawContact()||
       HandOwnership()||GeometryValidation()||ChangedContactRole()||ForgedCandidate()||NoImpliedCompletion())return 1;
    std::puts("FeedMechanism: 27 deterministic cases passed (physical candidates only; no native binding).");return 0;
}
