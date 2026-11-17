#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace fvr::bc2 {
// Passive cache evidence only: surviving Update return is not consumption.
enum class VehicleFinalInputCheck:unsigned {Okay,Owner,Route,Driver,Read,Nested,Unwind};
struct VehicleFinalInputStamp {std::int64_t qpc=0;std::uint64_t ms=0;};
struct VehicleFinalInputCommit {
    unsigned manager=0,player=0,actor=0,weak=0,vehicle=0,entry=0,router=0,cache=0,driver=0;
    std::uint64_t actorGeneration=0,seatGeneration=0,space=0,routeFingerprint=0;
    std::uint64_t update=0,gather=0,input=0,inputEpoch=0,movementEpoch=0;
    unsigned throttle=0,permissions=0;
    VehicleFinalInputStamp stamp{};
    VehicleFinalInputCheck check=VehicleFinalInputCheck::Okay;
};
struct VehicleFinalInputRecord {
    VehicleFinalInputCommit commit{};VehicleFinalInputStamp returned{};
    VehicleFinalInputCheck before=VehicleFinalInputCheck::Okay,after=VehicleFinalInputCheck::Okay;
    unsigned throttle=0,permissions=0;
    bool readable=false,complete=false,throttleChanged=false,permissionsChanged=false;
    std::uint64_t sequence=0;unsigned firstMask=0;
};
// validate runs on both sides of the two passive word reads, even on a read
// failure. No final words become qualified after a failed identity bookend.
template<class Validate,class Read>
VehicleFinalInputRecord ObserveVehicleFinalInput(const VehicleFinalInputCommit& commit,
    bool nested,bool unwind,Validate validate,Read read)noexcept {
    VehicleFinalInputRecord row;row.commit=commit;
    if(unwind){row.before=VehicleFinalInputCheck::Unwind;return row;}
    if(nested){row.before=VehicleFinalInputCheck::Nested;return row;}
    if(commit.check!=VehicleFinalInputCheck::Okay){row.before=commit.check;return row;}
    row.before=validate();if(row.before!=VehicleFinalInputCheck::Okay)return row;
    row.readable=read(row.throttle,row.permissions);row.after=validate();
    if(!row.readable){if(row.after==VehicleFinalInputCheck::Okay)row.after=VehicleFinalInputCheck::Read;return row;}
    if(row.after!=VehicleFinalInputCheck::Okay)return row;
    row.complete=true;row.throttleChanged=row.throttle!=commit.throttle;
    row.permissionsChanged=row.permissions!=commit.permissions;return row;
}
// Retain first active, first net change and first neutral after active, plus
// bounded latest attempts. Explicit eviction counts include failed attempts.
// Caller serializes using its existing try-lock; no allocation or I/O here.
template<std::size_t Capacity=16>
class VehicleFinalInputEvidence {
    static_assert(Capacity>=4);
public:
    static constexpr std::size_t MaxRecords=Capacity;
    void Observe(VehicleFinalInputRecord row)noexcept {
        row.sequence=++samples_;row.firstMask=0;
        const bool active=(row.commit.throttle&0x7fffffffu)!=0;
        if(active&&!(firstMask_&1))row.firstMask|=1;
        if(row.complete&&(row.throttleChanged||row.permissionsChanged)&&!(firstMask_&2))row.firstMask|=2;
        if(!active&&(firstMask_&1)&&!(firstMask_&4))row.firstMask|=4;
        firstMask_|=row.firstMask;
        std::size_t slot=count_;
        if(count_<Capacity)++count_;
        else {
            slot=Capacity;
            for(std::size_t n=0;n<Capacity;++n)if(!rows_[n].firstMask&&
                (slot==Capacity||rows_[n].sequence<rows_[slot].sequence))slot=n;
            ++omitted_;if(!rows_[slot].complete)++omittedFailures_;
        }
        if(!row.complete)++failures_;
        rows_[slot]=row;
    }
    const VehicleFinalInputRecord& At(std::size_t i)const noexcept{return rows_[i];}
    std::size_t Size()const noexcept{return count_;}
    std::uint64_t Samples()const noexcept{return samples_;}
    std::uint64_t Omitted()const noexcept{return omitted_;}
    std::uint64_t Failures()const noexcept{return failures_;}
    std::uint64_t OmittedFailures()const noexcept{return omittedFailures_;}
private:
    std::array<VehicleFinalInputRecord,Capacity> rows_{};std::size_t count_=0;
    unsigned firstMask_=0;std::uint64_t samples_=0,omitted_=0,failures_=0,omittedFailures_=0;
};
}
