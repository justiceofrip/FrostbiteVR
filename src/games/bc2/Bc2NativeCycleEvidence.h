#pragma once
#include "Bc2ReloadHold.h"

namespace fvr::bc2 {
struct ReloadFlowRecord;
// Exact original M95 client prediction rewind observed in stock234. This is
// usable only before any native hold; it never grants or completes a lease.
bool CycleM95PreHoldRewind(const ReloadFlowRecord&,const ReloadHoldIdentity&,unsigned branch)noexcept;
struct NativeCycleHeldBoundary {
    std::uint32_t current=0,previous=0,next=0;
    float minArmTimer=0,maxTimer=0;
    bool emptyAmmunition=false; // Explicit zero-loaded last-shot boundary only.
};
// Original snapshot progression between a configured post-shot wait and its
// manual hold boundary. Only pre-hold service policy may consume this proof.
bool CyclePreHoldSnapshotProgression(const ReloadFlowRecord&,const ReloadHoldIdentity&,unsigned branch,
    const NativeCycleHeldBoundary&,float precedingMaximum)noexcept;
struct NativeCycleCommitEvidence {
    std::uint64_t invocation=0,update=0;
    unsigned branch=3,beforeCurrent=0,beforePrevious=0,beforeNext=0;
    unsigned current=0,previous=0,next=0;
    int loaded=0,reserve=0;
    float timer=0;
    std::int64_t beginNs=0,endNs=0;
};
struct NativeCycleRestoreEvidence {
    std::uint64_t invocation=0;
    unsigned branch=3,beforePrevious=0;
    int loaded=0,reserve=0;
    std::int64_t beginNs=0,endNs=0;
    unsigned current=0,next=0;
    float beforeTimer=0,timer=0;
};
// Completed, independently numbered callback evidence only. Diagnostic record
// IDs, sibling reads and current snapshots cannot substitute for an own Update.
// The caller still owns code/config admission, invocation freshness and replay
// protection, all-three cohort agreement, and exclusion of ammunition commands.
bool ValidateCycleUpdate(const ReloadFlowRecord&,const ReloadHoldIdentity&,unsigned branch)noexcept;
bool CycleShot(const ReloadFlowRecord&,const ReloadHoldIdentity&,unsigned branch)noexcept;
// Exact last-round SPAS exit differs from its positive-loaded pump path.
bool CycleEmptyShot(const ReloadFlowRecord&,const ReloadHoldIdentity&,unsigned branch)noexcept;
bool CycleHold(const ReloadFlowRecord&,const ReloadHoldIdentity&,unsigned branch,
    const NativeCycleHeldBoundary&,bool arming=false)noexcept;
// Exact client prediction Restore may repeat current7 as previous7. This
// evidence establishes that specific original callback, not a shot or hold.
std::optional<NativeCycleRestoreEvidence> CycleRestore(const ReloadFlowRecord&,
    const ReloadHoldIdentity&,unsigned branch,const NativeCycleHeldBoundary&)noexcept;
// Actual M95 physical239 clientA forward correction8->2 using an unchanged
// copied native ready snapshot. Requires separately proven clientB completion
// and a subsequent own idle Update; this receipt alone never grants Ready.
bool CycleM95ForwardReadyRestore(const ReloadFlowRecord&,const ReloadHoldIdentity&)noexcept;
bool CycleM95ForwardIdleUpdate(const ReloadFlowRecord&,const ReloadHoldIdentity&)noexcept;
// Exact original client prediction callback with unchanged phase/counts and
// its copied snapshot. The service must qualify phase and ordered history;
// this receipt alone never grants a hold or native readiness.
std::optional<NativeCycleRestoreEvidence> CyclePredictionRestore(const ReloadFlowRecord&,
    const ReloadHoldIdentity&,unsigned branch)noexcept;
// A direct child Commit of the specified actual still-open Update. This only
// reports its exact transition; native-ready needs all required ordered children
// plus that Update's successful completion, never this value alone.
std::optional<NativeCycleCommitEvidence> CycleCommit(const ReloadFlowRecord&,
    const ReloadHoldIdentity&,unsigned branch,std::uint64_t parentUpdateId)noexcept;
}
