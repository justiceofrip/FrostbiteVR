#pragma once
#include "Bc2ReloadFlowRuntime.h"
#include <atomic>
namespace fvr::bc2 {
// Telemetry only. Producers copy an already captured End; the sole consumer
// must hold the record gate. Publication never waits or reads native state.
class ReloadDeferredCompletions {
public:
    static constexpr unsigned Capacity=64;
    bool Publish(std::uint64_t id,const ReloadFlowEventEnd& end)noexcept {
        const auto first=next_.fetch_add(1,std::memory_order_relaxed)%Capacity;
        for(unsigned n=0;n<Capacity;++n){auto& slot=slots_[(first+n)%Capacity];unsigned empty=0;
            if(!slot.state.compare_exchange_strong(empty,1,std::memory_order_acquire,std::memory_order_relaxed))continue;
            slot.id=id;slot.end=end;pending_.fetch_add(1,std::memory_order_relaxed);
            slot.state.store(2,std::memory_order_release);return true;
        }
        overflow_.fetch_add(1,std::memory_order_relaxed);return false;
    }
    void Drain(ReloadFlowRecords& records)noexcept {
        for(auto& slot:slots_){if(slot.state.load(std::memory_order_acquire)!=2)continue;
            if(records.End(slot.id,slot.end))recovered_.fetch_add(1,std::memory_order_relaxed);
            else rejected_.fetch_add(1,std::memory_order_relaxed);
            pending_.fetch_sub(1,std::memory_order_relaxed);slot.state.store(0,std::memory_order_release);
        }
    }
    unsigned Pending()const noexcept{return pending_.load(std::memory_order_relaxed);}
    unsigned Recovered()const noexcept{return recovered_.load(std::memory_order_relaxed);}
    unsigned Overflow()const noexcept{return overflow_.load(std::memory_order_relaxed);}
    unsigned Rejected()const noexcept{return rejected_.load(std::memory_order_relaxed);}
private:
    struct Slot {std::atomic<unsigned> state=0;std::uint64_t id=0;ReloadFlowEventEnd end{};};
    std::array<Slot,Capacity> slots_{};
    std::atomic<unsigned> next_=0,pending_=0,recovered_=0,overflow_=0,rejected_=0;
};
}
