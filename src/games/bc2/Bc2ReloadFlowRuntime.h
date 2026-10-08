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
#include "Bc2AmmoResourceBinding.h"
#include "Bc2AmmoResourceService.h"
#include <atomic>
#include <memory>
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
struct ReloadFlowRecord {
    std::uint64_t id=0;ReloadFlowEventInput entry{};ReloadFlowEventEnd exit{};
    bool finished=false,identityRetained=false;
};
// Pure captured-data comparison: consumers need no executable-hook runtime.
inline bool ReloadRestoreMatched(const ReloadFlowRecord& record)noexcept{
    if(record.entry.kind!=ReloadFlowEvent::Restore||!record.finished||!record.identityRetained||!record.exit.boundary||
       !record.entry.snapshotCopied||!record.exit.snapshotCopied||record.entry.copiedSnapshot!=record.exit.copiedSnapshot)return false;
    const auto source=DecodeReloadFiringSnapshot(record.entry.copiedSnapshot);if(!source)return false;
    const auto& after=*record.exit.boundary;
    return after.current==source->current&&after.next==source->next&&after.previous==record.entry.boundary.current&&
        after.timer==source->phaseTimer&&after.loaded==source->loaded&&after.reserve==source->reserve;
}
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
    static constexpr unsigned RecoveryCapacity=2*Capacity;
    static constexpr unsigned PumpCapacity=3*Capacity;
    ReloadFlowRecords()noexcept;
    // Configuration only, before the first callback. Ordinary runs keep their
    // original capacity; the explicit 40-second recovery recorder gets twice it.
    bool EnableRecoveryCapacity()noexcept;
    bool EnablePumpCapacity()noexcept;
    unsigned Limit()const noexcept{return limit_;}
    std::uint64_t Begin(const ReloadFlowEventInput&)noexcept;
    bool End(std::uint64_t,const ReloadFlowEventEnd&)noexcept;
    std::span<const ReloadFlowRecord> Records()const noexcept{return {records_.get(),count_};}
    unsigned Dropped()const noexcept{return dropped_;}
    unsigned Rejected()const noexcept{return rejected_;}
