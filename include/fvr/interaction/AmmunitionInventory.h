#pragma once
#include "fvr/interaction/AmmunitionLedger.h"
#include <array>

namespace fvr::interaction {
// One consumed physical event, not a continuously held button. The adapter
// supplies its original time/context after validating hand and rail evidence.
struct AmmunitionIntent {
    AmmoResourceContext context{};
    std::uint64_t id=0;
    std::int64_t observedNs=0,deadlineNs=0;
    AmmunitionOperation operation{};
    std::optional<RemovedMagazineResource> original;
    bool operator==(const AmmunitionIntent&)const=default;
};

// Serialized policy, owned by the actor's inventory service. Selection changes
// admission only; they never replace ammunition or forget a removed magazine.
// Slots cannot be evicted to make an ambiguous write or detached item disappear.
// Destruction is allowed only after the adapter has retired that actor domain.
template<std::size_t Capacity=64> class AmmunitionInventory {
    static_assert(Capacity>0);
public:
    AmmunitionInventory()=default;
    AmmunitionInventory(const AmmunitionInventory&)=delete;
    AmmunitionInventory& operator=(const AmmunitionInventory&)=delete;

    bool Select(const AmmunitionSnapshot& sample,std::int64_t now)noexcept {
        AmmunitionLedger validation;
        if(!validation.Bind(sample,now)||sample.sequence<selectionSequence_||sample.observedNs<selectionObserved_||
           (sample.sequence==selectionSequence_&&selectionContext_&&sample.context!=*selectionContext_)){
            Suspend();return false;
        }
        // The adapter's selection publication sequence is global across items.
        // Even a blocked item advances it, so an older selected gun cannot be
        // re-admitted after a failed attempt to bind the actual current one.
        selectionSequence_=sample.sequence;selectionObserved_=sample.observedNs;selectionContext_=sample.context;
        if(selected_&&*selected_!=sample.context)Suspend();
        auto* ledger=FindMutable(sample.context.resource);
        if(!ledger){
            for(auto& slot:slots_)if(slot.Phase()==AmmunitionLedgerPhase::Unbound){ledger=&slot;break;}
        }
        if(!ledger){Suspend();return false;}
        const auto& previous=ledger->Snapshot();
        const bool unchanged=ledger->Phase()==AmmunitionLedgerPhase::Ready&&
            sample.context==previous.context&&sample.sequence==previous.sequence&&
            sample.observedNs==previous.observedNs&&sample.deadlineNs==previous.deadlineNs&&sample.counts==previous.counts;
        if(!unchanged&&!ledger->Bind(sample,now)){Suspend();return false;}
        selected_=sample.context;return true;
    }
    std::optional<AmmunitionCommand> Submit(const AmmunitionIntent& intent,
                                           AmmunitionSnapshot current,std::int64_t now)noexcept {
        if(!selected_||intent.context!=*selected_||current.context!=*selected_||
           !intent.id||intent.id<=lastIntent_||intent.observedNs<=0||now<intent.observedNs||
           now>=intent.deadlineNs||intent.deadlineNs-intent.observedNs>100000000)return {};
        // An attempted current gesture cannot turn into a later automatic retry.
        lastIntent_=intent.id;
        auto* ledger=FindMutable(intent.context.resource);if(!ledger)return {};
        current.deadlineNs=std::min(current.deadlineNs,intent.deadlineNs);
        return ledger->Queue(intent.operation,current,now,intent.original);
    }
    std::optional<AmmunitionCommand> Dispatch(const AmmunitionCommand& command,
                                             const AmmunitionSnapshot& current,std::int64_t now)noexcept {
        auto* ledger=FindMutable(command.context.resource);
        if(!ledger||ledger->Pending()!=command)return {};
        if(!selected_||*selected_!=command.context){ledger->Cancel();return {};}
        return ledger->Dispatch(current,now);
    }
    bool Complete(const AmmunitionReceipt& receipt,std::int64_t now)noexcept {
        // A late completion goes to its original item, even with another gun in
        // the hand. Reconciliation never borrows the currently selected ledger.
        auto* ledger=FindMutable(receipt.command.context.resource);
        return ledger&&ledger->Complete(receipt,now);
    }
    bool Discard(const RemovedMagazineResource& original)noexcept {
        auto* ledger=FindMutable(original.owner);return ledger&&ledger->Discard(original);
    }
    void Cancel(const AmmunitionCommand& command)noexcept {
        auto* ledger=FindMutable(command.context.resource);
        if(ledger&&ledger->Pending()==command)ledger->Cancel();
    }
    void Suspend()noexcept {
        if(selected_)if(auto* ledger=FindMutable(selected_->resource))ledger->Cancel();
        selected_.reset();
    }
    void Expire(std::int64_t now)noexcept {for(auto& ledger:slots_)ledger.Expire(now);}
    const AmmunitionLedger* Find(const AmmoResourceIdentity& resource)const noexcept {
        for(const auto& ledger:slots_)if(ledger.Phase()!=AmmunitionLedgerPhase::Unbound&&ledger.Snapshot().context.resource==resource)return &ledger;
        return nullptr;
    }
    const auto& Selected()const noexcept{return selected_;}
    std::size_t Size()const noexcept {
        std::size_t n=0;for(const auto& ledger:slots_)n+=ledger.Phase()!=AmmunitionLedgerPhase::Unbound;return n;
    }
private:
    AmmunitionLedger* FindMutable(const AmmoResourceIdentity& resource)noexcept {
        for(auto& ledger:slots_)if(ledger.Phase()!=AmmunitionLedgerPhase::Unbound&&ledger.Snapshot().context.resource==resource)return &ledger;
        return nullptr;
    }
    std::array<AmmunitionLedger,Capacity> slots_{};
    std::optional<AmmoResourceContext> selected_;
    std::optional<AmmoResourceContext> selectionContext_;
    std::uint64_t lastIntent_=0,selectionSequence_=0;
    std::int64_t selectionObserved_=0;
};
}
