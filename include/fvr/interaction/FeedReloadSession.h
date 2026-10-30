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
// stable resource/mechanism/goal/lifetime. Context changes require migration.
// The adapter supplies measured profiles, current
// raw contact, explicit operation capabilities and authoritative observations.
// Only result.reload.dispatch is a native request, emitted once. Neither a
// gesture receipt nor State().pending is permission to dispatch or to fire.
class FeedReloadSession {
public:
    FeedReloadSession(FeedMechanismProfile mechanismProfile,FeedInteractionProfile interactionProfile,
        FeedOwner owner,FeedKey mechanism,FeedSupplyKey goal,std::uint64_t lifetime,AmmoResourceIdentity resource={})noexcept:
        mechanismProfile_(mechanismProfile),interactionProfile_(interactionProfile),owner_(owner),
        mechanism_(mechanism),goal_(goal),lifetime_(lifetime),
        interaction_(lifetime),reload_(mechanismProfile,owner,mechanism,goal,lifetime,resource){}
    FeedReloadSession(const FeedReloadSession&)=delete;
    FeedReloadSession& operator=(const FeedReloadSession&)=delete;
    [[nodiscard]] FeedReloadSessionResult Update(const FeedReloadSessionSample& sample)noexcept;
    [[nodiscard]] FeedReloadResult State()const noexcept {return reload_.State();}
    [[nodiscard]] bool HasObservation(const FeedMechanismSnapshot& s)const noexcept{return reload_.HasObservation(s);}
    // Rebase only the current free-placement source between gestures. The
    // measured destination, grip, policy and all retained counters stay fixed.
    [[nodiscard]] bool UpdateMotion(const FeedMechanismSnapshot& snapshot,const FeedPhysicalCandidate& candidate,
        const FeedMotionProfile& motion,std::int64_t nowNs)noexcept;
    [[nodiscard]] std::optional<FeedMotionRecovery> RecoveryMotion()const noexcept{return interaction_.RecoveryMotion();}
    [[nodiscard]] bool RecoverMotion(const FeedMechanismSnapshot& snapshot,const FeedMotionRecovery& proof,std::int64_t nowNs,
        const std::optional<FeedRecoveryFrame>& frame={})noexcept {
        return !retirementRequested_&&!gestureBinding_&&proof.Lifetime()==lifetime_&&reload_.CanRebaseMotion(snapshot,nowNs)&&
            interaction_.RecoverMotion(mechanismProfile_,interactionProfile_,snapshot,goal_,proof,nowNs,frame);
    }
    [[nodiscard]] bool CanMigrateContext(const FeedContextMigration& migration,const FeedMechanismSnapshot& before,
        const FeedMechanismSnapshot& after,std::int64_t nowNs)const noexcept {
        return !retirementRequested_&&!gestureBinding_&&reload_.CanMigrateContext(migration,before,after,nowNs);
    }
    [[nodiscard]] bool MigrateContext(const FeedContextMigration& migration,const FeedMechanismSnapshot& before,
        const FeedMechanismSnapshot& after,std::int64_t nowNs)noexcept {
        if(!CanMigrateContext(migration,before,after,nowNs)||!reload_.MigrateContext(migration,before,after,nowNs))return false;
        interaction_.MigrateContext(owner_,after.owner);owner_=after.owner;return true;
    }
    // Retirement cannot abandon native work. Reconcile the exact old request
    // and then supply a new old-owner snapshot, as required by FeedReload.
    [[nodiscard]] FeedReloadResult Retire(const FeedMechanismSnapshot& snapshot,std::int64_t nowNs)noexcept {
        retirementRequested_=true;Interrupt();return reload_.Retire(snapshot,nowNs);
    }
private:
    bool Binding(const FeedReloadBinding& binding,FeedPhysicalOperation operation)const noexcept;
    static bool SameBinding(const FeedReloadBinding& a,const FeedReloadBinding& b)noexcept;
    void Interrupt()noexcept {interaction_.Interrupt();gestureBinding_.reset();}
    const FeedMechanismProfile mechanismProfile_;
    FeedInteractionProfile interactionProfile_;
    FeedOwner owner_;
    const FeedKey mechanism_;
    const FeedSupplyKey goal_;
    const std::uint64_t lifetime_;
    FeedInteraction interaction_{};
    FeedReload reload_;
    std::optional<FeedReloadBinding> gestureBinding_{};
    FeedKey dispatchedBinding_{};
    bool retirementRequested_=false;
};
inline bool FeedReloadSession::UpdateMotion(const FeedMechanismSnapshot& s,const FeedPhysicalCandidate& candidate,
    const FeedMotionProfile& motion,std::int64_t nowNs)noexcept {
    if(retirementRequested_||gestureBinding_||!reload_.CanRebaseMotion(s,nowNs))return false;
    const auto current=EvaluateFeedMechanism(mechanismProfile_,s,goal_,nowNs);
    const auto index=static_cast<unsigned>(candidate.operation);
    if(!current.candidate||*current.candidate!=candidate||index>=interactionProfile_.operations.size()||
       interactionProfile_.profileId!=mechanismProfile_.id||interactionProfile_.profileRevision!=mechanismProfile_.revision||
       motion.operation!=candidate.operation||motion.kind!=FeedMotionKind::FreePlacement||
       !feed_interaction_detail::Motion(motion))return false;
    const auto& prior=interactionProfile_.operations[index];
    if(prior.operation!=candidate.operation||prior.kind!=FeedMotionKind::FreePlacement||!feed_interaction_detail::Motion(prior))return false;
    auto sourceOnly=prior;sourceOnly.placement.weaponFromSource=motion.placement.weaponFromSource;
    if(!feed_interaction_detail::Same(sourceOnly,motion))return false;
    const auto contact=feed_interaction_detail::Multiply(motion.placement.sourceFromGrip,motion.placement.weaponFromSource);
    const auto& observed=s.contacts[static_cast<unsigned>(candidate.part)].weaponFromContact;
    if(feed_interaction_detail::Distance(contact,observed)>motion.pathToleranceMeters||
       feed_interaction_detail::Angle(contact,observed)>motion.orientationToleranceRadians)return false;
    interactionProfile_.operations[index]=motion;Interrupt();return true;
}
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
    if(!keep){
        // A held packet while waiting for neutral cannot acquire, but it need
        // not erase the explicitly admitted recovery origin before neutral.
        if(out.recognitionEnabled&&out.interaction.status==FeedInteractionStatus::NeedNeutral)gestureBinding_.reset();
        else Interrupt();
    }
    return out;
}
}
