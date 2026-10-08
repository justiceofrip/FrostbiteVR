#pragma once
#include "Bc2MagazineReload.h"
#include "Bc2MagazineLeaseObservation.h"
#include "Bc2MagazineNativeProfile.h"
#include "Bc2ReloadRequestCycle.h"
#include "Bc2ReloadRoundGate.h"

namespace fvr::bc2
{
// Exact scoped-XM8 gate. The runtime exposes a separate, explicit diagnostic
// mode. Normal physical/gameplay dispatch stays off until native verification.
bool IsXm8MagazineConfig(const ReloadObservedConfig&) noexcept;
bool ReadXm8MagazineTiming(const ReloadStateMemory&,const ReloadObservedConfig&) noexcept;
struct ReloadMagazineAckEvidence {
    ReloadMagazineAcknowledgement acknowledgement{};
    std::int64_t observedNs=0,deadlineNs=0;
    bool verified=false;
};
struct ReloadMagazineNativeRequest {
    interaction::ManualReloadRequest request{};
    ReloadMagazineLease heldLease{};
    Bc2ReloadReservedItem reservation{};
    unsigned reservedUnits=0;
};
struct ReloadMagazineGateAcknowledgement {
    interaction::ManualReloadAck semantic{};
    ReloadMagazineLease lease{};
};
// Immutable Start-only provenance. KeepAlive never replaces this proof.
struct ReloadMagazineStartupPulse {ReloadCycleControl control{};std::int64_t endNs=0;bool ValidFor(const ReloadCycleControl& c)const noexcept{
 return control==c&&c.observedNs>0&&endNs>c.observedNs&&endNs<=c.deadlineNs&&endNs-c.observedNs==100000000ll;
}bool operator==(const ReloadMagazineStartupPulse&)const=default;};
// Persistent REQUEST policy, independent of the bounded diagnostic recorder.
// A cycle has no total time cap while original controller/owner leases remain
// fresh. Only a submitted insertion has a 3.5-second completion deadline.
// Explicit Start/Submit only; no synthetic requests, native calls, input injection
// or ammo writes. The adapter serializes short calls, never across an original,
// supplies coherent quiet-cohort evidence, and uses RunReloadDeltaOverride once.
// This XM8 policy does not modify the accepted SPAS implementation.
class Bc2MagazineReloadCycle
{
  public:
    using StartupPulseProof=ReloadMagazineStartupPulse;
    static constexpr std::int64_t AdvanceNs = 3500000000;
    // Profile string/timing views reference immutable compiled data. Candidate
    // profiles cannot create a cycle; native receipt/state validation is shared.
    explicit Bc2MagazineReloadCycle(bool enabled = false,MagazineNativeProfile profile=Xm8MagazineNativeProfile) noexcept
        : phase_(enabled&&profile.Reviewed() ? ReloadRequestCyclePhase::Idle : ReloadRequestCyclePhase::Disabled),profile_(profile)
    {
    }
    static constexpr bool DefaultRuntimeDispatchEnabled=false;
    // Called only under the adapter request lock with native callbacks excluded.
    // Preserve request/cycle watermarks across a profile change; a retired old
    // request must never become a valid new request just because data changed.
    bool SelectProfile(MagazineNativeProfile profile,bool callbacksDrained,bool priorRetired)noexcept {
        if(!callbacksDrained||!profile.Reviewed()||Open()||
           (phase_!=ReloadRequestCyclePhase::Idle&&phase_!=ReloadRequestCyclePhase::Cancelled)||
           (control_.cycle&&!priorRetired))return false;
        profile_=profile;return true;
    }
    bool Start(const ReloadCycleControl &,const interaction::ManualReloadRequest& unseat,std::int64_t nowNs,std::optional<ReloadMagazineStartupPulse> pulse=std::nullopt) noexcept;
    const auto& StartupPulse()const noexcept{return startupPulse_;}
    std::int64_t FirstHoldingNs()const noexcept{return firstHoldingNs_;}
    const auto& ArmingContexts()const noexcept{return armingContexts_;}
    // Applied means all three native reload updates have actually been held and
    // restored, not that BC2 exposes a detached-magazine inventory state.
    std::optional<ReloadMagazineGateAcknowledgement> TakeUnseatAcknowledgement(
        const ReloadHoldIdentity&,std::uint64_t cycle,std::int64_t nowNs) noexcept;
    bool ObservePredictionRestore(const ReloadMagazinePredictionRestore& e,std::int64_t now)noexcept {
        return Current(now)&&phase_==ReloadRequestCyclePhase::Advancing&&e.branch<2&&!open_[e.branch]&&
            configured_&&e.config==config_&&profile_.Matches(e.config)&&
            e.cycle==control_.cycle&&e.identity==control_.identity&&e.endNs<=now&&completion_.ObservePredictionRestore(e);
    }
    bool KeepAlive(const ReloadCycleControl &, std::int64_t nowNs) noexcept;
    ReloadRequestDecision Evaluate(const ReloadHoldInput &, bool timingVerified, bool cohortStable,
                                   std::uint64_t update) noexcept;
    // Adapter processing time is sampled under its policy lock. Native source
    // times and deadlines remain original, even if an API call overtook a read.
    ReloadRequestDecision Evaluate(const ReloadHoldInput &, bool timingVerified, bool cohortStable,
                                   std::uint64_t update, std::int64_t processingNs) noexcept;
    bool Finish(const ReloadRequestDecision &, const ReloadFiringObservation &, std::int64_t nowNs,
                bool identityRetained, const ReloadDeltaOverride &) noexcept;
    bool Finish(const ReloadRequestDecision &, const ReloadFiringObservation &, std::int64_t observedNs,
                bool identityRetained, const ReloadDeltaOverride &, std::int64_t processingNs) noexcept;
    bool Transfer(const ReloadMagazineTransfer &, std::uint64_t parentUpdate) noexcept;
    ReloadMagazineLeaseObservation ObserveLease(const ReloadHoldIdentity&,std::uint64_t,std::int64_t) noexcept;
    std::optional<ReloadMagazineLease> Lease(const ReloadHoldIdentity &, std::uint64_t cycle, std::int64_t nowNs) noexcept;
    bool Submit(const ReloadMagazineNativeRequest &, std::int64_t nowNs) noexcept;
    std::optional<ReloadMagazineAckEvidence> TakeAcknowledgement(const ReloadHoldIdentity &, std::uint64_t cycle,
                                                            std::int64_t nowNs) noexcept;
    // Recheck immediately before a context override. Entry timestamps are not
    // patch decision times; expired authorization never extends the hold.
    bool Allows(const ReloadRequestDecision &, std::int64_t nowNs) noexcept;
    void Cancel(ReloadRequestCycleFailure = ReloadRequestCycleFailure::Stopped) noexcept;
    // Only a still-fresh, never-held Arming cycle can wait for replacement
    // observation evidence. This changes no identity, input lease or deadline.
    bool MissingEvidence(std::int64_t nowNs) noexcept;
    // Only the adapter's shared callback-entry exclusion + drained/revision
    // proof may authorize this. Retires orphaned slots after a failed exit read
    // or try-lock; preserves cancellation, unresolved outcome and ID history.
    bool DrainCancelledInvocations(bool callbacksDrained) noexcept;
    ReloadRequestCyclePhase Phase() const noexcept
    {
        return phase_;
    }
    ReloadRequestCycleFailure Failure() const noexcept
    {
        return failure_;
    }
    const ReloadHoldIdentity &Identity() const noexcept
    {
        return control_.identity;
    }
    std::uint64_t Cycle() const noexcept
    {
        return control_.cycle;
    }
    bool UnresolvedRequest() const noexcept
    {
        return unresolved_;
    }
    std::uint64_t PendingRequest() const noexcept
    {
        return pendingRequest_;
    }
    const std::optional<ReloadRequestClockFailure> &ClockFailure() const noexcept { return clockFailure_; }

