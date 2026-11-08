#pragma once
#include "fvr/interaction/VehicleTracking.h"
#include <array>
#include <cstddef>
#include <cstdint>

namespace fvr::bc2 {
struct VehicleCameraEvidenceKey {
    interaction::VehicleTrackingOwner owner{};
    unsigned frame=0;
    std::uint64_t space=0,tracking=0;
    std::int64_t predictedNs=0;
    bool operator==(const VehicleCameraEvidenceKey&)const=default;
};
// Caller serializes this value-only journal. Preserve the first sample and
// the newest Capacity-1 samples in time order. Owner/space boundaries bypass
// the cadence; physical head motion does not. Omitted samples are explicit,
// so bounded retention cannot be mistaken for continuous ride coverage.
template<class Record,std::size_t Capacity=16>
class VehicleCameraEvidence {
    static_assert(Capacity>=2);
public:
    static constexpr std::uint64_t MinimumIntervalMs=500;
    static constexpr std::size_t MaxRecords=Capacity;
    bool Due(const VehicleCameraEvidenceKey& key,std::uint64_t nowMs)const noexcept {
        if(!key.frame)return false;
        if(!count_)return true;
        for(std::size_t i=0;i<count_;++i)if(key==At(i).key)return false;
        const auto& previous=At(count_-1);
        if(key.owner!=previous.key.owner||key.space!=previous.key.space)return true;
        return nowMs>previous.ms&&nowMs-previous.ms>=MinimumIntervalMs;
    }
    bool Observe(const Record& record)noexcept {
        if(!Due(record.key,record.ms))return false;
        std::size_t slot=0;
        if(count_<Capacity)slot=Slot(count_++);
        else {
            slot=Slot(1);
            ++omitted_;
            if(records_[slot].appliedMask!=3)++omittedIncomplete_;
            begin_=(begin_+1)%(Capacity-1);
        }
        records_[slot]=record;
        records_[slot].appliedMask=0;
        ++samples_;
        return true;
    }
    // As with ViewBasisEvidence, enrichment joins owned identity, never the
    // latest journal index. The caller supplies the current live owner/space
    // under the same lock; a saved plan key alone is not current ownership.
    // Repeated eye callbacks cannot overwrite evidence.
    template<class Update>
    bool Enrich(const VehicleCameraEvidenceKey& key,const interaction::VehicleCameraSample& current,unsigned eye,Update update)noexcept {
        if(eye>1)return false;
        if(!current.live||current.owner!=key.owner||current.space!=key.space){++unmatchedEyes_;return false;}
        for(std::size_t i=0;i<count_;++i){
            auto& row=records_[Slot(i)];
            if(row.key!=key)continue;
            if(row.appliedMask&(1u<<eye)){++duplicateEyes_;return false;}
            update(row);row.appliedMask|=1u<<eye;
            if(row.appliedMask==3)++complete_;
            return true;
        }
        ++unmatchedEyes_;return false;
    }
    std::size_t Size()const noexcept{return count_;}
    const Record& At(std::size_t index)const noexcept{return records_[Slot(index)];}
    std::uint64_t Samples()const noexcept{return samples_;}
    std::uint64_t Omitted()const noexcept{return omitted_;}
    std::uint64_t Incomplete()const noexcept{return samples_-complete_;}
    std::uint64_t OmittedIncomplete()const noexcept{return omittedIncomplete_;}
    std::uint64_t RetainedIncomplete()const noexcept{return Incomplete()-omittedIncomplete_;}
    std::uint64_t DuplicateEyes()const noexcept{return duplicateEyes_;}
    std::uint64_t UnmatchedEyes()const noexcept{return unmatchedEyes_;}
private:
    std::size_t Slot(std::size_t index)const noexcept{return index?1+(begin_+index-1)%(Capacity-1):0;}
    std::array<Record,Capacity> records_{};
    std::size_t count_=0,begin_=0;
    std::uint64_t samples_=0,omitted_=0,complete_=0,omittedIncomplete_=0,duplicateEyes_=0,unmatchedEyes_=0;
};
}
