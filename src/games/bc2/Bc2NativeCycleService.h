#pragma once
#include "Bc2ReloadFlowRuntime.h"
#include "Bc2AmmoReserve.h"
#include "Bc2NativeCycleRecoveryState.h"
#include "fvr/interaction/PhysicalWeaponCycle.h"

namespace fvr::bc2 {
// Explicit selected-native -> physical-hand mapping. BC2's native equip epoch
// and the hand arbiter's equip generation are separate domains.
struct Bc2NativeCycleControl {
    ReloadStateOwner nativeOwner{};
    interaction::HandInteractionSample input{};
    interaction::HandInteractionKey item{},mechanism{};
    std::optional<interaction::WeaponCycleRelease> release;
    bool permitted=false;
};
enum class Bc2NativeCyclePhase:unsigned {Watching,ShotObserved,Arming,Held,Releasing,Complete,Cancelled,Converging,Advancing};
enum class Bc2NativeCycleFailure:unsigned {None,Control,Owner,Input,State,Shot,Hold,Restore,Expired,Completion};
enum class Bc2NativeCycleMode:unsigned {Spas,M95};
// First actual Held -> Cancelled(Input) rejection for this service lifetime.
// Fixed-size captured values only: no IO, native reads or fresh authority.
// Retained across actor retirement so a late failure survives a bounded trace.
struct Bc2NativeCycleInputRejection {
    ReloadHoldIdentity native{};
    ReloadUpdateContext context{};
    std::uint64_t invocation=0,cycle=0,shot=0;
    unsigned branch=3;
    std::int64_t contextObservedNs=0,processingNs=0,leaseDeadlineNs=0;
    bool contextSafe=false,contextFresh=false;
};
// Bounded copied service state for the first rejected native callback. This
// grants no control authority and performs no native reads or IO.
struct Bc2NativeCycleDiagnostic {
    bool recoveryActive=false,concurrentCycle=false,releasePresent=false,readyPresent=false;
    unsigned firedMask=0,heldMask=0,readyMask=0;
    std::array<unsigned,3> tail{},cycleState{};
    std::array<std::uint64_t,3> lastInvocation{},openUpdate{};
    std::array<std::int64_t,3> finishedAt{};
    std::int64_t beganNs=0,deadlineNs=0,releaseSubmittedNs=0,releaseObservedNs=0,acknowledgedAtNs=0;
};
// Selection uses original coherent native reads, not a completion substitute.
struct Bc2NativeCycleSelection {
    Bc2NativeCycleMode mode{};Bc2AmmoReserveLease reserve{};ReloadObservedConfig config{};
    std::int64_t nowNs=0;
};
struct Bc2NativeCycleDecision {
    std::uint64_t invocation=0,cycle=0;
    unsigned branch=3;
    bool tracked=false,hold=false;
    std::int64_t deadlineNs=0;
};
struct Bc2NativeCycleTrackingRebase {
    ReloadStateOwner previous{},current{};
    interaction::HandInteractionOwner previousHand{},currentHand{};
    interaction::HandInteractionKey item{},mechanism{};
    std::uint64_t cycle=0,shot=0,callbackRevision=0;
    std::int64_t observedNs=0;
};
struct Bc2NativeCycleView {
    Bc2NativeCyclePhase phase=Bc2NativeCyclePhase::Watching;
    Bc2NativeCycleFailure failure=Bc2NativeCycleFailure::None;
    ReloadHoldIdentity native{};
    std::optional<interaction::WeaponCycleLease> held;
    std::optional<interaction::WeaponCycleReady> ready;
    bool blocksFire=false;
    int loaded=0,reserve=0,capacity=0;
    std::uint64_t cycle=0,shot=0;
    std::uint64_t failureInvocation=0;
    // Persistent selection identity only; supplies no lease or firing authority.
    std::optional<Bc2NativeCycleMode> selectedMode;ReloadStateOwner selectedOwner{};
    bool heldSuspended=false;
    std::uint64_t heldSuspensions=0,heldResumptions=0;
    std::optional<Bc2NativeCycleTrackingRebase> trackingRebase;
};
struct Bc2NativeCycleCompletionWatch {
    ReloadHoldIdentity identity{};ReloadObservedConfig config{};
    std::uint64_t cycle=0;
    std::int64_t beganNs=0,deadlineNs=0;
    bool holdsNativeCycle=false;
};
// Supplied only inside the runtime's verified callback exclusion. Cancelling
// a service does not prove that old native OriginalUpdate calls have returned.
struct Bc2NativeCycleActorDrain {
    ReloadHoldIdentity expected{};
    std::uint64_t cycle=0,shot=0,callbackRevision=0;
    std::int64_t observedNs=0,deadlineNs=0;
    bool callbacksDrained=false;
};
inline bool NativeCycleClientActorReplaced(const ReloadStateOwner& previous,const ReloadStateOwner& current)noexcept {
    return previous.soldier>=0x10000&&previous.weak>=0x10000&&previous.actorGeneration&&
        current.soldier>=0x10000&&current.weak>=0x10000&&current.actorGeneration>previous.actorGeneration&&
        (current.soldier!=previous.soldier||current.weak!=previous.weak);
}
// Durable diagnostic/physical-retirement receipt, never a new Ready or lease.
struct Bc2NativeCycleActorRetirement {
    std::uint64_t event=0,callbackRevision=0;
    std::int64_t observedNs=0;
    Bc2NativeCycleMode mode{};
    Bc2NativeCycleView outcome{};
    ReloadHoldIdentity replacement{};
    std::optional<interaction::WeaponCycleRelease> release;
    std::optional<Bc2NativeCycleRecoveryView> recovery;
    std::optional<interaction::WeaponCycleIdleDebtRelease> recoveryRelease;
    bool recoveryUncertain=false;
};
// Serialized BC2 callback service. The runtime owns its short, nonblocking
// gate and never retains that gate across OriginalUpdate. No native pointers
// are dereferenced and no state/count/timer write exists here. The only output
// requesting a mutation is the existing invocation-local delta override.
class Bc2NativeCycleService {
public:
    static constexpr std::int64_t ContextFreshNs=50000000,MaximumCycleNs=30000000000ll,MaximumCompletionNs=2000000000;
    static constexpr std::int64_t MaximumHoldSuspensionNs=3000000000ll;
    // Explicit pre-control private operating class. Default remains SPAS;
    // changing a configured or previously observed service is forbidden.
    bool Configure(Bc2NativeCycleMode)noexcept;
    bool EnableRecovery()noexcept;
    bool EnableConcurrentBolt()noexcept;
    bool RecoveryEnabled()const noexcept{return recoveryEnabled_;}
    bool BeginRecovery(std::int64_t now)noexcept;
    // Before opening an own Update, an established exact hold may encounter
    // ordinary action bits again (prediction/held trigger). Convert to the
    // existing guarded convergence route; the SAME callback must be guarded.
    // Invalid/stale native context and unobserved mutations never qualify.
    bool BeginInputRecovery(const ReloadHoldInput&,std::uint64_t invocation)noexcept;
    // Definitive non-dispatch of an original held-lease release overtaken by
    // guarded recovery. This denies only that command; no debt/Ready is cleared.
    bool ReleaseSupersededByRecovery(const Bc2NativeCycleControl&)const noexcept;
    // Exact pre-conversion held Update may finish its zero-delta Step after
    // recovery starts. This does not authorize a new native update or gesture.
    bool DrainingHeldInvocation(const Bc2NativeCycleDecision&,std::uint32_t firing,std::int64_t now)const noexcept;
    bool DrainingPaidOrdinaryInvocation(const Bc2NativeCycleDecision&,std::uint32_t firing,std::int64_t now)const noexcept;
    bool Recovering()const noexcept{return phase_==Bc2NativeCyclePhase::Converging||phase_==Bc2NativeCyclePhase::Advancing;}
    NativeCycleRecoveryDecision EvaluateRecovery(const NativeCycleRecoveryScope&,const ReloadFlowEventInput&,int capacity,std::int64_t deadline)noexcept;
    void FinishRecovery(const NativeCycleRecoveryDecision&,const ReloadFlowRecord&,const NativeCycleInputGuard&,unsigned steps,unsigned rejected)noexcept;
    std::optional<Bc2NativeCycleRecoveryView> RecoveryView(std::int64_t now)noexcept{return concurrentCycle_?std::nullopt:recovery_.View(now);}
    bool SubmitRecovery(const interaction::WeaponCycleIdleDebtRelease&,const interaction::HandInteractionSample&)noexcept;
    bool AcknowledgeRecovery(const interaction::WeaponCycleIdleDebtReady&,std::int64_t now)noexcept;
    // Session diagnostics survive selection and operation handoffs. These
    // counters do not grant authority or substitute for retained receipts.
    std::uint64_t RecoveryUpdates()const noexcept{return recoveryUpdates_;}
    std::uint64_t RecoverySteps()const noexcept{return recoverySteps_;}
    std::uint64_t RecoveryReleases()const noexcept{return recoveryReleases_;}
    // Host must exclude new callback decisions and drain all original calls.
    // Never discards debt/Ready or resets sequence high-water marks.
    bool Select(const Bc2NativeCycleSelection&)noexcept;
    // A real client/server actor replacement may retire old obligations after
    // callback drain. Same-actor equipment/tracking changes still use Select.
    std::optional<Bc2NativeCycleActorRetirement> SelectReplacementActor(
        const Bc2NativeCycleSelection&,const Bc2NativeCycleActorDrain&)noexcept;
    // A verified replacement actor may first carry a rifle or other family.
    // Retire old debt without choosing a pump/bolt class or granting Control.
    // Consumes the reserve sequence; later Select needs a newer publication.
    std::optional<Bc2NativeCycleActorRetirement> RetireForReplacementActor(
        const Bc2AmmoReserveLease&,std::int64_t now,const Bc2NativeCycleActorDrain&)noexcept;
    const auto& LastActorRetirement()const noexcept{return actorRetirement_;}
    Bc2NativeCycleMode Mode()const noexcept{return mode_;}
    Bc2NativeCyclePhase Phase()const noexcept{return phase_;}
    const auto& FirstInputRejection()const noexcept{return firstInputRejection_;}
    Bc2NativeCycleDiagnostic Diagnostic()const noexcept {
        return {recovery_.Active(),concurrentCycle_,release_.has_value(),ready_.has_value(),
            firedMask_,heldMask_,readyMask_,tail_,cycleState_,lastInvocation_,openUpdate_,finishedAt_,
            began_,deadline_,releaseSubmittedNs_,release_?release_->observedNs:0,
            std::max(acknowledgedAt_,recovery_.AcknowledgedAt())};
    }
    const interaction::HandInteractionSample& LastControlInput()const noexcept{return control_.input;}
    bool Control(const Bc2NativeCycleControl&)noexcept;
    bool RebaseTracking(const Bc2NativeCycleControl&,const Bc2NativeCycleActorDrain&)noexcept;
    // Called only at a drained runtime operation handoff. Pending outcomes and
    // unresolved manual debt cannot yield to shell/resource mutations.
    bool YieldForReload()noexcept;
    bool SuspendCompletion()noexcept;
    // Retain only established all-branch native holds for a fixed interruption
    // window. Revokes all physical-use leases; fresh neutral resumes them.
    bool SuspendHeld(std::int64_t now)noexcept;
    std::optional<Bc2NativeCycleCompletionWatch> CompletionWatch(std::int64_t now)const noexcept;
    std::optional<Bc2NativeCycleCompletionWatch> ObservationWatch(std::int64_t now)const noexcept;
    // Finishing an already-issued native observation may overlap neutral
    // resumption or another branch completing this same cycle. Preserve its
    // original deadline through that completion; no new watch is granted in
    // Complete, and owner cancellation/selection retires this drain authority.
    std::uint64_t ObservationAuthority(std::uint64_t previous,std::int64_t now)const noexcept;
    bool AcknowledgeReady(const interaction::WeaponCycleReady&,std::int64_t now=0)noexcept;
    Bc2NativeCycleDecision Evaluate(const ReloadHoldInput&,std::uint64_t invocation)noexcept;
    void Commit(const ReloadFlowRecord&)noexcept;
    void ObserveRestore(const ReloadFlowRecord&)noexcept;
    void Finish(const Bc2NativeCycleDecision&,const ReloadFlowRecord&)noexcept;
    void Missing(std::uint32_t firing,std::uint64_t invocation=0)noexcept;
    void Cancel(Bc2NativeCycleFailure)noexcept;
    Bc2NativeCycleView View(std::int64_t now)const noexcept;
private:
    bool ReserveValid(const Bc2AmmoReserveLease&,std::int64_t now)const noexcept;
    bool SelectionValid(const Bc2NativeCycleSelection&)const noexcept;
    void ApplySelection(const Bc2NativeCycleSelection&)noexcept;
    bool MappingScope(const Bc2NativeCycleControl&)const noexcept;
    bool Mapping(const Bc2NativeCycleControl&)const noexcept;
    std::optional<Bc2NativeCycleCompletionWatch> HeldWatch(std::int64_t now)const noexcept;
    bool Current(const ReloadHoldInput&,bool nativeOnly=false)const noexcept;
    bool SuspendExpiredController(const ReloadHoldInput&)noexcept;
    bool Release(const interaction::WeaponCycleRelease&,std::int64_t now)noexcept;
    void PublishHeld(std::int64_t observed,std::int64_t deadline)noexcept;
    void ContradictoryRestore()noexcept;
    bool PaidRecoveryRestoreOrdered(const ReloadFlowRecord&)const noexcept;
    void ClearAcknowledgedReady()noexcept{acknowledgedReady_.reset();acknowledgedAt_=0;paidRecoveryDrain_={};paidReplayBegan_=0;paidOrdinaryIdle_={};}
    unsigned TailLength()const noexcept{return emptyCycle_||mode_==Bc2NativeCycleMode::M95?2u:3u;}
    std::int64_t CompletionNs()const noexcept{return mode_==Bc2NativeCycleMode::M95?3000000000ll:MaximumCompletionNs;}
    Bc2NativeCycleMode mode_=Bc2NativeCycleMode::Spas;
    Bc2NativeCycleControl control_{};
    bool selectionRequired_=false;
    std::optional<Bc2NativeCycleSelection> selection_;
    std::uint64_t selectionSequence_=0;std::int64_t selectionTime_=0;
    std::optional<Bc2NativeCycleActorRetirement> actorRetirement_;
    std::optional<Bc2NativeCycleTrackingRebase> trackingRebase_;
    ReloadHoldIdentity native_{};ReloadObservedConfig config_{};
    Bc2NativeCyclePhase phase_=Bc2NativeCyclePhase::Watching;
    Bc2NativeCycleFailure failure_=Bc2NativeCycleFailure::None;
    std::optional<Bc2NativeCycleInputRejection> firstInputRejection_;
    std::array<std::uint64_t,3> lastInvocation_{},openUpdate_{};
    std::array<std::int64_t,3> contexts_{},heldAt_{},finishedAt_{};
    // Only exact post-shot client Restore receipts qualify repeated previous7.
    std::array<std::int64_t,2> restoredAt_{};
    std::array<unsigned,2> restoredPrevious_{}; // Exact own snapshot output provenance.
    // Exact client forward-ready snapshots; never invented Commit tails.
    std::array<std::int64_t,2> forwardReadyAt_{};
    std::array<std::int64_t,2> preRestoredAt_{};
    std::array<std::int64_t,2> preRewindAt_{};
    std::array<unsigned,3> cycleState_{};
    std::array<unsigned,3> tail_{};
    std::array<ReloadHoldInput,3> before_{};
    std::array<interaction::WeaponCycleLease,32> issued_{};
    unsigned issueAt_=0,firedMask_=0,heldMask_=0,readyMask_=0;
    std::uint64_t cycle_=0,shot_=0,sequence_=0;
    std::uint64_t lastEvent_=0,failureInvocation_=0;
    std::int64_t began_=0,deadline_=0;
    std::int64_t suspendedAt_=0,suspendedUntil_=0;
    std::uint64_t heldSuspensions_=0,heldResumptions_=0;
    int loaded_=0,reserve_=0,capacity_=0;
    bool emptyCycle_=false,recoveryEnabled_=false;
    bool concurrentBoltEnabled_=false,concurrentCycle_=false;
    std::uint64_t guardEpoch_=0;
    Bc2NativeCycleRecoveryState recovery_{};
    std::uint64_t recoveryUpdates_=0,recoverySteps_=0,recoveryReleases_=0;
    std::optional<interaction::WeaponCycleIdleDebtRelease> lastRecoveryRelease_;
    std::array<bool,3> openHold_{};
    std::array<std::int64_t,3> openDeadline_{};
    std::optional<interaction::WeaponCycleLease> held_;
    // Processing time of the first accepted submission; original release
    // source bounds stay immutable and remain the completion envelope.
    std::int64_t releaseSubmittedNs_=0;
    std::optional<interaction::WeaponCycleRelease> release_;
    std::optional<interaction::WeaponCycleReady> ready_;
    // A paid physical stroke may outlive another exact native client tail
    // replay. Retain its original receipt; never publish or consume it twice.
    std::optional<interaction::WeaponCycleReady> acknowledgedReady_;
    std::int64_t acknowledgedAt_=0;
    std::array<std::uint64_t,3> paidRecoveryDrain_{};
    std::int64_t paidReplayBegan_=0;
    // Copied proof of a paid ordinary sibling's unchanged idle Update. Its
    // original entry/exit may straddle another client's Restore entry even
    // when this Finish reaches the service first. Never an idle vote or lease.
    struct PaidOrdinaryIdleReceipt {
        std::uint64_t invocation=0,cycle=0,shot=0;
        std::int64_t beginNs=0,endNs=0;
    };
    std::array<PaidOrdinaryIdleReceipt,3> paidOrdinaryIdle_{};
};
}
