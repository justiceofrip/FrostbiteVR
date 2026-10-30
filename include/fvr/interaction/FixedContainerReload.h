#pragma once
#include "fvr/interaction/FeedInteraction.h"
#include "fvr/interaction/HandInteraction.h"

namespace fvr::interaction {
enum class FixedContainerKind:std::uint8_t {Unknown,Cylinder,EnBlocClip};
enum class FixedContainerStep:std::uint8_t {Open,EjectSpent,Insert,Close,Complete};
struct FixedContainerGeometry {
    FeedContactBinding contact{};
    FeedMotionProfile motion{};
};
struct FixedContainerProfile {
    FixedContainerKind kind=FixedContainerKind::Unknown;
    std::uint64_t id=0,revision=0;
    std::uint32_t ammunitionKind=0;
    int capacity=0,roundsPerInsertion=0;
    std::int64_t maxAgeNs=0;
    // Root supplies exact authored geometry plus explicitly identified mod
    // policy. No role, hinge, grip, capacity or chamber is inferred here.
    std::array<FixedContainerGeometry,4> geometry{};
};
struct FixedContainerReleaseEvidence {
    HandClaim before{}; // Captured immediately before the central safety Update.
    HandClaimRelease released{}; // Exact event returned by that Update.
    HandInteractionSample input{}; // Original packet that produced the event.
};
struct FixedContainerSample {
    AmmunitionSnapshot ammunition{};
    HandInteractionSample input{};
    FeedInteractionSample interaction{};
    std::optional<HandClaimToken> claim{};
    // Current same-input anatomical source contact. Includes carried-object
    // recovery; never a snapped wrist or a restamped historical render pose.
    math::Matrix4 weaponFromContact{};
    bool held=false,finishLoading=false;
    // Renderer evidence may precede the current safety packet. The original
    // input identity/deadline remains unchanged; only nowNs is processing time.
    std::optional<HandInteractionSample> currentSafety;
    std::optional<FixedContainerReleaseEvidence> releaseEvidence;
};
struct FixedContainerRequest {
    FeedReloadRequest physical{};
    FeedSupplyKey supply{};
    AmmunitionCommand ammunition{};
    bool operator==(const FixedContainerRequest&)const=default;
};
enum class FixedContainerStatus:std::uint8_t {
    Invalid,NeedNeutral,Gesture,PhysicalStep,NativeDispatched,AwaitingReceipt,
    Suspended,NoAmmunition,Complete,NativeQueued
};
struct FixedContainerResult {
    FixedContainerStatus status=FixedContainerStatus::Invalid;
    FixedContainerStep step=FixedContainerStep::Open;
    FeedInteractionResult interaction{};
    std::optional<FixedContainerRequest> dispatch{};
};
namespace fixed_container_detail {
inline FeedPhysicalOperation Operation(FixedContainerStep s)noexcept {
    switch(s){
    case FixedContainerStep::Open:return FeedPhysicalOperation::OpenCover;
    case FixedContainerStep::EjectSpent:return FeedPhysicalOperation::WithdrawFeed;
    case FixedContainerStep::Insert:return FeedPhysicalOperation::SeatFeed;
    case FixedContainerStep::Close:return FeedPhysicalOperation::CloseCover;
    default:return FeedPhysicalOperation::None;
    }
}
inline bool Profile(const FixedContainerProfile& p)noexcept {
    if((p.kind!=FixedContainerKind::Cylinder&&p.kind!=FixedContainerKind::EnBlocClip)||
       !p.id||!p.revision||!p.ammunitionKind||p.capacity<=0||p.capacity>1000000||
       p.roundsPerInsertion<=0||p.roundsPerInsertion>p.capacity||p.maxAgeNs<=0||p.maxAgeNs>100000000)return false;
    for(unsigned i=0;i<p.geometry.size();++i){
        if(p.kind==FixedContainerKind::EnBlocClip&&i!=static_cast<unsigned>(FixedContainerStep::Insert))continue;
        const auto& g=p.geometry[i];
        if(!g.contact.role||!std::isfinite(g.contact.radiusMeters)||g.contact.radiusMeters<=0||
           !std::isfinite(g.contact.maxAngleRadians)||g.contact.maxAngleRadians<0||g.contact.maxAngleRadians>3.141592654f||
           g.motion.operation!=Operation(static_cast<FixedContainerStep>(i))||!feed_interaction_detail::Motion(g.motion))return false;
    }
    return true;
}
}

// Explicit cylinder/clip policy, using the existing physical recognizer and
// the caller's ONE stable-weapon ammunition ledger. The internal FeedFamily
// projection is a recognizer implementation detail, not a physical belt,
// a native mechanism observation, or FeedAmmunition transfer authority.
// Open/eject/close update mod-owned physical state only. EjectSpent never
// removes live rounds. Insert emits one exact ledger command; only Complete
// can accept its native effect. Pending work survives tracking loss and stow.
class FixedContainerReload {
public:
    FixedContainerReload(FixedContainerProfile profile,AmmoResourceContext context,
        FeedKey mechanism,FeedKey binding,FeedSupplyKey supply,std::uint64_t lifetime,
        AmmunitionLedger& ledger,bool externalAdmission=false,
        std::optional<HandInteractionOwner> handOwner={})noexcept:
        profile_(profile),context_(context),mechanism_(mechanism),binding_(binding),supply_(supply),
        lifetime_(lifetime),ledger_(ledger),externalAdmission_(externalAdmission),handOwner_(handOwner),gesture_(lifetime),
        step_(profile.kind==FixedContainerKind::EnBlocClip?FixedContainerStep::Insert:FixedContainerStep::Open){}
    FixedContainerReload(const FixedContainerReload&)=delete;
    FixedContainerReload& operator=(const FixedContainerReload&)=delete;
    [[nodiscard]] bool Bind(const AmmunitionSnapshot& s,std::int64_t now)noexcept {
        if(bound_||!Valid()||!Snapshot(s,now))return false;
        if(ledger_.Phase()==AmmunitionLedgerPhase::Unbound){if(!ledger_.Bind(s,now))return false;}
        else if(ledger_.Phase()!=AmmunitionLedgerPhase::Ready||ledger_.Snapshot().context!=s.context||
                ledger_.Snapshot().counts!=s.counts||ledger_.Snapshot().sequence!=s.sequence||
                ledger_.Snapshot().observedNs!=s.observedNs||ledger_.WellEmpty())return false;
        bound_=true;return true;
    }
    [[nodiscard]] FixedContainerStep Step()const noexcept{return step_;}
    [[nodiscard]] const auto& Pending()const noexcept{return pending_;}
    // A mechanical interlock only. Root must separately establish native fire
    // readiness; an inactive native bolt never creates an extra chamber.
    [[nodiscard]] bool BlocksFire()const noexcept {
        return pending_.has_value()||gestureClaim_.has_value()||(profile_.kind==FixedContainerKind::Cylinder&&
            mechanismTouched_&&step_!=FixedContainerStep::Complete);
    }
    void Suspend()noexcept {gesture_.Interrupt();gestureClaim_.reset();}
    [[nodiscard]] bool RebindContext(const AmmunitionSnapshot& snapshot,const HandInteractionOwner& hand,std::int64_t now)noexcept {
        if(!bound_||pending_||snapshot.context.resource!=context_.resource||hand.actor!=HandOwner().actor||
           hand.actorGeneration!=context_.resource.actorGeneration||!hand.equipGeneration||hand.space!=snapshot.context.space||
           ledger_.Phase()!=AmmunitionLedgerPhase::Ready||ledger_.Pending()||ledger_.Snapshot().context!=snapshot.context||
           ledger_.Snapshot().counts!=snapshot.counts||ledger_.Snapshot().sequence!=snapshot.sequence||
           ledger_.Snapshot().observedNs!=snapshot.observedNs||ledger_.Snapshot().deadlineNs!=snapshot.deadlineNs||
           !ledger_.Snapshot().coherentIdleVerified||ledger_.WellEmpty()||
           now<lastNow_||snapshot.sequence<lastSnapshot_)return false;
        const auto previous=context_;const auto before=Owner();context_=snapshot.context;
        if(!Snapshot(snapshot,now)){context_=previous;return false;}
        // Migrate retained mechanical geometry, never the prior hand claim or
        // armed grip. Recovery still needs a fresh neutral/current contact.
        gesture_.MigrateContext(before,Owner());gestureClaim_.reset();
        handOwner_=hand;lastSnapshot_=snapshot.sequence;lastNow_=now;lastInput_=0;return true;
    }
    // Rebase a newly picked-up object's source between grips. Destination,
    // anatomical grip and motion policy stay pinned; no held object teleports.
    [[nodiscard]] bool RebaseInsertionSource(const AmmunitionSnapshot& snapshot,const FeedMotionProfile& motion,
        std::int64_t now)noexcept {
        if(!bound_||pending_||gestureClaim_||step_!=FixedContainerStep::Insert||!Snapshot(snapshot,now)||
           now<lastNow_||snapshot.sequence<lastSnapshot_||ledger_.Phase()!=AmmunitionLedgerPhase::Ready||
           snapshot.counts!=ledger_.Snapshot().counts||motion.kind!=FeedMotionKind::FreePlacement||
           !feed_interaction_detail::Motion(motion))return false;
        auto& prior=profile_.geometry[static_cast<unsigned>(FixedContainerStep::Insert)].motion;
        if(prior.kind!=FeedMotionKind::FreePlacement)return false;
        auto expected=prior;expected.placement.weaponFromSource=motion.placement.weaponFromSource;
        if(!feed_interaction_detail::Same(expected,motion))return false;
        prior=motion;Suspend();return true;
    }
    [[nodiscard]] std::optional<FeedMotionRecovery> RecoveryMotion()const noexcept{return gesture_.RecoveryMotion();}
    [[nodiscard]] bool RecoverMotion(const FixedContainerSample& x,const FeedMotionRecovery& proof,
        const std::optional<FeedRecoveryFrame>& frame={})noexcept {
        if(!bound_||pending_||step_==FixedContainerStep::Complete||!Snapshot(x.ammunition,x.input.nowNs))return false;
        const auto p=ProjectionProfile();const auto s=Projection(x);
        return gesture_.RecoverMotion(p,ProjectionMotion(),s,ProjectionSupply(),proof,x.input.nowNs,frame);
    }
    [[nodiscard]] FixedContainerResult Update(const FixedContainerSample& x,HandInteraction& hands)noexcept {
        FixedContainerResult out;out.step=step_;
        const auto stop=[&](FixedContainerStatus status){Suspend();out.status=status;return out;};
        if(!bound_||!Valid())return stop(FixedContainerStatus::Invalid);
        // Always expire hand authority on the current safety packet, including
        // pending native work and suspended input. A release may finish a
        // placement only from the exact still-fresh claim held before Update.
        std::optional<HandClaim> releasing;
        if(x.claim&&(x.claim->hand==InteractionHand::Left||x.claim->hand==InteractionHand::Right))
            releasing=hands.Current(x.claim->hand);
        const auto& safety=x.currentSafety?*x.currentSafety:x.input;
        const auto handUpdate=hands.Update(safety);(void)handUpdate;
        if(!releasing&&x.claim&&x.releaseEvidence){
            const auto& e=*x.releaseEvidence;const auto h=static_cast<unsigned>(x.claim->hand);
            if(h<2&&e.before.token==*x.claim&&e.released.token==*x.claim&&e.released.reason==HandInteractionReason::Released&&
               e.input.owner==safety.owner&&e.input.released[h]&&safety.released[h]&&
               e.input.sequence<=safety.sequence&&e.input.sequence>e.before.inputSequence&&
               e.before.inputSequence<=x.input.sequence&&x.input.sequence<=e.input.sequence&&
               e.before.deadlineNs>safety.nowNs&&
               hands.HasRecordedEvidence(e.input,x.claim->hand)&&
               hands.HasRecordedEvidence(x.input,h?InteractionHand::Left:InteractionHand::Right))releasing=e.before;
        }
        if(pending_){out.status=FixedContainerStatus::AwaitingReceipt;return out;}
        if(step_==FixedContainerStep::Complete){out.status=FixedContainerStatus::Complete;return out;}
        const auto& a=x.ammunition;const auto& c=x.interaction.contact;
        if(!Snapshot(a,x.input.nowNs)||ledger_.Phase()!=AmmunitionLedgerPhase::Ready||ledger_.WellEmpty()||
           ledger_.Snapshot().context!=context_||ledger_.Snapshot().counts!=a.counts||
           a.sequence<lastSnapshot_||x.input.sequence<lastInput_||x.input.nowNs<lastNow_||
           c.owner!=Owner()||c.mechanism!=mechanism_||c.snapshotSequence!=a.sequence||
           c.inputSequence!=x.input.sequence||c.observedNs!=x.input.observedNs||
           x.input.owner!=HandOwner()||safety.owner!=x.input.owner||safety.sequence<x.input.sequence||
           safety.observedNs<x.input.observedNs||safety.nowNs!=x.input.nowNs||
           x.input.nowNs<x.input.observedNs||x.input.nowNs>=x.input.deadlineNs||
           !feed_mechanism_detail::Pose(x.weaponFromContact))return stop(FixedContainerStatus::Invalid);
        lastSnapshot_=a.sequence;lastInput_=x.input.sequence;lastNow_=x.input.nowNs;
        if(!handUpdate.inputValid||!x.held||!x.input.focused||!safety.focused||!safety.tracked[0]||!safety.tracked[1]||
           !x.interaction.focused||!c.tracked||!x.input.tracked[0]||!x.input.tracked[1])
            return stop(FixedContainerStatus::Suspended);
        // Closing early is an explicit player choice and retains all rounds
        // already committed. A button alone never closes the cylinder.
        if(step_==FixedContainerStep::Insert&&profile_.kind==FixedContainerKind::Cylinder&&
           (x.finishLoading||a.counts.loaded==a.counts.capacity||a.counts.reserve==0)){
            step_=FixedContainerStep::Close;out.step=step_;return stop(FixedContainerStatus::NeedNeutral);
        }
        if(step_==FixedContainerStep::Insert&&(a.counts.loaded==a.counts.capacity||a.counts.reserve==0))
            return stop(FixedContainerStatus::NoAmmunition);
        if(!Hand(x,hands,releasing))return stop(FixedContainerStatus::Suspended);
        auto raw=x.interaction;raw.contact.handAvailable=true;raw.operationPermitted=true;
        out.interaction=gesture_.Update(ProjectionProfile(),ProjectionMotion(),Projection(x),ProjectionSupply(),raw,x.input.nowNs);
        // A rejected gesture has already dropped its acquisition state. Drop
        // the matching hand binding too, so a fresh neutral can rearm without
        // borrowing the rejected claim. Retained mechanical recovery is kept.
        if(out.interaction.status>=FeedInteractionStatus::InvalidProfile)
            return stop(FixedContainerStatus::Suspended);
        if(out.interaction.status==FeedInteractionStatus::Acquired){gestureClaim_=x.claim;mechanismTouched_=true;}
        out.status=FixedContainerStatus::Gesture;
        if(!out.interaction.completion)return out;
        gestureClaim_.reset();
        if(step_==FixedContainerStep::Insert){
            // One atomic native transfer for a clip, bounded by actual reserve
            // and remaining capacity. A partial clip does not discard existing
            // rounds or invent capacity+1. A cylinder can use one round per grip.
            auto admission=a;admission.deadlineNs=std::min(admission.deadlineNs,x.input.deadlineNs);
            const auto queued=ledger_.Queue(AmmunitionOperation::InsertRounds,admission,x.input.nowNs,{},profile_.roundsPerInsertion);
            if(!queued)return stop(FixedContainerStatus::Invalid);
            const auto command=externalAdmission_?queued:ledger_.Dispatch(a,x.input.nowNs);
            if(!command)return stop(FixedContainerStatus::Invalid);
            const auto& completion=*out.interaction.completion;
            pending_=FixedContainerRequest{FeedReloadRequest{completion.id,lifetime_,binding_,completion,x.input.nowNs},supply_,*command};
            out.dispatch=pending_;out.status=externalAdmission_?FixedContainerStatus::NativeQueued:FixedContainerStatus::NativeDispatched;return out;
        }
        if(step_==FixedContainerStep::Open)step_=FixedContainerStep::EjectSpent;
        else if(step_==FixedContainerStep::EjectSpent)step_=FixedContainerStep::Insert;
        else if(step_==FixedContainerStep::Close)step_=FixedContainerStep::Complete;
        Suspend();out.step=step_;out.status=FixedContainerStatus::PhysicalStep;return out;
    }
    [[nodiscard]] bool Complete(const FixedContainerRequest& request,const AmmunitionReceipt& receipt,
        const AmmunitionSnapshot& post,std::int64_t now)noexcept {
        if(!pending_)return lastRequest_&&request==*lastRequest_&&lastReceipt_&&receipt==*lastReceipt_;
        if(request!=*pending_||receipt.command!=request.ammunition||!Snapshot(post,now)||
           post.sequence<=request.ammunition.sourceSequence||post.observedNs<receipt.completedNs||
           post.counts!=request.ammunition.after||post.counts!=receipt.after||
           (ledger_.Pending()?*ledger_.Pending()!=request.ammunition:
                !externalAdmission_||ledger_.Phase()!=AmmunitionLedgerPhase::Ready||ledger_.Snapshot().counts!=receipt.after))return false;
        // In external mode this is an exact idempotent receipt check against
        // the SAME ledger already completed by the native resource service.
        if(!ledger_.Complete(receipt,now))return false;
        // Preconditions above make this a newer coherent same-owner binding.
        const auto& current=ledger_.Snapshot();
        if(!(current.context==post.context&&current.sequence==post.sequence&&current.observedNs==post.observedNs&&
             current.deadlineNs==post.deadlineNs&&current.counts==post.counts)&&!ledger_.Bind(post,now))return false;
        lastRequest_=request;lastReceipt_=receipt;pending_.reset();Suspend();
        lastSnapshot_=post.sequence;lastNow_=now;
        if(profile_.kind==FixedContainerKind::EnBlocClip)step_=FixedContainerStep::Complete;
        return true;
    }
    // The service may finish the original command while this weapon is
    // stowed. Consume only its exact retained receipt in the SAME stable
    // ledger after a newer selected view arrives. This never binds a ledger,
    // dispatches a command, grants rounds, or migrates hand authority.
    [[nodiscard]] bool CompleteAfterContextChange(const FixedContainerRequest& request,const AmmunitionReceipt& receipt,
        const AmmunitionSnapshot& current,std::int64_t now)noexcept {
        if(!bound_||!externalAdmission_||current.context.resource!=context_.resource||
           request.ammunition.context.resource!=context_.resource||!SnapshotFor(current,current.context,now)||
           now<lastNow_||current.sequence<lastSnapshot_||current.sequence<=request.ammunition.sourceSequence||
           current.observedNs<receipt.completedNs||receipt.command!=request.ammunition||
           current.counts!=request.ammunition.after||current.counts!=receipt.after||
           ledger_.Phase()!=AmmunitionLedgerPhase::Ready||ledger_.Pending()||ledger_.WellEmpty())return false;
        const auto& selected=ledger_.Snapshot();
        if(selected.context!=current.context||selected.sequence!=current.sequence||selected.observedNs!=current.observedNs||
           selected.deadlineNs!=current.deadlineNs||selected.counts!=current.counts||!selected.coherentIdleVerified)return false;
        if(!pending_)return lastRequest_&&request==*lastRequest_&&lastReceipt_&&receipt==*lastReceipt_&&ledger_.Complete(receipt,now);
        if(request!=*pending_||request.ammunition.context!=context_||current.context==context_)return false;
        // Ready/no pending makes Complete an exact idempotent service-receipt
        // check; even plausible final counts and a fabricated receipt fail.
        if(!ledger_.Complete(receipt,now))return false;
        lastRequest_=request;lastReceipt_=receipt;pending_.reset();Suspend();
        lastSnapshot_=current.sequence;lastNow_=now;
        if(profile_.kind==FixedContainerKind::EnBlocClip)step_=FixedContainerStep::Complete;
        return true;
    }
    // External service has rejected/cancelled this exact queued command before
    // entry. Its shared ledger is Ready with unchanged counts and no pending
    // native ambiguity. A later insertion still needs a new physical gesture.
    [[nodiscard]] bool RejectUndispatched(const FixedContainerRequest& request)noexcept {
        if(!externalAdmission_||!pending_||*pending_!=request||ledger_.Pending()||
           ledger_.Phase()!=AmmunitionLedgerPhase::Ready||ledger_.Snapshot().context!=context_||
           ledger_.Snapshot().counts!=request.ammunition.before)return false;
        pending_.reset();Suspend();return true;
    }
private:
    FeedOwner Owner()const noexcept {const auto& r=context_.resource;return {r.actor,r.actorGeneration,r.weapon,context_.equipGeneration,context_.space};}
    HandInteractionOwner HandOwner()const noexcept {const auto& r=context_.resource;
        return handOwner_.value_or(HandInteractionOwner{r.actor,r.actorGeneration,context_.equipGeneration,context_.space});}
    FeedSupplyKey ProjectionSupply()const noexcept{return {supply_.resource,{}};}
    bool Valid()const noexcept {
        const auto hand=HandOwner();
        return fixed_container_detail::Profile(profile_)&&feed_mechanism_detail::Owner(Owner())&&context_.resource.weaponGeneration&&
            hand.actor&&hand.actorGeneration==context_.resource.actorGeneration&&hand.equipGeneration&&hand.space==context_.space&&
            feed_mechanism_detail::Key(mechanism_)&&feed_mechanism_detail::Key(binding_)&&lifetime_&&
            feed_mechanism_detail::Supply(supply_,profile_.kind==FixedContainerKind::EnBlocClip);
    }
    bool Snapshot(const AmmunitionSnapshot& a,std::int64_t now)const noexcept {
        return SnapshotFor(a,context_,now);
    }
    bool SnapshotFor(const AmmunitionSnapshot& a,const AmmoResourceContext& context,std::int64_t now)const noexcept {
        return a.context==context&&a.context.equipGeneration&&a.context.space&&a.coherentIdleVerified&&a.sequence&&a.observedNs>0&&
            now>=a.observedNs&&now<a.deadlineNs&&a.deadlineNs-a.observedNs<=profile_.maxAgeNs&&
            a.counts.capacity==profile_.capacity&&a.counts.loaded>=0&&a.counts.loaded<=a.counts.capacity&&
            a.counts.reserve>=0&&a.counts.reserve<=1000000;
    }
    bool Hand(const FixedContainerSample& x,const HandInteraction& hands,const std::optional<HandClaim>& releasing)const noexcept {
        if(!x.claim){
            return !x.interaction.grip&&!gestureClaim_&&
                ((!hands.Current(InteractionHand::Left)&&hands.Current(InteractionHand::Right))||
                 (!hands.Current(InteractionHand::Right)&&hands.Current(InteractionHand::Left)));
        }
        const auto& t=*x.claim;
        if(t.hand!=InteractionHand::Left&&t.hand!=InteractionHand::Right)return false;
        const auto h=static_cast<unsigned>(t.hand);
        const auto other=hands.Current(h?InteractionHand::Left:InteractionHand::Right);
        const HandInteractionKey gun{context_.resource.weapon,handOwner_?HandOwner().equipGeneration:context_.resource.weaponGeneration};
        if(!other||other->token.kind!=HandClaimKind::GunHold||other->token.owner!=HandOwner()||other->token.item!=gun||
           other->deadlineNs<=x.input.nowNs||t.owner!=HandOwner()||
           t.contact!=HandInteractionKey{profile_.geometry[static_cast<unsigned>(step_)].contact.role,profile_.revision})return false;
        if(step_==FixedContainerStep::Insert){
            if(t.kind!=HandClaimKind::AmmoObject||t.item!=HandInteractionKey{supply_.resource.id,supply_.resource.generation})return false;
        }else if(t.kind!=HandClaimKind::Mechanism||t.item!=gun||t.prerequisiteClaim!=other->token.id)return false;
        if(gestureClaim_&&t!=*gestureClaim_)return false;
        const auto current=hands.Current(t.hand);
        if(current&&current->token==t&&current->deadlineNs>x.input.nowNs&&current->inputSequence==x.input.sequence)return true;
        const auto& safety=x.currentSafety?*x.currentSafety:x.input;
        return !x.interaction.grip&&safety.released[h]&&gestureClaim_&&releasing&&releasing->token==t&&
            releasing->deadlineNs>safety.nowNs&&releasing->inputSequence<=x.input.sequence&&
            releasing->inputSequence<safety.sequence;
    }
    FeedMechanismProfile ProjectionProfile()const noexcept {
        FeedMechanismProfile p{};p.family=FeedFamily::Belt;p.id=profile_.id;p.revision=profile_.revision;
        p.ammunitionKind=profile_.ammunitionKind;p.maxAgeNs=profile_.maxAgeNs;
        const auto& g=profile_.geometry[static_cast<unsigned>(step_)];
        p.cover=step_==FixedContainerStep::Open||step_==FixedContainerStep::Close;
        p.feedNeedsOpen=p.cover;p.contacts[static_cast<unsigned>(FeedPart::Feed)]=g.contact;
        if(p.cover){p.contacts[static_cast<unsigned>(FeedPart::Cover)]=g.contact;
            // Distinct inactive internal feed contact, derived in a separate
            // role namespace. It is never exposed as an anatomical role.
            p.contacts[static_cast<unsigned>(FeedPart::Feed)].role=g.contact.role==1?2:1;}
        return p;
    }
    FeedInteractionProfile ProjectionMotion()const noexcept {
        FeedInteractionProfile p{};p.profileId=profile_.id;p.profileRevision=profile_.revision;
        const auto& m=profile_.geometry[static_cast<unsigned>(step_)].motion;
        p.operations[static_cast<unsigned>(m.operation)]=m;return p;
    }
    FeedMechanismSnapshot Projection(const FixedContainerSample& x)const noexcept {
        const auto p=ProjectionProfile();FeedMechanismSnapshot s{};
        s.owner=Owner();s.mechanism=mechanism_;s.profileId=profile_.id;s.profileRevision=profile_.revision;
        // This private snapshot describes geometry, so its clock belongs to
        // the raw controller source. Native counts have their own independent
        // freshness checks in Update; a newer count view cannot restamp a pose.
        s.sequence=x.ammunition.sequence;s.inputSequence=x.input.sequence;s.observedNs=x.input.observedNs;s.reconciled=true;
        s.cover=p.cover?(step_==FixedContainerStep::Open?FeedCoverState::Closed:FeedCoverState::Open):FeedCoverState::Absent;
        s.latch=FeedLatchState::Absent;s.container=FeedContainerState::Absent;s.cycle=FeedCycleState::Absent;
        s.feed=step_==FixedContainerStep::EjectSpent||step_==FixedContainerStep::Close?FeedSeatState::Seated:FeedSeatState::Unseated;
        if(s.feed==FeedSeatState::Seated)s.seated=ProjectionSupply();
        // WithdrawFeed needs a different desired resource; this is a private
        // semantic sentinel for spent-case motion, never an ammunition grant.
        if(step_==FixedContainerStep::EjectSpent)s.seated.resource.generation=supply_.resource.generation==1?2:1;
        s.supply={ProjectionSupply(),profile_.ammunitionKind,FeedSupplyState::Usable};
        for(unsigned i=0;i<s.contacts.size();++i)if(feed_mechanism_detail::Part(p,i))s.contacts[i]={p.contacts[i].role,x.weaponFromContact};
        return s;
    }
    FixedContainerProfile profile_;
    AmmoResourceContext context_;
    const FeedKey mechanism_,binding_;
    const FeedSupplyKey supply_;
    const std::uint64_t lifetime_;
    AmmunitionLedger& ledger_;
    const bool externalAdmission_;
    std::optional<HandInteractionOwner> handOwner_;
    FeedInteraction gesture_;
    FixedContainerStep step_;
    bool bound_=false,mechanismTouched_=false;
    std::uint64_t lastSnapshot_=0,lastInput_=0;
    std::int64_t lastNow_=0;
    std::optional<HandClaimToken> gestureClaim_;
    std::optional<FixedContainerRequest> pending_,lastRequest_;
    std::optional<AmmunitionReceipt> lastReceipt_;
};
}
