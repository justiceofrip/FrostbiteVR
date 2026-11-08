#pragma once
#include <array>
#include <atomic>
#include <cstdint>

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
}
