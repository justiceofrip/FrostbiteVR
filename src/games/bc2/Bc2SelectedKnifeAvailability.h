#pragma once
#include "Bc2SelectedKnifeNativeReader.h"
#include "fvr/interaction/SelectedMeleeAttack.h"

namespace fvr::bc2::selectedKnife {
enum class RecognitionGapResult:unsigned {None,Held,Repeated,Recovered,Rejected,Count};
// A pre-intent recognizer checkpoint, not native availability or an input
// delivery retry. Skipping a failed observation never calls the recognizer,
// copies a pose, emits Fire, extends a lease, or settles a pending outcome.
class RecognitionGap {
public:
    void Reset()noexcept {anchor_.reset();missing_.reset();}
    bool Active()const noexcept{return bool(missing_);}
    std::uint64_t MissingSequence()const noexcept{return missing_?missing_->sequence:0;}
    std::uint64_t AnchorSequence()const noexcept{return anchor_?anchor_->sample.motion.source.sequence:0;}
    std::int64_t AnchorObservedNs()const noexcept{return anchor_?anchor_->sample.motion.source.observedNs:0;}
    bool NativeOwnerContinues(const ReloadStateOwner& owner)const noexcept {
        return !anchor_||owner==anchor_->owner.client.owner;
    }
    RecognitionGapResult Skip(const NativeReadDiagnostic& failed,const interaction::SelectedMeleeSample& sample,
        const interaction::HandInteraction& hands,bool available,interaction::SelectedMeleePending pending)noexcept {
        const auto& source=sample.motion.source;
        if(!anchor_||!available||pending!=interaction::SelectedMeleePending::None||
           failed.failure!=NativeReadFailure::FiringIncoherent||!failed.recognitionOwner||!failed.branch.readyPair||
           !SameOwner(anchor_->owner,*failed.recognitionOwner)||!ClientsReady(failed.recognitionOwner->client)||
           !Continues(sample,hands)){
            Reset();return RecognitionGapResult::Rejected;
        }
        if(missing_){
            // Multiple Gathers of one immutable failed packet are one gap.
            // A second distinct missing packet is not silently bridged.
            if(!SamePacket(*missing_,source)||source.nowNs<missing_->nowNs){Reset();return RecognitionGapResult::Rejected;}
            missing_->nowNs=source.nowNs;return RecognitionGapResult::Repeated;
        }
        missing_=source;return RecognitionGapResult::Held;
    }
    RecognitionGapResult BeforeVerified(const Native& native,const interaction::SelectedMeleeSample& sample,
        const interaction::HandInteraction& hands,bool available,interaction::SelectedMeleePending pending)noexcept {
        if(!missing_)return RecognitionGapResult::None;
        const auto& source=sample.motion.source;
        const bool okay=anchor_&&available&&pending==interaction::SelectedMeleePending::None&&Ready(native)&&
            sample.geometryCurrent&&sample.motion.attackAvailable&&SameOwner(anchor_->owner,Owner(native))&&
            Continues(sample,hands)&&source.sequence>=missing_->sequence&&source.nowNs>=missing_->nowNs&&
            (source.sequence==missing_->sequence?SamePacket(source,*missing_):source.observedNs>missing_->observedNs);
        missing_.reset();
        if(!okay){Reset();return RecognitionGapResult::Rejected;}
        return RecognitionGapResult::Recovered;
    }
    // Only an actual current geometry sample may establish/advance the
    // checkpoint. Duplicate Gathers never renew its physical time or lease.
    void Remember(const Native& native,const interaction::SelectedMeleeSample& sample,
        const interaction::HandInteraction& hands,const interaction::SelectedMeleeResult& result,
        const interaction::SelectedMeleeObservation& observed)noexcept {
        if(result.intent||observed.pendingAfter!=interaction::SelectedMeleePending::None||
           !Ready(native)||!sample.geometryCurrent||!sample.motion.attackAvailable||!Physical(sample,hands)||
           (result.reason!=interaction::SelectedMeleeReason::Ready&&
            result.reason!=interaction::SelectedMeleeReason::IdentityChanged&&result.reason!=interaction::SelectedMeleeReason::Duplicate)){
            Reset();return;
        }
        if(observed.motionUpdated){
            if(observed.motion.after==interaction::MeleeStrokePhase::Unavailable)Reset();
            else anchor_=Anchor{Owner(native),sample};
        }
        else if(!anchor_||!SameOwner(anchor_->owner,Owner(native))||sample.identity!=anchor_->sample.identity||
            sample.motion.hold!=anchor_->sample.motion.hold)Reset();
    }
    // Called at the existing source boundary, before native reads. A real
    // focus/tracking/menu/identity/order loss cannot disappear between Gathers.
    bool SourceContinues(const interaction::HandInteractionSample& source,std::uint64_t inputEpoch)const noexcept {
        if(!anchor_)return true;
        const auto& old=anchor_->sample.motion.source;
        return source.owner==old.owner&&inputEpoch==anchor_->sample.identity.inputEpoch&&source.focused&&
            source.tracked[1]&&!source.released[1]&&source.sequence>=old.sequence&&source.observedNs>=old.observedNs&&
            source.nowNs>=source.observedNs&&source.deadlineNs>source.nowNs&&
            source.deadlineNs-source.observedNs<=100000000&&
            (source.sequence!=old.sequence||SamePacket(source,old));
    }
private:
    struct Anchor {NativeRecognitionOwner owner;interaction::SelectedMeleeSample sample;};
    std::optional<Anchor> anchor_;
    std::optional<interaction::HandInteractionSample> missing_;
    static NativeRecognitionOwner Owner(const Native& n)noexcept{return {n.client,n.links,n.clientEffects};}
    static bool SameOwner(const NativeRecognitionOwner& a,const NativeRecognitionOwner& b)noexcept {
        const auto& x=a.client;const auto& y=b.client;
        if(x.owner!=y.owner||x.config!=y.config||x.inventory!=y.inventory||x.selectedSlot!=y.selectedSlot||
           a.clientEffects!=b.clientEffects)return false;
        for(unsigned n=0;n<2;++n)if(x.branches[n].address!=y.branches[n].address||
            x.branches[n].wrapperOffset!=y.branches[n].wrapperOffset)return false;
        auto normalized=b.links;
        // Same already-proven context-only mask as OwnerCurrent. No pointer,
        // selected item, configuration or inventory flag is normalized.
        normalized.clientSoldierFlags^=std::uint8_t((normalized.clientSoldierFlags^a.links.clientSoldierFlags)&detail::OwnerContextFlags);
        return normalized==a.links;
    }
    static bool ClientsReady(const ReloadStateSnapshot& native)noexcept {
        for(const auto& b:native.branches)if(b.currentState!=2||b.nextState!=2||b.loaded!=-1||b.reserve!=-1||
            !std::isfinite(b.phaseTimer)||b.phaseTimer<0||b.phaseTimer>1000000||(b.flagsA8&8))return false;
        return true;
    }
    static bool SamePacket(const interaction::HandInteractionSample& a,const interaction::HandInteractionSample& b)noexcept {
        return a.owner==b.owner&&a.sequence==b.sequence&&a.observedNs==b.observedNs&&a.deadlineNs==b.deadlineNs&&
            a.focused==b.focused&&a.tracked==b.tracked&&a.released==b.released;
    }
    static bool Physical(const interaction::SelectedMeleeSample& s,const interaction::HandInteraction& hands)noexcept {
        const auto& f=s.motion.source;const auto& token=s.motion.hold;
        const auto held=hands.Current(interaction::InteractionHand::Right);
        return s.selected&&s.identity.inputEpoch&&s.identity.owner==f.owner&&s.identity.item==token.item&&
            s.identity.binding.id&&s.identity.binding.generation&&token.owner==f.owner&&token.id&&
            token.hand==interaction::InteractionHand::Right&&token.kind==interaction::HandClaimKind::GunHold&&
            f.sequence&&f.observedNs>0&&f.nowNs>=f.observedNs&&f.deadlineNs>f.nowNs&&
            f.deadlineNs-f.observedNs<=100000000&&f.focused&&f.tracked[1]&&!f.released[1]&&
            std::isfinite(s.trigger)&&s.trigger>=0&&s.trigger<=.1f&&held&&held->token==token&&
            held->inputSequence==f.sequence&&held->deadlineNs>f.nowNs&&hands.HasRecordedEvidence(f,token.hand);
    }
    bool Continues(const interaction::SelectedMeleeSample& sample,const interaction::HandInteraction& hands)const noexcept {
        if(!anchor_||!Physical(sample,hands)||sample.identity!=anchor_->sample.identity||
           sample.motion.hold!=anchor_->sample.motion.hold)return false;
        const auto& old=anchor_->sample.motion.source;const auto& now=sample.motion.source;
        // Prepare supplies a fresh processing time. SourceAdvanced separately
        // receives the cached immutable packet and may have an older nowNs.
        return SourceContinues(now,sample.identity.inputEpoch)&&now.nowNs>=old.nowNs&&old.deadlineNs>now.nowNs&&
            now.nowNs-old.observedNs<=interaction::MeleeStrokeConfig{}.maximumSampleGapNs&&
            (now.sequence==old.sequence?SamePacket(now,old):now.observedNs>old.observedNs);
    }
};
}
