#pragma once
#include "Bc2OriginalFiringObserver.h"
#include <atomic>

// Only the private x86 mounted dispatcher and its dedicated test include this.
// It never stores a stack address in TLS. Every ingress, including foreign
// callers and zero observer tokens, shadows and invalidates earlier tickets.
namespace fvr::bc2::mounted419 {
class UpdateSequence {
public:
    struct Entry {std::uint64_t previous=0;bool open=false;};
    Entry Begin()noexcept {
        const auto previous=lane_;
        const auto stamp=revision_.fetch_add(1,std::memory_order_acq_rel)+1;
        if(!stamp)exhausted_.store(true);
        const auto prior=depth_.fetch_add(1,std::memory_order_acq_rel);
        lane_=prior==0?stamp:0;
        return {previous,true};
    }
    void End(Entry& entry)noexcept {
        if(!entry.open)return;
        entry.open=false;lane_=entry.previous;depth_.fetch_sub(1,std::memory_order_acq_rel);
    }
    void Revoke()noexcept {
        if(revision_.fetch_add(1,std::memory_order_acq_rel)==UINT64_MAX)exhausted_.store(true);
    }
    std::uint64_t Exclusive()const noexcept {
        return lane_&&depth_.load()==1&&Current(lane_)?lane_:0;
    }
    bool Current(std::uint64_t stamp)const noexcept {
        return stamp&&!exhausted_.load()&&revision_.load()==stamp;
    }
    unsigned Depth()const noexcept{return depth_.load();}
private:
    inline static thread_local std::uint64_t lane_=0;
    std::atomic<std::uint64_t> revision_{0};
    std::atomic<unsigned> depth_{0};
    std::atomic<bool> exhausted_{false};
};
inline UpdateSequence updateSequence;
using UpdateBody=void(*)(void*);
using UpdateCleanup=void(*)(void*,bool)noexcept;
inline void RunUpdate(const OriginalFiringObserver& observer,OriginalFiringUpdate call,
    void* context,UpdateBody body,UpdateCleanup cleanup) {
    auto entry=updateSequence.Begin();
    OriginalFiringToken token{};
    bool entered=false,returned=false;
    // No C++ objects requiring unwinding in this frame. Hardware SEH and
    // C++ exceptions continue to their native handlers after both finallys.
    __try {
        if(observer.enter&&observer.leave){token=observer.enter(observer.context,call);entered=true;}
        body(context);
        returned=true;
    } __finally {
        __try {
            if(!returned){updateSequence.Revoke();token.words[7]=1;}
            if(entered)observer.leave(observer.context,call,token);
        } __finally {
            updateSequence.End(entry);
            cleanup(context,returned);
        }
    }
}
} // namespace fvr::bc2::mounted419
