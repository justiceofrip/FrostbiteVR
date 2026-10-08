#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include <ostream>

namespace fvr::bc2 {
// Diagnostic outcomes only. OwnerMatched does not imply a delivered stereo pair,
// and BridgeNoLease does not distinguish IPC starvation from bridge contention.
enum class StereoProgressReason : unsigned {
    OwnerMatched, WorldChanged, RequestChanged, RequestWorldChanged,
    RequestFlagsChanged, EyeTypeChanged, EyeOwnerChanged,
    FrameNotAdvanced, RetirementFrameWait, BeginActiveTransaction,
    BeginRestoreFailure, BridgeNoLease, ViewportUnreadable, ViewportSizeChanged,
    BeginAccepted, RetirementBorrowedWait, RetirementReferenceWait, Count
};
constexpr std::array<const char*, unsigned(StereoProgressReason::Count)> StereoProgressNames{
    "owner_matched", "world_changed", "request_changed", "request_world_changed",
    "request_flags_changed", "eye_type_changed", "eye_owner_changed",
    "frame_not_advanced", "retirement_frame_wait", "begin_active_transaction",
    "begin_restore_failure", "bridge_no_lease", "viewport_unreadable", "viewport_size_changed",
    "begin_accepted", "retirement_borrowed_wait", "retirement_reference_wait"
};
struct StereoProgressSample {
    std::uint64_t tickMs=0;
    std::uint32_t stage=0, frame=0, incomingFrame=0, retireFrame=0;
    std::uint32_t world=0, request=0, expectedWorld=0, expectedRequest=0, eye=0, expectedEyeType=0;
    std::uint32_t checked=0, requestWorld=0, requestFlags=0, eyeType=0, eyeOwner=0;
    std::uint32_t trackedState=0, sampling=0;
};
// Mirrors the existing native guard's exact short-circuit order. The caller
// supplies U32; this adds no memory read and never dereferences a rejected owner.
template<class Read32>
StereoProgressReason CheckStereoProgressOwner(StereoProgressSample& s, Read32&& read) noexcept {
    if(s.world!=s.expectedWorld)return StereoProgressReason::WorldChanged;
    if(s.request!=s.expectedRequest)return StereoProgressReason::RequestChanged;
    s.requestWorld=read(std::uintptr_t(s.request)+8);s.checked|=1;
    if(s.requestWorld!=s.world)return StereoProgressReason::RequestWorldChanged;
    s.requestFlags=read(std::uintptr_t(s.request)+0xc4);s.checked|=2;
    if(s.requestFlags!=3)return StereoProgressReason::RequestFlagsChanged;
    s.eyeType=read(s.eye);s.checked|=4;
    if(s.eyeType!=s.expectedEyeType)return StereoProgressReason::EyeTypeChanged;
    s.eyeOwner=read(std::uintptr_t(s.eye)+0x70);s.checked|=8;
    if(s.eyeOwner!=s.request)return StereoProgressReason::EyeOwnerChanged;
    return StereoProgressReason::OwnerMatched;
}

// Fixed storage, persistent for this probe's lifetime. Counters remain available
// even if a sample copy loses the nonblocking diagnostic gate. Nothing waits or
// performs native work while holding it. Report copies are safe with retained
// hooks: they explicitly report an unavailable sample snapshot instead of racing.
class StereoProgressEvidence {
    static constexpr auto Size=unsigned(StereoProgressReason::Count);
public:
    struct Rows {bool present=false;StereoProgressSample first{},last{};};
    struct Snapshot {
        std::array<std::uint64_t,Size> counts{};
        std::array<Rows,Size> rows{};
        std::uint64_t droppedSamples=0;
        bool samplesAvailable=false;
    };
    void Record(StereoProgressReason reason,const StereoProgressSample& sample) noexcept {
        const auto i=unsigned(reason);if(i>=Size)return;
        counts_[i].fetch_add(1,std::memory_order_relaxed);
        if(gate_.test_and_set(std::memory_order_acquire)){
            dropped_.fetch_add(1,std::memory_order_relaxed);return;
        }
        auto& row=rows_[i];if(!row.present){row.first=sample;row.present=true;}row.last=sample;
        gate_.clear(std::memory_order_release);
    }
    Snapshot Read() noexcept {
        Snapshot out;
        for(unsigned i=0;i<Size;++i)out.counts[i]=counts_[i].load(std::memory_order_relaxed);
        out.droppedSamples=dropped_.load(std::memory_order_relaxed);
        if(!gate_.test_and_set(std::memory_order_acquire)){
            out.rows=rows_;out.samplesAvailable=true;gate_.clear(std::memory_order_release);
        }
        return out;
    }
    static void WriteJson(std::ostream& out,const Snapshot& snapshot) {
        out<<"{\"samples_available\":"<<(snapshot.samplesAvailable?"true":"false")
           <<",\"dropped_samples\":"<<snapshot.droppedSamples<<",\"outcomes\":[";
        for(unsigned i=0;i<Size;++i){
            if(i)out<<',';
            out<<"{\"reason\":\""<<StereoProgressNames[i]<<"\",\"count\":"<<snapshot.counts[i];
            if(snapshot.samplesAvailable&&snapshot.rows[i].present){
                out<<",\"first\":";WriteSample(out,snapshot.rows[i].first);
                out<<",\"last\":";WriteSample(out,snapshot.rows[i].last);
            }
            out<<'}';
        }
        out<<"]}";
    }
private:
    static void WriteSample(std::ostream& out,const StereoProgressSample& s){
        out<<"{\"tick_ms\":"<<s.tickMs<<",\"stage\":"<<s.stage<<",\"frame\":"<<s.frame
           <<",\"incoming_frame\":"<<s.incomingFrame<<",\"retire_frame\":"<<s.retireFrame
           <<",\"world\":"<<s.world<<",\"request\":"<<s.request
           <<",\"expected_world\":"<<s.expectedWorld<<",\"expected_request\":"<<s.expectedRequest
           <<",\"eye\":"<<s.eye<<",\"expected_eye_type\":"<<s.expectedEyeType
           <<",\"checked\":"<<s.checked<<",\"request_world\":"<<s.requestWorld
           <<",\"request_flags\":"<<s.requestFlags<<",\"eye_type\":"<<s.eyeType
           <<",\"eye_owner\":"<<s.eyeOwner<<",\"tracked_state\":"<<s.trackedState
           <<",\"sampling\":"<<s.sampling<<'}';
    }
    std::array<std::atomic<std::uint64_t>,Size> counts_{};
    std::atomic<std::uint64_t> dropped_=0;
    std::array<Rows,Size> rows_{};
    std::atomic_flag gate_=ATOMIC_FLAG_INIT;
};
} // namespace fvr::bc2
