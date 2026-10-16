#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include <optional>
#include <type_traits>

namespace fvr::bc2 {
// Finite fixture observation, never a native action/admission receipt. Times
// come unchanged from the original input packet; the worker adds only identity.
struct ContextualToolProbeWitness {
    std::uint64_t version=1,pid=0,creation=0,tickMs=0;
    std::uint64_t observedNs=0,deadlineNs=0,input=0,space=0;
    std::uint64_t actor=0,actorGeneration=0,weak=0,inventory=0;
    std::uint64_t originalWeapon=0,originalData=0,toolWeapon=0,toolData=0;
    std::uint64_t selectedSlot=0,bodyOperation=0,lastBodyReturn=0,phase=0;
    std::uint64_t equipCommits=0,selectedObservations=0,returnCommits=0,hideCompletions=0;
    std::uint64_t empty=0,held=0,holdToken=0,revoked=0;
    std::array<char,192> toolAsset{};
};
static_assert(std::is_trivially_copyable_v<ContextualToolProbeWitness>);
static_assert(sizeof(ContextualToolProbeWitness)==416);

constexpr bool ContextualToolProbeFresh(const ContextualToolProbeWitness& w,std::int64_t now)noexcept {
    return now>0&&w.version==1&&w.observedNs&&w.observedNs<=std::uint64_t(now)&&
        w.deadlineNs>std::uint64_t(now)&&w.deadlineNs>w.observedNs&&w.deadlineNs-w.observedNs<=100000000;
}
// Both directions are nonblocking. Serialization/file I/O happens only after
// Read has copied and unlocked. A miss retains no renewed input authority.
class ContextualToolProbePublication {
public:
    void Publish(const ContextualToolProbeWitness& value)noexcept {
        if(lock_.test_and_set(std::memory_order_acquire)){++publishMisses_;return;}
        value_=value;lock_.clear(std::memory_order_release);
    }
    std::optional<ContextualToolProbeWitness> Read(std::int64_t now)const noexcept {
        if(lock_.test_and_set(std::memory_order_acquire)){++readMisses_;return {};}
        const auto copy=value_;lock_.clear(std::memory_order_release);
        return ContextualToolProbeFresh(copy,now)?std::optional(copy):std::nullopt;
    }
    std::uint64_t PublishMisses()const noexcept{return publishMisses_.load();}
    std::uint64_t ReadMisses()const noexcept{return readMisses_.load();}
private:
    mutable std::atomic_flag lock_=ATOMIC_FLAG_INIT;
    ContextualToolProbeWitness value_{};
    std::atomic<std::uint64_t> publishMisses_{0};
    mutable std::atomic<std::uint64_t> readMisses_{0};
};
// No config layout changes. The existing private build admits only this finite
// ordinary body/resource stream; continuous/other scripted modes fail closed.
constexpr bool ValidContextualToolFixture(unsigned flags,unsigned duration,unsigned session,bool other)noexcept {
    constexpr unsigned required=9u|0x197800u|0x2000000u|0x10000000u;
    return duration==20000&&session==3&&(flags&~0x200u)==required&&!other;
}
}
