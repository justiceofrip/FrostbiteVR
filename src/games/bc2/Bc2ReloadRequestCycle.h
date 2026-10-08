#pragma once
#include "Bc2ReloadRequestBridge.h"
#include "Bc2ReloadRoundGate.h"

namespace fvr::bc2
{
// A fresh controller/ownership authorization, separate from native observations.
// The adapter clears it on focus, gun-claim, tracking, equip or space loss.
struct ReloadCycleControl
{
    ReloadHoldIdentity identity{};
    std::uint64_t cycle = 0, sequence = 0;
    std::int64_t observedNs = 0, deadlineNs = 0;
    bool permitted = false;
    bool operator==(const ReloadCycleControl &) const = default;
};
enum class ReloadRequestCyclePhase : unsigned
{
    Disabled,
    Idle,
    Arming,
    Holding,
    Advancing,
    Finished,
    Cancelled
};
enum class ReloadRequestCycleFailure : unsigned
{
    None,
    Control,
    Owner,
    Timing,
    State,
    Overlap,
    Patch,
    Completion,
    Expired,
    Stopped
};
struct ReloadRequestDecision
{
    std::uint64_t update = 0, cycle = 0;
    unsigned branch = 3;
    bool tracked = false, hold = false;
    std::int64_t decisionNs = 0, deadlineNs = 0;
    ReloadFiringObservation before{};
};
struct ReloadRequestClockFailure
{
    std::int64_t nowNs = 0, lastNowNs = 0, sourceNs = 0, deadlineNs = 0;
    std::uint64_t sequence = 0;
    bool regression = false;
};
// Persistent REQUEST policy, independent of the bounded diagnostic recorder.
// A cycle has no total time cap while original controller/owner leases remain
// fresh. Only a submitted insertion has a 1.5-second completion deadline.
// Explicit Start/Submit only; no synthetic requests, native calls, input injection
// or ammo writes. The adapter serializes short calls, never across an original,
// supplies coherent quiet-cohort evidence, and uses RunReloadDeltaOverride once.
// SPAS native capability remains disabled until separately integrated/verified.
class Bc2ReloadRequestCycle
{
  public:
    static constexpr std::int64_t AdvanceNs = 1500000000;
    explicit Bc2ReloadRequestCycle(bool enabled = false) noexcept
        : phase_(enabled ? ReloadRequestCyclePhase::Idle : ReloadRequestCyclePhase::Disabled)
    {
    }
    bool Start(const ReloadCycleControl &, std::int64_t nowNs) noexcept;
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
    bool Transfer(const ReloadRoundTransfer &, std::uint64_t parentUpdate) noexcept;
    std::optional<ReloadRoundLease> Lease(const ReloadHoldIdentity &, std::uint64_t cycle, std::int64_t nowNs) noexcept;
    bool Submit(const Bc2ReloadNativeRequest &, std::int64_t nowNs) noexcept;
    std::optional<Bc2ReloadAckEvidence> TakeAcknowledgement(const ReloadHoldIdentity &, std::uint64_t cycle,
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
    ReloadRequestCycleFailure failure_ = ReloadRequestCycleFailure::None;
    ReloadCycleControl control_{};
    ReloadObservedConfig config_{};
    bool configured_ = false, unresolved_ = false;
    int loaded_ = 0, reserve_ = 0, capacity_ = 0;
    std::array<std::int64_t, 3> contexts_{}, holds_{};
    std::array<std::uint64_t, 3> open_{};
    std::uint64_t lastCycle_ = 0, lastRequest_ = 0, sequence_ = 0, pendingRequest_ = 0;
    std::int64_t advanceDeadline_ = 0, lastNow_ = 0;
    ReloadRoundCompletion completion_{true};
    std::optional<ReloadRoundLease> published_{};
    std::optional<Bc2ReloadAckEvidence> ack_{};
    std::optional<ReloadRequestClockFailure> clockFailure_{};
};
} // namespace fvr::bc2
