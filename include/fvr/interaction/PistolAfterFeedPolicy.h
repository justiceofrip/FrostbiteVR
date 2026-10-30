#pragma once
#include "fvr/interaction/AmmunitionInventory.h"
#include "fvr/interaction/PhysicalPistolSlide.h"

namespace fvr::interaction {
// Explicit MOD policy, not a chamber model or NativeAfterShot authority. Adapter
// inputs below must come from successful ledger outcomes and own native Updates.
struct PistolAfterFeedTransaction {
    AmmunitionIntent intent{};
    AmmunitionReceipt receipt{};
    bool operator==(const PistolAfterFeedTransaction&)const=default;
};
struct PistolAfterFeedOwnUpdate {
    std::uint64_t invocation=0;
    std::int64_t beganNs=0,completedNs=0;
    bool phaseInputReadyVerified=false;
    bool operator==(const PistolAfterFeedOwnUpdate&)const=default;
};
struct PistolAfterFeedNativeReady {
    PistolAfterFeedTransaction feed{};
    AmmoResourceContext context{};
    std::uint64_t sequence=0;
    std::int64_t observedNs=0,deadlineNs=0;
    // Fixed branch order, each from that branch's own original Update. The
    // adapter validates identities, neutral input, phase and release latches.
    std::array<PistolAfterFeedOwnUpdate,3> updates{};
    bool operator==(const PistolAfterFeedNativeReady&)const=default;
};
struct PistolAfterFeedAdmission {
    AmmunitionSnapshot snapshot{};
    // Initial native readiness for a newly enrolled stable resource only.
    // Never use re-enrollment to forget an existing policy obligation.
    bool initialPolicyReadyVerified=false;
};
struct PistolAfterFeedPending {
    AmmunitionIntent intent{};
    AmmunitionCommand command{};
    bool retainedPolicyReady=false;
    bool operator==(const PistolAfterFeedPending&)const=default;
};
struct PistolAfterFeedMigration {
    AmmoResourceContext from{},to{};
    std::uint64_t sequence=0;
    std::int64_t observedNs=0,deadlineNs=0;
    bool sameResourceVerified=false;
};
struct PistolAfterFeedRetirement {
    AmmoResourceIdentity resource{};
    std::uint64_t sequence=0;
    std::int64_t observedNs=0,deadlineNs=0;
    bool actorLifetimeEnded=false,callbacksQuiesced=false;
};
struct PistolAfterFeedNonDispatch {
    PistolAfterFeedPending pending{};
    std::int64_t observedNs=0,deadlineNs=0;
    bool neverDispatched=false;
};
struct PistolAfterFeedHandBinding {
    AmmoResourceContext context{};
    HandInteractionOwner owner{};
    HandInteractionKey item{};
    std::uint64_t sequence=0;
    std::int64_t observedNs=0,deadlineNs=0;
    // Adapter proves the mapping. Hand actor/item keys are independent from
    // stable ammunition keys and must never be reconstructed from them.
    bool exactResourceVerified=false;
    bool operator==(const PistolAfterFeedHandBinding&)const=default;
};
// Immutable adapter observation of the actual current-input arbiter update.
// Retaining it does not grant custody or extend the original packet/claim lease.
struct PistolAfterFeedHandObservation {
    HandInteractionSample input{};
    std::optional<HandClaim> beforeMechanism;
    HandInteractionResult lifecycle{};
    std::optional<HandClaim> gun;
};
struct PistolAfterFeedMotionSample {
    HandInteractionSample source{},currentSafety{};
    std::optional<PistolAfterFeedHandObservation> sourceObservation,currentObservation;
    AmmoResourceContext context{};
    PistolAfterFeedHandBinding binding{};
    std::uint64_t feedRequest=0;
    math::Matrix4 rawContact{};
    bool grip=false;
    HandClaimToken gun{};
    HandContactProof gunContact{},contact{};
    std::uint64_t acquireIntent=0;
};
struct PistolAfterFeedStroke {
    PistolAfterFeedTransaction feed{};
    AmmoResourceContext context{};
    HandClaimToken mechanism{},gun{};
    std::uint64_t request=0,profile=0,revision=0,inputSequence=0;
    std::int64_t observedNs=0,deadlineNs=0;
    bool operator==(const PistolAfterFeedStroke&)const=default;
};
struct PistolAfterFeedClearance {
    PistolAfterFeedTransaction feed{};
    PistolAfterFeedNativeReady nativeReady{};
    std::optional<PistolAfterFeedStroke> stroke;
    bool retainedPolicyReady=false;
};
struct PistolAfterFeedTarget {
    AmmoResourceContext context{};
    std::uint64_t feedRequest=0,inputSequence=0;
    HandClaimToken mechanism{},gun{};
    math::Matrix4 contact{};
    float travel=0;
    std::int64_t observedNs=0,deadlineNs=0;
};
struct PistolAfterFeedMotionResult {
    PistolSlidePhase phase=PistolSlidePhase::Idle;
    PistolSlideFailure failure=PistolSlideFailure::None;
    bool blocksFire=true,ownsMechanism=false;
    std::optional<PistolAfterFeedTarget> target;
    std::optional<PistolAfterFeedStroke> stroke;
};

namespace pistol_after_feed_detail {
inline bool Packet(const HandInteractionSample& a,const HandInteractionSample& b)noexcept {
    return a.owner==b.owner&&a.sequence==b.sequence&&a.observedNs==b.observedNs&&a.deadlineNs==b.deadlineNs&&
        a.focused==b.focused&&a.tracked==b.tracked&&a.released==b.released;
}
inline bool Context(const AmmoResourceContext& c)noexcept {
    const auto& r=c.resource;
    return r.actor&&r.actorGeneration&&r.weapon&&r.weaponGeneration&&c.equipGeneration&&c.space;
}
inline bool Counts(const AmmunitionCounts& c)noexcept {
    return c.capacity>0&&c.capacity<=1000000&&c.loaded>=0&&c.loaded<=c.capacity&&c.reserve>=0&&c.reserve<=1000000;
}
inline bool Command(const AmmunitionIntent& i,const AmmunitionCommand& c)noexcept {
    if(!Context(c.context)||i.context!=c.context||!i.id||!c.id||!c.sourceSequence||i.operation!=c.operation||
       i.original!=c.original||i.observedNs<=0||i.observedNs>c.requestedNs||c.requestedNs>=i.deadlineNs||
       i.deadlineNs-i.observedNs>100000000||c.admissionDeadlineNs<=c.requestedNs||c.admissionDeadlineNs>i.deadlineNs||
       c.deadlineNs<=c.admissionDeadlineNs||c.deadlineNs-c.requestedNs>2000000000ll||
       !Counts(c.before)||!Counts(c.after)||c.before.capacity!=c.after.capacity||c.mutationRequired!=(c.before!=c.after))return false;
    const auto& b=c.before;const auto& a=c.after;
    switch(c.operation){
    case AmmunitionOperation::RemoveMagazine:return !c.original&&a.loaded==0&&a.reserve==b.reserve;
    case AmmunitionOperation::ReturnMagazine:return c.original&&c.original->owner==c.context.resource&&
        c.original->id&&c.original->capacity==b.capacity&&c.original->rounds>=0&&c.original->rounds<=b.capacity&&
        b.loaded==0&&a.loaded==c.original->rounds&&a.reserve==b.reserve;
    case AmmunitionOperation::RefillMagazine:return !c.original&&b.loaded==0&&b.reserve>0&&
        a.loaded==std::min(b.capacity,b.reserve)&&a.reserve==b.reserve-a.loaded;
    default:return false;
    }
}
inline bool Receipt(const AmmunitionReceipt& r,std::int64_t now)noexcept {
    return r.authorityVerified&&r.copiesVerified&&r.authorityInvocation&&
        r.beganNs>=r.command.requestedNs&&r.beganNs<r.command.admissionDeadlineNs&&
        r.completedNs>=r.beganNs&&r.completedNs<r.command.deadlineNs&&now>=r.completedNs&&
        r.before==r.command.before&&r.after==r.command.after;
}
}

// One serialized instance per stable weapon resource. No reset/rebind API:
// stow and reference-space changes preserve completed transfers and pending
// commands. Genuine actor retirement is a terminal lifetime transition.
class PistolAfterFeedPolicy {
public:
    PistolAfterFeedPolicy()=default;
    PistolAfterFeedPolicy(const PistolAfterFeedPolicy&)=delete;
    PistolAfterFeedPolicy& operator=(const PistolAfterFeedPolicy&)=delete;
    bool Bind(const PistolAfterFeedAdmission& a,std::int64_t now)noexcept {
        AmmunitionLedger validator;
        if(bound_||retired_||!validator.Bind(a.snapshot,now))return false;
        context_=a.snapshot.context;bound_=true;lastNow_=now;
        ready_=a.initialPolicyReadyVerified&&a.snapshot.counts.loaded>0;
        owed_=!ready_;return true;
    }
    bool ConfigureMotion(const PistolSlideProfile& p,HandInteractionKey mechanism)noexcept {
        if(configured_||!pistol_slide_detail::Profile(p)||!weapon_cycle_detail::Key(mechanism))return false;
        profile_=p;mechanismKey_=mechanism;configured_=true;return true;
    }
    bool BeginTransaction(const AmmunitionIntent& intent,const AmmunitionCommand& command,std::int64_t now)noexcept {
        if(!bound_||retired_||pending_||mechanism_||command.context!=context_||now<lastNow_||
           now<command.requestedNs||now>=command.admissionDeadlineNs||
           intent.id<=lastIntent_||command.id<=lastCommand_||!pistol_after_feed_detail::Command(intent,command))return false;
        if(command.operation==AmmunitionOperation::RemoveMagazine){if(detached_)return false;}
        else {
            if(!detached_||!removal_||removal_->receipt.command.context.resource!=context_.resource||
               command.before.capacity!=removal_->receipt.before.capacity)return false;
            if(command.operation==AmmunitionOperation::ReturnMagazine&&command.original!=Original())return false;
        }
        lastIntent_=intent.id;lastCommand_=command.id;lastNow_=now;
        pending_=PistolAfterFeedPending{intent,command,ready_&&!owed_};return true;
    }
    bool CompleteTransaction(const AmmunitionIntent& intent,const AmmunitionReceipt& receipt,std::int64_t now)noexcept {
        if(retired_||!pending_||intent!=pending_->intent||receipt.command!=pending_->command||now<lastNow_||
           !pistol_after_feed_detail::Receipt(receipt,now))return false;
        const bool retained=pending_->retainedPolicyReady;
        const PistolAfterFeedTransaction transaction{intent,receipt};
        pending_.reset();lastNow_=now;nativeReady_.reset();stroke_.reset();Restart();
        if(receipt.command.operation==AmmunitionOperation::RemoveMagazine){
            removal_=transaction;feed_.reset();detached_=true;
            // Acknowledged PRE-removal counts classify explicit mod policy.
            // A later positive count never erases an existing slide obligation.
            retainedAtRemoval_=retained&&receipt.before.loaded>0;
            owed_=owed_||!retainedAtRemoval_;ready_=false;
        }else {
            feed_=transaction;detached_=false;
            // Tactical policy retains its status, but still waits for a fresh
            // own-Update cohort before the adapter may release its fire veto.
            ready_=false;
            if(receipt.after.loaded==0){owed_=true;ready_=false;}
            phase_=owed_?PistolSlidePhase::AwaitingGrip:PistolSlidePhase::AwaitingNative;
        }
        return true;
    }
    bool RejectUndispatched(const PistolAfterFeedNonDispatch& proof,std::int64_t now)noexcept {
        if(retired_||!pending_||proof.pending!=*pending_||!proof.neverDispatched||now<lastNow_||
           proof.observedNs<pending_->command.requestedNs||
           !weapon_cycle_detail::Window(proof.observedNs,proof.deadlineNs,now))return false;
        pending_.reset();lastNow_=now;return true;
    }
    bool ObserveReady(const PistolAfterFeedNativeReady& ready,std::int64_t now)noexcept {
        if(!bound_||retired_||pending_||detached_||!feed_||ready.feed!=*feed_||ready.context!=context_||
           feed_->receipt.after.loaded<=0||ready.sequence<=lastReadySequence_||now<lastNow_||
           !weapon_cycle_detail::Window(ready.observedNs,ready.deadlineNs,now))return false;
        for(std::size_t n=0;n<ready.updates.size();++n){const auto& row=ready.updates[n];
            if(!row.invocation||row.invocation<=lastReadyInvocations_[n]||row.invocation==feed_->receipt.authorityInvocation||
               !row.phaseInputReadyVerified||row.beganNs<feed_->receipt.completedNs||
               row.completedNs<row.beganNs||row.completedNs>ready.observedNs||
               ready.observedNs-row.beganNs>100000000)return false;
            for(std::size_t k=0;k<n;++k)if(row.invocation==ready.updates[k].invocation)return false;
        }
        nativeReady_=ready;lastReadySequence_=ready.sequence;lastNow_=now;
        for(std::size_t n=0;n<ready.updates.size();++n)lastReadyInvocations_[n]=ready.updates[n].invocation;
        TryReady(now);return true;
    }
    bool Migrate(const PistolAfterFeedMigration& proof,std::int64_t now)noexcept {
        if(!bound_||retired_||mechanism_||proof.from!=context_||proof.to.resource!=context_.resource||
           proof.to==proof.from||!pistol_after_feed_detail::Context(proof.to)||!proof.sameResourceVerified||
           proof.to.equipGeneration<proof.from.equipGeneration||proof.to.space<proof.from.space||
           proof.sequence<=lastMigration_||now<lastNow_||
           !weapon_cycle_detail::Window(proof.observedNs,proof.deadlineNs,now))return false;
        context_=proof.to;lastMigration_=proof.sequence;lastNow_=now;nativeReady_.reset();handBinding_.reset();Restart();return true;
    }
    bool Retire(const PistolAfterFeedRetirement& proof,std::int64_t now)noexcept {
        if(!bound_||retired_||mechanism_||proof.resource!=context_.resource||!proof.sequence||
           !proof.actorLifetimeEnded||!proof.callbacksQuiesced||now<lastNow_||
           !weapon_cycle_detail::Window(proof.observedNs,proof.deadlineNs,now))return false;
        retired_=true;lastNow_=now;phase_=PistolSlidePhase::Retired;
        // Retain immutable receipts even after retirement; never roll back ammo.
        return true;
    }
    void Suspend(const HandInteractionSample& source,HandInteraction& hands)noexcept {
        Drop(source,hands);Restart();
    }
    PistolAfterFeedMotionResult UpdateMotion(const PistolAfterFeedMotionSample& sample,HandInteraction& hands)noexcept {
        using namespace weapon_cycle_detail;
        const auto& s=sample.source;const auto& current=sample.currentSafety;const auto now=current.nowNs;
        if(!bound_||retired_||!configured_||!owed_||pending_||detached_||!feed_||
           feed_->receipt.after.loaded<=0||stroke_)return Output(now,hands);
        const auto fail=[&](PistolSlideFailure failure){Drop(current,hands);Restart();failure_=failure;return Output(now,hands);};
        const auto& binding=sample.binding;
        // Native inventory identity is observed when the original controller
        // packet is consumed, after its XR observation time. Preserve that
        // original construction clock instead of borrowing this later tick.
        const auto constructed=sample.sourceObservation?sample.sourceObservation->input.nowNs:
            (s.sequence==current.sequence?now:0);
        if(sample.context!=context_||sample.feedRequest!=feed_->intent.id||binding.context!=context_||
           !binding.exactResourceVerified||binding.sequence!=s.sequence||binding.owner!=s.owner||
           current.owner!=s.owner||!Owner(binding.owner)||!Key(binding.item)||
           binding.owner.actorGeneration!=context_.resource.actorGeneration||binding.item.generation!=binding.owner.equipGeneration||
           binding.owner.space!=context_.space||constructed<s.observedNs||constructed>now||binding.observedNs>constructed||
           !Window(binding.observedNs,binding.deadlineNs,now)||
           (handBinding_&&(binding.owner!=handBinding_->owner||binding.item!=handBinding_->item||
               binding.sequence<handBinding_->sequence||binding.observedNs<handBinding_->observedNs||
               (binding.sequence==handBinding_->sequence&&binding!=*handBinding_))))
            return fail(PistolSlideFailure::Owner);
        if(now<lastNow_||s.nowNs!=now||s.sequence>current.sequence||s.observedNs>current.observedNs)
            return fail(PistolSlideFailure::Input);
        lastNow_=now;
        if(!current.sequence||!Window(current.observedNs,current.deadlineNs,now)||!current.focused||
           !current.tracked[0]||!current.tracked[1]||current.released[HandIndex(profile_.hands.gun)]||
           !s.sequence||!Window(s.observedNs,s.deadlineNs,now)||!s.focused||!s.tracked[0]||!s.tracked[1]||
           s.released[HandIndex(profile_.hands.gun)]||s.released[HandIndex(profile_.hands.mechanism)]==sample.grip)
            return fail(PistolSlideFailure::Tracking);
        const auto before=hands.Current(profile_.hands.mechanism);
        // Gameplay may already have advanced the arbiter to N while renderer
        // geometry still belongs to N-2. Only the actual current packet writes.
        const auto safety=hands.UpdateAndRenewGunHold(current,sample.gun,sample.gunContact);
        const auto gun=hands.Current(profile_.hands.gun);
        if(!safety.lifecycle.inputValid||!gun||gun->token!=sample.gun||sample.gun.owner!=s.owner||
           sample.gun.kind!=HandClaimKind::GunHold||sample.gun.hand!=profile_.hands.gun||
           sample.gun.item!=binding.item||gun->inputSequence!=current.sequence||
           sample.gunContact.key!=sample.gun.contact||sample.gunContact.inputSequence!=current.sequence||
           sample.gunContact.deadlineNs!=current.deadlineNs||!sample.gunContact.eligible||
           gun->deadlineNs<=now||gun->deadlineNs>current.deadlineNs)return fail(PistolSlideFailure::Custody);
        const auto observationValid=[&](const PistolAfterFeedHandObservation& o,const HandInteractionSample& packet){
            return pistol_after_feed_detail::Packet(o.input,packet)&&o.lifecycle.inputValid&&
                o.input.nowNs>=o.input.observedNs&&o.input.nowNs<=now&&o.gun&&o.gun->token==sample.gun&&
                o.gun->inputSequence==packet.sequence&&o.gun->deadlineNs>now&&o.gun->deadlineNs<=packet.deadlineNs;
        };
        if(sample.sourceObservation&&!observationValid(*sample.sourceObservation,s))return fail(PistolSlideFailure::Input);
        if(sample.currentObservation&&!observationValid(*sample.currentObservation,current))return fail(PistolSlideFailure::Input);
        const PistolAfterFeedHandObservation local{current,before,safety.lifecycle,gun};
        const auto retainRelease=[&](const PistolAfterFeedHandObservation& observation){
            if(!mechanism_)return true;
            for(const auto& event:observation.lifecycle.released)if(event&&event->token==*mechanism_){
                if(event->reason!=HandInteractionReason::Released||!observation.beforeMechanism||
                   observation.beforeMechanism->token!=*mechanism_||observation.beforeMechanism->deadlineNs<=now||
                   observation.beforeMechanism->deadlineNs!=mechanismEvidence_.deadlineNs||
                   observation.beforeMechanism->inputSequence!=mechanismEvidence_.inputSequence||
                   observation.beforeMechanism->inputSequence>observation.input.sequence||
                   !observation.input.released[HandIndex(profile_.hands.mechanism)]||
                   observation.input.sequence<lastInput_)return false;
                if(release_&&!pistol_after_feed_detail::Packet(release_->input,observation.input))return false;
                release_=observation;
            }
            return true;
        };
        if(!retainRelease(local)||(sample.currentObservation&&!retainRelease(*sample.currentObservation)))
            return fail(PistolSlideFailure::Custody);
        const auto held=hands.Current(profile_.hands.mechanism);
        if(mechanism_){
            if(gun_!=sample.gun||mechanism_->prerequisiteClaim!=sample.gun.id||
               (held&&(held->token!=*mechanism_||held->deadlineNs!=mechanismEvidence_.deadlineNs||
                   held->inputSequence!=mechanismEvidence_.inputSequence))||(!held&&!release_))return fail(PistolSlideFailure::Custody);
            if(release_&&(held||!release_->beforeMechanism||!release_->gun||
               release_->beforeMechanism->deadlineNs<=now||release_->gun->deadlineNs<=now||
               !Window(release_->input.observedNs,release_->input.deadlineNs,now)||
               s.sequence>release_->input.sequence))return fail(PistolSlideFailure::Expired);
        }
        // A release barrier forbids old geometry from acquiring/renewing current
        // custody. An exact retained release permits only immutable physical
        // observations leading up to that release; it never revives a claim.
        if(!hands.HasRecordedEvidence(s,profile_.hands.mechanism)&&
           !(mechanism_&&release_&&sample.sourceObservation&&s.sequence<=release_->input.sequence))
            return fail(PistolSlideFailure::Input);
        if(s.sequence<lastInput_||s.observedNs<lastObserved_)return fail(PistolSlideFailure::Input);
        if(s.sequence==lastInput_)return Output(now,hands);
        const bool interrupted=lastObserved_&&s.observedNs-lastObserved_>150000000;
        if(s.observedNs<=lastObserved_)return fail(PistolSlideFailure::Input);
        lastInput_=s.sequence;lastObserved_=s.observedNs;
        if(interrupted)return fail(PistolSlideFailure::Expired);
        handBinding_=binding;
        const auto measured=pistol_slide_detail::Travel(profile_,sample.rawContact);
        if(!measured)return fail(PistolSlideFailure::Geometry);
        const float travel=*measured;
        if(phase_==PistolSlidePhase::AwaitingGrip){
            if(!sample.grip){neutral_=true;return Output(now,hands);}
            const bool press=neutral_;neutral_=false;if(!press)return Output(now,hands);
            if(std::abs(travel)>profile_.frontTolerance||Distance(sample.rawContact,profile_.closedContact)>profile_.contactRadius||
               sample.contact.key!=mechanismKey_||sample.contact.inputSequence!=s.sequence||!sample.contact.eligible||
               sample.contact.deadlineNs!=s.deadlineNs||sample.contact.deadlineNs<=now)return fail(PistolSlideFailure::Geometry);
            const auto acquired=hands.AcquireFrom(current,s,{s.owner,profile_.hands.mechanism,HandClaimKind::Mechanism,
                sample.gun.item,sample.contact,sample.acquireIntent,sample.gun.id});
            if(!acquired.accepted||!acquired.claim)return fail(PistolSlideFailure::Custody);
            mechanism_=acquired.claim->token;mechanismEvidence_=*acquired.claim;gun_=sample.gun;phase_=PistolSlidePhase::Pull;
            motion_.Start(travel,s.observedNs);failure_=PistolSlideFailure::None;return WithTarget(sample,hands,travel);
        }
        if(motion_.Expired(profile_,s.observedNs))return fail(PistolSlideFailure::Expired);
        if(!mechanism_)return fail(PistolSlideFailure::Custody);
        if(!motion_.Step(profile_,travel))return fail(PistolSlideFailure::Geometry);
        if(!sample.grip){
            const bool released=release_&&pistol_after_feed_detail::Packet(release_->input,s);
            if(!motion_.Release(profile_,phase_,travel,released)||nextStroke_==std::numeric_limits<std::uint64_t>::max())
                return fail(PistolSlideFailure::Interrupted);
            stroke_=PistolAfterFeedStroke{*feed_,context_,*mechanism_,sample.gun,++nextStroke_,profile_.id,profile_.revision,
                s.sequence,s.observedNs,std::min({s.deadlineNs,gun->deadlineNs,binding.deadlineNs,
                    release_->beforeMechanism->deadlineNs,release_->gun->deadlineNs})};
            mechanism_.reset();release_.reset();phase_=PistolSlidePhase::AwaitingNative;TryReady(now);
            auto out=Output(now,hands);out.stroke=stroke_;return out;
        }
        if(!release_){
            if(!held||held->token!=*mechanism_)return fail(PistolSlideFailure::Custody);
            const auto renewed=hands.RenewFrom(current,s,*mechanism_,{mechanismKey_,s.sequence,s.deadlineNs,true});
            if(!renewed.accepted||!renewed.claim)return fail(PistolSlideFailure::Custody);
            mechanismEvidence_=*renewed.claim;
        }
        motion_.Advance(profile_,phase_,travel,s.observedNs);return WithTarget(sample,hands,travel);
    }
    bool BlocksFire()const noexcept {return !bound_||retired_||pending_.has_value()||detached_||!ready_||owed_;}
    bool Owed()const noexcept {return owed_&&!retired_;}
    bool Retired()const noexcept {return retired_;}
    const auto& Context()const noexcept {return context_;}
    const auto& Pending()const noexcept {return pending_;}
    const auto& Removal()const noexcept {return removal_;}
    const auto& Feed()const noexcept {return feed_;}
    const auto& Stroke()const noexcept {return stroke_;}
    std::optional<PistolAfterFeedClearance> Clearance(std::int64_t now)const noexcept {
        if(BlocksFire()||!feed_||!nativeReady_||now<lastNow_||
           !weapon_cycle_detail::Window(nativeReady_->observedNs,nativeReady_->deadlineNs,now))return {};
        return PistolAfterFeedClearance{*feed_,*nativeReady_,stroke_,retainedAtRemoval_};
    }
    PistolSlidePhase Phase()const noexcept {return phase_;}
private:
    std::optional<RemovedMagazineResource> Original()const noexcept {
        if(!removal_)return {};const auto& r=removal_->receipt;
        return RemovedMagazineResource{r.command.context.resource,r.command.id,r.before.loaded,r.before.capacity};
    }
    void TryReady(std::int64_t now)noexcept {
        if(nativeReady_&&feed_&&(stroke_?stroke_->feed==*feed_:retainedAtRemoval_&&!owed_)&&nativeReady_->feed==*feed_&&
           nativeReady_->context==context_&&weapon_cycle_detail::Window(nativeReady_->observedNs,nativeReady_->deadlineNs,now)){
            owed_=false;ready_=true;phase_=PistolSlidePhase::Complete;
        }
    }
    void Restart()noexcept {
        phase_=retired_?PistolSlidePhase::Retired:stroke_?PistolSlidePhase::AwaitingNative:
            ready_?PistolSlidePhase::Complete:PistolSlidePhase::AwaitingGrip;
        failure_=PistolSlideFailure::None;neutral_=false;motion_={};release_.reset();
    }
    void Drop(const HandInteractionSample& s,HandInteraction& hands)noexcept {
        if(mechanism_){const auto held=hands.Current(mechanism_->hand);if(held&&held->token==*mechanism_)hands.Release(s,*mechanism_);}
        mechanism_.reset();
    }
    PistolAfterFeedMotionResult Output(std::int64_t now,const HandInteraction& hands)const noexcept {
        const auto held=hands.Current(profile_.hands.mechanism);
        return {phase_,failure_,BlocksFire(),mechanism_&&held&&held->token==*mechanism_&&held->deadlineNs>now,{},{}};
    }
    PistolAfterFeedMotionResult WithTarget(const PistolAfterFeedMotionSample& sample,const HandInteraction& hands,float travel)const noexcept {
        auto out=Output(sample.source.nowNs,hands);const auto held=hands.Current(profile_.hands.mechanism),gun=hands.Current(profile_.hands.gun);
        if(!mechanism_||!held||!gun||!feed_)return out;
        auto contact=profile_.closedContact;const float clamped=std::clamp(travel,0.f,profile_.stroke);
        for(unsigned k=0;k<3;++k)contact.values[3][k]+=profile_.axis[k]*clamped;
        out.target=PistolAfterFeedTarget{context_,feed_->intent.id,sample.source.sequence,*mechanism_,sample.gun,contact,clamped,
            sample.source.observedNs,std::min({sample.source.deadlineNs,sample.binding.deadlineNs,held->deadlineNs,gun->deadlineNs})};return out;
    }
    AmmoResourceContext context_{};PistolSlideProfile profile_{};HandInteractionKey mechanismKey_{};
    std::optional<PistolAfterFeedPending> pending_;
    std::optional<PistolAfterFeedTransaction> removal_,feed_;
    std::optional<PistolAfterFeedNativeReady> nativeReady_;
    std::optional<PistolAfterFeedHandBinding> handBinding_;
    std::optional<PistolAfterFeedHandObservation> release_;
    std::optional<PistolAfterFeedStroke> stroke_;
    std::optional<HandClaimToken> mechanism_;HandClaimToken gun_{};HandClaim mechanismEvidence_{};
    pistol_slide_detail::MeasuredStroke motion_{};
    PistolSlidePhase phase_=PistolSlidePhase::Idle;PistolSlideFailure failure_=PistolSlideFailure::None;
    std::uint64_t lastIntent_=0,lastCommand_=0,lastReadySequence_=0,lastMigration_=0,lastInput_=0,nextStroke_=0;
    std::array<std::uint64_t,3> lastReadyInvocations_{};
    std::int64_t lastNow_=0,lastObserved_=0;
    bool bound_=false,retired_=false,configured_=false,ready_=false,owed_=false,detached_=false,retainedAtRemoval_=false,neutral_=false;
};
}
