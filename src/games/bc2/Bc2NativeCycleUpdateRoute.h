#pragma once
#include "Bc2NativeCycleService.h"

namespace fvr::bc2 {
// Caller prepares both applicable native proof sets outside the policy gate,
// then holds ONE service gate across route selection and decision reservation.
// No engine read, callback, original call or context write belongs in this API.
struct Bc2NativeCycleUpdateEvidence {
    const ReloadHoldInput* normal=nullptr;
    const NativeCycleRecoveryScope* recovery=nullptr;
    const ReloadFlowEventInput* entry=nullptr;
    std::uint32_t firing=0;
    std::uint64_t invocation=0;
    int capacity=0;
    std::int64_t deadlineNs=0;
};
struct Bc2NativeCycleUpdateRoute {
    bool recovering=false;
    Bc2NativeCycleDecision normal{};
    NativeCycleRecoveryDecision recovery{};
};
inline Bc2NativeCycleUpdateRoute EvaluateNativeCycleUpdateRoute(Bc2NativeCycleService& service,
    const Bc2NativeCycleUpdateEvidence& proof)noexcept {
    Bc2NativeCycleUpdateRoute out;out.recovering=service.Recovering();
    if(out.recovering){
        if(proof.recovery&&proof.entry)
            out.recovery=service.EvaluateRecovery(*proof.recovery,*proof.entry,proof.capacity,proof.deadlineNs);
        else service.Missing(proof.firing,proof.invocation);
    }else {
        if(proof.normal)out.normal=service.Evaluate(*proof.normal,proof.invocation);
        else service.Missing(proof.firing,proof.invocation);
    }
    return out;
}
}
