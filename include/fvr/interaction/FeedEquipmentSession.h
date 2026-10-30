#pragma once
#include "fvr/interaction/FeedReloadSession.h"
#include "fvr/interaction/FeedAmmunition.h"

namespace fvr::interaction {
struct FeedEquipmentSessionSample {
    FeedMechanismSnapshot snapshot{};
    FeedAmmunitionSnapshot ammunition{};
    FeedReloadBinding binding{};
    FeedAmmunitionAuthority authority{};
    FeedInteractionSample interaction{};
    std::int64_t nowNs=0;
    bool held=false;
    std::optional<FeedAmmunitionReceipt> receipt{};
    // An explicit adapter observation, required when the ledger receipt is
    // historical or its mechanism poststate was unavailable. Never retimestamp
    // the original receipt to manufacture this corroboration.
    std::optional<FeedReloadAck> acknowledgement{};
};
enum class FeedEquipmentStatus:std::uint8_t {
    Unbound,Ready,Suspended,PreflightRejected,Dispatched,AwaitingReceipt,
    AwaitingCorroboration,ReceiptRejected,AcknowledgementRejected,Reconciled,
    Complete,RetireNeedsReconciliation,Retired,InvariantMismatch
};
struct FeedEquipmentSessionResult {
    FeedEquipmentStatus status=FeedEquipmentStatus::Unbound;
    FeedReloadSessionResult session{};
    FeedAmmunitionState ammunition{};
    // This is the ONLY adapter dispatch. session.reload.dispatch is always
    // empty; its request has already become part of this exact command.
    std::optional<FeedAmmunitionCommand> dispatch{};
    bool receiptAccepted=false,acknowledgementDelivered=false;
};
struct FeedEquipmentContextObservation {
    FeedMechanismSnapshot snapshot{};
    FeedAmmunitionSnapshot ammunition{};
};

// Keep one instance for the immutable equipment lifetime, including stow and
// tracking loss. Geometry is mod-owned presentation/intent; counts and effects
// require separately verified adapter authority. No native adapter is enabled
// by constructing this portable coordinator, and it grants no firing permission.
class FeedEquipmentSession {
public:
    FeedEquipmentSession(FeedMechanismProfile mechanismProfile,FeedInteractionProfile interactionProfile,
        FeedOwner owner,FeedKey mechanism,FeedSupplyKey goal,std::uint64_t lifetime,AmmoResourceIdentity resource={})noexcept:
        profile_(mechanismProfile),owner_(owner),mechanism_(mechanism),goal_(goal),lifetime_(lifetime),
        session_(mechanismProfile,interactionProfile,owner,mechanism,goal,lifetime,resource),
        ammunition_(mechanismProfile,owner,mechanism,goal,lifetime,resource){}
    FeedEquipmentSession(const FeedEquipmentSession&)=delete;
    FeedEquipmentSession& operator=(const FeedEquipmentSession&)=delete;
    [[nodiscard]] FeedEquipmentSessionResult Update(const FeedEquipmentSessionSample& sample)noexcept;
    [[nodiscard]] FeedEquipmentSessionResult State()const noexcept;
    [[nodiscard]] std::optional<FeedEquipmentContextObservation> ContextObservation()const noexcept{return contextObservation_;}
    [[nodiscard]] bool UpdateMotion(const FeedMechanismSnapshot& snapshot,const FeedAmmunitionSnapshot& ammunition,
        const FeedPhysicalCandidate& candidate,const FeedMotionProfile& motion,std::int64_t nowNs)noexcept;
    [[nodiscard]] std::optional<FeedMotionRecovery> RecoveryMotion()const noexcept{return session_.RecoveryMotion();}
    [[nodiscard]] bool RecoverMotion(const FeedMechanismSnapshot& snapshot,const FeedAmmunitionSnapshot& ammunition,
        const FeedMotionRecovery& proof,std::int64_t nowNs,const std::optional<FeedRecoveryFrame>& frame={})noexcept;
    [[nodiscard]] bool MigrateContext(const FeedContextMigration& migration,
        const FeedMechanismSnapshot& before,const FeedAmmunitionSnapshot& beforeAmmunition,
        const FeedMechanismSnapshot& after,const FeedAmmunitionSnapshot& afterAmmunition,std::int64_t nowNs)noexcept;
    [[nodiscard]] FeedEquipmentSessionResult Retire(const FeedMechanismSnapshot& snapshot,
        const FeedAmmunitionSnapshot& ammunition,std::int64_t nowNs)noexcept;
private:
    bool Joined(const FeedMechanismSnapshot& s,const FeedAmmunitionSnapshot& a,std::int64_t now)const noexcept;
    bool JoinedFor(const FeedOwner& owner,const FeedMechanismSnapshot& s,const FeedAmmunitionSnapshot& a,std::int64_t now)const noexcept;
    bool Binding(const FeedReloadBinding& b,const FeedAmmunitionAuthority& a,FeedPhysicalOperation op)const noexcept;
    static bool Unchanged(const FeedAmmunitionSnapshot& current,const FeedAmmunitionSnapshot& prior)noexcept;
    static bool Consistent(const FeedReloadResult& reload,const FeedAmmunitionState& ammunition)noexcept;
    const FeedMechanismProfile profile_;
    FeedOwner owner_;
    const FeedKey mechanism_;
    const FeedSupplyKey goal_;
    const std::uint64_t lifetime_;
    FeedReloadSession session_;
    FeedAmmunition ammunition_;
    std::optional<FeedAmmunitionAuthority> gestureAuthority_{};
    std::optional<FeedEquipmentContextObservation> contextObservation_{};
    bool invariantMismatch_=false,retirementRequested_=false;
};
inline bool FeedEquipmentSession::Joined(const FeedMechanismSnapshot& s,const FeedAmmunitionSnapshot& a,
    std::int64_t now)const noexcept {
    return JoinedFor(owner_,s,a,now);
}
inline bool FeedEquipmentSession::JoinedFor(const FeedOwner& owner,const FeedMechanismSnapshot& s,const FeedAmmunitionSnapshot& a,
    std::int64_t now)const noexcept {
    using namespace feed_mechanism_detail;
    const auto& c=a.counts;
    if(!lifetime_||!ValidateFeedMechanismProfile(profile_)||!Owner(owner)||!Key(mechanism_)||!Supply(goal_,profile_.container)||
       s.owner!=owner||s.mechanism!=mechanism_||s.profileId!=profile_.id||s.profileRevision!=profile_.revision||
       a.owner!=owner||a.mechanism!=mechanism_||a.lifetime!=lifetime_||!a.coherentIdleVerified||
       !a.sequence||a.sequence!=s.sequence||a.observedNs!=s.observedNs||a.observedNs<=0||
       a.deadlineNs<=a.observedNs||a.deadlineNs-a.observedNs>profile_.maxAgeNs||now<a.observedNs||now>=a.deadlineNs||
       c.capacity<=0||c.capacity>1000000||c.loaded<0||c.loaded>c.capacity||c.reserve<0||c.reserve>1000000||
       !(Supply(a.loadedSupply,profile_.container)||(c.loaded==0&&Empty(a.loadedSupply)))||
       (profile_.sameContainerReplenishment&&(!Empty(a.loadedSupply)&&a.loadedSupply!=goal_)))return false;
    const auto evidence=EvaluateFeedMechanism(profile_,s,goal_,now);
    return feed_reload_detail::Authoritative(evidence.status,s);
}
inline bool FeedEquipmentSession::Binding(const FeedReloadBinding& b,const FeedAmmunitionAuthority& a,
    FeedPhysicalOperation op)const noexcept {
    const auto n=static_cast<unsigned>(op);
    if(profile_.sameContainerReplenishment&&a.effect!=(op==FeedPhysicalOperation::AttachContainer?
       FeedAmmunitionEffect::RefillLoaded:FeedAmmunitionEffect::PhysicalOnly))return false;
    return b.verified&&feed_mechanism_detail::Key(b.key)&&b.key==a.binding&&b.owner==owner_&&b.mechanism==mechanism_&&
        b.profileId==profile_.id&&b.profileRevision==profile_.revision&&n>0&&n<b.operations.size()&&b.operations[n];
}
inline bool FeedEquipmentSession::Unchanged(const FeedAmmunitionSnapshot& s,const FeedAmmunitionSnapshot& prior)noexcept {
    return s.sequence>=prior.sequence&&s.observedNs>=prior.observedNs&&s.counts==prior.counts&&
        s.loadedSupply==prior.loadedSupply&&(s.sequence!=prior.sequence||s==prior);
}
inline bool FeedEquipmentSession::Consistent(const FeedReloadResult& r,const FeedAmmunitionState& a)noexcept {
    if((r.phase==FeedReloadPhase::Retired)!=a.retired)return false;
    if(a.pending)return r.pending&&*r.pending==a.pending->request;
    return !r.pending||(a.lastReceipt&&*r.pending==a.lastReceipt->command.request);
}
inline FeedEquipmentSessionResult FeedEquipmentSession::State()const noexcept {
    FeedEquipmentSessionResult out;out.session.reload=session_.State();out.ammunition=ammunition_.State();
    if(invariantMismatch_||!Consistent(out.session.reload,out.ammunition))out.status=FeedEquipmentStatus::InvariantMismatch;
    else if(out.ammunition.retired)out.status=FeedEquipmentStatus::Retired;
    else if(out.ammunition.pending)out.status=FeedEquipmentStatus::AwaitingReceipt;
    else if(out.session.reload.pending)out.status=FeedEquipmentStatus::AwaitingCorroboration;
    else if(retirementRequested_)out.status=FeedEquipmentStatus::RetireNeedsReconciliation;
    else if(!out.ammunition.bound)out.status=FeedEquipmentStatus::Unbound;
    else if(out.session.reload.completed)out.status=FeedEquipmentStatus::Complete;
    else if(out.ammunition.suspended||out.session.reload.suspended)out.status=FeedEquipmentStatus::Suspended;
    else out.status=FeedEquipmentStatus::Ready;
    return out;
}
inline bool FeedEquipmentSession::UpdateMotion(const FeedMechanismSnapshot& s,const FeedAmmunitionSnapshot& a,
    const FeedPhysicalCandidate& candidate,const FeedMotionProfile& motion,std::int64_t nowNs)noexcept {
    const auto prior=State();
    if(invariantMismatch_||retirementRequested_||gestureAuthority_||!prior.ammunition.bound||prior.ammunition.retired||
       prior.ammunition.pending||prior.ammunition.needsReconciliation||prior.session.reload.pending||
       !Joined(s,a,nowNs)||!Unchanged(a,prior.ammunition.snapshot))return false;
    return session_.UpdateMotion(s,candidate,motion,nowNs);
}
inline bool FeedEquipmentSession::RecoverMotion(const FeedMechanismSnapshot& s,const FeedAmmunitionSnapshot& a,
    const FeedMotionRecovery& proof,std::int64_t now,const std::optional<FeedRecoveryFrame>& frame)noexcept {
    const auto prior=State();
    if(invariantMismatch_||retirementRequested_||gestureAuthority_||!prior.ammunition.bound||prior.ammunition.retired||
       prior.ammunition.pending||prior.ammunition.needsReconciliation||prior.session.reload.pending||
       !Joined(s,a,now)||!Unchanged(a,prior.ammunition.snapshot))return false;
    return session_.RecoverMotion(s,proof,now,frame);
}
inline bool FeedEquipmentSession::MigrateContext(const FeedContextMigration& m,
    const FeedMechanismSnapshot& before,const FeedAmmunitionSnapshot& beforeAmmo,
    const FeedMechanismSnapshot& after,const FeedAmmunitionSnapshot& afterAmmo,std::int64_t now)noexcept {
    if(invariantMismatch_||retirementRequested_||gestureAuthority_||!contextObservation_||
       !feed_reload_detail::SameSnapshot(before,contextObservation_->snapshot)||beforeAmmo!=contextObservation_->ammunition||
       !Joined(before,beforeAmmo,before.observedNs)||!JoinedFor(after.owner,after,afterAmmo,now)||
       !ammunition_.CanMigrateContext(m,beforeAmmo,afterAmmo,now)||!session_.CanMigrateContext(m,before,after,now))return false;
    // Both preflights inspect retained state without mutation. There is no
    // callback or dispatch between these commits and no history rewrite.
    if(!session_.MigrateContext(m,before,after,now))return false;
    if(!ammunition_.MigrateContext(m,beforeAmmo,afterAmmo,now)){invariantMismatch_=true;return false;}
    owner_=after.owner;contextObservation_=FeedEquipmentContextObservation{after,afterAmmo};return true;
}
inline FeedEquipmentSessionResult FeedEquipmentSession::Update(const FeedEquipmentSessionSample& x)noexcept {
    const auto prior=State();
    if(prior.status==FeedEquipmentStatus::Retired||prior.status==FeedEquipmentStatus::InvariantMismatch)return prior;
    const bool wasPending=prior.session.reload.pending||prior.ammunition.pending||prior.ammunition.needsReconciliation;
    const bool receiptAccepted=x.receipt&&ammunition_.Complete(*x.receipt,x.nowNs);
    const bool receiptRejected=x.receipt&&!receiptAccepted;
    const bool joined=Joined(x.snapshot,x.ammunition,x.nowNs);
    auto ammo=ammunition_.State();
    if(!ammo.bound&&joined){(void)ammunition_.Bind(x.ammunition,x.nowNs);ammo=ammunition_.State();}
    const bool active=x.held&&x.interaction.focused&&x.interaction.contact.tracked;
    if(!active||!joined){ammunition_.Suspend();ammo=ammunition_.State();}
    if(active&&joined&&ammo.suspended&&!ammo.pending&&!wasPending){
        (void)ammunition_.Resume(x.ammunition,x.nowNs);ammo=ammunition_.State();
    }
    const auto evidence=EvaluateFeedMechanism(profile_,x.snapshot,goal_,x.nowNs);
    const bool changed=gestureAuthority_&&*gestureAuthority_!=x.authority;
    const bool preflight=joined&&active&&!retirementRequested_&&!wasPending&&!receiptRejected&&!x.acknowledgement&&
        !changed&&evidence.candidate&&Binding(x.binding,x.authority,evidence.candidate->operation)&&
        ammunition_.CanDispatch(*evidence.candidate,x.authority,x.ammunition,x.nowNs);

    FeedReloadSessionSample physical;
    physical.snapshot=x.snapshot;physical.binding=x.binding;physical.interaction=x.interaction;physical.nowNs=x.nowNs;
    // FeedReloadSession owns the recognizer gate. Suspending its transient hand
    // admission never cancels its pending request or the ledger's custody.
    physical.held=preflight;
    bool acknowledgementRejected=false;
    if(prior.session.reload.pending&&ammo.lastReceipt&&
       ammo.lastReceipt->command.request==*prior.session.reload.pending){
        const auto& receipt=*ammo.lastReceipt;
        const auto& ack=x.acknowledgement?*x.acknowledgement:receipt.acknowledgement;
        const bool corroborated=ack.request==receipt.command.request&&ack.status==receipt.acknowledgement.status&&
            joined&&Unchanged(x.ammunition,ammo.snapshot)&&ack.snapshotSequence==x.snapshot.sequence&&
            ack.observedNs==x.snapshot.observedNs&&feed_mechanism_detail::Fresh(ack.observedNs,x.nowNs,profile_.maxAgeNs)&&
            (ack.status!=FeedReloadAcknowledgement::Applied||feed_reload_detail::EffectObserved(ack.request,x.snapshot));
        if(corroborated)physical.acknowledgement=ack;
        else acknowledgementRejected=x.acknowledgement.has_value();
    }else acknowledgementRejected=x.acknowledgement.has_value();

    FeedReloadSessionResult sessionResult;
    if(prior.session.reload.pending&&(!joined||!ammo.bound||!Unchanged(x.ammunition,ammo.snapshot))){
        // Reload can retain an accepted acknowledgement when its snapshot was
        // rejected (for example sequence rollback). Do not let that retained
        // acknowledgement reconcile later against mechanism-only evidence.
        // Withhold the whole inadmissible observation, preserving its original
        // fields and both retained transactions. The completed gesture has
        // already been interrupted; no presentation target is emitted here.
        sessionResult.reload=session_.State();
        sessionResult.interaction.status=FeedInteractionStatus::Interrupted;
    }else sessionResult=session_.Update(physical);
    std::optional<FeedAmmunitionCommand> dispatch;
    if(sessionResult.reload.dispatch){
        dispatch=ammunition_.Dispatch(*sessionResult.reload.dispatch,x.authority,x.ammunition,x.nowNs);
        if(!dispatch)invariantMismatch_=true; // Never invent a rejection of a request that was not sent.
        sessionResult.reload.dispatch.reset();
    }
    if(preflight&&sessionResult.interaction.status==FeedInteractionStatus::Armed)gestureAuthority_=x.authority;
    else if(!preflight||sessionResult.interaction.completion||sessionResult.reload.suspended)gestureAuthority_.reset();
    auto out=State();out.session=sessionResult;out.dispatch=dispatch;out.receiptAccepted=receiptAccepted;
    if(joined&&!out.ammunition.pending&&!out.session.reload.pending&&!out.ammunition.needsReconciliation&&
       Unchanged(x.ammunition,out.ammunition.snapshot)&&session_.HasObservation(x.snapshot))
        contextObservation_=FeedEquipmentContextObservation{x.snapshot,x.ammunition};
    out.acknowledgementDelivered=physical.acknowledgement.has_value();
    if(out.status==FeedEquipmentStatus::InvariantMismatch)return out;
    if(dispatch)out.status=FeedEquipmentStatus::Dispatched;
    else if(receiptRejected)out.status=FeedEquipmentStatus::ReceiptRejected;
    else if(acknowledgementRejected)out.status=FeedEquipmentStatus::AcknowledgementRejected;
    else if(sessionResult.reload.status==FeedReloadStatus::Reconciled)out.status=FeedEquipmentStatus::Reconciled;
    else if(!retirementRequested_&&!wasPending&&!preflight&&!out.session.reload.completed)out.status=FeedEquipmentStatus::PreflightRejected;
    return out;
}
inline FeedEquipmentSessionResult FeedEquipmentSession::Retire(const FeedMechanismSnapshot& s,
    const FeedAmmunitionSnapshot& a,std::int64_t now)noexcept {
    auto out=State();
    if(out.status==FeedEquipmentStatus::Retired||out.status==FeedEquipmentStatus::InvariantMismatch)return out;
    retirementRequested_=true;gestureAuthority_.reset();ammunition_.Suspend();
    const auto& prior=out.ammunition;
    // Exact ledger retirement preflight. Check this first, then ask the reload
    // coordinator to retire using its own private observation watermarks. If it
    // refuses, neither component is retired. Its success cannot change these
    // ledger prerequisites, so the subsequent ledger retirement cannot refuse.
    if(!prior.bound||prior.pending||out.session.reload.pending||!Joined(s,a,now)||!Unchanged(a,prior.snapshot)||
       a.sequence<=prior.snapshot.sequence||a.observedNs<=prior.snapshot.observedNs){
        out=State();out.status=FeedEquipmentStatus::RetireNeedsReconciliation;return out;
    }
    const auto retired=session_.Retire(s,now);
    if(retired.phase!=FeedReloadPhase::Retired){
        out=State();out.session.reload=retired;out.status=FeedEquipmentStatus::RetireNeedsReconciliation;return out;
    }
    if(!ammunition_.Retire(a,now))invariantMismatch_=true;
    return State(); // Quantities, resources and exact receipts remain retained.
}
}
