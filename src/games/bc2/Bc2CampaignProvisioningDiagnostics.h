#pragma once
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace fvr::bc2::campaignProvisioning {
enum class AdmissionEvent:std::uint32_t {
    SeatHook,RuntimeGateBusy,SourceGateBusy,SourceUnavailable,SourceStale,
    SourceOwnerVerification,SoldierMismatch,InputGateRefused,NestedDepth,ServiceEntered,Count
};
// Diagnostic only: ten saturating five-bit counters and a non-reused scope tag
// share ONE atomic word. Freeze is a single linearization point; delayed old
// callbacks cannot alter a frozen or subsequent request. No engine reads,
// allocation, new lock, or scheduling authority. 31 means "at least 31".
class AdmissionCounters {
public:
    static constexpr unsigned Count=unsigned(AdmissionEvent::Count),Bits=5,Limit=31;
    static constexpr unsigned TagShift=Count*Bits,MaxScopes=(1u<<(63-TagShift))-1;
    static constexpr std::uint64_t Frozen=std::uint64_t(1)<<63;
    static_assert(Count==10&&std::atomic<std::uint64_t>::is_always_lock_free);
    struct Snapshot {
        std::array<std::uint32_t,Count> counts{};
        std::uint32_t scope=0,frozen=1,saturatedMask=0;
    };
    // Begin/Freeze are serialized by the existing provisioning runtime gate.
    // Exhaustion disables diagnostics only; a tag is never reused in-process.
    void Begin(std::int64_t accepted,std::int64_t deadline)noexcept {
        Freeze();accepted_.store(accepted);deadline_.store(deadline);
        if(scopes_==MaxScopes){state_.store(Frozen,std::memory_order_release);return;}
        state_.store(std::uint64_t(++scopes_)<<TagShift,std::memory_order_release);
    }
    std::uint64_t Token()const noexcept {
        const auto state=state_.load(std::memory_order_acquire);
        return state&Frozen?0:state>>TagShift;
    }
    void Record(std::uint64_t token,AdmissionEvent event,std::int64_t now)noexcept {
        if(!token||unsigned(event)>=Count||now<accepted_.load()||now>=deadline_.load())return;
        const auto shift=unsigned(event)*Bits;
        auto state=state_.load(std::memory_order_acquire);
        // Each failed STRONG CAS means a monotonic counter increment or a
        // scope/freeze change. At most Count*Limit competing increments can
        // occur in this scope, so this loop is bounded (no spurious retries).
        for(unsigned attempt=0;attempt<=Count*Limit;++attempt){
            if(state&Frozen||state>>TagShift!=token||((state>>shift)&Limit)==Limit)return;
            if(state_.compare_exchange_strong(state,state+(std::uint64_t(1)<<shift),
                    std::memory_order_acq_rel,std::memory_order_acquire))return;
        }
    }
    bool Freeze()noexcept{return !(state_.fetch_or(Frozen,std::memory_order_acq_rel)&Frozen);}
    Snapshot Read()const noexcept {
        const auto state=state_.load(std::memory_order_acquire);Snapshot out;
        out.scope=std::uint32_t((state&~Frozen)>>TagShift);out.frozen=bool(state&Frozen);
        for(unsigned n=0;n<Count;++n){out.counts[n]=std::uint32_t((state>>(n*Bits))&Limit);
            if(out.counts[n]==Limit)out.saturatedMask|=1u<<n;}
        return out;
    }
private:
    std::atomic<std::uint64_t> state_{Frozen};
    std::atomic<std::int64_t> accepted_{0},deadline_{0};
    unsigned scopes_=0;
};

enum class PreflightOutcome:std::uint32_t {
    Accepted,SnapshotMismatch,RequestMismatch,BoundaryRejected,RevalidationRejected,
    PolicyPhase,SeatSequence,OwnerChanged,ServerSequence,NestedDepth,Count
};
enum class PreflightServerEvent:std::uint32_t {Advanced,Invalid,Conflict,NotAdvanced,WrongFiring,OwnerRejected,Count};
struct PreflightSequenceProgress {
    // lastSequence starts at the policy's sequence at Submit. firstSequence is
    // the first nonzero sequence sampled by this track, not the first advance.
    std::uint64_t firstSequence=0,lastSequence=0;
    // Advancing, repeated, regressing, unavailable (zero), relative to the
    // previous sample (or Submit baseline). No sequence-gap size is a count.
    std::array<std::uint32_t,4> counts{};
};
struct PreflightDetails {
    // Saturation bits: outcomes 0..9, initial boundary 10..22, second boundary
    // 23..35, depths 36..39, server events 40..45, seat progression 46..49,
    // depth-zero top-level seat progression 50..53. 65535 is a lower bound.
    std::uint64_t saturatedMask=0,serverSequenceAtSubmit=0,serverSequenceLatest=0;
    PreflightSequenceProgress seats{},zeroDepthSeats{};
    std::uint32_t maximumNativeDepth=0,reserved=0;
    std::array<std::uint32_t,10> outcomes{};
    // BoundaryFailure values, including None. Count only checks actually run;
    // revalidation is the existing second validation inside policy Preflight.
    std::array<std::uint32_t,13> boundary{},revalidatedBoundary{};
    // Exact depth 0, 1, 2, and >=3, for all in-window preflight SeatEntry calls.
    std::array<std::uint32_t,4> nativeDepth{};
    std::array<std::uint32_t,6> serverEvents{};
};
static_assert(std::is_trivially_copyable_v<PreflightDetails>);
static_assert(sizeof(PreflightSequenceProgress)==32&&sizeof(PreflightDetails)==280);
static_assert(offsetof(PreflightDetails,outcomes)==96&&offsetof(PreflightDetails,serverEvents)==256);

// Serialized solely by the existing runtime gate. Service owns the exact arm
// window and terminal freeze. No native reads, clocks, allocation, locks, or
// callback IO; every update has fixed work independent of callback count.
class PreflightDiagnostics {
public:
    static constexpr std::uint32_t Limit=65535;
    void Begin(std::uint64_t sequence)noexcept{
        details_={};details_.serverSequenceAtSubmit=details_.serverSequenceLatest=sequence;
        details_.seats.lastSequence=details_.zeroDepthSeats.lastSequence=sequence;
    }
    void Seat(std::uint64_t sequence,std::uint32_t nativeDepth,std::uint32_t seatDepth)noexcept{
        const auto bucket=nativeDepth<3?nativeDepth:3;
        Increment(details_.nativeDepth[bucket],36+bucket);
        if(nativeDepth>details_.maximumNativeDepth)details_.maximumNativeDepth=nativeDepth;
        Sequence(details_.seats,sequence,46);
        if(!nativeDepth&&seatDepth==1)Sequence(details_.zeroDepthSeats,sequence,50);
    }
    void Outcome(PreflightOutcome outcome)noexcept{
        const auto index=unsigned(outcome);if(index<details_.outcomes.size())Increment(details_.outcomes[index],index);
    }
    void Boundary(unsigned boundary,bool revalidated=false)noexcept{
        auto& counts=revalidated?details_.revalidatedBoundary:details_.boundary;
        if(boundary<counts.size())Increment(counts[boundary],(revalidated?23:10)+boundary);
    }
    void Server(PreflightServerEvent event,std::uint64_t sequence)noexcept{
        details_.serverSequenceLatest=sequence;const auto index=unsigned(event);
        if(index<details_.serverEvents.size())Increment(details_.serverEvents[index],40+index);
    }
    const PreflightDetails& Read()const noexcept{return details_;}
private:
    void Increment(std::uint32_t& count,unsigned bit)noexcept{
        if(count<Limit)++count;
        if(count==Limit)details_.saturatedMask|=std::uint64_t(1)<<bit;
    }
    void Sequence(PreflightSequenceProgress& progress,std::uint64_t sequence,unsigned bit)noexcept{
        const unsigned kind=!sequence?3:sequence>progress.lastSequence?0:sequence==progress.lastSequence?1:2;
        Increment(progress.counts[kind],bit+kind);
        if(sequence){if(!progress.firstSequence)progress.firstSequence=sequence;progress.lastSequence=sequence;}
    }
    PreflightDetails details_{};
};
}
