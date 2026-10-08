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
enum class Bc2NativeCyclePhase:unsigned {Watching,ShotObserved,Arming,Held,Releasing,Complete,Cancelled,Converging};
enum class Bc2NativeCycleFailure:unsigned {None,Control,Owner,Input,State,Shot,Hold,Restore,Expired,Completion};
enum class Bc2NativeCycleMode:unsigned {Spas,M95};
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
};
struct Bc2NativeCycleCompletionWatch {
    ReloadHoldIdentity identity{};ReloadObservedConfig config{};
    std::uint64_t cycle=0;
    std::int64_t beganNs=0,deadlineNs=0;
    bool holdsNativeCycle=false;
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
    bool RecoveryEnabled()const noexcept{return recoveryEnabled_;}
    bool BeginRecovery(std::int64_t now)noexcept;
    bool Recovering()const noexcept{return phase_==Bc2NativeCyclePhase::Converging;}
    NativeCycleRecoveryDecision EvaluateRecovery(const NativeCycleRecoveryScope&,const ReloadFlowEventInput&,int capacity,std::int64_t deadline)noexcept;
    void FinishRecovery(const NativeCycleRecoveryDecision&,const ReloadFlowRecord&,const NativeCycleInputGuard&,unsigned steps,unsigned rejected)noexcept;
    std::optional<Bc2NativeCycleRecoveryView> RecoveryView(std::int64_t now)noexcept{return recovery_.View(now);}
    bool SubmitRecovery(const interaction::WeaponCycleIdleDebtRelease&,const interaction::HandInteractionSample&)noexcept;
    bool AcknowledgeRecovery(const interaction::WeaponCycleIdleDebtReady&,std::int64_t now)noexcept;
    std::uint64_t RecoveryUpdates()const noexcept{return recovery_.GuardedUpdates();}
    std::uint64_t RecoverySteps()const noexcept{return recovery_.GuardedSteps();}
    // Host must exclude new callback decisions and drain all original calls.
    // Never discards debt/Ready or resets sequence high-water marks.
    bool Select(const Bc2NativeCycleSelection&)noexcept;
    Bc2NativeCycleMode Mode()const noexcept{return mode_;}
    bool Control(const Bc2NativeCycleControl&)noexcept;
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
    // resumption. Preserve its original deadline while the SAME debt remains;
    // this token alone never authorizes a new native-only observation.
    std::uint64_t ObservationAuthority(std::uint64_t previous,std::int64_t now)const noexcept;
    bool AcknowledgeReady(const interaction::WeaponCycleReady&)noexcept;
    Bc2NativeCycleDecision Evaluate(const ReloadHoldInput&,std::uint64_t invocation)noexcept;
    void Commit(const ReloadFlowRecord&)noexcept;
    void ObserveRestore(const ReloadFlowRecord&)noexcept;
    void Finish(const Bc2NativeCycleDecision&,const ReloadFlowRecord&)noexcept;
    void Missing(std::uint32_t firing,std::uint64_t invocation=0)noexcept;
    void Cancel(Bc2NativeCycleFailure)noexcept;
    Bc2NativeCycleView View(std::int64_t now)const noexcept;
private:
    bool Mapping(const Bc2NativeCycleControl&)const noexcept;
    std::optional<Bc2NativeCycleCompletionWatch> HeldWatch(std::int64_t now)const noexcept;
    bool Current(const ReloadHoldInput&)const noexcept;
    bool Release(const interaction::WeaponCycleRelease&,std::int64_t now)noexcept;
    void PublishHeld(std::int64_t observed,std::int64_t deadline)noexcept;
    void ContradictoryRestore()noexcept;
    unsigned TailLength()const noexcept{return emptyCycle_||mode_==Bc2NativeCycleMode::M95?2u:3u;}
    std::int64_t CompletionNs()const noexcept{return mode_==Bc2NativeCycleMode::M95?3000000000ll:MaximumCompletionNs;}
    Bc2NativeCycleMode mode_=Bc2NativeCycleMode::Spas;
    Bc2NativeCycleControl control_{};
    bool selectionRequired_=false;
    std::optional<Bc2NativeCycleSelection> selection_;
    std::uint64_t selectionSequence_=0;std::int64_t selectionTime_=0;
    ReloadHoldIdentity native_{};ReloadObservedConfig config_{};
    Bc2NativeCyclePhase phase_=Bc2NativeCyclePhase::Watching;
    Bc2NativeCycleFailure failure_=Bc2NativeCycleFailure::None;
    std::array<std::uint64_t,3> lastInvocation_{},openUpdate_{};
    std::array<std::int64_t,3> contexts_{},heldAt_{},finishedAt_{};
    // Only exact post-shot client Restore receipts qualify repeated previous7.
    std::array<std::int64_t,2> restoredAt_{};
    std::array<unsigned,2> restoredPrevious_{}; // Exact own snapshot output provenance.
    // Measured clientA forward ready snapshot; never an invented Commit tail.
    std::int64_t forwardReadyAt_=0;
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
    std::uint64_t guardEpoch_=0;
    Bc2NativeCycleRecoveryState recovery_{};
    std::array<bool,3> openHold_{};
    std::array<std::int64_t,3> openDeadline_{};
    std::optional<interaction::WeaponCycleLease> held_;
    // Processing time of the first accepted submission; original release
    // source bounds stay immutable and remain the completion envelope.
    std::int64_t releaseSubmittedNs_=0;
    std::optional<interaction::WeaponCycleRelease> release_;
    std::optional<interaction::WeaponCycleReady> ready_;
};
}
