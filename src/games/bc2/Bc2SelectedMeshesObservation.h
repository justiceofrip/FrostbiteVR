#pragma once
#include "Bc2SelectedMeshes1p.h"
#include <atomic>
#include <memory>
#include <mutex>
#include <ostream>
#include <vector>

namespace fvr::bc2 {
enum class SelectedObservationStatus:std::uint8_t {
    Disabled,Ready,Observed,BindingUnavailable,InvalidSource,Throttled,ReadRejected,
    SourceMismatch,ExpiredDuringRead,Cancelled,Busy,BudgetExhausted
};
struct SelectedObservationCounters {
    bool persistent=false;
    std::uint64_t attempts=0,published=0,clears=0,throttled=0,stale=0,busy=0;
    std::uint64_t readCalls=0,readBytes=0,totalDurationNs=0,maxDurationNs=0,lastDurationNs=0;
    std::uint32_t lastReadCalls=0,lastReadBytes=0;
    SelectedObservationStatus status=SelectedObservationStatus::Disabled;
    SelectedMeshesStatus readerStatus=SelectedMeshesStatus::Disabled;
};
// Diagnostic configured metadata only. Callback reads use the existing verified
// resolver. Immutable executable storage outlives its borrowed binding and all
// concurrent Observe calls. Read never refreshes a snapshot's original lease.
class SelectedMeshesObservation {
public:
    static constexpr std::int64_t LeaseNs=200000000,CadenceNs=100000000;
    static constexpr std::uint64_t MaxObservations=200;
    // Producer and resolver use the same original-source clock. Scheduling a
    // read with coarse wall-clock milliseconds can arrive just under100ms,
    // consume a producer slot, then leave a gap after the old200ms lease ends.
    static constexpr bool SourceObservationDue(std::int64_t previousNs,std::int64_t sourceNs)noexcept {
        return previousNs>=0&&sourceNs>0&&(!previousNs||(sourceNs>=previousNs&&sourceNs-previousNs>=CadenceNs));
    }
    SelectedMeshesObservation()=default;
    SelectedMeshesObservation(const SelectedMeshesObservation&)=delete;
    SelectedMeshesObservation& operator=(const SelectedMeshesObservation&)=delete;
    bool Install(std::span<const std::byte>,const engine::PeImage&,std::uint32_t imageBase,
        ReloadStateMemory,bool enabled=false,bool persistent=false)noexcept;
    bool Observe(const ReloadStateSnapshot&,std::int64_t nowNs)noexcept;
    std::shared_ptr<const SelectedMeshesSnapshot> Read(const ReloadStateOwner&,std::int64_t nowNs)const noexcept;
    // Discovery consumer: original immutable lease; caller must recheck native
    // ownership before using it. Clear invalidates this through the same atomic.
    std::shared_ptr<const SelectedMeshesSnapshot> ReadCurrent(std::int64_t nowNs)const noexcept;
    std::shared_ptr<const CarriedMeshesSnapshot> ReadCarried(const ReloadStateOwner&,std::uint32_t inventory,
        std::uint32_t slot,std::uint32_t weapon,std::uint32_t data,std::uint32_t persistence,
        std::uint64_t sequence,std::int64_t observedNs,std::int64_t nowNs)noexcept;
    void Clear()noexcept;
    SelectedObservationCounters Counters()const noexcept;
    void Report(std::ostream&)const;
private:
    struct BindingStorage {
        std::vector<std::byte> executable;
        SelectedMeshesBinding binding{};
        ReloadStateMemory memory{};
        std::uint32_t imageBase=0;
    };
    mutable std::mutex mutex_;
    std::shared_ptr<const BindingStorage> binding_;
    std::atomic<std::shared_ptr<const SelectedMeshesSnapshot>> published_{};
    std::atomic_flag observing_=ATOMIC_FLAG_INIT;
    std::array<std::shared_ptr<const CarriedMeshesSnapshot>,8> carried_{};
    SelectedObservationCounters counters_{};
    std::uint64_t generation_=0;
    ReloadStateOwner lastOwner_{};
    std::uint64_t lastSequence_=0;
    std::int64_t lastObservedNs_=0;
    struct IdentitySample {
        std::shared_ptr<const SelectedMeshesSnapshot> snapshot;
        std::uint64_t generation=0,firstSequence=0,observations=0;
    };
    static constexpr unsigned IdentityCapacity=8;
    std::array<IdentitySample,IdentityCapacity> identities_{};
    unsigned identityCount_=0;
    std::uint64_t identityDropped_=0;
};
} // namespace fvr::bc2
