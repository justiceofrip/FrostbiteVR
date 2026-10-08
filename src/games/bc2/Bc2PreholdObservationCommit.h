#pragma once
#include "Bc2PreholdCleanup.h"
namespace fvr::bc2 {
struct PreholdObservationKey {
 ReloadHoldIdentity identity{};std::uint64_t cycle=0,ownerRevision=0,cancelEpoch=0,callbackRevision=0;
 unsigned family=0,profile=0,phase=0;std::int64_t operationDeadlineNs=0;
 bool operator==(const PreholdObservationKey&)const=default;
};
// CPU commit check only. No receipt/helper/control authority, no renewed deadline.
inline bool PreholdObservationMayCommit(const PreholdObservationKey& captured,const PreholdObservationKey& current,
 const PreholdCleanupEvidence& e,const ReloadObservedConfig& config,std::int64_t now)noexcept {
 return captured==current&&captured.cycle&&captured.ownerRevision&&captured.operationDeadlineNs>0&&
  e.operation==captured.cycle&&e.revision==captured.ownerRevision&&e.source.identity==captured.identity&&
  e.source.config==config&&e.coherent&&e.configVerified&&e.source.verified&&e.observedNs>0&&e.observedNs<=now&&
  e.deadlineNs>now&&e.deadlineNs>=e.observedNs&&e.deadlineNs-e.observedNs<=PreholdCleanupLedger::ObservationNs;
}
}
