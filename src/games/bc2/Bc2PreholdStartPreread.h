#pragma once
#include "Bc2PreholdObservationCommit.h"
#include "Bc2ReloadRequestCycle.h"
namespace fvr::bc2 {
inline bool PreholdStartPublishedMatches(const PreholdCleanupEvidence& e,const ReloadStateSnapshot& snapshot)noexcept {
 if(snapshot.owner!=e.source.identity.owner||snapshot.config!=e.source.config)return false;
 for(unsigned n=0;n<2;++n)if(snapshot.branches[n].address!=e.source.identity.firing[n]||
     snapshot.branches[n].loaded!=e.source.branches[n].loaded||snapshot.branches[n].reserve!=e.source.branches[n].reserve)return false;
 return true;
}
// Commit a native preread only under the existing quiet Start exclusion.
// Exactly one revision increment is the calling Active registration itself.
inline bool PreholdStartPrereadMayCommit(const PreholdObservationKey& captured,PreholdObservationKey current,
 const PreholdCleanupEvidence& e,const ReloadCycleControl& control,const ReloadObservedConfig& config,std::int64_t now)noexcept {
 if(captured.callbackRevision==UINT64_MAX||current.callbackRevision!=captured.callbackRevision+1)return false;
 current.callbackRevision=captured.callbackRevision;
 if(captured!=current||e.revision!=captured.ownerRevision||e.operation!=control.cycle||
    e.source.identity!=control.identity||e.source.config!=config||control.observedNs<=0||
    control.observedNs>now||now>=control.deadlineNs)return false;
 auto confirmed=e;confirmed.callbacksExcluded=true;PreholdCleanupLedger trial;
 return trial.Prepare(confirmed,now);
}
}
