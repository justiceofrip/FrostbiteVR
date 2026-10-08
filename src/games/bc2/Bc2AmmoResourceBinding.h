#pragma once
#include "Bc2ReloadState.h"
#include "fvr/interaction/AmmunitionLedger.h"
#include <atomic>

namespace fvr::bc2 {
// Identity only, from the body inventory's coherent native read and existing
// item lifetime. No hand pose, render frame, count or mutation authority.
struct AmmoResourceBinding {
    ReloadStateOwner owner{};
    interaction::AmmoResourceContext context{};
    std::uint32_t inventory=0,switching=0,data=0,persistence=0;
    std::int64_t observedNs=0,deadlineNs=0;
    bool operator==(const AmmoResourceBinding&)const=default;
};
inline bool AmmoResourceBindingFresh(const AmmoResourceBinding& b,const ReloadStateOwner& owner,std::int64_t now)noexcept {
    const auto& c=b.context;const auto& r=c.resource;
    return b.owner==owner&&owner.player>=0x10000&&owner.soldier>=0x10000&&owner.weak>=0x10000&&owner.weapon>=0x10000&&
        owner.actorGeneration&&owner.equipGeneration&&owner.space&&r.actor==owner.soldier&&r.actorGeneration==owner.actorGeneration&&
        r.weapon==owner.weapon&&r.weaponGeneration&&c.equipGeneration==owner.equipGeneration&&c.space==owner.space&&
        b.inventory>=0x10000&&b.switching>=0x10000&&b.data>=0x10000&&
        b.observedNs>0&&now>=b.observedNs&&now<b.deadlineNs&&b.deadlineNs-b.observedNs<=100000000;
}
// Latest gather publication. A busy writer invalidates the previous sample,
// rather than leaving old item identity available during replacement.
class AmmoResourceBindingChannel {
public:
    bool Publish(const std::optional<AmmoResourceBinding>& binding)noexcept {
        const auto revision=revision_.fetch_add(1,std::memory_order_acq_rel)+1;
        if(!revision)return false;
        Gate gate(gate_);if(!gate.held||revision!=revision_.load(std::memory_order_acquire))return false;
        value_=binding;storedRevision_=revision;return true;
    }
    std::optional<AmmoResourceBinding> Read(const ReloadStateOwner& owner,std::int64_t now)noexcept {
        Gate gate(gate_);if(!gate.held||!storedRevision_||storedRevision_!=revision_.load(std::memory_order_acquire)||
            !value_||!AmmoResourceBindingFresh(*value_,owner,now))return {};
        return value_;
    }
private:
    struct Gate {std::atomic_flag& flag;bool held;
        explicit Gate(std::atomic_flag& f):flag(f),held(!f.test_and_set(std::memory_order_acquire)){}
        ~Gate(){if(held)flag.clear(std::memory_order_release);}};
    std::atomic_flag gate_=ATOMIC_FLAG_INIT;std::atomic<std::uint64_t> revision_=0;
    std::uint64_t storedRevision_=0;std::optional<AmmoResourceBinding> value_;
};
}
