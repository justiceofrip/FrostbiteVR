#pragma once
#include <atomic>
#include <cstdint>

namespace fvr::bc2 {
// Ordinary invocations register before inspecting the exclusive barrier. They
// never acquire it, so simultaneous client/server entries cannot exclude each
// other. All coordinating operations are SC: either the exclusive quiet scan
// sees a new registration/revision, or the entrant sees exclusion and cannot
// acquire a request decision. No exclusion spans a native original call.
inline bool EnterReloadInvocation(std::atomic<unsigned>& active,
    std::atomic<std::uint64_t>& revision,const std::atomic_flag& exclusive)noexcept {
    active.fetch_add(1,std::memory_order_seq_cst);
    revision.fetch_add(1,std::memory_order_seq_cst);
    return !exclusive.test(std::memory_order_seq_cst);
}
inline void ExitReloadInvocation(std::atomic<unsigned>& active,
    std::atomic<std::uint64_t>& revision)noexcept {
    revision.fetch_add(1,std::memory_order_seq_cst);
    active.fetch_sub(1,std::memory_order_seq_cst);
}
class ReloadInvocationExclusion {
    std::atomic_flag& flag_;
    std::atomic<unsigned>& active_;
    std::atomic<std::uint64_t>& revision_;
    std::uint64_t observedRevision_=0;
public:
    const bool held;
    ReloadInvocationExclusion(std::atomic_flag& flag,std::atomic<unsigned>& active,
        std::atomic<std::uint64_t>& revision)noexcept
        :flag_(flag),active_(active),revision_(revision),held(!flag.test_and_set(std::memory_order_seq_cst)) {
        if(held)observedRevision_=revision_.load(std::memory_order_seq_cst);
    }
    ~ReloadInvocationExclusion(){if(held)flag_.clear(std::memory_order_seq_cst);}
    ReloadInvocationExclusion(const ReloadInvocationExclusion&)=delete;
    ReloadInvocationExclusion& operator=(const ReloadInvocationExclusion&)=delete;
    bool Quiet()const noexcept {
        return held&&active_.load(std::memory_order_seq_cst)==1&&
            revision_.load(std::memory_order_seq_cst)==observedRevision_;
    }
};
}
