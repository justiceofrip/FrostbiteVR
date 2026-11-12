#pragma once
#include <cstdint>
#include <limits>
#include <mutex>
#include <optional>

namespace fvr::bc2::chestKnifeNative {
// Operation revisions are protected by the service gate. Revocation stamps
// also cover source changes which deliberately precede acquisition of that gate.
struct InspectionStamp {
    std::uint64_t operation=0,source=0,pose=0,trigger=0,motion=0;
    bool enabled=false,closing=false;
    bool operator==(const InspectionStamp&)const=default;
};
inline void AdvanceInspectionOperation(std::uint64_t& revision)noexcept {
    if(revision!=(std::numeric_limits<std::uint64_t>::max)())++revision;
}
inline bool InspectionStillCurrent(const InspectionStamp& before,const InspectionStamp& after)noexcept {
    return before.enabled&&before.operation&&before.operation!=(std::numeric_limits<std::uint64_t>::max)()&&before==after;
}

// Capture and publication are bounded try-lock operations. The potentially
// expensive native reader never holds the observer's gate. A failed rejoin
// discards the read; it never retries, renews its timestamp or clears history.
template<class State,class Result,class Capture,class Read,class Join>
std::optional<Result> InspectOutsideGate(std::mutex& gate,Capture capture,Read read,Join join)noexcept {
    std::optional<State> state;
    {
        std::unique_lock lock(gate,std::try_to_lock);
        if(!lock.owns_lock())return {};
        state=capture();
    }
    if(!state)return {};
    auto result=read(*state);
    if(!result)return {};
    std::unique_lock lock(gate,std::try_to_lock);
    if(!lock.owns_lock()||!join(*state,*result))return {};
    return result;
}
}
