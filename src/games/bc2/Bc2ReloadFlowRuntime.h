#include "Bc2ReloadObservation.h"
#include "Bc2PreholdEntryObservation.h"
#include "Bc2ReloadKeepAlive.h"
#pragma once
#include "Bc2OwnerPublication.h"
#include "Bc2ReloadFlow.h"
#include "Bc2ReloadHold.h"
#include "Bc2AmmoReserve.h"
#include "Bc2ReloadRequestCycle.h"
#include "Bc2MagazineReloadCycle.h"
#include "Bc2ReloadFamily.h"
#include "Bc2MagazineStart.h"
#include "Bc2MagazineEmptyDiagnostic.h"
#include "Bc2ReloadRetirement.h"
#include <ostream>
namespace fvr::bc2 {
enum class ReloadFlowEvent : std::uint8_t { Update,Commit,Transfer,Restore };
struct ReloadFlowBoundary {
    ReloadStateOwner owner{};std::uint64_t snapshotSequence=0;
    std::uint32_t firing=0,wrapperOffset=0;
    // branch2 uses the server item +0x10; owner.weapon remains the client item.
    std::uint32_t serverPlayer=0,serverSoldier=0,serverItem=0;
    std::uint8_t branch=0,soldierFlags=0;
    std::uint32_t current=0,previous=0,next=0;
    float timer=0;std::int32_t loaded=0,reserve=0;std::uint8_t flagsA8=0;
};
bool ReloadReserveCopiesAgree(const ReloadHoldIdentity&,const std::array<ReloadFlowBoundary,3>&,
    const std::array<ReloadFlowBoundary,3>&,const std::array<int,3>&,const std::array<int,3>&)noexcept;
bool ReloadReserveIdle(const ReloadHoldIdentity&,const std::array<ReloadFlowBoundary,3>&,
    const std::array<ReloadFlowBoundary,3>&,const std::array<int,3>&,const std::array<int,3>&)noexcept;
bool ReloadReserveInputReady(const ReloadHoldIdentity&,const std::array<ReloadFlowBoundary,3>&,
    const std::array<ReloadFlowBoundary,3>&,const std::array<int,3>&,const std::array<int,3>&)noexcept;
// Fast callback boundary reader. Published must come from ReadReloadState; its
// exact configuration/branch identities are rechecked without reflection scans.
// Stable repeated reads are not a claim of atomicity across native threads.
enum class ReloadFlowBoundaryFailure : unsigned {None,Lease,Branch,ScopeBefore,StateReadBefore,ScopeAfter,ChangedScope,StateReadAfter,ChangedState,StateIdentity,StateRange};
struct ReloadFlowBoundaryDiagnostic {
    ReloadFlowBoundaryFailure failure=ReloadFlowBoundaryFailure::None;
    std::uint8_t beforeSoldierFlags=0,afterSoldierFlags=0,changedBefore=0,changedAfter=0;
    unsigned changedOffset=UINT32_MAX;
    bool differsOnlySoldierFlags=false;
};
std::optional<ReloadFlowBoundary> ReadReloadFlowBoundary(const ReloadStateMemory&,const ReloadFlowBinding&,
    std::uint32_t base,const ReloadStateSnapshot& published,std::int64_t deadlineNs,
    std::uint32_t firing,std::int64_t nowNs,ReloadFlowBoundaryDiagnostic* diagnostic=nullptr)noexcept;
// Structural retention only; deliberately supplies no counts, state, timer or
// hold authority. Sibling firing state may advance while this owner stays valid.
bool ReadReloadFlowOwner(const ReloadStateMemory&,const ReloadFlowBinding&,
    std::uint32_t base,const ReloadStateSnapshot& published,std::int64_t deadlineNs,
    std::uint32_t firing,std::int64_t nowNs,ReloadFlowBoundaryDiagnostic* diagnostic=nullptr)noexcept;
struct ReloadFlowEventInput {
    ReloadFlowEvent kind=ReloadFlowEvent::Update;
    std::uint64_t parent=0,update=0;
    // Optional independent evidence IDs. Diagnostic record IDs remain unchanged.
    std::uint64_t nativeInvocation=0,nativeParent=0,nativeUpdate=0;
    std::uint32_t thread=0,depth=0,caller=0,context=0,argument=0;
    std::int64_t nowNs=0;std::uint64_t tickMs=0;
    ReloadFlowBoundary boundary{};
    std::array<std::byte,0x30> copiedContext{};bool contextCopied=false;
    std::optional<ReloadTransferPath> transferPath;
    std::array<std::byte,0x40> copiedSnapshot{};bool snapshotCopied=false;
};
struct ReloadFlowEventEnd {
    std::array<std::byte,0x40> copiedSnapshot{};bool snapshotCopied=false;
    std::uint32_t thread=0;std::int64_t nowNs=0;std::uint64_t tickMs=0;
    std::optional<ReloadFlowBoundary> boundary;
    std::array<std::byte,0x30> copiedContext{};bool contextCopied=false;
    ReloadDeltaOverride hold{};bool holdRequested=false;
};
// True only for a fully captured owner-retained restore whose output matches
// the captured source fields and whose source bytes did not change. No authority
// or gameplay acknowledgement is inferred from this observation.
bool ReloadRestoreMatched(const struct ReloadFlowRecord&)noexcept;
struct ReloadFlowRecord {
    std::uint64_t id=0;ReloadFlowEventInput entry{};ReloadFlowEventEnd exit{};
    bool finished=false,identityRetained=false;
};
// Independent callback evidence: no recording window, capacity, or slot ID.
// One object lives on each original invocation's stack. The native TLS caller
// supplies only the actual still-open parent from the same owned firing chain.
class ReloadFlowInvocation {
public:
    bool Begin(std::uint64_t,const ReloadFlowEventInput&,const ReloadFlowInvocation* parent=nullptr)noexcept;
    std::optional<ReloadFlowRecord> Finish(const ReloadFlowEventEnd&)noexcept;
    const ReloadFlowRecord& Record()const noexcept{return record_;}
private:
    ReloadFlowRecord record_{};
};
class ReloadFlowInvocationIds {
public:
    explicit ReloadFlowInvocationIds(std::uint64_t previous=0)noexcept:previous_(previous){}
    std::uint64_t Next()noexcept;
private:
    std::atomic<std::uint64_t> previous_;
};
// Storage owns no locks; runtime wraps individual operations in nonblocking
// try-locks and never holds a lock across an original native invocation.
class ReloadFlowRecords {
public:
    static constexpr unsigned Capacity=20480;
    std::uint64_t Begin(const ReloadFlowEventInput&)noexcept;
    bool End(std::uint64_t,const ReloadFlowEventEnd&)noexcept;
    std::span<const ReloadFlowRecord> Records()const noexcept{return {records_.data(),count_};}
    unsigned Dropped()const noexcept{return dropped_;}
    unsigned Rejected()const noexcept{return rejected_;}
private:
    std::array<ReloadFlowRecord,Capacity> records_{};unsigned count_=0,dropped_=0,rejected_=0;
};
namespace reloadFlowRuntime {
// Cached existing owned-Update observation only, no native read or renewal.
std::optional<ReloadPreholdEntryObservation> ReadPreholdEntry(const ReloadHoldIdentity&,std::uint64_t cycle,std::int64_t now)noexcept;
// Fresh coherent reserve before Start; no cycle/held state is invented.
std::optional<Bc2AmmoReserveLease> ReadReserve()noexcept;
// Opt-in diagnostic observer only; native request policy selection is unchanged.
std::optional<Bc2AmmoReserveLease> ReadDiagnosticFireReserve()noexcept;
ReloadReserveObservation ReadReserveObserved()noexcept;
// Read-only diagnostic snapshot; sequential branch reads are not atomic.
struct RequestProbeSnapshot {
    ReloadHoldIdentity identity{};
    std::array<ReloadFiringObservation,3> branches{};
    std::array<unsigned,3> applied{},restored{},transfers{};
    std::array<std::uint64_t,3> lastTransfer{};
    std::int64_t observedNs=0;
    std::uint64_t cycle=0,pendingRequest=0,revision=0;
    unsigned phase=0,failure=0,activeCallbacks=0,patchFailures=0,restoreFailures=0;
    bool unresolved=false;unsigned family=~0u; // Existing policy snapshot only; no new native authority.
};
std::optional<RequestProbeSnapshot> ReadRequestProbeSnapshot()noexcept;
// x86 only. MinHook must already be initialized. Creates disabled hooks only;
// caller performs its existing global enable after all modules install.
// Copy/type callbacks must be bounded, exception-safe and remain alive through
// Stop/drain. Default observation makes no writes. Explicit diagnosticHold may
// temporarily replace only current Update context+18 and always restores it.
// The original is called once; physical/manual reload remains disabled.
// diagnosticRound is a separate, mutually-exclusive one-round/re-hold fixture.
bool EnablePumpHoldDiagnostic()noexcept; // Pre-Start only; existing exact delta transaction, isolated native trial.
bool Install(std::span<const std::byte>,const engine::PeImage&,std::uintptr_t imageBase,const ReloadStateMemory&,bool diagnosticHold=false,bool diagnosticRound=false,bool requestCycle=false,bool magazineRequests=false);
// Explicit request mode only, mutually exclusive with both diagnostics. No
// reload input is fabricated. Gameplay owns the physical cycle intent/bridge.
// These use the current native clock, exact owner and fresh original timestamps.
// Pre-Start capability grant; normal SPAS request mode stays the initial family.
bool EnableMagazineRequestCycles()noexcept;
// Atomic value snapshot safe for incremental telemetry; no report serialization
// or policy/container access while native callbacks run.
struct MagazineEmptyControlCounters {
    std::array<unsigned,3> requested{},applied{},restored{};
    unsigned patchFailures=0,restoreFailures=0,receiptFailures=0;
};
MagazineEmptyControlCounters ReadMagazineEmptyControlCounters()noexcept;
// Read-only context for rejected empty-Step evidence; never a control capability.
void PublishMagazineInteractionDiagnostic(const MagazineInteractionDiagnostic&)noexcept;
// Explicit idle Gather selection only. Caller has reconciled/retired both
// physical consumers. Adapter independently verifies exact current ownership,
// configuration, prior native retirement, and callback exclusion. No ammo write.
bool SelectRequestFamily(ReloadNativeFamily,const ReloadStateOwner& currentOwner)noexcept;
// Same selection boundary, shared state-machine family, immutable profile data.
// Unknown/unreviewed profiles reject before changing the active dispatch.
bool SelectMagazineRequestProfile(NativeMagazineProfileId,const ReloadStateOwner& currentOwner)noexcept;
// Immutable descriptor selection from the current published native config.
// This read confers no reload/ammunition capability; normal native operations
// still validate exact current configuration, ownership and original deadlines.
std::optional<NativeMagazineProfileId> ReadMagazineRequestProfile(const ReloadStateOwner&)noexcept;
std::optional<ReloadHoldIdentity> RequestIdentity()noexcept;
bool StartRequestCycle(const ReloadCycleControl&)noexcept;
bool KeepAliveRequestCycle(const ReloadCycleControl&)noexcept;
ReloadKeepAliveResult KeepAliveRequestCycleObserved(const ReloadCycleControl&)noexcept;
std::optional<ReloadRoundLease> RequestLease(const ReloadHoldIdentity&,std::uint64_t cycle)noexcept;
bool SubmitRequest(const Bc2ReloadNativeRequest&)noexcept;
std::optional<Bc2ReloadAckEvidence> TakeRequestAcknowledgement(const ReloadHoldIdentity&,std::uint64_t cycle)noexcept;
// Isolated exact-XM8 magazine mode. Unseat acknowledges an established native
// reload hold, not an ammunition removal. No Start method fabricates Reload.
MagazineCycleStartResult StartMagazineRequestCycleObserved(const ReloadCycleControl&,const interaction::ManualReloadRequest&,std::optional<ReloadMagazineStartupPulse> pulse=std::nullopt)noexcept;
// Read-only policy registration resolution after an Unknown startup. Requires
// fresh callback exclusion; no owner reads, native calls or synthetic receipts.
MagazineCycleStartResult InspectMagazineRequestCycleStart(const ReloadHoldIdentity&,std::uint64_t cycle,std::optional<ReloadMagazineStartupPulse> pulse=std::nullopt)noexcept;
bool StartMagazineRequestCycle(const ReloadCycleControl&,const interaction::ManualReloadRequest&)noexcept;
ReloadMagazineLeaseObservation ObserveMagazineLease(const ReloadHoldIdentity&,std::uint64_t cycle)noexcept;
std::optional<ReloadMagazineLease> RequestMagazineLease(const ReloadHoldIdentity&,std::uint64_t cycle)noexcept;
std::optional<ReloadMagazineGateAcknowledgement> TakeMagazineUnseatAcknowledgement(const ReloadHoldIdentity&,std::uint64_t cycle)noexcept;
bool SubmitMagazineRequest(const ReloadMagazineNativeRequest&)noexcept;
std::optional<ReloadMagazineAckEvidence> TakeMagazineAcknowledgement(const ReloadHoldIdentity&,std::uint64_t cycle)noexcept;
void CancelRequestCycle()noexcept;
// Cancels/drains the exact old policy cycle, even when its actor is no longer
// live. A verified receipt is not a transfer acknowledgement or ammo rollback.
std::optional<ReloadCycleRetirement> RetireRequestCycle(const ReloadHoldIdentity&,std::uint64_t cycle)noexcept;
// Publish fresh successful ReadReloadState snapshots at <=100ms cadence. Stop
// publishing/ClearOwner when the actor/item is unavailable. Deadline is absolute
// QPC-derived nanoseconds (same epoch as QueryPerformanceCounter), max250ms.
OwnerPublicationResult PublishOwnerObserved(const ReloadStateSnapshot&,std::int64_t deadlineNs)noexcept;
bool PublishOwner(const ReloadStateSnapshot&,std::int64_t deadlineNs)noexcept;
void ClearOwner()noexcept;
// One bounded recording session, at most20 seconds; no automatic reset/rearm.
void Start()noexcept;
// Disables only these hooks, drains for at most2s, retains trampolines/module.
// False means caller must keep the module loaded and may retry Stop.
bool Stop()noexcept;
// Complete JSON object. Full records are emitted only after a successful drain.
void Report(std::ostream&);
}
}
