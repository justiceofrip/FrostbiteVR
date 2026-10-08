#pragma once
#include "Bc2AmmoResourceEvidence.h"
#include <atomic>
#include <limits>

namespace fvr::bc2 {
// One command may be in flight. Retain its exact completed server Update even
// when the policy gate is busy. No allocation, waiting, or native reread occurs.
// Arm/Take belong to the serialized policy consumer; Publish may be concurrent.
class AmmoResourceHandoff {
public:
    bool Arm(std::uint64_t invocation)noexcept {
        if(!invocation||invocation>(std::numeric_limits<std::uint64_t>::max()>>3))return false;
        auto old=state_.load(std::memory_order_acquire);
        if(old&&(old&7)!=Consumed)return false;
        // Invocation ids must increase: a late publisher cannot enter a reused slot.
        if((old>>3)>=invocation)return false;
        return state_.compare_exchange_strong(old,(invocation<<3)|Armed,
            std::memory_order_release,std::memory_order_relaxed);
    }
    bool Publish(const AmmoResourceOwnUpdate& row)noexcept {
        if(row.branch!=2||!row.invocation||row.invocation>(std::numeric_limits<std::uint64_t>::max()>>3))return false;
        auto expected=(row.invocation<<3)|Armed;
        if(!state_.compare_exchange_strong(expected,(row.invocation<<3)|Writing,
            std::memory_order_acquire,std::memory_order_relaxed))return false;
        row_=row;
        state_.store((row.invocation<<3)|Ready,std::memory_order_release);
        return true;
    }
    std::optional<AmmoResourceOwnUpdate> Take()noexcept {
        auto expected=state_.load(std::memory_order_acquire);
        if((expected&7)!=Ready)return {};
        const auto id=expected>>3;
        if(!state_.compare_exchange_strong(expected,(id<<3)|Reading,
            std::memory_order_acquire,std::memory_order_relaxed))return {};
        const auto result=row_;
        state_.store((id<<3)|Consumed,std::memory_order_release);
        return result;
    }
private:
    enum : std::uint64_t {Armed=1,Writing=2,Ready=3,Reading=4,Consumed=5};
    static_assert(std::atomic<std::uint64_t>::is_always_lock_free);
    std::atomic<std::uint64_t> state_=0;
    AmmoResourceOwnUpdate row_{};
};
}
