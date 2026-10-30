#pragma once
#include "fvr/interaction/FeedInteraction.h"

namespace fvr::interaction {
struct FeedReloadSessionSample {
    FeedMechanismSnapshot snapshot{};
    FeedReloadBinding binding{};
    FeedInteractionSample interaction{};
    std::int64_t nowNs=0;
    bool held=false;
    std::optional<FeedReloadAck> acknowledgement{};
};
struct FeedReloadSessionResult {
    FeedReloadResult reload{};
    FeedInteractionResult interaction{};
    bool recognitionEnabled=false;
};

// One physical recognizer and retained transaction coordinator per immutable
// owner/mechanism/goal/lifetime. The adapter supplies measured profiles, current
// raw contact, explicit operation capabilities and authoritative observations.
// Only result.reload.dispatch is a native request, emitted once. Neither a
// gesture receipt nor State().pending is permission to dispatch or to fire.
class FeedReloadSession {
public:
    FeedReloadSession(FeedMechanismProfile mechanismProfile,FeedInteractionProfile interactionProfile,
        FeedOwner owner,FeedKey mechanism,FeedSupplyKey goal,std::uint64_t lifetime)noexcept:
        mechanismProfile_(mechanismProfile),interactionProfile_(interactionProfile),owner_(owner),
        mechanism_(mechanism),goal_(goal),lifetime_(lifetime),
        reload_(mechanismProfile,owner,mechanism,goal,lifetime){}
    FeedReloadSession(const FeedReloadSession&)=delete;
    FeedReloadSession& operator=(const FeedReloadSession&)=delete;
    [[nodiscard]] FeedReloadSessionResult Update(const FeedReloadSessionSample& sample)noexcept;
    [[nodiscard]] FeedReloadResult State()const noexcept {return reload_.State();}
    // Retirement cannot abandon native work. Reconcile the exact old request
    // and then supply a new old-owner snapshot, as required by FeedReload.
    [[nodiscard]] FeedReloadResult Retire(const FeedMechanismSnapshot& snapshot,std::int64_t nowNs)noexcept {
        Interrupt();return reload_.Retire(snapshot,nowNs);
    }
private:
    bool Binding(const FeedReloadBinding& binding,FeedPhysicalOperation operation)const noexcept;
    static bool SameBinding(const FeedReloadBinding& a,const FeedReloadBinding& b)noexcept;
    void Interrupt()noexcept {interaction_.Interrupt();gestureBinding_.reset();}
    const FeedMechanismProfile mechanismProfile_;
    const FeedInteractionProfile interactionProfile_;
    const FeedOwner owner_;
    const FeedKey mechanism_;
    const FeedSupplyKey goal_;
    const std::uint64_t lifetime_;
    FeedInteraction interaction_{};
    FeedReload reload_;
    std::optional<FeedReloadBinding> gestureBinding_{};
    FeedKey dispatchedBinding_{};
};
inline bool FeedReloadSession::SameBinding(const FeedReloadBinding& a,const FeedReloadBinding& b)noexcept {
    return a.key==b.key&&a.owner==b.owner&&a.mechanism==b.mechanism&&a.profileId==b.profileId&&
        a.profileRevision==b.profileRevision&&a.verified==b.verified&&a.operations==b.operations;
}
inline bool FeedReloadSession::Binding(const FeedReloadBinding& b,FeedPhysicalOperation op)const noexcept {
    const auto index=static_cast<unsigned>(op);
    return b.verified&&feed_mechanism_detail::Key(b.key)&&b.owner==owner_&&b.mechanism==mechanism_&&
        b.profileId==mechanismProfile_.id&&b.profileRevision==mechanismProfile_.revision&&
        index>0&&index<b.operations.size()&&b.operations[index]&&
        (feed_mechanism_detail::Empty(dispatchedBinding_)||b.key==dispatchedBinding_);
}
inline FeedReloadSessionResult FeedReloadSession::Update(const FeedReloadSessionSample& x)noexcept {
    FeedReloadSessionResult out;
    const auto prior=reload_.State();
    const auto evidence=EvaluateFeedMechanism(mechanismProfile_,x.snapshot,goal_,x.nowNs);
    const bool identity=x.snapshot.owner==owner_&&x.snapshot.mechanism==mechanism_&&
        x.snapshot.profileId==mechanismProfile_.id&&x.snapshot.profileRevision==mechanismProfile_.revision;
    const bool blocked=prior.pending||prior.completed||prior.phase==FeedReloadPhase::Retired;
    const bool binding=evidence.candidate&&Binding(x.binding,evidence.candidate->operation);
    const bool changed=gestureBinding_&&!SameBinding(*gestureBinding_,x.binding);
    // Gate using PRIOR retained state: an acknowledgement resolving this frame
    // cannot also acquire/complete a gesture or count this frame as neutral.
    out.recognitionEnabled=lifetime_&&identity&&!blocked&&binding&&!changed&&x.held;
    if(out.recognitionEnabled){
        auto physical=x.interaction;
        // The session owns this gate; an input packet cannot override pending
        // native work. Hand arbitration remains the adapter's handAvailable.
        physical.operationPermitted=true;
        out.interaction=interaction_.Update(mechanismProfile_,interactionProfile_,x.snapshot,goal_,physical,x.nowNs);
        if(out.interaction.status==FeedInteractionStatus::Armed)gestureBinding_=x.binding;
    }else {
        Interrupt();out.interaction.status=FeedInteractionStatus::Interrupted;
        out.interaction.evidence=evidence.status;
    }

    FeedReloadSample native;
    native.snapshot=x.snapshot;native.binding=x.binding;native.nowNs=x.nowNs;
    native.focused=x.interaction.focused;native.tracked=x.interaction.contact.tracked;native.held=x.held;
    // Only a fresh validated ungripped sample can be neutral. In particular,
    // tracking loss, duplicate packets and a completion receipt never are.
    native.neutral=out.recognitionEnabled&&out.interaction.status==FeedInteractionStatus::Armed&&
        !x.interaction.grip&&!out.interaction.completion;
    native.completion=out.interaction.completion;native.acknowledgement=x.acknowledgement;
    out.reload=reload_.Update(native); // Exactly one update for this observation.
    if(out.reload.dispatch)dispatchedBinding_=out.reload.dispatch->binding;

    // A rejected observation must not leave a grasp armed in just one half of
    // the session. Harmless identical packets retain an already valid grasp.
    bool keep=false;
    if(!out.reload.suspended&&out.reload.phase==FeedReloadPhase::Ready){
        switch(out.interaction.status){
        case FeedInteractionStatus::Armed:keep=out.reload.status==FeedReloadStatus::Armed;break;
        case FeedInteractionStatus::Acquired:case FeedInteractionStatus::Moving:
        case FeedInteractionStatus::Endpoint:case FeedInteractionStatus::Returning:
            keep=out.reload.status==FeedReloadStatus::AwaitingPhysicalCompletion;break;
        case FeedInteractionStatus::Duplicate:keep=out.reload.status==FeedReloadStatus::DuplicatePacket;break;
        default:break;
        }
    }
    if(!keep)Interrupt();
    return out;
}
}