  private:
    bool Current(std::int64_t nowNs) noexcept;
    bool Common(const ReloadHoldInput &, bool timingVerified, std::int64_t processingNs) noexcept;
    bool Recent(const std::array<std::int64_t, 3> &, std::int64_t nowNs) const noexcept;
    bool Open() const noexcept;
    void Publish(const ReloadHoldInput &, bool cohortStable, std::int64_t processingNs) noexcept;
    bool Complete(const ReloadHoldInput &, bool cohortStable, std::int64_t processingNs) noexcept;
    ReloadRequestDecision Track(const ReloadHoldInput &, std::uint64_t update, bool hold, std::int64_t processingNs) noexcept;
    ReloadRequestCyclePhase phase_ = ReloadRequestCyclePhase::Disabled;
    MagazineNativeProfile profile_{};
    ReloadRequestCycleFailure failure_ = ReloadRequestCycleFailure::None;
    ReloadCycleControl control_{};
    std::optional<ReloadMagazineStartupPulse> startupPulse_;
    std::int64_t firstHoldingNs_=0;std::array<std::int64_t,3> armingContexts_{};
    ReloadObservedConfig config_{};
    bool configured_ = false, unresolved_ = false;
    int loaded_ = 0, reserve_ = 0, capacity_ = 0,units_=0;
    interaction::ManualReloadRequest unseatRequest_{};
    bool unseatAckTaken_=false;
    std::uint64_t lastUnseat_=0;
    std::array<std::int64_t, 3> contexts_{}, holds_{};
    std::array<std::uint64_t, 3> open_{};
    std::uint64_t lastCycle_ = 0, lastRequest_ = 0, sequence_ = 0, pendingRequest_ = 0;
    std::int64_t advanceDeadline_ = 0, lastNow_ = 0;
    ReloadMagazineCompletion completion_{true};
    std::optional<ReloadMagazineLease> published_{};
    std::optional<ReloadMagazineAckEvidence> ack_{};
    std::optional<ReloadRequestClockFailure> clockFailure_{};
};
} // namespace fvr::bc2