private:
    // Allocate once during setup, never in a game callback. Besides keeping
    // ordinary storage small, this avoids a huge aggregate initializer in every
    // translation unit that includes this header.
    std::unique_ptr<ReloadFlowRecord[]> records_;unsigned limit_=0,count_=0,dropped_=0,rejected_=0;
};
// Presentation-only copy of an existing reserve observation. A render read
// cannot enter native invocation exclusion, perform memory reads, or cancel a
// reload. Invalidations also reject publications from reads already in flight.
class ReloadReservePublication {
    struct Snapshot {std::uint64_t epoch;Bc2AmmoReserveLease lease;};
    std::atomic<std::uint64_t> epoch_{1};
    std::atomic<std::shared_ptr<const Snapshot>> snapshot_;
    void Invalidate(std::uint64_t epoch)noexcept {
        epoch_.compare_exchange_strong(epoch,epoch+1,std::memory_order_acq_rel);
    }
public:
    std::uint64_t Begin()const noexcept{return epoch_.load(std::memory_order_acquire);}
    void Clear()noexcept{epoch_.fetch_add(1,std::memory_order_acq_rel);}
    void Observe(std::uint64_t epoch,const ReloadReserveObservation& observation)noexcept {
        if(observation.result==ReloadObservationResult::Deferred)return;
        if(observation.result!=ReloadObservationResult::Available||!observation.lease||
           !observation.lease->verified||!observation.lease->sequence){Invalidate(epoch);return;}
        try{
            const std::shared_ptr<const Snapshot> next=std::make_shared<const Snapshot>(Snapshot{epoch,*observation.lease});
            auto current=snapshot_.load(std::memory_order_acquire);
            for(unsigned attempt=0;attempt<4;++attempt){
                if(epoch!=Begin()||(current&&current->epoch==epoch&&current->lease.sequence>=next->lease.sequence))return;
                if(snapshot_.compare_exchange_strong(current,next,std::memory_order_acq_rel,std::memory_order_acquire))return;
            }
        }catch(...){Invalidate(epoch);}
    }
    std::optional<Bc2AmmoReserveLease> Read()const noexcept {
        const auto current=snapshot_.load(std::memory_order_acquire);
        if(current&&current->epoch==Begin())return current->lease;
        return {};
    }
};
struct MagazineStartAttempt {
    ReloadCycleControl requested{};
    interaction::ManualReloadRequest unseat{};
    ReloadStateOwner nativeOwner{};
    std::int64_t beginNs=0,endNs=0,checkNs=0,pulseEndNs=0;
    std::uint64_t profile=0,nativeCycle=0,nativePending=0,revisionBefore=0,revisionAfter=0;
    std::uint64_t ownerRevisionBefore=0,ownerRevisionAfter=0,cancelBefore=0,cancelAfter=0;
    unsigned thread=0,activeBefore=0,activeAfter=0,nativePhase=0;
    bool policyObserved=false,entryHeld=false,policyEntered=false,policyDelivered=false,registered=false;
    MagazineCycleStartResult result=MagazineCycleStartResult::Unknown;
    // Literal stage names only. No strings/allocations or new native evidence.
    const char* gate="not_entered";
    const char* identityGate="not_checked";
};
// Dedicated immutable slots: ordinary reload callbacks cannot fill this journal
// before the user's later start attempts. Release publication makes a drained
// report safe even if a Start's final diagnostic write trails native drain.
class MagazineStartJournal {
    struct Slot {MagazineStartAttempt value{};std::atomic<bool> ready=false;};
    std::array<Slot,128> rows_{};std::atomic<unsigned> total_=0;
public:
    void Observe(const MagazineStartAttempt& value)noexcept {
        const auto n=total_.fetch_add(1,std::memory_order_relaxed);
        if(n<rows_.size()){rows_[n].value=value;rows_[n].ready.store(true,std::memory_order_release);}
    }
    void Report(std::ostream& out,bool drained)const {
        const auto total=total_.load();out<<"{\"capacity\":128,\"total\":"<<total<<",\"dropped\":"<<(total>128?total-128:0)
            <<",\"drained\":"<<(drained?"true":"false")<<",\"rows\":[";
        const auto owner=[&](const ReloadStateOwner& o){out<<'['<<o.player<<','<<o.soldier<<','<<o.weak<<','<<o.weapon<<','
            <<o.actorGeneration<<','<<o.equipGeneration<<','<<o.space<<']';};
        bool comma=false;
        if(drained)for(unsigned n=0;n<rows_.size()&&n<total;++n)if(rows_[n].ready.load(std::memory_order_acquire)){
            const auto& e=rows_[n].value;const auto& c=e.requested;if(comma)out<<',';comma=true;
            out<<"{\"attempt\":"<<n+1<<",\"thread\":"<<e.thread<<",\"input\":"<<c.sequence<<",\"cycle\":"<<c.cycle
                <<",\"request\":"<<e.unseat.id<<",\"profile\":"<<e.profile<<",\"owner\":";owner(c.identity.owner);
            const auto& u=e.unseat.owner;out<<",\"request_owner\":["<<u.actor<<','<<u.actorGeneration<<','<<u.weapon<<','<<u.equipGeneration<<','<<u.space<<']';
            out<<",\"firing\":["<<c.identity.firing[0]<<','<<c.identity.firing[1]<<','<<c.identity.firing[2]
                <<"],\"server\":["<<c.identity.serverPlayer<<','<<c.identity.serverSoldier<<','<<c.identity.serverItem<<']'
                <<",\"begin_ns\":"<<e.beginNs<<",\"end_ns\":"<<e.endNs<<",\"check_ns\":"<<e.checkNs
                <<",\"input_observed_ns\":"<<c.observedNs<<",\"input_deadline_ns\":"<<c.deadlineNs<<",\"permitted\":"<<c.permitted
                <<",\"pulse_end_ns\":"<<e.pulseEndNs<<",\"result\":"<<unsigned(e.result)<<",\"gate\":\""<<e.gate
                <<"\",\"identity_gate\":\""<<e.identityGate<<"\",\"native_policy_observed\":"<<e.policyObserved
                <<",\"native_cycle\":"<<e.nativeCycle<<",\"native_phase\":"<<e.nativePhase<<",\"native_pending\":"<<e.nativePending
                <<",\"native_owner\":";owner(e.nativeOwner);
            out<<",\"entry_held\":"<<e.entryHeld<<",\"policy_entered\":"<<e.policyEntered<<",\"policy_delivered\":"<<e.policyDelivered
                <<",\"registered\":"<<e.registered<<",\"active_before\":"<<e.activeBefore<<",\"active_after\":"<<e.activeAfter
                <<",\"revision_before\":"<<e.revisionBefore<<",\"revision_after\":"<<e.revisionAfter
                <<",\"owner_revision_before\":"<<e.ownerRevisionBefore<<",\"owner_revision_after\":"<<e.ownerRevisionAfter
                <<",\"cancel_before\":"<<e.cancelBefore<<",\"cancel_after\":"<<e.cancelAfter<<'}';
        }out<<"]}";
    }
};
namespace reloadFlowRuntime {
// Cached existing owned-Update observation only, no native read or renewal.
std::optional<ReloadPreholdEntryObservation> ReadPreholdEntry(const ReloadHoldIdentity&,std::uint64_t cycle,std::int64_t now)noexcept;
// Fresh coherent reserve before Start; no cycle/held state is invented.
std::optional<Bc2AmmoReserveLease> ReadReserve()noexcept;
// Pure published copy for rendering. No native reads, policy entry, clock
// renewal or cancellation. Consumers must validate original identity/expiry.
std::optional<Bc2AmmoReserveLease> ReadPublishedReserve()noexcept;
// Opt-in diagnostic observer only; native request policy selection is unchanged.
std::optional<Bc2AmmoReserveLease> ReadDiagnosticFireReserve()noexcept;
// Exact stock M95 observation only; grants no manual-cycle or reload authority.
std::optional<Bc2AmmoReserveLease> ReadM95StockShotReserve()noexcept;
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
// Read-only, exact-config pump diagnostic state bracket. No normal backend or
// gameplay authority. Original owner/server deadline remains unchanged.
struct PumpPartNativeSample {
    ReloadHoldIdentity identity{};
    ReloadObservedConfig config{};
    std::array<ReloadFlowBoundary,3> branches{};
    std::int64_t observedNs=0,completedNs=0,deadlineNs=0;
    std::uint64_t callbackRevision=0;
    unsigned holdPhase=0;
};
std::optional<PumpPartNativeSample> ReadPumpPartDiagnosticSnapshot()noexcept;
// Passive exact-M95 original state bracket. No callback activity/state mutation.
std::optional<PumpPartNativeSample> ReadM95ShotPartDiagnosticSnapshot()noexcept;

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
// Diagnostic log starts at the reload portion of the combined controller test.
// Neither function resets consumers, native cycles, records, or lease clocks.
bool DeferInventoryReloadRecords(bool recovery=false)noexcept;
bool BeginInventoryReloadRecords()noexcept;
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
// Private resource backend consumes the body's native lifetime, independently
// of rendering or active hand claims. Normal builds do not dispatch from it.
void PublishAmmoResourceBinding(const std::optional<AmmoResourceBinding>&)noexcept;
bool ResourceHandsEnabled()noexcept;
std::optional<AmmoResourceView> ReadAmmoResourceView(const ReloadStateOwner&,std::int64_t now)noexcept;
bool SubmitAmmoResourceRequest(const AmmoResourceRequest&)noexcept;
std::optional<AmmoResourceOutcome> ReadAmmoResourceOutcome(std::uint64_t,const interaction::AmmoResourceContext&)noexcept;
// One bounded recording session, at most20 seconds; no automatic reset/rearm.
void Start()noexcept;
// Disables only these hooks, drains for at most2s, retains trampolines/module.
// False means caller must keep the module loaded and may retry Stop.
bool Stop()noexcept;
// Complete JSON object. Full records are emitted only after a successful drain.
void Report(std::ostream&);
}
}
