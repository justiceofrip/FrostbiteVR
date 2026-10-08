#pragma once
#include "Bc2ReloadRequestCycle.h"
#include <limits>
namespace fvr::bc2 {
struct ReloadCycleRetirement {
    ReloadHoldIdentity identity{};
    std::uint64_t cycle=0,event=0;
    std::int64_t observedNs=0,deadlineNs=0;
    bool verified=false;
};
// Shared request-entry exclusion is an adapter responsibility. Its proof must
// include this API's sole active scope and unchanged callback revision. No
// native owner lookup is needed: death can invalidate the owner being retired.
template<class Policy>
inline bool CancelAndDrainReloadCycle(Policy& policy,
    const ReloadHoldIdentity& identity,std::uint64_t cycle,bool callbacksDrained)noexcept {
    if(!callbacksDrained||!cycle||policy.Cycle()!=cycle||policy.Identity()!=identity)return false;
    policy.Cancel(ReloadRequestCycleFailure::Stopped);
    return policy.DrainCancelledInvocations(true);
}
class ReloadRetirementReceipts {
public:
    explicit ReloadRetirementReceipts(std::uint64_t event=0,std::int64_t observedNs=0)noexcept:lastEvent_(event),lastObserved_(observedNs){}
    // Call only after CancelAndDrain and the adapter's final unchanged-revision
    // check, while entry and policy exclusions are still held. This is a new
    // retirement observation, never an insertion ack or an ammo adjustment.
    template<class Policy>
    std::optional<ReloadCycleRetirement> Observe(const Policy& policy,
        const ReloadHoldIdentity& identity,std::uint64_t cycle,std::int64_t nowNs,bool callbacksDrained)noexcept {
        if(!callbacksDrained||!cycle||policy.Identity()!=identity||policy.Cycle()!=cycle||
           policy.Phase()!=ReloadRequestCyclePhase::Cancelled||nowNs<=0||nowNs<=lastObserved_||
           nowNs>std::numeric_limits<std::int64_t>::max()-200000000||lastEvent_==UINT64_MAX)return {};
        lastObserved_=nowNs;++lastEvent_;
        return ReloadCycleRetirement{identity,cycle,lastEvent_,nowNs,nowNs+200000000,true};
    }
private:
    std::uint64_t lastEvent_=0;
    std::int64_t lastObserved_=0;
};
// Counts are read now twice. Their observation begins before the FIRST read,
// while the original native owner deadline still limits the resulting lease.
// An old owner snapshot must not backdate newly read ammo before a native ack.
inline std::optional<std::int64_t> ReloadReserveReadDeadline(std::int64_t ownerObserved,
    std::int64_t ownerDeadline,std::int64_t countObserved,std::int64_t nowNs)noexcept {
    if(ownerObserved<=0||ownerDeadline<=ownerObserved||ownerDeadline-ownerObserved>250000000||
       countObserved<ownerObserved||nowNs<countObserved||nowNs>=ownerDeadline||
       countObserved>std::numeric_limits<std::int64_t>::max()-200000000)return {};
    const auto deadline=std::min(ownerDeadline,countObserved+200000000);
    return nowNs<deadline?std::optional<std::int64_t>{deadline}:std::nullopt;
}
}
