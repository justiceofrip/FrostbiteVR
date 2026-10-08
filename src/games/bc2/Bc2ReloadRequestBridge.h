#pragma once
#include "Bc2ReloadInteraction.h"
#include "Bc2ReloadRound.h"
namespace fvr::bc2
{
struct Bc2ReloadOwnerMap
{
    ReloadHoldIdentity native{};
    interaction::HandInteractionOwner physical{};
    interaction::HandInteractionKey weapon{};
    std::uint64_t cycle = 0;
    bool operator==(const Bc2ReloadOwnerMap &) const = default;
};
// SPAS-only identity bridge: packed weak/soldier + handEquip and native
// soldier + equipEpoch are intentionally separate. Both generations survive.
std::optional<Bc2ReloadOwnerMap> BindBc2ReloadOwners(const interaction::HandInteractionOwner &,
                                                     interaction::HandInteractionKey, const ReloadRoundLease &,
                                                     std::int64_t nowNs) noexcept;
std::optional<Bc2ReloadNativeLease> ToBc2ReloadInteractionLease(const Bc2ReloadOwnerMap &, const ReloadRoundLease &,
                                                                std::int64_t nowNs) noexcept;
struct Bc2ReloadAckEvidence
{
    ReloadRoundAcknowledgement acknowledgement{};
    std::int64_t observedNs = 0, deadlineNs = 0;
    bool verified = false;
};
struct Bc2ReloadReservedItem
{
    interaction::HandInteractionKey item{};
    interaction::HandClaimToken claim{};
    std::uint64_t seat = 0, request = 0, cycle = 0;
    bool operator==(const Bc2ReloadReservedItem &) const = default;
};
struct Bc2ReloadNativeRequest
{
    interaction::ManualReloadRequest request{};
    ReloadRoundLease heldLease{};
    Bc2ReloadReservedItem reservation{};
};
enum class Bc2ReloadBridgePhase : unsigned
{
    Disabled,
    Idle,
    Pending,
    Complete,
    Cancelled
};
enum class Bc2ReloadBridgeReason : unsigned
{
    None,
    Disabled,
    Busy,
    StaleSeat,
    InvalidRequest,
    OwnerChanged,
    NativeUnavailable,
    InvalidClaim,
    InvalidSeat,
    TrackingLost,
    Expired,
    SequenceRollback,
    Explicit
};
struct Bc2ReloadBridgeResult
{
    Bc2ReloadBridgePhase phase = Bc2ReloadBridgePhase::Disabled;
    Bc2ReloadBridgeReason reason = Bc2ReloadBridgeReason::None;
    std::optional<Bc2ReloadNativeRequest> submit{};
    std::optional<interaction::ManualReloadAck> acknowledged{};
    std::optional<Bc2ReloadTargets> presentation{};
    std::optional<Bc2ReloadReservedItem> consumed{}, releasedReservation{};
    bool unresolvedNative = false;
};
// Command/ack and presentation lifecycle only. No native calls, ammo writes,
// resource creation, hand stealing, or synthetic held-native lease. Root must
// submit exactly once and use consumed/releasedReservation by exact item/token.
class Bc2ReloadRequestBridge
{
  public:
    explicit Bc2ReloadRequestBridge(bool enabled = false) noexcept
        : enabled_(enabled), phase_(enabled ? Bc2ReloadBridgePhase::Idle : Bc2ReloadBridgePhase::Disabled)
    {
    }
    Bc2ReloadBridgeResult Begin(const Bc2ReloadInteractionSample &, const ReloadRoundLease &,
                                const interaction::ReloadInsertionSeat &,
                                const interaction::ManualReloadRequest &physicalRequest,
                                const Bc2ReloadTargets &) noexcept;
    Bc2ReloadBridgeResult Update(const Bc2ReloadInteractionSample &, const ReloadRoundLease &,
                                 const std::optional<Bc2ReloadAckEvidence> &evidence = {}) noexcept;
    // A typed native cohort gap can retain an already submitted transaction.
    // Validates current ownership/input and the ORIGINAL operation deadline;
    // drops presentation and grants no native lease, command, or completion.
    Bc2ReloadBridgeResult ObservePendingInput(const Bc2ReloadInteractionSample &) noexcept;
    Bc2ReloadBridgeResult Cancel(Bc2ReloadBridgeReason = Bc2ReloadBridgeReason::Explicit) noexcept;
    Bc2ReloadBridgePhase Phase() const noexcept
    {
        return phase_;
    }
    const Bc2ReloadOwnerMap &Owners() const noexcept
    {
        return owners_;
    }

  private:
    Bc2ReloadBridgeResult Snapshot(Bc2ReloadBridgeReason = Bc2ReloadBridgeReason::None) const noexcept;
    void UpdatePresentation(const Bc2ReloadInteractionSample &, const ReloadRoundLease &, bool fresh) noexcept;
    bool enabled_ = false, unresolved_ = false;
    Bc2ReloadBridgePhase phase_ = Bc2ReloadBridgePhase::Disabled;
    Bc2ReloadOwnerMap owners_{};
    Bc2ReloadReservedItem reservation_{};
    interaction::ManualReloadRequest physicalRequest_{}, nativeRequest_{};
    interaction::HandClaimToken gunClaim_{};
    Bc2ReloadTargets anchor_{};
    std::optional<Bc2ReloadTargets> presentation_{};
    std::uint64_t lastSeat_ = 0, lastRequest_ = 0, lastSequence_ = 0, lastNativeSequence_ = 0, trackingEpoch_ = 0;
    std::uint64_t initialNativeSequence_ = 0;
    std::int64_t lastNow_ = 0, lastObserved_ = 0, startedNs_ = 0, deadline_ = 0;
};
} // namespace fvr::bc2
