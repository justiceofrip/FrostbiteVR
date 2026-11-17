#pragma once
#include "Bc2NativeCycleService.h"

namespace fvr::bc2 {
struct Bc2NativeCycleRoute {
    Bc2NativeCycleSlot slot{};NativeCycleRecoveryScope scope{};
    std::uint64_t cycle=0,shot=0,guardEpoch=0;
    bool operator==(const Bc2NativeCycleRoute&)const noexcept=default;
};
// Runtime serializes this fixed bank under its existing policy gate. Whole
// services stay in their slots: no View reconstruction, LRU or expiry eviction.
class Bc2NativeCycleBank {
public:
    static constexpr unsigned Capacity=8;
    // Explicit initial high-water mark also permits deterministic exhaustion
    // checks. It never imports a service, debt or native authority.
    explicit Bc2NativeCycleBank(std::uint64_t generation=1)noexcept: generation_(generation) {
        slots_[0].generation=generation;
    }
    Bc2NativeCycleService& Selected()noexcept{return slots_[selected_].service;}
    const Bc2NativeCycleService& Selected()const noexcept{return slots_[selected_].service;}
    Bc2NativeCycleSlot SelectedSlot()const noexcept{return {selected_,slots_[selected_].generation};}
    const Bc2NativeCycleService* Find(Bc2NativeCycleSlot handle)const noexcept {
        return handle&&handle.index<Capacity&&slots_[handle.index].generation==handle.generation?
            &slots_[handle.index].service:nullptr;
    }
    unsigned Owned()const noexcept {
        unsigned count=0;for(const auto& slot:slots_)if(slot.service.NativeScope().identity.owner.weapon)++count;return count;
    }
    Bc2NativeCycleView View(std::int64_t now)const noexcept {
        auto result=Selected().View(now);result.slot=SelectedSlot();return result;
    }
    Bc2NativeCycleRoute Route(std::int64_t now)const noexcept {
        const auto v=Selected().View(now);
        return {SelectedSlot(),Selected().NativeScope(),v.cycle,v.shot,v.debt?v.debt->guardEpoch:0};
    }
    bool Current(const Bc2NativeCycleRoute& route,std::int64_t now)const noexcept {
        return route.slot&&route==Route(now)&&!Selected().View(now).selectionParked;
    }
    template<class Work>bool Apply(const Bc2NativeCycleRoute& route,std::int64_t now,Work&& work)noexcept {
        if(!Current(route,now))return false;
        return work(Selected());
    }
    std::optional<Bc2NativeCycleDeparture> Departure(const ReloadStateOwner& owner)const noexcept {
        for(unsigned n=0;n<Capacity;++n){const auto& slot=slots_[n];
            if(slot.departure&&slot.departure->slot==Bc2NativeCycleSlot{n,slot.generation}&&
               slot.departure->previous.owner==owner&&slot.service.View(slot.departure->observedNs).selectionParked&&
               (n==selected_||slot.departure->selected==Selected().NativeScope().identity))return slot.departure;
        }
        return {};
    }
    bool EnableRecovery()noexcept {
        if(Owned())return false;
        for(auto& slot:slots_)if(!slot.service.EnableRecovery())return false;
        return true;
    }
    bool EnableConcurrentBolt()noexcept {
        if(Owned())return false;
        for(auto& slot:slots_)if(!slot.service.EnableConcurrentBolt())return false;
        return true;
    }
    // Only an actual away observation can produce this receipt. This does not
    // authorize dispatching a selector while the current physical stroke is owed.
    bool ObserveDeparture(const NativeCycleRecoveryScope& selected,const Bc2NativeCycleActorDrain& drain,
        std::int64_t now)noexcept {
        if(!generation_||generation_==UINT64_MAX)return false;
        const auto& observed=selected.identity;
        auto& slot=slots_[selected_];const auto before=slot.service.View(now);
        bool room=false;
        for(const auto& other:slots_){const auto scope=other.service.NativeScope();
            if(scope.identity.owner.weapon&&(!SameActor(scope.identity.owner,observed.owner)||
               (SameResource(scope.identity,observed)?scope.config!=selected.config:ConflictingResource(scope.identity,observed))))return false;
            if(!scope.identity.owner.weapon||SameResource(scope.identity,observed)||other.service.Reclaimable(now))room=true;
        }
        if(!room||!Drain(slot.service,drain,now))return false;
        if(slot.departure&&slot.departure->selected==observed&&before.selectionParked)return true;
        if(!slot.service.ParkSelection(observed,drain,now))return false;
        slot.departure=Bc2NativeCycleDeparture{SelectedSlot(),before.native,observed,before.cycle,before.shot,
            before.debt?before.debt->guardEpoch:0,drain.callbackRevision,now};return true;
    }
    // Source is a verified selected native observation. Both affected services
    // are staged before either changes. A refused admission preserves every slot.
    bool SelectObserved(const Bc2NativeCycleSelection& source,const Bc2NativeCycleActorDrain& drain)noexcept {
        const auto now=source.nowNs;
        if(!generation_||generation_==UINT64_MAX||!Drain(Selected(),drain,now))return false;
        unsigned target=Capacity,free=Capacity;
        for(unsigned n=0;n<Capacity;++n){
            const auto scope=slots_[n].service.NativeScope();
            if(!scope.identity.owner.weapon){if(free==Capacity)free=n;continue;}
            if(!SameActor(scope.identity.owner,source.reserve.identity.owner))return false;
            if(SameResource(scope.identity,source.reserve.identity)){
                if(scope.config!=source.config||slots_[n].service.Mode()!=source.mode||target!=Capacity)return false;
                target=n;
            }else if(ConflictingResource(scope.identity,source.reserve.identity))return false;
        }
        // Fixed-order reclamation is allowed only for an unused watcher or an
        // acknowledged, reconciled service. Outstanding debt is never evicted.
        if(target==Capacity&&Selected().Reclaimable(now))target=selected_;
        if(target==Capacity)target=free;
        if(target==Capacity)for(unsigned n=0;n<Capacity;++n)
            if(slots_[n].service.Reclaimable(now)){target=n;break;}
        if(target==Capacity)return false;
        auto incoming=slots_[target].service;
        const auto incomingView=incoming.View(now);
        const Bc2NativeCycleActorDrain incomingDrain{incomingView.native,incomingView.cycle,incomingView.shot,
            drain.callbackRevision,drain.observedNs,drain.deadlineNs,drain.callbacksDrained};
        if(!incoming.CallbacksDrained())return false;
        if(incomingView.selectionParked){
            if(!incoming.ReturnSelection(source,incomingDrain))return false;
        }else if(!incoming.Reclaimable(now)||!incoming.Select(source))return false;
        if(target!=selected_){
            auto outgoing=Selected();const auto old=outgoing.View(now);
            if(outgoing.NativeScope().identity.owner.weapon&&!old.selectionParked){
                // Unfinished pump parking is deliberately unsupported. An owed
                // bolt must pass the existing exact observed-away policy.
                if(!outgoing.Reclaimable(now)){
                    if(!outgoing.ParkSelection(source.reserve.identity,drain,now))return false;
                }else if(!outgoing.YieldForReload())return false;
            }
            if(old.selectionParked||outgoing.View(now).selectionParked){
                if(!slots_[selected_].departure){
                    slots_[selected_].departure=Bc2NativeCycleDeparture{SelectedSlot(),old.native,source.reserve.identity,
                        old.cycle,old.shot,old.debt?old.debt->guardEpoch:0,drain.callbackRevision,now};
                }
            }
            slots_[selected_].service=std::move(outgoing);
        }
        slots_[target].service=std::move(incoming);slots_[target].departure.reset();
        slots_[target].generation=++generation_;selected_=target;return true;
    }
    // Preflight ALL old slots, then retire exactly those actor lifetimes. A
    // failed sibling retirement cannot partially erase another retained debt.
    bool RetireForReplacementActor(const Bc2AmmoReserveLease& reserve,std::int64_t now,
        const Bc2NativeCycleActorDrain& drain)noexcept {
        return Retire(reserve,now,drain,nullptr);
    }
    bool SelectReplacementActor(const Bc2NativeCycleSelection& source,const Bc2NativeCycleActorDrain& drain)noexcept {
        return Retire(source.reserve,source.nowNs,drain,&source);
    }
private:
    bool Retire(const Bc2AmmoReserveLease& reserve,std::int64_t now,const Bc2NativeCycleActorDrain& drain,
        const Bc2NativeCycleSelection* selection)noexcept {
        if(!generation_||generation_==UINT64_MAX||!Drain(Selected(),drain,now))return false;
        bool any=false;
        for(unsigned n=0;n<Capacity;++n){const auto& slot=slots_[n];if(!slot.service.NativeScope().identity.owner.weapon)continue;
            auto candidate=slot.service;const auto v=candidate.View(now);
            auto proof=drain;proof.expected=v.native;proof.cycle=v.cycle;proof.shot=v.shot;
            if(selection&&n==selected_){if(!candidate.SelectReplacementActor(*selection,proof))return false;}
            else if(!candidate.RetireForReplacementActor(reserve,now,proof))return false;
            any=true;
        }
        if(!any)return false;
        for(unsigned n=0;n<Capacity;++n){auto& slot=slots_[n];if(!slot.service.NativeScope().identity.owner.weapon)continue;
            const auto v=slot.service.View(now);auto proof=drain;
            proof.expected=v.native;proof.cycle=v.cycle;proof.shot=v.shot;
            if(selection&&n==selected_)slot.service.SelectReplacementActor(*selection,proof);
            else slot.service.RetireForReplacementActor(reserve,now,proof);
            slot.departure.reset();slot.generation=0;
        }
        slots_[selected_].generation=++generation_;return true;
    }
    static bool SameResource(ReloadHoldIdentity a,ReloadHoldIdentity b)noexcept {
        a.owner.equipGeneration=b.owner.equipGeneration;return a==b;
    }
    static bool SameActor(ReloadStateOwner a,const ReloadStateOwner& b)noexcept {
        a.weapon=b.weapon;a.equipGeneration=b.equipGeneration;return a==b;
    }
    static bool ConflictingResource(const ReloadHoldIdentity& a,const ReloadHoldIdentity& b)noexcept {
        if(a.owner.weapon==b.owner.weapon||a.serverItem==b.serverItem)return true;
        for(auto x:a.firing)for(auto y:b.firing)if(x==y)return true;
        return false;
    }
    static bool Drain(const Bc2NativeCycleService& service,const Bc2NativeCycleActorDrain& d,std::int64_t now)noexcept {
        const auto v=service.View(now);
        return d.callbacksDrained&&d.callbackRevision&&d.expected==v.native&&d.cycle==v.cycle&&d.shot==v.shot&&
            service.CallbacksDrained()&&interaction::weapon_cycle_detail::Window(d.observedNs,d.deadlineNs,now)&&
            d.deadlineNs-d.observedNs<=Bc2NativeCycleService::ContextFreshNs;
    }
    struct Slot {Bc2NativeCycleService service{};std::uint64_t generation=0;std::optional<Bc2NativeCycleDeparture> departure;};
    std::array<Slot,Capacity> slots_{};unsigned selected_=0;std::uint64_t generation_=1;
};
}
