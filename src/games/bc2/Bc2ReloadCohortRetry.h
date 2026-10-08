#pragma once
#include "Bc2ReloadFlowRuntime.h"
#include "Bc2ReloadServer.h"

namespace fvr::bc2 {
struct ReloadCohortRejection {
    unsigned stage=0,callerBranch=3,readBranch=3;
    bool boundaryOkay=false,capacityOkay=false;
    ReloadFlowBoundaryDiagnostic client{};
    ReloadServerBoundaryDiagnostic server{};
};
struct ReloadCohortRetryAttempt {
    bool attempted=false,recovered=false,clockRejected=false;
    std::int64_t firstNs=0,retryNs=0;
    ReloadCohortRejection first{},second{};
};
inline bool ReloadCohortStateRace(const ReloadCohortRejection& d)noexcept {
    if(d.stage!=3||d.callerBranch>=3||d.readBranch>=3||d.boundaryOkay||!d.capacityOkay)return false;
    return d.readBranch<2?d.client.failure==ReloadFlowBoundaryFailure::ChangedState:
        d.server.failure==ReloadServerBoundaryFailure::State&&d.server.stateFailure==3;
}
// Every read must reconstruct the ENTIRE strict three-copy observation with
// fresh context/capacity reads under the SAME captured owner/config/deadline.
// The failed partial snapshot never becomes a result and grants no native write.
// A full strict reader's ChangedState race permits exactly one new observation;
// any second failure, identity/read failure, or expired source still rejects.
template<class Read,class Clock>
auto ReadReloadCohortCoherent(Read&& read,Clock&& clock,std::int64_t firstNs,
    ReloadCohortRejection& rejected,ReloadCohortRetryAttempt& attempt)noexcept -> decltype(read(firstNs,rejected)) {
    attempt={};auto result=read(firstNs,rejected);
    if(result||!ReloadCohortStateRace(rejected))return result;
    attempt.attempted=true;attempt.firstNs=firstNs;attempt.first=rejected;attempt.retryNs=clock();
    if(attempt.retryNs<=0||attempt.retryNs<firstNs){attempt.clockRejected=true;attempt.second=rejected;return result;}
    result=read(attempt.retryNs,rejected);attempt.recovered=bool(result);attempt.second=rejected;return result;
}
}
