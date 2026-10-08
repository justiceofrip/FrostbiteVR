#pragma once
#include "fvr/interaction/HandInteraction.h"
#include <optional>

namespace fvr::interaction {
// Gesture continuity only: a deliberate press may precede body contact. Every
// sample still proves current tracking/ownership; this never grants a pose,
// native action, slot assignment or visibility receipt from the original press.
enum class BodyGripApproachMode {Held,Empty};
class BodyGripApproach {
public:
    static constexpr std::int64_t WindowNs=600000000,SampleGapNs=150000000;
    void Reset()noexcept {pending_.reset();}
    bool Update(const HandInteractionSample& in,HandInteractionKey weapon,std::uint64_t revision,std::uint64_t claim,
        bool pressed,bool held,bool eligible,bool contact,BodyGripApproachMode mode=BodyGripApproachMode::Held)noexcept {
        const bool fresh=in.owner.actor&&in.owner.actorGeneration&&in.owner.equipGeneration&&in.owner.space&&
            weapon.id&&weapon.generation==in.owner.equipGeneration&&revision&&(claim||mode==BodyGripApproachMode::Empty)&&in.sequence&&
            in.focused&&in.tracked[0]&&in.tracked[1]&&in.observedNs>0&&in.observedNs<=in.nowNs&&in.deadlineNs>in.nowNs&&
            in.deadlineNs-in.observedNs<=SampleGapNs&&in.nowNs>=lastNow_;
        if(!fresh){Reset();return false;}lastNow_=in.nowNs;
        if(owner_!=in.owner||weapon_!=weapon||revision_!=revision||claim_!=claim||mode_!=mode){
            Reset();owner_=in.owner;weapon_=weapon;revision_=revision;claim_=claim;mode_=mode;lastSequence_=0;
        }
        if(!eligible||!held)Reset();
        if(in.sequence<lastSequence_){Reset();return false;}
        if(in.sequence==lastSequence_)return false;lastSequence_=in.sequence;
        if(!eligible||!held)return false;
        if(pending_&&(in.observedNs<pending_->lastObserved||in.observedNs-pending_->lastObserved>SampleGapNs||
            in.nowNs-pending_->started>=WindowNs))Reset();
        if(pressed){Reset();if(!contact)pending_=Pending{in.observedNs,in.observedNs};return false;}
        if(!pending_)return false;
        if(contact){Reset();return true;}
        pending_->lastObserved=in.observedNs;return false;
    }
private:
    struct Pending {std::int64_t started=0,lastObserved=0;};
    std::optional<Pending> pending_;
    BodyGripApproachMode mode_=BodyGripApproachMode::Held;
    HandInteractionOwner owner_{};HandInteractionKey weapon_{};
    std::uint64_t revision_=0,claim_=0,lastSequence_=0;std::int64_t lastNow_=0;
};
}
