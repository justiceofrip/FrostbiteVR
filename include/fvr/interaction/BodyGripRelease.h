#pragma once
#include "fvr/interaction/BodyInventory.h"
#include "fvr/interaction/HandInteraction.h"
namespace fvr::interaction {
// Deliberate held-grip -> neutral transition at the SAME current body slot.
// This stores gesture continuity only. It never retains a contact pose, native
// action, hand claim or authorization beyond the original observed deadline.
class BodyGripRelease {
public:
    void Reset()noexcept {contact_.reset();held_=false;}
    std::optional<BodySlotAssignment> Update(const HandInteractionSample& in,HandInteractionKey weapon,
        std::uint64_t revision,std::uint64_t claim,bool held,bool released,bool eligible,
        std::optional<BodySlotAssignment> contact)noexcept {
        const bool fresh=in.owner.actor&&in.owner.actorGeneration&&in.owner.equipGeneration&&in.owner.space&&
            weapon.id&&weapon.generation==in.owner.equipGeneration&&revision&&claim&&in.sequence&&
            in.focused&&in.tracked[0]&&in.tracked[1]&&in.observedNs>0&&in.observedNs<=in.nowNs&&
            in.deadlineNs>in.nowNs&&in.deadlineNs-in.observedNs<=150000000&&in.nowNs>=lastNow_;
        if(!fresh){Reset();return {};}lastNow_=in.nowNs;
        if(owner_!=in.owner||weapon_!=weapon||revision_!=revision||claim_!=claim){
            Reset();owner_=in.owner;weapon_=weapon;revision_=revision;claim_=claim;sequence_=0;
        }
        if(in.sequence<sequence_){Reset();return {};}
        if(in.sequence==sequence_)return {};sequence_=in.sequence;
        if(!eligible){Reset();return {};}
        if(contact_&&(in.observedNs<contact_->observed||in.nowNs>=contact_->deadline))Reset();
        // A strong squeeze establishes held state. Its middle hysteresis band
        // can keep that state while every contact is sampled fresh; it cannot
        // establish a held gesture after a tracking/ownership/reset boundary.
        if(held||(held_&&!released)){held_=true;if(contact)contact_=Contact{*contact,in.observedNs,in.deadlineNs};else contact_.reset();return {};}
        if(!released)return {}; // Analog middle band is not an invented release.
        const auto result=held_&&contact_&&contact&&*contact==contact_->slot?contact:std::nullopt;
        Reset();return result;
    }
private:
    struct Contact {BodySlotAssignment slot;std::int64_t observed=0,deadline=0;};
    std::optional<Contact> contact_;bool held_=false;
    HandInteractionOwner owner_{};HandInteractionKey weapon_{};
    std::uint64_t revision_=0,claim_=0,sequence_=0;std::int64_t lastNow_=0;
};
} // namespace fvr::interaction
