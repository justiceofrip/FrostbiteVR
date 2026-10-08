#pragma once
#include "Bc2ReloadFlowRuntime.h"
#include "Bc2ReloadServer.h"

namespace fvr::bc2 {
struct ReloadPhaseReadEvidence {
    unsigned failure=0;
    std::uint8_t beforeFlags=0,afterFlags=0;
};
struct ReloadPhaseRetryAttempt {
    bool attempted=false,recovered=false,clockRejected=false;
    std::int64_t firstNs=0,retryNs=0;
    ReloadPhaseReadEvidence first{},second{};
};
inline bool ReloadPhaseOnlyRace(const ReloadFlowBoundaryDiagnostic& d)noexcept {
    return d.failure==ReloadFlowBoundaryFailure::ChangedScope&&d.differsOnlySoldierFlags&&
        (d.beforeSoldierFlags^d.afterSoldierFlags)==0x10;
}
inline bool ReloadPhaseOnlyRace(const ReloadServerBoundaryDiagnostic& d)noexcept {
    return d.failure==ReloadServerBoundaryFailure::ChangedLinks&&d.changedOnlySoldierFlags&&
        (d.observedSoldierFlags^d.afterSoldierFlags)==0x10;
}
inline ReloadPhaseReadEvidence ReloadPhaseEvidence(const ReloadFlowBoundaryDiagnostic& d)noexcept {
    return {unsigned(d.failure),d.beforeSoldierFlags,d.afterSoldierFlags};
}
inline ReloadPhaseReadEvidence ReloadPhaseEvidence(const ReloadServerBoundaryDiagnostic& d)noexcept {
    return {unsigned(d.failure),d.observedSoldierFlags,d.afterSoldierFlags};
}
// The supplied reader must perform its complete strict read on each call using
// the SAME original lease and deadline. Never pass cached pointers/state as the
// second result. Only an observed 0x10 phase-only scope race permits one retry.
// Successful snapshots, other failures and a second failure are never retried.
template<class Read,class Clock,class Diagnostic>
auto ReadReloadPhaseCoherent(Read&& read,Clock&& clock,std::int64_t firstNs,
    Diagnostic& diagnostic,ReloadPhaseRetryAttempt& attempt)noexcept -> decltype(read(firstNs,diagnostic)) {
    attempt={};auto result=read(firstNs,diagnostic);
    if(result||!ReloadPhaseOnlyRace(diagnostic))return result;
    attempt.attempted=true;attempt.firstNs=firstNs;attempt.first=ReloadPhaseEvidence(diagnostic);
    attempt.retryNs=clock();
    if(attempt.retryNs<firstNs||attempt.retryNs<=0){attempt.clockRejected=true;attempt.second=attempt.first;return result;}
    result=read(attempt.retryNs,diagnostic);
    attempt.recovered=bool(result);attempt.second=ReloadPhaseEvidence(diagnostic);return result;
}
}
