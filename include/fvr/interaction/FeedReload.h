#pragma once
#include "fvr/interaction/FeedMechanism.h"
#include <limits>

namespace fvr::interaction {
// Emitted by a physical recognizer after measured motion/placement. Contact or
// presentation alone cannot issue this receipt. IDs never repeat in a lifetime;
// resets of the recognizer must preserve its watermark. This is intent only.
struct FeedPhysicalCompletion {
    std::uint64_t id=0,inputSequence=0;
    std::int64_t completedNs=0;
    FeedPhysicalCandidate candidate{};
    bool operator==(const FeedPhysicalCompletion&)const=default;
};
struct FeedReloadRequest {
    std::uint64_t id=0,lifetime=0;
    FeedKey binding{};
    FeedPhysicalCompletion completion{};
    std::int64_t requestedNs=0;
    bool operator==(const FeedReloadRequest&)const=default;
};
enum class FeedReloadAcknowledgement:std::uint8_t {None,Applied,Rejected};
struct FeedReloadAck {
    FeedReloadRequest request{};
    FeedReloadAcknowledgement status=FeedReloadAcknowledgement::None;
    // A native observer's explicit evidence, not a timer or render completion.
    // The cited authoritative snapshot must postdate dispatch. Rejected means
    // the adapter proved no effect (including confirmed expiry), not a timeout.
    std::uint64_t snapshotSequence=0;
    std::int64_t observedNs=0;
    bool operator==(const FeedReloadAck&)const=default;
};
struct FeedReloadBinding {
    FeedKey key{};
    FeedOwner owner{};
    FeedKey mechanism{};
    std::uint64_t profileId=0,profileRevision=0;
    // Granted separately for each of the nine feed operations by an adapter
    // with proved dispatch, completion and resource authority. All start off.
    bool verified=false;
    std::array<bool,10> operations{};
};
struct FeedReloadSample {
    FeedMechanismSnapshot snapshot{};
    FeedReloadBinding binding{};
    std::int64_t nowNs=0;
    // held=false includes stow/vehicle suspension. Neutral is a fresh raw input
    // observation, never synthesized from tracking loss or lack of contact.
    bool focused=false,tracked=false,held=false,neutral=false;
    std::optional<FeedPhysicalCompletion> completion{};
    std::optional<FeedReloadAck> acknowledgement{};
};
enum class FeedReloadPhase:std::uint8_t {
    AwaitingNeutral,Ready,AwaitingAcknowledgement,AwaitingSnapshot,Suspended,Complete,Retired
};
enum class FeedReloadStatus:std::uint8_t {
    Armed,AwaitingPhysicalCompletion,Dispatched,AwaitingAcknowledgement,AwaitingSnapshot,
    Reconciled,Complete,NoPhysicalStep,Suspended,NeutralRequired,InvalidLifetime,InvalidSample,
    LifetimeMismatch,UnverifiedBinding,DuplicatePacket,DuplicateCompletion,InvalidCompletion,
    AcknowledgementMismatch,InvalidAcknowledgement,SnapshotMismatch,SequenceRollback,
    ClockDiscontinuity,MechanismUnavailable,RetireNeedsReconciliation,Retired,TokenExhausted
};
struct FeedReloadResult {
    FeedReloadPhase phase=FeedReloadPhase::AwaitingNeutral;
    FeedReloadStatus status=FeedReloadStatus::NeutralRequired;
    FeedMechanismStatus mechanismStatus=FeedMechanismStatus::InvalidSnapshot;
    // dispatch is returned ONCE. pending is inspection/reconciliation state and
    // MUST NOT be dispatched again. The adapter deduplicates by lifetime + id.
    std::optional<FeedReloadRequest> dispatch{},pending{},lastApplied{};
    std::optional<FeedPhysicalCandidate> candidate{};
    std::uint64_t acceptedOperations=0;
    // completed records this transaction's observed final effect; it is never
    // permission to fire and does not replace the adapter's current readiness.
    bool suspended=true,completed=false;
};

namespace feed_reload_detail {
inline bool SameSnapshot(const FeedMechanismSnapshot& a,const FeedMechanismSnapshot& b)noexcept {
    if(a.owner!=b.owner||a.mechanism!=b.mechanism||a.profileId!=b.profileId||a.profileRevision!=b.profileRevision||
       a.sequence!=b.sequence||a.inputSequence!=b.inputSequence||a.observedNs!=b.observedNs||a.reconciled!=b.reconciled||
       a.cover!=b.cover||a.latch!=b.latch||a.container!=b.container||a.feed!=b.feed||a.cycle!=b.cycle||
       a.attached!=b.attached||a.seated!=b.seated||a.supply.key!=b.supply.key||
       a.supply.ammunitionKind!=b.supply.ammunitionKind||a.supply.state!=b.supply.state)return false;
    for(unsigned i=0;i<a.contacts.size();++i)
        if(a.contacts[i].role!=b.contacts[i].role||a.contacts[i].weaponFromContact.values!=b.contacts[i].weaponFromContact.values)return false;
    return true;
}
inline bool EffectObserved(const FeedReloadRequest& r,const FeedMechanismSnapshot& s)noexcept {
    const auto& c=r.completion.candidate;
    switch(c.operation){
    case FeedPhysicalOperation::ReleaseLatch:return s.latch==FeedLatchState::Released;
    case FeedPhysicalOperation::OpenCover:return s.cover==FeedCoverState::Open;
    case FeedPhysicalOperation::WithdrawFeed:return s.feed==FeedSeatState::Unseated&&feed_mechanism_detail::Empty(s.seated);
    case FeedPhysicalOperation::DetachContainer:return s.container==FeedContainerState::Detached&&feed_mechanism_detail::Empty(s.attached);
    case FeedPhysicalOperation::AttachContainer:return s.container==FeedContainerState::Attached&&s.attached==c.supply;
    case FeedPhysicalOperation::SeatFeed:return s.feed==FeedSeatState::Seated&&s.seated==c.supply;
    case FeedPhysicalOperation::CloseCover:return s.cover==FeedCoverState::Closed;
    case FeedPhysicalOperation::EngageLatch:return s.latch==FeedLatchState::Engaged;
    case FeedPhysicalOperation::CycleAction:return s.cycle==FeedCycleState::NativeReady;
    default:return false;
    }
}
inline bool Authoritative(FeedMechanismStatus status,const FeedMechanismSnapshot& s)noexcept {
    return status==FeedMechanismStatus::Candidate||status==FeedMechanismStatus::NoPhysicalStep||
        status==FeedMechanismStatus::CyclePending||(status==FeedMechanismStatus::ResourceUnavailable&&
        (s.supply.state==FeedSupplyState::Depleted||s.supply.state==FeedSupplyState::Unavailable));
}
}

// Stateful portable coordinator, not a native binding. Profile and all identity
// are immutable for this lifetime; the owner supplies a unique nonzero lifetime
// token and keeps this object while requests/effects need reconciliation. No
// copy, reset, timeout cancellation, ammo arithmetic or fire-ready output exists.
// All nine FeedPhysicalOperation values retain their own request semantics.
class FeedReload {
public:
    FeedReload(FeedMechanismProfile profile,FeedOwner owner,FeedKey mechanism,
        FeedSupplyKey goal,std::uint64_t lifetime)noexcept:
        profile_(profile),owner_(owner),mechanism_(mechanism),goal_(goal),lifetime_(lifetime){}
    FeedReload(const FeedReload&)=delete;
    FeedReload& operator=(const FeedReload&)=delete;
    [[nodiscard]] FeedReloadResult Update(const FeedReloadSample& sample)noexcept;
    [[nodiscard]] FeedReloadResult State()const noexcept;
    // Explicit end of this owner's lifetime. All requests must first receive
    // exact Applied/Rejected evidence and a post-operation snapshot. Then a NEW
    // reconciled old-owner snapshot permits retirement. Accepted effects remain
    // in State(); a new item needs a new object/token. Nothing is rolled back.
    [[nodiscard]] FeedReloadResult Retire(const FeedMechanismSnapshot& snapshot,std::int64_t nowNs)noexcept;
private:
    bool Valid()const noexcept;
    bool Identity(const FeedMechanismSnapshot& s)const noexcept;
    bool Binding(const FeedReloadBinding& b,FeedPhysicalOperation op)const noexcept;
    void Suspend()noexcept;
    FeedReloadResult Result(FeedReloadStatus status,FeedMechanismStatus mechanism=FeedMechanismStatus::InvalidSnapshot)const noexcept;
    FeedMechanismProfile profile_{};
    FeedOwner owner_{};
    FeedKey mechanism_{},bindingKey_{};
    FeedSupplyKey goal_{};
    std::uint64_t lifetime_=0,nextRequest_=0,lastCompletion_=0,accepted_=0;
    std::uint64_t lastInput_=0,resumeInput_=0,resumeSnapshot_=0;
    std::int64_t lastCallNs_=0,lastInputNs_=0;
    std::optional<FeedMechanismSnapshot> lastSnapshot_{};
    std::optional<FeedReloadRequest> pending_{},lastApplied_{};
    std::optional<FeedReloadAck> acknowledgement_{};
    bool armed_=false,suspended_=true,completed_=false,retired_=false;
};
inline bool FeedReload::Valid()const noexcept {
    return lifetime_&&ValidateFeedMechanismProfile(profile_)&&feed_mechanism_detail::Owner(owner_)&&
        feed_mechanism_detail::Key(mechanism_)&&feed_mechanism_detail::Supply(goal_,profile_.container);
}
inline bool FeedReload::Identity(const FeedMechanismSnapshot& s)const noexcept {
    return s.owner==owner_&&s.mechanism==mechanism_&&s.profileId==profile_.id&&s.profileRevision==profile_.revision;
}
inline bool FeedReload::Binding(const FeedReloadBinding& b,FeedPhysicalOperation op)const noexcept {
    const auto n=static_cast<unsigned>(op);
    return b.verified&&feed_mechanism_detail::Key(b.key)&&b.owner==owner_&&b.mechanism==mechanism_&&
        b.profileId==profile_.id&&b.profileRevision==profile_.revision&&n>0&&n<b.operations.size()&&b.operations[n]&&
        (feed_mechanism_detail::Empty(bindingKey_)||bindingKey_==b.key);
}
inline void FeedReload::Suspend()noexcept {
    armed_=false;suspended_=true;
    resumeInput_=lastInput_;
    resumeSnapshot_=lastSnapshot_?lastSnapshot_->sequence:0;
}
inline FeedReloadResult FeedReload::State()const noexcept {
    FeedReloadResult r;
    r.pending=pending_;r.lastApplied=lastApplied_;r.acceptedOperations=accepted_;
    r.suspended=suspended_;r.completed=completed_;
    if(retired_){r.phase=FeedReloadPhase::Retired;r.status=FeedReloadStatus::Retired;}
    else if(pending_){r.phase=acknowledgement_?FeedReloadPhase::AwaitingSnapshot:FeedReloadPhase::AwaitingAcknowledgement;
        r.status=acknowledgement_?FeedReloadStatus::AwaitingSnapshot:FeedReloadStatus::AwaitingAcknowledgement;}
    else if(completed_){r.phase=FeedReloadPhase::Complete;r.status=FeedReloadStatus::Complete;}
    else if(suspended_){r.phase=lastSnapshot_?FeedReloadPhase::Suspended:FeedReloadPhase::AwaitingNeutral;r.status=FeedReloadStatus::NeutralRequired;}
    else {r.phase=armed_?FeedReloadPhase::Ready:FeedReloadPhase::AwaitingNeutral;
        r.status=armed_?FeedReloadStatus::AwaitingPhysicalCompletion:FeedReloadStatus::NeutralRequired;}
    return r;
}
inline FeedReloadResult FeedReload::Result(FeedReloadStatus status,FeedMechanismStatus mechanism)const noexcept {
    auto r=State();r.status=status;r.mechanismStatus=mechanism;return r;
}
inline FeedReloadResult FeedReload::Update(const FeedReloadSample& x)noexcept {
    using namespace feed_mechanism_detail;
    using namespace feed_reload_detail;
    if(retired_)return Result(FeedReloadStatus::Retired);
    if(!Valid())return Result(FeedReloadStatus::InvalidLifetime);
    // Consume every presented intent once, including invalid/suspended/pending
    // ones. No queued gesture can become valid after an unrelated later ack.
    const bool newCompletion=x.completion&&x.completion->id>lastCompletion_;
    if(newCompletion)lastCompletion_=x.completion->id;
    if(x.nowNs<=0){Suspend();return Result(FeedReloadStatus::InvalidSample);}
    if(lastCallNs_&&x.nowNs<lastCallNs_){Suspend();return Result(FeedReloadStatus::ClockDiscontinuity);}
    lastCallNs_=x.nowNs;
    if(!x.focused||!x.tracked||!x.held||!Identity(x.snapshot))Suspend();

    // Native evidence remains receivable during tracking/stow/owner loss. It
    // always refers to the retained exact old request, never the new actor.
    if(x.acknowledgement){
        const auto& a=*x.acknowledgement;
        if(!pending_||a.request!=*pending_)return Result(FeedReloadStatus::AcknowledgementMismatch);
        if(acknowledgement_){
            if(a!=*acknowledgement_)return Result(FeedReloadStatus::AcknowledgementMismatch);
        }else{
            if((a.status!=FeedReloadAcknowledgement::Applied&&a.status!=FeedReloadAcknowledgement::Rejected)||
               a.snapshotSequence<=pending_->completion.candidate.snapshotSequence||
               a.observedNs<=pending_->requestedNs||!Fresh(a.observedNs,x.nowNs,profile_.maxAgeNs))
                return Result(FeedReloadStatus::InvalidAcknowledgement);
            acknowledgement_=a;
            if(a.status==FeedReloadAcknowledgement::Applied){++accepted_;lastApplied_=pending_;}
        }
    }
    if(!Identity(x.snapshot)){Suspend();return Result(FeedReloadStatus::LifetimeMismatch);}
    const auto& s=x.snapshot;
    if(!s.sequence||!s.inputSequence){Suspend();return Result(FeedReloadStatus::InvalidSample);}
    if(lastSnapshot_&&(s.sequence<lastSnapshot_->sequence||s.inputSequence<lastInput_||s.observedNs<lastSnapshot_->observedNs)){
        Suspend();return Result(FeedReloadStatus::SequenceRollback);
    }
    if(lastSnapshot_&&s.sequence==lastSnapshot_->sequence&&!SameSnapshot(s,*lastSnapshot_)){
        Suspend();return Result(FeedReloadStatus::SnapshotMismatch);
    }
    const bool freshInput=s.inputSequence>lastInput_;
    const bool freshSnapshot=!lastSnapshot_||s.sequence>lastSnapshot_->sequence;
    const bool inputGap=lastInputNs_&&x.nowNs-lastInputNs_>profile_.maxAgeNs;
    if(freshSnapshot)lastSnapshot_=s;
    if(freshInput){lastInput_=s.inputSequence;lastInputNs_=x.nowNs;}
    if(!x.focused||!x.tracked||!x.held||inputGap)Suspend();
    const auto next=EvaluateFeedMechanism(profile_,s,goal_,x.nowNs);
    if(!Authoritative(next.status,s)){Suspend();return Result(FeedReloadStatus::MechanismUnavailable,next.status);}

    if(pending_){
        armed_=false;
        if(!acknowledgement_)return Result(FeedReloadStatus::AwaitingAcknowledgement,next.status);
        const auto& a=*acknowledgement_;
        if(!s.reconciled||s.sequence<a.snapshotSequence||s.sequence<=pending_->completion.candidate.snapshotSequence||
           s.observedNs<a.observedNs||s.observedNs<=pending_->requestedNs||
           (a.status==FeedReloadAcknowledgement::Applied&&!EffectObserved(*pending_,s)))
            return Result(FeedReloadStatus::AwaitingSnapshot,next.status);
        const bool applied=a.status==FeedReloadAcknowledgement::Applied;
        pending_.reset();acknowledgement_.reset();
        // NoPhysicalStep alone never completes a transaction; this is the
        // fresh post-operation observation joined to its exact Applied receipt.
        completed_=applied&&next.status==FeedMechanismStatus::NoPhysicalStep;
        return Result(completed_?FeedReloadStatus::Complete:FeedReloadStatus::Reconciled,next.status);
    }
    if(completed_)return Result(FeedReloadStatus::Complete,next.status);
    if(!x.focused||!x.tracked||!x.held)return Result(FeedReloadStatus::Suspended,next.status);
    if(!freshInput||!freshSnapshot)return Result(FeedReloadStatus::DuplicatePacket,next.status);
    if(suspended_){
        if(!x.neutral||x.completion||s.inputSequence<=resumeInput_||s.sequence<=resumeSnapshot_)
            return Result(FeedReloadStatus::NeutralRequired,next.status);
        suspended_=false;
    }
    if(!next.candidate){armed_=false;return Result(next.status==FeedMechanismStatus::NoPhysicalStep?
        FeedReloadStatus::NoPhysicalStep:FeedReloadStatus::MechanismUnavailable,next.status);}
    if(!Binding(x.binding,next.candidate->operation)){Suspend();return Result(FeedReloadStatus::UnverifiedBinding,next.status);}
    if(x.neutral){
        if(x.completion){armed_=false;return Result(FeedReloadStatus::InvalidCompletion,next.status);}
        armed_=true;auto r=Result(FeedReloadStatus::Armed,next.status);r.candidate=next.candidate;return r;
    }
    if(!x.completion){auto r=Result(armed_?FeedReloadStatus::AwaitingPhysicalCompletion:FeedReloadStatus::NeutralRequired,next.status);
        r.candidate=next.candidate;return r;}
    const bool wasArmed=armed_;armed_=false;
    if(!newCompletion)return Result(FeedReloadStatus::DuplicateCompletion,next.status);
    if(!wasArmed)return Result(FeedReloadStatus::NeutralRequired,next.status);
    const auto& c=*x.completion;
    if(c.candidate!=*next.candidate||c.inputSequence!=s.inputSequence||c.completedNs<s.observedNs||
       !Fresh(c.completedNs,x.nowNs,profile_.maxAgeNs))return Result(FeedReloadStatus::InvalidCompletion,next.status);
    if(nextRequest_==std::numeric_limits<std::uint64_t>::max())return Result(FeedReloadStatus::TokenExhausted,next.status);
    bindingKey_=x.binding.key;
    pending_=FeedReloadRequest{++nextRequest_,lifetime_,bindingKey_,c,x.nowNs};
    auto r=Result(FeedReloadStatus::Dispatched,next.status);r.dispatch=pending_;return r;
}
inline FeedReloadResult FeedReload::Retire(const FeedMechanismSnapshot& s,std::int64_t nowNs)noexcept {
    if(retired_)return Result(FeedReloadStatus::Retired);
    if(!Valid())return Result(FeedReloadStatus::InvalidLifetime);
    Suspend();
    if(pending_)return Result(FeedReloadStatus::RetireNeedsReconciliation);
    if(!Identity(s))return Result(FeedReloadStatus::LifetimeMismatch);
    if(nowNs<=0||nowNs<lastCallNs_)return Result(FeedReloadStatus::ClockDiscontinuity);
    const auto next=EvaluateFeedMechanism(profile_,s,goal_,nowNs);
    if(!feed_reload_detail::Authoritative(next.status,s))return Result(FeedReloadStatus::RetireNeedsReconciliation,next.status);
    if(lastSnapshot_&&(s.sequence<=lastSnapshot_->sequence||s.inputSequence<lastInput_||s.observedNs<=lastSnapshot_->observedNs))
        return Result(FeedReloadStatus::RetireNeedsReconciliation,next.status);
    lastSnapshot_=s;lastCallNs_=nowNs;retired_=true;
    return Result(FeedReloadStatus::Retired,next.status);
}
}
