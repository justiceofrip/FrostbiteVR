#pragma once
#include "ChestKnifeCustody.h"
#include <array>
#include <optional>
namespace fvr::bc2::private_knife {
// Serialized by the native adapter. One semantic command, at most one chooser
// override per engine inventory. First override requires its ORIGINAL source;
// accepted sibling completion has native lifetime authority only, never input.
// Gather-only identity comparison; native callbacks receive only the atomic
// epoch. Processing time alone never invalidates or renews source authority.
class OriginalKnifeSource {
public:
    bool Advance(const interaction::HandInteractionSample& value)noexcept {
        const bool changed=!last_||last_->owner!=value.owner||last_->sequence!=value.sequence||
            last_->observedNs!=value.observedNs||last_->deadlineNs!=value.deadlineNs||
            last_->focused!=value.focused||last_->tracked!=value.tracked||last_->released!=value.released;
        last_=value;return changed;
    }
private:std::optional<interaction::HandInteractionSample> last_;
};
class DrawReturnGate {
public:
    using Command=interaction::KnifeCommand;
    bool Stage(const Command& c,std::int64_t now)noexcept {
        if(pending_||!c.serial||c.serial<=lastSerial_||c.kind==interaction::KnifeCommandKind::Attack||
           !c.returnToEmptyHands||!Fresh(c,now))return false;
        pending_=c;lastSerial_=c.serial;mask_=0;acceptedNs_=0;delivered_=false;return true;
    }
    // Called before portable Prepare sees a new source. If no chooser consumed
    // the stage it is atomically revoked; an old callback can never replay it.
    std::optional<interaction::KnifeCommit> ResolveBeforeSource(std::uint64_t sequence,std::int64_t observed,std::int64_t now)noexcept {
        if(!pending_||delivered_)return {};
        if(mask_){delivered_=true;return interaction::KnifeCommit::Accepted;}
        if(sequence!=pending_->source.sequence||observed!=pending_->source.observedNs||!Fresh(*pending_,now)){
            pending_.reset();return interaction::KnifeCommit::RejectedNoEffect;
        }return {};
    }
    bool Choose(unsigned branch,std::int64_t now,bool originalSourceCurrent)noexcept {
        if(!pending_||branch>=2||(mask_&(1u<<branch))||now<=0||
           (!mask_&&(!originalSourceCurrent||!Fresh(*pending_,now))))return false;
        if(!mask_)acceptedNs_=now;mask_|=1u<<branch;return true;
    }
    bool Complete(std::uint64_t serial)noexcept {
        if(!pending_||pending_->serial!=serial||mask_!=3||!delivered_)return false;
        pending_.reset();return true;
    }
    // A known actor/inventory replacement retires the original command. This
    // never manufactures RejectedNoEffect for a partially consumed operation.
    void LifetimeEnded()noexcept {pending_.reset();mask_=0;delivered_=false;acceptedNs_=0;}
    const std::optional<Command>& Pending()const noexcept{return pending_;}
    unsigned Mask()const noexcept{return mask_;}
    bool Accepted()const noexcept{return mask_!=0;}
    std::int64_t AcceptedNs()const noexcept{return acceptedNs_;}
    static bool Fresh(const Command& c,std::int64_t now)noexcept {
        const auto& s=c.source;
        return s.owner.actor&&s.owner.actorGeneration&&s.owner.equipGeneration&&s.owner.space&&s.sequence&&
            s.focused&&s.tracked[0]&&s.tracked[1]&&s.observedNs>0&&now>=s.observedNs&&s.deadlineNs>now&&
            s.deadlineNs-s.observedNs<=100000000&&c.knife.id&&c.knife.generation;
    }
private:
    std::optional<Command> pending_;std::uint64_t lastSerial_=0;
    unsigned mask_=0;std::int64_t acceptedNs_=0;bool delivered_=false;
};
}
