#pragma once
#include "Bc2ReloadPolicyLock.h"
#include <atomic>
#include <cstdint>
#include <optional>
#include <ostream>
#include <utility>

namespace fvr::bc2 {
enum class NativeCycleStepRoute {OrdinaryEmptyControl,IssuedHold,GuardedRecovery};
// Only an actually issued hold or continuous guard bypasses empty control.
inline NativeCycleStepRoute SelectNativeCycleStepRoute(bool frame,bool normalDecision,bool issuedHold)noexcept {
    return !frame?NativeCycleStepRoute::OrdinaryEmptyControl:
        !normalDecision?NativeCycleStepRoute::GuardedRecovery:
        issuedHold?NativeCycleStepRoute::IssuedHold:NativeCycleStepRoute::OrdinaryEmptyControl;
}
// Read exits once and selection enters once; any intervening callback retires it.
inline bool NativeCycleSelectionReadCurrent(std::uint64_t observed,std::uint64_t current)noexcept {
    return observed&&observed<=UINT64_MAX-2&&current==observed+2;
}
// Busy proves the operation was not dispatched. Rejected supplies no such
// retry permission; a callback or native outcome may already be ambiguous.
enum class Bc2NativeCycleSubmitResult:unsigned {Accepted,Busy,Rejected,RecoveryRequired,NotDispatched};
enum class NativeCycleAccess:unsigned {
    Observation,Control,Selection,Acknowledge,RecoveryAttempt,RecoverySubmit,
    NativeUpdate,NativeFinish,NativeCommit,NativeRestore,NativeStep,NativeMissing,
    ExplicitCancellation
};
constexpr bool NativeCycleAccessRequiresCancellation(NativeCycleAccess access)noexcept {
    return access>=NativeCycleAccess::NativeUpdate;
}
// Exactly the runtime's bounded gate, with explicit failure semantics. Merely
// missing a read or an undispatched command does not revoke existing debt.
class NativeCyclePolicyAccess {
    ReloadPolicyLock lock_;
    std::uint64_t before_=0,after_=0;
public:
    template<class Clock,class Pause>
    NativeCyclePolicyAccess(std::atomic_flag& gate,std::atomic<std::uint64_t>& cancellation,
        NativeCycleAccess access,Clock&& clock,Pause&& pause)noexcept:
        lock_(gate,std::forward<Clock>(clock),std::forward<Pause>(pause)) {
        before_=cancellation.load(std::memory_order_acquire);after_=before_;
        if(!lock_.Held()&&NativeCycleAccessRequiresCancellation(access))
            after_=cancellation.fetch_add(1,std::memory_order_acq_rel)+1;
    }
    bool Held()const noexcept{return lock_.Held();}
    const ReloadPolicyLockEvidence& Evidence()const noexcept{return lock_.Evidence();}
    std::uint64_t Before()const noexcept{return before_;}
    std::uint64_t After()const noexcept{return after_;}
};
struct NativeCycleContention {
    NativeCycleAccess access=NativeCycleAccess::Observation;
    ReloadPolicyLockEvidence lock{};
    std::uint64_t cancelBefore=0,cancelAfter=0,invocation=0;
    std::uint32_t firing=0,thread=0;
};
// First immutable contention cause; no IO, allocation or native memory read on
// the callback. It remains useful when the bounded full callback window is over.
class NativeCycleContentionJournal {
    std::atomic<unsigned> state_{0};
    NativeCycleContention value_{};
public:
    bool Capture(const NativeCycleContention& value)noexcept {
        if(value.lock.held||!value.lock.contended)return false;
        unsigned empty=0;if(!state_.compare_exchange_strong(empty,1,std::memory_order_relaxed))return false;
        value_=value;state_.store(2,std::memory_order_release);return true;
    }
    std::optional<NativeCycleContention> Read()const noexcept {
        if(state_.load(std::memory_order_acquire)!=2)return {};return value_;
    }
    static void WriteJson(std::ostream& out,const NativeCycleContention& value) {
        out<<"{\"access\":"<<unsigned(value.access)<<",\"thread\":"<<value.thread
           <<",\"firing\":"<<value.firing<<",\"invocation\":"<<value.invocation
           <<",\"lock_failure\":"<<unsigned(value.lock.failure)<<",\"attempts\":"<<value.lock.attempts
           <<",\"begin_ns\":"<<value.lock.beginNs<<",\"end_ns\":"<<value.lock.endNs
           <<",\"cancel_before\":"<<value.cancelBefore<<",\"cancel_after\":"<<value.cancelAfter<<'}';
    }
};
}
