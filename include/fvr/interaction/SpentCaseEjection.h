#pragma once
#include "fvr/interaction/AmmunitionLedger.h"
#include "fvr/interaction/HandInteraction.h"
#include "fvr/interaction/TrackingMath.h"

namespace fvr::interaction {
inline constexpr float SpentCaseTiltUpDotLimit=-.5f;
inline constexpr std::int64_t SpentCaseTiltDwellNs=100000000,SpentCaseTiltMaxInputGapNs=100000000;
struct SpentCaseTiltSample {
    AmmoResourceContext context{};
    HandInteractionSample raw{},safety{};
    math::Matrix4 caseWorldMeters{};
    // Adapter-proven open state and same-owner gun/raw-hand authority. Neutral
    // means both grip levels are up and no left claim, not a release-edge bit.
    bool open=false,leftNeutral=false,exactHands=false;
};
struct SpentCaseEjectionEvent {
    AmmoResourceContext context{};
    std::uint64_t epoch=0,inputSequence=0;
    std::int64_t observedNs=0,deadlineNs=0;
    math::Matrix4 worldMeters{};
};
// Persistent mod custody for one exact native ammunition-resource lifetime.
// Empty alone never creates a case. The observation is a mod policy based on
// coherent consumption, not a claim that it proves a native Fire invocation.
// This class cannot issue ammunition commands or modify the caller's ledger.
class SpentCaseEjection {
public:
    explicit SpentCaseEjection(AmmoResourceIdentity owner)noexcept:owner_(owner){}
    [[nodiscard]] bool Present()const noexcept{return present_;}
    [[nodiscard]] std::uint64_t Epoch()const noexcept{return epoch_;}
    [[nodiscard]] const std::optional<SpentCaseEjectionEvent>& Ejected()const noexcept{return ejected_;}
    void Suspend()noexcept {tiltStarted_=tiltObserved_=0;candidate_.reset();}
    bool Observe(const AmmunitionSnapshot& s,std::int64_t now,bool transactionPending)noexcept {
        candidate_.reset();
        if(!Fresh(s,now)){Suspend();return false;}
        if(current_&&(s.sequence<current_->sequence||s.observedNs<current_->observedNs)){
            Suspend();return false;
        }
        if(current_&&s.sequence==current_->sequence){
            if(s.context!=current_->context||s.counts!=current_->counts||s.observedNs!=current_->observedNs){
                previous_.reset();Suspend();return false;
            }
            if(transactionPending){previous_.reset();Suspend();}
            idle_=!transactionPending;return true;
        }
        if(current_&&s.context!=current_->context){
            Suspend();
            // The physical case survives recenter/equip. A world-space visual
            // cannot be carried into a different reference-space generation.
            if(s.context.space!=current_->context.space)ejected_.reset();
        }
        const bool consumed=!transactionPending&&previous_&&previous_->context.resource==s.context.resource&&
            previous_->sequence<s.sequence&&previous_->observedNs<s.observedNs&&
            previous_->counts.loaded==1&&s.counts.loaded==0&&previous_->counts.reserve==s.counts.reserve;
        current_=s;idle_=!transactionPending;
        if(transactionPending){previous_.reset();Suspend();return true;}
        previous_=s;
        if(consumed&&!present_&&epoch_!=UINT64_MAX){present_=true;++epoch_;ejected_.reset();Suspend();}
        return true;
    }
    [[nodiscard]] std::optional<SpentCaseEjectionEvent> Tilt(const SpentCaseTiltSample& s)noexcept {
        candidate_.reset();const auto now=s.safety.nowNs;
        if(!present_||!idle_||!current_||!Fresh(*current_,now)||current_->context!=s.context||
           current_->counts.loaded!=0||!s.open||!s.leftNeutral||!s.exactHands||
           !Input(s.raw,now)||!Input(s.safety,now)||s.raw.owner!=s.safety.owner||
           s.raw.owner.actorGeneration!=owner_.actorGeneration||s.raw.owner.space!=s.context.space||
           s.raw.sequence>s.safety.sequence||s.raw.observedNs>s.safety.observedNs||
           s.raw.nowNs!=now||!InverseRigid(s.caseWorldMeters)){
            Suspend();return {};
        }
        // Duplicate source packets cannot add dwell, even with a newer poll or
        // safety packet. Source gaps and nonadvancing clocks break continuity.
        if(s.raw.sequence<=lastInput_)return {};
        lastInput_=s.raw.sequence;
        if(s.raw.observedNs<=lastObserved_){Suspend();return {};}
        lastObserved_=s.raw.observedNs;
        // +Z is the explicitly authored outward axis of the mod visual. It is
        // not inferred to be a native bore or case-extraction direction.
        if(s.caseWorldMeters.values[2][1]>SpentCaseTiltUpDotLimit){Suspend();return {};}
        if(!tiltStarted_||s.raw.observedNs-tiltObserved_>SpentCaseTiltMaxInputGapNs)tiltStarted_=s.raw.observedNs;
        tiltObserved_=s.raw.observedNs;
        if(tiltObserved_-tiltStarted_<SpentCaseTiltDwellNs)return {};
        candidate_=SpentCaseEjectionEvent{s.context,epoch_,s.raw.sequence,s.raw.observedNs,
            (std::min)({s.raw.deadlineNs,s.safety.deadlineNs,current_->deadlineNs}),s.caseWorldMeters};
        return candidate_;
    }
    // The adapter first commits the same event in its mechanical policy. A
    // rejected policy event must not silently delete this persistent case.
    [[nodiscard]] bool Commit(const SpentCaseEjectionEvent& event,std::int64_t now)noexcept {
        if(!candidate_||!present_||!idle_||!current_||!Fresh(*current_,now)||current_->counts.loaded!=0||
           current_->context!=event.context||event.context!=candidate_->context||event.epoch!=epoch_||
           event.inputSequence!=candidate_->inputSequence||event.observedNs!=candidate_->observedNs||
           event.deadlineNs!=candidate_->deadlineNs||event.worldMeters.values!=candidate_->worldMeters.values||
           now<event.observedNs||now>=event.deadlineNs)return false;
        present_=false;ejected_=event;Suspend();return true;
    }
private:
    bool Fresh(const AmmunitionSnapshot& s,std::int64_t now)const noexcept {
        return owner_.actor&&owner_.actorGeneration&&owner_.weapon&&owner_.weaponGeneration&&
            s.context.resource==owner_&&s.context.equipGeneration&&s.context.space&&s.sequence&&s.coherentIdleVerified&&
            s.counts.capacity==1&&s.counts.loaded>=0&&s.counts.loaded<=1&&s.counts.reserve>=0&&s.counts.reserve<=1000000&&
            s.observedNs>0&&s.observedNs<=now&&now<s.deadlineNs&&s.deadlineNs-s.observedNs<=100000000;
    }
    static bool Input(const HandInteractionSample& i,std::int64_t now)noexcept {
        return i.owner.actor&&i.owner.actorGeneration&&i.owner.equipGeneration&&i.owner.space&&i.sequence&&
            i.focused&&i.tracked[0]&&i.tracked[1]&&!i.released[1]&&i.observedNs>0&&i.observedNs<=now&&
            now<i.deadlineNs&&i.deadlineNs-i.observedNs<=100000000;
    }
    const AmmoResourceIdentity owner_;
    std::optional<AmmunitionSnapshot> current_,previous_;
    std::optional<SpentCaseEjectionEvent> candidate_,ejected_;
    bool present_=false,idle_=false;
    std::uint64_t epoch_=0,lastInput_=0;
    std::int64_t lastObserved_=0,tiltStarted_=0,tiltObserved_=0;
};
}
