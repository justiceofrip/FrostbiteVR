#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace fvr::bc2 {
// Bounded history of successful view adjustments. Compaction retains every
// other historical sample, then doubles the stride; the newest eligible sample
// has its own slot. This spans long sessions but can still miss brief events.
// The caller serializes Observe and snapshots; timestamps remain caller-owned.
template<class Record,std::size_t Capacity=192>
class ViewBasisEvidence {
    static_assert(Capacity>=4);
    struct Entry {Record record{};std::uint64_t ordinal=0;};
public:
    static constexpr std::uint64_t MinimumIntervalMs=100;
    static constexpr std::size_t MaxRecords=Capacity;
    bool Due(std::uint64_t nowMs)const noexcept {
        return !samples_||(nowMs>observedMs_&&nowMs-observedMs_>=MinimumIntervalMs);
    }
    bool Observe(const Record& record,std::uint64_t nowMs)noexcept {
        if(!Due(nowMs))return false;
        const auto ordinal=samples_;
        if(samples_!=(std::numeric_limits<std::uint64_t>::max)())++samples_;
        observedMs_=nowMs;latest_={record,ordinal};
        if(ordinal%stride_)return true;
        if(count_==history_.size()){
            std::size_t kept=0;
            for(std::size_t i=0;i<count_;i+=2)history_[kept++]=history_[i];
            count_=kept;++compactions_;
            const auto limit=(std::numeric_limits<std::uint64_t>::max)();
            stride_=stride_<=limit/2?stride_*2:limit;
        }
        if(ordinal%stride_==0)history_[count_++]=latest_;
        return true;
    }
    std::size_t Size()const noexcept {
        return count_+(samples_&&(!count_||history_[count_-1].ordinal!=latest_.ordinal)?1u:0u);
    }
    const Record& At(std::size_t index)const noexcept {
        return index<count_?history_[index].record:latest_.record;
    }
    std::uint64_t Samples()const noexcept {return samples_;}
    std::uint64_t Stride()const noexcept {return stride_;}
    std::uint64_t Compactions()const noexcept {return compactions_;}
private:
    std::array<Entry,Capacity-1> history_{};Entry latest_{};
    std::size_t count_=0;
    std::uint64_t samples_=0,observedMs_=0,stride_=1,compactions_=0;
};
}
