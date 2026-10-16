#pragma once
#include "fvr/interaction/MeleeStroke.h"
#include "KnifeRecognitionDiagnostic.h"
#include "fvr/interaction/InputMenuOwnership.h"

namespace fvr::interaction {
// PRIVATE portable candidate. The native adapter must independently prove all
// admissions/receipts; this header grants no native operation or render binding.
enum class KnifePhase:unsigned {NeedNeutral,Stowed,DrawStaged,Drawing,Held,AttackStaged,Attacking,ReturnStaged,Returning,Unknown};
enum class KnifeCommandKind:unsigned {Draw,Attack,Return};
enum class KnifeCommit:unsigned {RejectedNoEffect,Accepted,Unknown};
struct KnifeSample {
    HandInteractionSample source{};
    InputMenuOwnership menu{};
    HandInteractionKey knife{};
    // From the actor's existing shared hand-intent allocator, never a second
    // per-mechanic counter that can collide with another consumer's intents.
    std::uint64_t claimIntent=0;
    std::array<bool,2> gripHeld{},handSupported{};
    std::array<HandContactProof,2> chest{};
    HandContactProof heldContact{};
    // Fresh adapter proofs, not configurable claims or user-facing toggles.
    bool emptyHands=false,fireSuppressed=false,mechanicsSettled=false;
    bool onFoot=false,controlsNeutral=false;
    // A fresh validated controller packet may maintain an existing held item
    // while moving/turning. It grants no new command on its own. Older
    // callers' controlsNeutral is already the stronger admission fact.
    bool holdInputCurrent=false;
    // Fresh, conflict-free locomotion may accompany a deliberate tracked stroke
    // or release-return. This is independently checked at the native boundary.
    bool actionInputCurrent=false;
    bool knifeSelected=false,trackedKnifeOwned=false,nativeAttackReady=false;
    MeleePoint tip{},direction{0,0,1};
};
struct KnifeCommand {
    KnifeCommandKind kind=KnifeCommandKind::Draw;
    std::uint64_t serial=0;
    HandInteractionSample source{};
    HandInteractionKey knife{};
    InteractionHand hand=InteractionHand::Right;
    std::optional<MeleeStrokeIntent> stroke;
    // Chest draw begins from stowed firearm custody. Return must acknowledge
    // hidden original selection + suppression, never expose/claim a firearm.
    bool returnToEmptyHands=true;
};
struct KnifeReceipt {
    KnifeCommandKind kind=KnifeCommandKind::Draw;
    std::uint64_t serial=0,actor=0,actorGeneration=0,space=0;
    HandInteractionKey knife{};
    std::int64_t observedNs=0,deadlineNs=0,nowNs=0;
    // Native callbacks own these facts. Repeated polling alone proves nothing.
    bool coherent=false,originalAmmoUnchanged=false,knifeSentinelUnchanged=false;
    bool selectedKnife=false,attackObserved=false,attackSettled=false,drawDidNotAttack=false,holdGatesOwned=false;
    bool originalSelectedHidden=false,fireSuppressed=false;
    bool attackQuiescentOnly=false; // Known native idle; attack outcome remains unobserved.
};
inline bool KnifeActionInputCurrent(const KnifeSample& s)noexcept {
    return s.controlsNeutral||s.actionInputCurrent;
}
inline bool KnifeCommandInputCurrent(const KnifeCommand& c,const KnifeSample& s)noexcept {
    return c.kind==KnifeCommandKind::Draw?s.controlsNeutral:KnifeActionInputCurrent(s);
}
class ChestKnifeCustody {
public:
    KnifePhase Phase()const noexcept{return phase_;}
    bool NeedsNativeCleanup()const noexcept{return nativeOwned_&&returnWanted_;}
    bool NativeAttackPending()const noexcept{return phase_==KnifePhase::Attacking;}
    const std::optional<HandClaimToken>& Hold()const noexcept{return claim_;}
    bool NeedsInitialHeldPose()const noexcept{return !returnWanted_&&(phase_==KnifePhase::Drawing||(phase_==KnifePhase::Held&&mayAcquireHold_));}
    // Caller first updates the existing shared arbiter with this original input.
    // A native read miss is not an actor-lifetime replacement.
    void ObserveRecognition(KnifeRecognitionDiagnostic* diagnostic)noexcept{recognition_=diagnostic;}
    bool RecognitionReady(const HandInteractionSample& source)const noexcept {
        return phase_==KnifePhase::Held&&!returnWanted_&&motion_.ReadyFor(source);
    }
    std::optional<KnifeCommand> Prepare(const KnifeSample& s,HandInteraction& hands)noexcept {
        KnifeRecognitionRecord record;
        auto* observation=recognition_?&record:nullptr;
        if(observation){record.source=s.source;record.knife=s.knife;record.phaseBefore=unsigned(phase_);record.motion.before=motion_.Phase();
            record.tip=s.tip;record.direction=s.direction;record.claim=claim_?claim_->id:0;
            const auto held=hands.Current(hand_);
            record.prerequisites=unsigned(Fresh(s))|unsigned(GameplayOwnsInput(s.menu,s.source.nowNs))<<1|
                unsigned(s.onFoot)<<2|unsigned(s.controlsNeutral)<<3|unsigned(s.fireSuppressed)<<4|
                unsigned(s.knifeSelected)<<5|unsigned(s.trackedKnifeOwned)<<6|unsigned(s.nativeAttackReady)<<7|
                unsigned(s.mechanicsSettled)<<8|unsigned(s.gripHeld[unsigned(hand_)])<<9|
                unsigned(Contact(s.heldContact,s.source))<<10|unsigned(held&&claim_&&held->token==*claim_)<<11|
                unsigned(returnWanted_)<<12|unsigned(mayAcquireHold_)<<13|unsigned(HoldInputCurrent(s))<<14;
        }
        const auto result=PrepareCore(s,hands,observation);
        if(observation){record.phaseAfter=unsigned(phase_);record.motion.after=motion_.Phase();
            record.command=result?unsigned(result->kind)+1:0;record.recognizerReady=RecognitionReady(s.source);recognition_->Record(record);}
        return result;
    }
private:
    std::optional<KnifeCommand> PrepareCore(const KnifeSample& s,HandInteraction& hands,KnifeRecognitionRecord* observation)noexcept {
        const auto gate=[&](KnifeRecognitionGate value){if(observation)observation->gate=value;};
        const auto& f=s.source;
        if(Fresh(s)&&f.observedNs>observed_&&actor_&&(actor_!=f.owner.actor||actorGeneration_!=f.owner.actorGeneration)){
            // Native adapter separately retires its old lifetime. No request is
            // transplanted to a new actor or reference space by this policy.
            Clear();
        }
        if(!actor_&&Fresh(s)){actor_=f.owner.actor;actorGeneration_=f.owner.actorGeneration;space_=f.owner.space;}
        if(Fresh(s)&&space_!=f.owner.space){
            space_=f.owner.space;motion_.Invalidate();armed_=false;returnWanted_|=nativeOwned_;
            if(staged_){staged_.reset();phase_=nativeOwned_?KnifePhase::Held:KnifePhase::NeedNeutral;}
            // A recenter invalidates poses/claims, never a committed attack.
            // Any later return uses a new, neutral source in the new space.
        }
        if(phase_==KnifePhase::Unknown){gate(KnifeRecognitionGate::Unknown);return {};}
        // A fresh original release remains meaningful even if this callback's
        // native/suppression observation is unavailable. Default missing fields
        // have no source identity and cannot enter this path.
        if(nativeOwned_&&Fresh(s)&&f.owner.actor==actor_&&f.owner.actorGeneration==actorGeneration_&&
           f.sequence>sequence_&&f.observedNs>observed_&&!s.gripHeld[unsigned(hand_)]&&f.released[unsigned(hand_)]){
            returnWanted_=true;motion_.Invalidate();
        }
        if(!Fresh(s)||f.owner.actor!=actor_||f.owner.actorGeneration!=actorGeneration_||
           !GameplayOwnsInput(s.menu,f.nowNs)||!s.onFoot||!HoldInputCurrent(s)||!s.fireSuppressed){
            // Missing controller/native observations suspend action authority;
            // they do not prove a grip release. In particular, expected native
            // selection convergence must not cancel an accepted draw's first
            // fresh attachment/hold. Original command/claim leases stay intact.
            motion_.Invalidate();armed_=false;
            if(staged_){staged_.reset();phase_=nativeOwned_?KnifePhase::Held:KnifePhase::NeedNeutral;}
            if(phase_==KnifePhase::Stowed)phase_=KnifePhase::NeedNeutral;
            gate(KnifeRecognitionGate::Admission);return {};
        }
        if(menuEpoch_&&menuEpoch_!=s.menu.epoch){
            motion_.Invalidate();armed_=false;returnWanted_|=nativeOwned_;
            if(staged_){staged_.reset();phase_=nativeOwned_?KnifePhase::Held:KnifePhase::NeedNeutral;}
            if(phase_==KnifePhase::Stowed)phase_=KnifePhase::NeedNeutral;
        }
        menuEpoch_=s.menu.epoch;
        // Conflicting input discards partial motion before duplicate exit.
        // Locomotion itself is not a physical stroke and does not reset one.
        if(!KnifeActionInputCurrent(s)){motion_.Invalidate();armed_=false;
            if(phase_==KnifePhase::Stowed)phase_=KnifePhase::NeedNeutral;}
        if(f.sequence==sequence_){gate(KnifeRecognitionGate::Duplicate);return {};} // No duplicate observation progress.
        if(f.sequence<sequence_||f.observedNs<=observed_){motion_.Invalidate();armed_=false;returnWanted_|=nativeOwned_;gate(KnifeRecognitionGate::SourceOrder);return {};}
        if(staged_){
            // A missed native boundary cannot leave an uncommittable command
            // parked forever or restamp it onto this newer original packet.
            staged_.reset();motion_.Invalidate();armed_=false;
            phase_=nativeOwned_?KnifePhase::Held:KnifePhase::NeedNeutral;
            returnWanted_|=nativeOwned_;
        }
        sequence_=f.sequence;observed_=f.observedNs;
        const bool neutral=!s.gripHeld[0]&&!s.gripHeld[1]&&f.released[0]&&f.released[1];
        if(phase_==KnifePhase::NeedNeutral){
            if(claim_)Release(s,hands);
            if(s.controlsNeutral&&neutral&&!hands.Current(InteractionHand::Left)&&!hands.Current(InteractionHand::Right)&&s.emptyHands&&s.fireSuppressed){phase_=KnifePhase::Stowed;armed_=true;}
            return {};
        }
        if(phase_==KnifePhase::Stowed){
            if(s.controlsNeutral&&neutral){armed_=true;return {};}
            const bool eligible=s.controlsNeutral&&armed_&&s.emptyHands&&s.fireSuppressed&&s.mechanicsSettled&&
                !hands.Current(InteractionHand::Left)&&!hands.Current(InteractionHand::Right)&&s.knife.id&&s.knife.generation;
            armed_=false;
            if(!eligible||s.gripHeld[0]==s.gripHeld[1])return {};
            const unsigned h=s.gripHeld[0]?0:1;
            if(!s.handSupported[h]||!Contact(s.chest[h],f))return {};
            hand_=static_cast<InteractionHand>(h);knife_=s.knife;
            auto acquired=hands.Acquire(f,{f.owner,hand_,HandClaimKind::BodyInventory,knife_,s.chest[h],s.claimIntent,0});
            if(!acquired.claim)return {};claim_=acquired.claim->token;
            return Stage(KnifeCommandKind::Draw,s);
        }
        if(!s.gripHeld[unsigned(hand_)]){returnWanted_=true;motion_.Invalidate();}
        if(phase_==KnifePhase::Attacking){
            // Stock windup/recovery outlasts a hand lease. Retain only this
            // existing GunHold from each fresh original contact; native busy
            // is not a tracking failure. No acquire, motion or new action.
            if(!returnWanted_){
                const auto held=hands.Current(hand_);
                if(!claim_||claim_->kind!=HandClaimKind::GunHold||!held||held->token!=*claim_)
                    returnWanted_=true;
                else if(s.knifeSelected&&s.trackedKnifeOwned&&s.knife==knife_&&Contact(s.heldContact,f)&&
                        !hands.Renew(f,*claim_,s.heldContact).claim)returnWanted_=true;
            }
            return {};
        }
        if(phase_==KnifePhase::Drawing||phase_==KnifePhase::Returning||
           phase_==KnifePhase::DrawStaged||phase_==KnifePhase::AttackStaged||phase_==KnifePhase::ReturnStaged)return {};
        if(phase_!=KnifePhase::Held){gate(KnifeRecognitionGate::Phase);return {};}
        if(returnWanted_){
            gate(KnifeRecognitionGate::ReturnWanted);
            if(KnifeActionInputCurrent(s)&&!s.gripHeld[unsigned(hand_)]&&f.released[unsigned(hand_)]&&s.knifeSelected&&s.mechanicsSettled){Release(s,hands);return Stage(KnifeCommandKind::Return,s);}
            return {};
        }
        if(!s.knifeSelected||!s.trackedKnifeOwned||s.knife!=knife_||!Contact(s.heldContact,f)){motion_.Invalidate();gate(KnifeRecognitionGate::HeldProof);return {};}
        auto held=hands.Current(hand_);
        if(mayAcquireHold_&&held&&claim_&&held->token==*claim_&&claim_->kind==HandClaimKind::BodyInventory){
            // Draw completion can arrive before an equipment-epoch change.
            // Convert only our own surviving body claim, never another owner.
            const auto transferred=hands.Transfer(f,*claim_,{f.owner,hand_,HandClaimKind::GunHold,knife_,s.heldContact,s.claimIntent,0});
            if(!transferred.claim){returnWanted_=true;motion_.Invalidate();gate(KnifeRecognitionGate::ClaimTransfer);return {};}
            claim_=transferred.claim->token;held=transferred.claim;mayAcquireHold_=false;
        }
        if(!held){
            // Only the first acknowledged draw may mint its new equipment hold.
            // A lost held claim can never be recreated by a still-held grip.
            if(!mayAcquireHold_){returnWanted_=true;motion_.Invalidate();gate(KnifeRecognitionGate::ClaimLost);return {};}
            auto acquired=hands.Acquire(f,{f.owner,hand_,HandClaimKind::GunHold,knife_,s.heldContact,s.claimIntent,0});
            if(!acquired.claim){returnWanted_=true;motion_.Invalidate();gate(KnifeRecognitionGate::ClaimAcquire);return {};}
            claim_=acquired.claim->token;held=acquired.claim;mayAcquireHold_=false;
        }else if(!claim_||held->token!=*claim_){returnWanted_=true;motion_.Invalidate();gate(KnifeRecognitionGate::ClaimForeign);return {};}
        const auto renewed=hands.Renew(f,*claim_,s.heldContact);
        if(!renewed.claim){returnWanted_=true;motion_.Invalidate();gate(KnifeRecognitionGate::ClaimRenew);return {};}
        if(!KnifeActionInputCurrent(s)){gate(KnifeRecognitionGate::Admission);return {};}
        MeleeStrokeSample stroke{f,*claim_,s.tip,s.direction,s.nativeAttackReady&&s.mechanicsSettled};
        gate(KnifeRecognitionGate::Motion);
        const auto motion=motion_.Update(stroke,hands,observation?&observation->motion:nullptr);
        if(!motion.intent)return {};
        auto command=Stage(KnifeCommandKind::Attack,s);command->stroke=motion.intent;staged_=command;return command;
    }
public:
    bool CanCommit(const KnifeCommand& c,const KnifeSample& s,const HandInteraction& hands)const noexcept {
        if(!staged_||!SameCommand(*staged_,c)||c.knife!=knife_||s.knife!=knife_||!Fresh(s)||!s.fireSuppressed||
           !GameplayOwnsInput(s.menu,s.source.nowNs)||s.menu.epoch!=menuEpoch_||!s.onFoot||!KnifeCommandInputCurrent(c,s)||
           c.source.owner!=s.source.owner||c.source.sequence!=s.source.sequence||
           c.source.observedNs!=s.source.observedNs||c.source.deadlineNs!=s.source.deadlineNs||!s.mechanicsSettled)return false;
        if(c.kind==KnifeCommandKind::Return)return nativeOwned_&&returnWanted_&&s.knifeSelected&&!s.gripHeld[unsigned(hand_)]&&s.source.released[unsigned(hand_)];
        if(!s.gripHeld[unsigned(hand_)]||s.source.released[unsigned(hand_)])return false;
        const auto held=hands.Current(hand_);if(!held||!claim_||held->token!=*claim_||held->inputSequence!=s.source.sequence||held->deadlineNs<=s.source.nowNs)return false;
        if(c.kind==KnifeCommandKind::Draw)return s.emptyHands&&s.fireSuppressed&&held->token.kind==HandClaimKind::BodyInventory;
        return s.knifeSelected&&s.trackedKnifeOwned&&s.nativeAttackReady&&held->token.kind==HandClaimKind::GunHold&&c.stroke.has_value();
    }
    // Adapter calls CanCommit immediately before its verified native boundary;
    // this method records that boundary's result, never issues or retries it.
    void Committed(const KnifeCommand& c,KnifeCommit result)noexcept {
        if(!staged_||!SameCommand(*staged_,c))return;
        staged_.reset();motion_.Invalidate();
        if(result==KnifeCommit::Unknown){phase_=KnifePhase::Unknown;returnWanted_=nativeOwned_=true;return;}
        if(result==KnifeCommit::RejectedNoEffect){phase_=nativeOwned_?KnifePhase::Held:KnifePhase::NeedNeutral;returnWanted_|=nativeOwned_;return;}
        pending_=c;nativeOwned_=true;
        phase_=c.kind==KnifeCommandKind::Draw?KnifePhase::Drawing:c.kind==KnifeCommandKind::Attack?KnifePhase::Attacking:KnifePhase::Returning;
    }
    bool Observe(const KnifeReceipt& r)noexcept {
        if(phase_==KnifePhase::Unknown||!pending_||r.serial!=pending_->serial||r.kind!=pending_->kind||r.actor!=actor_||r.actorGeneration!=actorGeneration_||
           r.space!=pending_->source.owner.space||r.knife!=knife_||r.observedNs<=0||r.nowNs<r.observedNs||r.deadlineNs<=r.nowNs||
           r.deadlineNs-r.observedNs>100000000||r.observedNs<pending_->source.observedNs)return false;
        if(!r.coherent||!r.originalAmmoUnchanged||!r.knifeSentinelUnchanged){phase_=KnifePhase::Unknown;returnWanted_=true;return false;}
        if(r.kind==KnifeCommandKind::Attack&&r.attackQuiescentOnly&&!r.attackObserved&&
           r.attackSettled&&r.selectedKnife&&r.holdGatesOwned){
            pending_.reset();motion_.Invalidate();phase_=KnifePhase::Held;returnWanted_=true;armed_=false;
            return true; // Cleanup only. No further attack or held-input rearm.
        }
        const bool done=r.kind==KnifeCommandKind::Draw?r.selectedKnife&&r.drawDidNotAttack&&r.holdGatesOwned:
            r.kind==KnifeCommandKind::Attack?r.selectedKnife&&r.attackObserved&&r.attackSettled&&r.holdGatesOwned:r.originalSelectedHidden&&r.fireSuppressed;
        if(!done)return false;
        pending_.reset();motion_.Invalidate();
        if(r.kind==KnifeCommandKind::Return){nativeOwned_=returnWanted_=false;phase_=KnifePhase::NeedNeutral;armed_=false;}
        else {phase_=KnifePhase::Held;if(r.kind==KnifeCommandKind::Draw)mayAcquireHold_=true;}
        return true;
    }
private:
    static bool SameCommand(const KnifeCommand& a,const KnifeCommand& b)noexcept {
        if(a.kind!=b.kind||a.serial!=b.serial||a.knife!=b.knife||a.hand!=b.hand||a.returnToEmptyHands!=b.returnToEmptyHands||
           a.source.owner!=b.source.owner||a.source.sequence!=b.source.sequence||a.source.observedNs!=b.source.observedNs||
           a.source.deadlineNs!=b.source.deadlineNs||a.source.nowNs!=b.source.nowNs||a.source.focused!=b.source.focused||
           a.source.tracked!=b.source.tracked||a.source.released!=b.source.released||a.stroke.has_value()!=b.stroke.has_value())return false;
        if(!a.stroke)return true;const auto& x=*a.stroke;const auto& y=*b.stroke;
        return x.serial==y.serial&&x.hold==y.hold&&x.sourceSequence==y.sourceSequence&&x.observedNs==y.observedNs&&
            x.deadlineNs==y.deadlineNs&&x.start==y.start&&x.end==y.end&&x.direction==y.direction;
    }
    static bool HoldInputCurrent(const KnifeSample& s)noexcept {
        return s.controlsNeutral||s.holdInputCurrent;
    }
    static bool Fresh(const KnifeSample& s)noexcept {
        const auto& f=s.source;return f.owner.actor&&f.owner.actorGeneration&&f.owner.equipGeneration&&f.owner.space&&f.sequence&&
            f.focused&&f.tracked[0]&&f.tracked[1]&&f.observedNs>0&&f.nowNs>=f.observedNs&&f.deadlineNs>f.nowNs&&
            f.deadlineNs-f.observedNs<=100000000;
    }
    static bool Contact(const HandContactProof& p,const HandInteractionSample& f)noexcept {
        return p.eligible&&p.key.id&&p.key.generation&&p.inputSequence==f.sequence&&p.deadlineNs>f.nowNs&&p.deadlineNs<=f.deadlineNs;
    }
    std::optional<KnifeCommand> Stage(KnifeCommandKind kind,const KnifeSample& s)noexcept {
        if(serial_==UINT64_MAX){phase_=KnifePhase::Unknown;return {};}
        staged_=KnifeCommand{kind,++serial_,s.source,knife_,hand_,{},true};
        phase_=kind==KnifeCommandKind::Draw?KnifePhase::DrawStaged:kind==KnifeCommandKind::Attack?KnifePhase::AttackStaged:KnifePhase::ReturnStaged;
        return staged_;
    }
    void Release(const KnifeSample& s,HandInteraction& hands)noexcept {
        if(claim_){const auto held=hands.Current(claim_->hand);if(held&&held->token==*claim_)hands.Release(s.source,*claim_);claim_.reset();}
    }
    void Clear()noexcept {
        phase_=KnifePhase::NeedNeutral;actor_=actorGeneration_=space_=sequence_=menuEpoch_=0;observed_=0;knife_={};claim_.reset();
        staged_.reset();pending_.reset();motion_.Invalidate();nativeOwned_=returnWanted_=mayAcquireHold_=armed_=false;
    }
    KnifeRecognitionDiagnostic* recognition_=nullptr;
    KnifePhase phase_=KnifePhase::NeedNeutral;InteractionHand hand_=InteractionHand::Right;HandInteractionKey knife_{};
    std::optional<HandClaimToken> claim_;std::optional<KnifeCommand> staged_,pending_;MeleeStroke motion_;
    std::uint64_t actor_=0,actorGeneration_=0,space_=0,sequence_=0,menuEpoch_=0,serial_=0;
    std::int64_t observed_=0;bool nativeOwned_=false,returnWanted_=false,mayAcquireHold_=false,armed_=false;
};
}
