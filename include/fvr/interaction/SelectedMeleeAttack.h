#pragma once
#include "fvr/interaction/MeleeStroke.h"

namespace fvr::interaction {
// Adapter-issued semantic identities. No engine address, slot or action number.
struct SelectedMeleeIdentity {
    HandInteractionOwner owner{};
    HandInteractionKey item{},binding{};
    std::uint64_t inputEpoch=0;
    bool operator==(const SelectedMeleeIdentity&)const=default;
};
struct SelectedMeleeSample {
    SelectedMeleeIdentity identity{};
    MeleeStrokeSample motion{}; // Current existing GunHold; never acquired here.
    float trigger=0;
    bool selected=false;
    // Same item/binding/hold/input source as motion. Adapter must validate its
    // calibration/render receipt. Missing geometry disables only motion.
    bool geometryCurrent=false;
};
enum class SelectedMeleeCause:std::uint8_t {Trigger,Stroke};
struct SelectedMeleeIntent {
    std::uint64_t serial=0;
    SelectedMeleeIdentity identity{};
    HandClaimToken hold{};
    std::uint64_t sourceSequence=0;
    std::int64_t observedNs=0,deadlineNs=0;
    SelectedMeleeCause cause=SelectedMeleeCause::Trigger;
    std::optional<MeleeStrokeIntent> stroke;
};
enum class SelectedMeleePending:std::uint8_t {None,Staged,Submitted,Unknown};
// These are exact adapter outcomes, not deductions from idle animation, input
// publication, elapsed time, lost tracking or a changed selection.
enum class SelectedMeleeOutcome:std::uint8_t {RejectedNoEffect,Submitted,Completed,Unknown,OwnerRetired};
enum class SelectedMeleeReason:std::uint8_t {Invalid,IdentityChanged,Duplicate,Order,
    Pending,NativeUnavailable,NeutralRequired,Ready,Intent,SerialLimit};
struct SelectedMeleeResult {
    SelectedMeleeReason reason=SelectedMeleeReason::Invalid;
    std::optional<SelectedMeleeIntent> intent;
};
struct SelectedMeleeObservation {
    bool motionUpdated=false;
    bool armedBefore=false,armedAfter=false,triggerDownBefore=false,triggerDownAfter=false;
    SelectedMeleePending pendingBefore=SelectedMeleePending::None,pendingAfter=SelectedMeleePending::None;
    MeleeStrokeObservation motion{};
};
// Recognition and one outstanding intent only. No native calls, hand ownership
// changes, weapon suppression, holster/vehicle gates or hit/damage authority.
// Update emits once; adapter validates the original intent at its boundary and
// explicitly reports outcome. Trigger and motion share this one latch.
class SelectedMeleeAttack {
public:
    explicit SelectedMeleeAttack(MeleeStrokeConfig config={}):motion_(config){}
    SelectedMeleeResult Update(const SelectedMeleeSample& s,const HandInteraction& hands,
        SelectedMeleeObservation* observation=nullptr)noexcept {
        if(observation){*observation={};observation->armedBefore=armed_;observation->triggerDownBefore=triggerDown_;
            observation->pendingBefore=pendingState_;}
        const auto finish=[&](SelectedMeleeReason reason,std::optional<SelectedMeleeIntent> intent=std::nullopt){
            if(observation){observation->armedAfter=armed_;observation->triggerDownAfter=triggerDown_;
                observation->pendingAfter=pendingState_;}
            return SelectedMeleeResult{reason,intent};
        };
        const auto motion=[&](const MeleeStrokeSample& sample){
            if(observation)observation->motionUpdated=true;
            return motion_.Update(sample,hands,observation?&observation->motion:nullptr);
        };
        if(!Valid(s,hands)){InvalidateMotion();return finish(SelectedMeleeReason::Invalid);}
        const auto& f=s.motion.source;
        const bool identityChanged=!previous_||s.identity!=previous_->identity||s.motion.hold!=previous_->motion.hold;
        if(identityChanged){
            motion_.Invalidate();armed_=false;triggerDown_=s.trigger>.1f;previous_=s;
            // First packet establishes ownership only; neither draw/regrip nor
            // an already-held trigger is an attack edge.
            if(!pending_&&s.motion.attackAvailable&&s.geometryCurrent&&s.trigger<=.1f)
                (void)motion(s.motion);
            return finish(pending_?SelectedMeleeReason::Pending:SelectedMeleeReason::IdentityChanged);
        }
        const auto& old=previous_->motion.source;
        if(f.sequence<old.sequence||f.observedNs<old.observedNs||f.nowNs<old.nowNs||
           (f.sequence>old.sequence&&f.observedNs==old.observedNs)){
            InvalidateMotion();return finish(SelectedMeleeReason::Order);
        }
        if(f.sequence==old.sequence){
            const bool same=SamePacket(s,*previous_);
            if(!same){InvalidateMotion();return finish(SelectedMeleeReason::Invalid);}
            previous_->motion.source.nowNs=f.nowNs;
            // Geometry is re-derived from an independently produced render
            // receipt; native readiness is also a current observation, not a
            // controller-packet field. A repeat cannot advance or replace the
            // first geometry sampled for this packet. Current authority loss
            // still revokes recognition, and recovery needs a fresh packet.
            if(!s.motion.attackAvailable){motion_.Invalidate();armed_=false;triggerDown_=s.trigger>.1f;
                return finish(SelectedMeleeReason::NativeUnavailable);}
            if(!s.geometryCurrent)motion_.Invalidate();
            return finish(SelectedMeleeReason::Duplicate);
        }
        previous_=s;
        if(pending_){motion_.Invalidate();armed_=false;triggerDown_=s.trigger>.1f;return finish(SelectedMeleeReason::Pending);}
        if(!s.motion.attackAvailable){motion_.Invalidate();armed_=false;triggerDown_=s.trigger>.1f;return finish(SelectedMeleeReason::NativeUnavailable);}
        const bool press=s.trigger>=.75f&&!triggerDown_;
        if(s.trigger<=.1f)triggerDown_=false;else if(s.trigger>=.75f)triggerDown_=true;
        if(s.trigger<=.1f)armed_=true; // Fresh neutral after identity/readiness recovery.
        auto stroke=s.motion;stroke.attackAvailable=s.geometryCurrent&&!triggerDown_&&armed_;
        const auto gesture=motion(stroke);
        if(!(press&&armed_)&&!gesture.intent)return finish(armed_?SelectedMeleeReason::Ready:SelectedMeleeReason::NeutralRequired);
        if(serial_==UINT64_MAX){InvalidateMotion();return finish(SelectedMeleeReason::SerialLimit);}
        SelectedMeleeIntent command{++serial_,s.identity,s.motion.hold,f.sequence,f.observedNs,f.deadlineNs,
            press&&armed_?SelectedMeleeCause::Trigger:SelectedMeleeCause::Stroke,{}};
        if(command.cause==SelectedMeleeCause::Stroke)command.stroke=gesture.intent;
        pending_=command;pendingState_=SelectedMeleePending::Staged;
        motion_.Invalidate();armed_=false;
        return finish(SelectedMeleeReason::Intent,command);
    }
    bool Resolve(std::uint64_t serial,const SelectedMeleeIdentity& identity,SelectedMeleeOutcome outcome)noexcept {
        if(!pending_||pending_->serial!=serial||pending_->identity!=identity)return false;
        switch(outcome){
        case SelectedMeleeOutcome::Submitted:
            if(pendingState_!=SelectedMeleePending::Staged)return false;
            pendingState_=SelectedMeleePending::Submitted;return true;
        case SelectedMeleeOutcome::RejectedNoEffect:
            if(pendingState_!=SelectedMeleePending::Staged)return false;
            break;
        case SelectedMeleeOutcome::Completed:
            if(pendingState_!=SelectedMeleePending::Submitted&&pendingState_!=SelectedMeleePending::Unknown)return false;
            break;
        case SelectedMeleeOutcome::Unknown:
            pendingState_=SelectedMeleePending::Unknown;motion_.Invalidate();armed_=false;return true;
        case SelectedMeleeOutcome::OwnerRetired:break; // Explicit proven native owner retirement only.
        default:return false;
        }
        pending_.reset();pendingState_=SelectedMeleePending::None;motion_.Invalidate();armed_=false;
        // Keep previous packet/trigger state. Same-packet or held-trigger replay
        // cannot rearm after an outcome, and motion must settle afresh.
        return true;
    }
    void InvalidateMotion()noexcept {motion_.Invalidate();previous_.reset();armed_=false;triggerDown_=true;}
    const std::optional<SelectedMeleeIntent>& Pending()const noexcept{return pending_;}
    SelectedMeleePending PendingState()const noexcept{return pendingState_;}
private:
    static bool Valid(const SelectedMeleeSample& s,const HandInteraction& hands)noexcept {
        const auto& f=s.motion.source;const auto& hold=s.motion.hold;const auto& id=s.identity;
        if(!s.selected||!id.owner.actor||!id.owner.actorGeneration||!id.owner.equipGeneration||!id.owner.space||
           !id.item.id||!id.item.generation||!id.binding.id||!id.binding.generation||!id.inputEpoch||
           id.owner!=f.owner||hold.owner!=f.owner||hold.item!=id.item||hold.kind!=HandClaimKind::GunHold||
           hold.prerequisiteClaim||!f.sequence||!f.focused||f.observedNs<0||f.nowNs<f.observedNs||
           f.deadlineNs<=f.nowNs||f.deadlineNs-f.observedNs>150000000||
           !std::isfinite(s.trigger)||s.trigger<0||s.trigger>1||!hands.HasRecordedEvidence(f,hold.hand))return false;
        const auto current=hands.Current(hold.hand);
        return current&&current->token==hold&&current->inputSequence==f.sequence&&current->deadlineNs>f.nowNs;
    }
    static bool SamePacket(const SelectedMeleeSample& a,const SelectedMeleeSample& b)noexcept {
        const auto& x=a.motion.source;const auto& y=b.motion.source;
        return a.identity==b.identity&&a.motion.hold==b.motion.hold&&a.trigger==b.trigger&&
            a.selected==b.selected&&x.owner==y.owner&&
            x.sequence==y.sequence&&x.observedNs==y.observedNs&&x.deadlineNs==y.deadlineNs&&
            x.focused==y.focused&&x.tracked==y.tracked&&x.released==y.released;
    }
    MeleeStroke motion_;
    std::optional<SelectedMeleeSample> previous_;
    std::optional<SelectedMeleeIntent> pending_;
    SelectedMeleePending pendingState_=SelectedMeleePending::None;
    std::uint64_t serial_=0;
    bool armed_=false,triggerDown_=true;
};
}
