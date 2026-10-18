#pragma once
#include <atomic>
#include <cstdint>
namespace fvr::runtime {
// A failed read/validation owns only its observation frame. Unlike a failed
// exact native restoration, it made no persistent temporary write. Tag callbacks
// at entry: a late rejection cannot clear or poison a newer observation frame.
// This protects failure accounting, not native context/cache object lifetime.
class FrameObservationFailures {
    std::atomic<std::uint32_t> total_=0;
    // Low bit is rejection; remaining bits are a monotonically advancing token.
    std::atomic<std::uint64_t> state_=0;
public:
    using Token=std::uint64_t;
    void BeginFrame()noexcept {
        auto old=state_.load(std::memory_order_acquire);
        for(;;){
            const auto epoch=old&~Token{1};
            const auto next=epoch==~Token{1}?epoch|Token{1}:epoch+2;
            if(state_.compare_exchange_weak(old,next,std::memory_order_acq_rel,std::memory_order_acquire))return;
        }
    }
    Token Capture()const noexcept {return state_.load(std::memory_order_acquire)&~Token{1};}
    bool Current(Token token)const noexcept {return token&&token==Capture();}
    void Reject(Token token)noexcept {
        total_.fetch_add(1,std::memory_order_relaxed);
        if(!token||(token&1))return;
        auto expected=token;
        state_.compare_exchange_strong(expected,token|Token{1},std::memory_order_acq_rel,std::memory_order_acquire);
    }
    bool CanApply(bool restorationFailed)const noexcept {
        const auto state=state_.load(std::memory_order_acquire);
        return !restorationFailed&&state&&!(state&1);
    }
    std::uint32_t Total()const noexcept {return total_.load(std::memory_order_relaxed);}
};
}
