#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <vector>

namespace fvr::interaction {
// Physical inventory identity, not an equipment-array index or a weapon name.
// Generation must change after removal/replacement, including pointer reuse.
struct BodyItemKey {
    std::uint64_t id=0,generation=0;
    bool operator==(const BodyItemKey&)const=default;
};
struct BodyInventoryOwner {
    std::uint64_t actor=0,generation=0,space=0;
    bool operator==(const BodyInventoryOwner&)const=default;
};
using BodySlotId=std::uint32_t; // Zero is unassigned; geometry belongs to adapter.
struct BodyInventoryItem {
    BodyItemKey key;
    // Adapter derives priorities/preferences from verified item category and
    // user preference (e.g. shotgun on back), never a hard-coded asset name.
    std::array<BodySlotId,4> preferredSlots{};
    std::uint8_t preferenceCount=0;
    std::uint16_t priority=0; // Higher priority receives contested slots first.
    bool operator==(const BodyInventoryItem&)const=default;
};
enum class BodyPresentation:std::uint8_t {Unknown,WeaponVisible,EmptyHands};
struct BodyInventorySnapshot {
    BodyInventoryOwner owner;
    std::uint64_t revision=0,sequence=0;
    std::int64_t observedNs=0;
    std::span<const BodyInventoryItem> items;
    std::optional<BodyItemKey> selected; // May remain selected while hidden.
    BodyPresentation presentation=BodyPresentation::Unknown;
    // EmptyHands requires verified hidden weapon AND native fire suppression.
    // An engine lacking this binding must report Unknown, not invent an ack.
    bool fireSuppressed=false;
};
enum class BodyInventoryOperation:std::uint8_t {None,Holster,Draw};
struct BodyInventoryIntent {
    std::uint64_t serial=0;
    BodyInventoryOperation operation=BodyInventoryOperation::None;
    BodySlotId slot=0;
    BodyItemKey item; // Exact item sampled at body contact; never resolve late.
};
struct BodyInventoryRequest {
    std::uint64_t id=0,revision=0,sourceSequence=0;
    BodyInventoryOwner owner;
    BodyInventoryOperation operation=BodyInventoryOperation::None;
    BodySlotId slot=0;
    BodyItemKey item;
};
struct BodyInventoryAcknowledgement {
    std::uint64_t request=0,revision=0;
    BodyInventoryOwner owner;
    BodyInventoryOperation operation=BodyInventoryOperation::None;
    BodyItemKey item;
    bool accepted=false;
};
struct BodyInventorySample {
    BodyInventorySnapshot inventory;
    std::int64_t nowNs=0;
    bool focused=false,handTracked=false;
    BodyInventoryIntent intent;
    std::optional<BodyInventoryAcknowledgement> acknowledgement;
};
enum class BodyInventoryPhase:std::uint8_t {Unavailable,Held,Empty,AwaitingHolster,AwaitingDraw};
enum class BodyInventoryReason:std::uint8_t {
    None,InvalidSnapshot,StaleSnapshot,IdentityChanged,InventoryChanged,
    InputUnavailable,ClockReversed,Timeout,NativeRejected,SelectionChanged,
    StaleIntent,UnassignedSlot,WrongItem,UnavailablePresentation,RequestExhausted
};
struct BodySlotAssignment {
    BodySlotId slot=0;BodyItemKey item;
    bool operator==(const BodySlotAssignment&)const=default;
};
struct BodyInventoryResult {
    BodyInventoryPhase phase=BodyInventoryPhase::Unavailable;
    std::optional<BodyItemKey> held;
    bool emptyHands=false,blockFire=true;
    std::vector<BodySlotAssignment> slots;
    std::optional<BodyInventoryRequest> request;
    std::uint64_t cancelledRequest=0,committedRequest=0;
    BodyInventoryReason reason=BodyInventoryReason::None;
};
struct BodyInventoryConfig {
    std::int64_t acknowledgementTimeoutNs=1500000000,maxSnapshotAgeNs=150000000;
};

// No equip calls, native offsets, render hiding, input injection, spatial reach
// tests or item creation. A command is emitted once, and remains uncommitted until
// an exact later acknowledgement plus authoritative selected/presentation state.
class BodyInventory {
public:
    explicit BodyInventory(BodyInventoryConfig config={}):config_(config){}
    BodyInventoryResult Update(const BodyInventorySample& sample) {
        BodyInventoryResult out;
        const auto& s=sample.inventory;
        const auto finish=[&](){
            out.phase=phase_;out.held=held_;out.emptyHands=empty_;
            out.slots=slots_;
            if(out.reason==BodyInventoryReason::InvalidSnapshot||out.reason==BodyInventoryReason::StaleSnapshot||
               out.reason==BodyInventoryReason::ClockReversed)out.slots.clear();
            out.blockFire=phase_!=BodyInventoryPhase::Held||!sample.focused||!sample.handTracked||
                out.cancelledRequest!=0||out.reason==BodyInventoryReason::InvalidSnapshot||
                out.reason==BodyInventoryReason::StaleSnapshot||out.reason==BodyInventoryReason::ClockReversed;
            return out;
        };
        const auto cancel=[&](BodyInventoryReason reason){
            if(pending_){out.cancelledRequest=pending_->id;pending_.reset();}
            armed_=false;out.reason=reason;
        };
        if(config_.acknowledgementTimeoutNs<=0||config_.maxSnapshotAgeNs<=0||!Valid(s)){
            cancel(BodyInventoryReason::InvalidSnapshot);Unavailable();return finish();
        }
        if(sample.nowNs<0||s.observedNs>sample.nowNs||sample.nowNs-s.observedNs>config_.maxSnapshotAgeNs){
            cancel(BodyInventoryReason::StaleSnapshot);Unavailable();return finish();
        }
        if(initialized_&&sample.nowNs<lastNow_){
            cancel(BodyInventoryReason::ClockReversed);Unavailable();return finish();
        }
        lastNow_=sample.nowNs;
        const bool newOwner=!initialized_||s.owner!=owner_;
        if(newOwner){
            // Space epochs retire interactions, not independently proven physical
            // item assignments. Assign still rejects every absent/changed key.
            const bool sameActor=initialized_&&s.owner.actor==owner_.actor&&s.owner.generation==owner_.generation;
            cancel(BodyInventoryReason::IdentityChanged);
            owner_=s.owner;revision_=s.revision;lastSequence_=0;lastIntent_=sample.intent.serial;
            items_.clear();if(!sameActor)slots_.clear();initialized_=true;
        }
        if(s.sequence<lastSequence_){
            cancel(BodyInventoryReason::StaleSnapshot);Unavailable();return finish();
        }
        if(!newOwner&&s.revision<revision_){
            cancel(BodyInventoryReason::StaleSnapshot);Unavailable();return finish();
        }
        const bool changed=revision_!=s.revision;
        const bool sameItems=std::equal(items_.begin(),items_.end(),s.items.begin(),s.items.end());
        if(!newOwner&&!changed&&!sameItems){
            cancel(BodyInventoryReason::InvalidSnapshot);Unavailable();return finish();
        }
        // An observation sequence identifies an immutable complete snapshot.
        // Duplicate polls can timeout/cancel, never issue or commit commands.
        if(s.sequence==lastSequence_){
            if(changed||s.observedNs!=lastObserved_||s.selected!=lastSelected_||
               s.presentation!=lastPresentation_||s.fireSuppressed!=lastSuppressed_){
                cancel(BodyInventoryReason::InvalidSnapshot);Unavailable();return finish();
            }
            if(!sample.focused||!sample.handTracked){cancel(BodyInventoryReason::InputUnavailable);Adopt(s);}
            else if(pending_&&sample.nowNs-startedNs_>=config_.acknowledgementTimeoutNs){cancel(BodyInventoryReason::Timeout);Adopt(s);}
            return finish();
        }
        if(changed){cancel(BodyInventoryReason::InventoryChanged);revision_=s.revision;
            lastIntent_=std::max(lastIntent_,sample.intent.serial);}
        if(newOwner||changed||slots_.empty()){Assign(s.items);items_.assign(s.items.begin(),s.items.end());}
        lastSequence_=s.sequence;lastObserved_=s.observedNs;lastSelected_=s.selected;
        lastPresentation_=s.presentation;lastSuppressed_=s.fireSuppressed;
        if(!sample.focused||!sample.handTracked){cancel(BodyInventoryReason::InputUnavailable);Adopt(s);return finish();}
        if(pending_){
            lastIntent_=std::max(lastIntent_,sample.intent.serial);
            if(sample.nowNs-startedNs_>=config_.acknowledgementTimeoutNs){cancel(BodyInventoryReason::Timeout);Adopt(s);return finish();}
            const bool allowedSelection=s.selected==beforeSelected_||s.selected==std::optional(pending_->item)||
                (pending_->operation==BodyInventoryOperation::Holster&&!s.selected);
            if(!allowedSelection){cancel(BodyInventoryReason::SelectionChanged);Adopt(s);return finish();}
            if(sample.acknowledgement){
                const auto& ack=*sample.acknowledgement;const auto command=*pending_;
                const bool exact=ack.request==command.id&&ack.owner==command.owner&&ack.revision==command.revision&&
                    ack.operation==command.operation&&ack.item==command.item&&s.sequence>command.sourceSequence;
                if(exact&&!ack.accepted){cancel(BodyInventoryReason::NativeRejected);Adopt(s);return finish();}
                const bool confirmed=command.operation==BodyInventoryOperation::Holster?
                    s.presentation==BodyPresentation::EmptyHands&&s.fireSuppressed:
                    s.presentation==BodyPresentation::WeaponVisible&&s.selected==std::optional(command.item);
                if(exact&&ack.accepted&&confirmed){
                    out.committedRequest=command.id;pending_.reset();armed_=false;Adopt(s);return finish();
                }
            }
            return finish();
        }
        Adopt(s);
        if(newOwner||changed){armed_=sample.intent.operation==BodyInventoryOperation::None;return finish();}
        if(sample.intent.operation==BodyInventoryOperation::None){armed_=true;return finish();}
        const auto intent=sample.intent;
        if(!armed_||!intent.serial||intent.serial<=lastIntent_){out.reason=BodyInventoryReason::StaleIntent;return finish();}
        lastIntent_=intent.serial;armed_=false;
        if(intent.operation!=BodyInventoryOperation::Holster&&intent.operation!=BodyInventoryOperation::Draw){
            out.reason=BodyInventoryReason::StaleIntent;return finish();
        }
        if(phase_!=BodyInventoryPhase::Held&&phase_!=BodyInventoryPhase::Empty){
            out.reason=BodyInventoryReason::UnavailablePresentation;return finish();
        }
        const auto assigned=std::find_if(slots_.begin(),slots_.end(),[&](const auto& a){return a.slot==intent.slot;});
        if(!intent.slot||assigned==slots_.end()){out.reason=BodyInventoryReason::UnassignedSlot;return finish();}
        if(assigned->item!=intent.item||(intent.operation==BodyInventoryOperation::Holster&&held_!=std::optional(intent.item))||
           (intent.operation==BodyInventoryOperation::Draw&&held_==std::optional(intent.item))){
            out.reason=BodyInventoryReason::WrongItem;return finish();
        }
        if(nextRequest_==std::numeric_limits<std::uint64_t>::max()){
            out.reason=BodyInventoryReason::RequestExhausted;return finish();
        }
        BodyInventoryRequest command{++nextRequest_,s.revision,s.sequence,s.owner,intent.operation,intent.slot,intent.item};
        pending_=command;beforeSelected_=s.selected;startedNs_=sample.nowNs;out.request=command;
        phase_=intent.operation==BodyInventoryOperation::Holster?BodyInventoryPhase::AwaitingHolster:BodyInventoryPhase::AwaitingDraw;
        return finish();
    }
private:
    static bool Key(BodyItemKey k){return k.id&&k.generation;}
    static bool Valid(const BodyInventorySnapshot& s){
        if(!s.owner.actor||!s.owner.generation||!s.owner.space||!s.revision||!s.sequence||s.observedNs<0||s.items.size()>32)return false;
        for(std::size_t n=0;n<s.items.size();++n){
            const auto& item=s.items[n];if(!Key(item.key)||item.preferenceCount>item.preferredSlots.size())return false;
            for(std::size_t j=0;j<n;++j)if(s.items[j].key.id==item.key.id)return false;
            for(unsigned k=0;k<item.preferenceCount;++k){if(!item.preferredSlots[k])return false;
                for(unsigned j=0;j<k;++j)if(item.preferredSlots[j]==item.preferredSlots[k])return false;}
        }
        if(s.selected&&(!Key(*s.selected)||std::none_of(s.items.begin(),s.items.end(),[&](const auto& i){return i.key==*s.selected;})))return false;
        if(s.presentation!=BodyPresentation::Unknown&&s.presentation!=BodyPresentation::WeaponVisible&&s.presentation!=BodyPresentation::EmptyHands)return false;
        return (s.presentation!=BodyPresentation::WeaponVisible||s.selected.has_value())&&
            (s.presentation!=BodyPresentation::EmptyHands||s.fireSuppressed);
    }
    void Assign(std::span<const BodyInventoryItem> items){
        std::vector<BodyInventoryItem> order(items.begin(),items.end());
        std::sort(order.begin(),order.end(),[](const auto& a,const auto& b){
            if(a.priority!=b.priority)return a.priority>b.priority;
            return a.key.id<b.key.id;
        });
        std::vector<BodySlotAssignment> next;
        const auto available=[&](BodySlotId slot){return slot&&std::none_of(next.begin(),next.end(),[&](const auto& a){return a.slot==slot;});};
        // Reconcile surviving physical items before placing replacements. A new
        // high-priority item must not steal an unchanged item's other shoulder.
        for(const auto& prior:slots_){
            const auto item=std::find_if(order.begin(),order.end(),[&](const auto& i){return i.key==prior.item;});
            if(item!=order.end()&&available(prior.slot)&&
               std::find(item->preferredSlots.begin(),item->preferredSlots.begin()+item->preferenceCount,prior.slot)!=item->preferredSlots.begin()+item->preferenceCount)
                next.push_back(prior);
        }
        for(const auto& item:order){
            if(std::any_of(next.begin(),next.end(),[&](const auto& a){return a.item==item.key;}))continue;
            BodySlotId slot=0;
            const auto prior=std::find_if(slots_.begin(),slots_.end(),[&](const auto& a){return a.item==item.key;});
            if(prior!=slots_.end()&&available(prior->slot)&&
               std::find(item.preferredSlots.begin(),item.preferredSlots.begin()+item.preferenceCount,prior->slot)!=item.preferredSlots.begin()+item.preferenceCount)slot=prior->slot;
            if(!slot)for(unsigned p=0;p<item.preferenceCount;++p)if(available(item.preferredSlots[p])){slot=item.preferredSlots[p];break;}
            if(slot)next.push_back({slot,item.key});
        }
        slots_=std::move(next);
    }
    void Unavailable(){phase_=BodyInventoryPhase::Unavailable;held_.reset();empty_=false;}
    void Adopt(const BodyInventorySnapshot& s){
        if(s.presentation==BodyPresentation::WeaponVisible&&s.selected){phase_=BodyInventoryPhase::Held;held_=s.selected;empty_=false;}
        else if(s.presentation==BodyPresentation::EmptyHands&&s.fireSuppressed){phase_=BodyInventoryPhase::Empty;held_.reset();empty_=true;}
        else Unavailable();
    }
    BodyInventoryConfig config_;
    BodyInventoryOwner owner_{};
    std::uint64_t revision_=0,lastSequence_=0,nextRequest_=0,lastIntent_=0;
    std::int64_t lastNow_=0,lastObserved_=0,startedNs_=0;
    bool initialized_=false,armed_=false,empty_=false,lastSuppressed_=false;
    BodyPresentation lastPresentation_=BodyPresentation::Unknown;
    BodyInventoryPhase phase_=BodyInventoryPhase::Unavailable;
    std::vector<BodyInventoryItem> items_;
    std::vector<BodySlotAssignment> slots_;
    std::optional<BodyItemKey> held_,lastSelected_,beforeSelected_;
    std::optional<BodyInventoryRequest> pending_;
};
}
