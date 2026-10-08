#pragma once
#include "Bc2ReloadRound.h"
namespace fvr::bc2 {
enum class ReloadRoundGatePhase:unsigned {Disabled,Waiting,FirstHold,Advancing,SecondHold,Released,Aborted};
enum class ReloadRoundGateFailure:unsigned {None,OwnerOrRead,UnsafeInput,Timing,InitialHold,MissingHolds,UnexpectedState,Completion,Overlap,Patch,Expired,Stopped};
struct ReloadRoundGateDecision {
    std::uint64_t update=0;unsigned branch=3;bool tracked=false,hold=false;
    std::int64_t deadlineNs=0;ReloadFiringObservation before{};ReloadRoundGatePhase phase=ReloadRoundGatePhase::Disabled;
};
// The reader checks the already-proved FiringFunctionData fields twice. No calls
// or writes; exact .72/1 makes the next State11 interval larger than dt<=.05.
bool ReadReloadRoundTiming(const ReloadStateMemory&,const ReloadObservedConfig&)noexcept;
// One diagnostic request only. Serialized by the adapter with a nonblocking
// lock; no lock may span an original. This does NOT enable physical reload.
class ReloadRoundGate {
public:
    static constexpr std::int64_t ReholdNs=350000000,AdvanceNs=1500000000;
    bool Enable()noexcept;
    ReloadRoundGateDecision Evaluate(const ReloadHoldInput&,bool timingVerified,bool cohortStable,std::uint64_t update)noexcept;
    bool Transfer(const ReloadRoundTransfer&,std::uint64_t parentUpdate)noexcept;
    bool Finish(const ReloadRoundGateDecision&,const ReloadFiringObservation&,std::int64_t nowNs,bool identityRetained,const ReloadDeltaOverride&)noexcept;
    void Cancel(ReloadRoundGateFailure why)noexcept;
    ReloadRoundGatePhase Phase()const noexcept{return phase_;}
    ReloadRoundGateFailure Failure()const noexcept{return failure_;}
    const ReloadHoldIdentity& Identity()const noexcept{return initial_.Identity();}
    std::int64_t FirstBegin()const noexcept{return initial_.BeginNs();}
    std::int64_t FirstDeadline()const noexcept{return initial_.DeadlineNs();}
    std::int64_t RequestNs()const noexcept{return requestNs_;}
    std::int64_t AdvanceDeadline()const noexcept{return advanceDeadline_;}
    std::int64_t SecondBegin()const noexcept{return secondBegin_;}
    std::int64_t SecondDeadline()const noexcept{return secondDeadline_;}
    const std::array<std::uint64_t,3>& FirstReholds()const noexcept{return firstReholds_;}
    const std::array<std::uint64_t,3>& TransferIds()const noexcept{return transfers_;}
    const std::optional<ReloadRoundAcknowledgement>& Acknowledgement()const noexcept{return ack_;}
private:
    bool Common(const ReloadHoldInput&,bool timing)noexcept;
    bool RecentHolds(const ReloadHoldInput&,const std::array<std::int64_t,3>&)const noexcept;
    bool Sample(const ReloadHoldInput&,bool stable)noexcept;
    ReloadRoundGateDecision Track(const ReloadHoldInput&,std::uint64_t,bool hold,std::int64_t deadline)noexcept;
    ReloadHoldProbe initial_;ReloadRoundCompletion completion_{true};
    ReloadRoundGatePhase phase_=ReloadRoundGatePhase::Disabled;ReloadRoundGateFailure failure_=ReloadRoundGateFailure::None;
    std::array<std::int64_t,3> contexts_{},initialHolds_{},reholds_{};
    std::array<std::uint64_t,3> open_{},transfers_{},firstReholds_{};
    std::uint64_t sequence_=0;std::int64_t requestNs_=0,advanceDeadline_=0,secondBegin_=0,secondDeadline_=0;
    ReloadObservedConfig armedConfig_{};int loaded_=0,reserve_=0,capacity_=0;std::optional<ReloadRoundAcknowledgement> ack_;
};
}
