#pragma once
#include "fvr/interaction/ReloadInsertion.h"
#include <optional>

namespace fvr::interaction {
// A representation of EXISTING reserve ammunition. None of these types owns a
// native count, creates inventory, or acknowledges a native operation.
struct AmmoSupplyIdentity {
    HandInteractionOwner owner{};
    HandInteractionKey weapon{},profile{},pool{};
    std::uint64_t trackingEpoch=0;
    bool operator==(const AmmoSupplyIdentity&)const=default;
};
struct AmmoSupplySource {
    AmmoSupplyIdentity identity{};
    ReloadInsertionFamily family=ReloadInsertionFamily::Unknown;
    // Explicit adapter units. Shells use 1 round. Magazine adapters must supply
    // a verified resource cost; this policy does not infer magazine capacity.
    std::uint32_t reserveUnits=0,objectUnits=0;
    std::uint64_t sequence=0;
    std::int64_t observedNs=0,deadlineNs=0;
    bool verified=false;
    bool operator==(const AmmoSupplySource&)const=default;
};
struct AmmoSupplyContact {
    std::array<float,3> centerMeters{};
    float radiusMeters=0;
};
struct AmmoSupplyConfig {
    InteractionHand hand=InteractionHand::Left;
    // Adapter allocates a resource namespace distinct from weapons/other items.
    // Each acquisition gets a new generation, preserved across cancellation.
    std::uint64_t itemNamespace=0;
    HandInteractionKey pouch{};
    std::array<float,3> pouchCenterMeters{}; // current body frame, not world origin
    float pouchRadiusMeters=0;
    std::int64_t maxSourceLifetimeNs=200000000;
    // A second contact on the SAME provider: no new pool, item namespace,
    // reservation or claim. Both contacts use bodyFromHand's exact frame.
    std::optional<AmmoSupplyContact> alternateContact{};
};
struct AmmoSupplySample {
    HandInteractionSample input{}; // real current safety, shared with arbiter
    AmmoSupplySource source{};
    std::uint64_t geometrySequence=0,trackingEpoch=0;
    math::Matrix4 bodyFromHand{}; // RAW coherent pose, metres
    bool gripPressed=false;
    // Shared per-hand intent allocator; do not use a separate feature counter.
    std::uint64_t intent=0;
};
struct AmmoSupplyObject {
    AmmoSupplyIdentity identity{};
    HandInteractionKey item{};
    HandClaim claim{};
    ReloadInsertionFamily family=ReloadInsertionFamily::Unknown;
    std::uint32_t units=0;
};
struct AmmoSupplyReservation {
    AmmoSupplyIdentity identity{};
    HandInteractionKey item{};
    HandClaimToken claim{};
    std::uint64_t seat=0,request=0,cycle=0,sourceSequence=0;
    std::int64_t startedNs=0;
    std::uint32_t units=0,reserveBefore=0;
    ReloadOperation operation=ReloadOperation::None;
    bool operator==(const AmmoSupplyReservation&)const=default;
};
// Produced ONLY by an adapter after its native completion observer/bridge has
// confirmed this exact request. Physical seating, elapsed time, animations and
// submitted commands cannot manufacture this evidence. Rejected means proven
// no effect, never an unresolved timeout or lost owner.
struct AmmoSupplyReceipt {
    AmmoSupplyReservation reservation{};
    ManualReloadAck acknowledgement{}; // original PHYSICAL request identity
    AmmoSupplySource currentReserve{};
    std::uint64_t nativeEvent=0;
    std::int64_t observedNs=0,deadlineNs=0;
    bool verified=false;
};
// Retires UNKNOWN local intent only after the adapter proves the old native
// cycle can no longer execute (cancelled/drained), and samples reserve afterward.
// Owner loss or elapsed time by themselves are not that proof. This does not
// classify the old operation as applied/rejected, and never reports consumed.
struct AmmoSupplyRebaseline {
    AmmoSupplyReservation reservation{};
    AmmoSupplySource currentReserve{};
    std::uint64_t retirement=0; // monotonic verified native lifecycle event
    std::int64_t observedNs=0,deadlineNs=0; // time old cycle was proved drained
    bool verifiedNativeCycleDrained=false;
};
// Durable completion of the original reservation after its hand/context ended.
// The adapter must prove the exact native terminal outcome. This releases only
// local custody; it supplies no current reserve and cannot create another item.
struct AmmoSupplyTerminalReceipt {
    AmmoSupplyReservation reservation{};
    ManualReloadAck acknowledgement{};
    std::uint64_t event=0;
    std::int64_t completedNs=0;
    std::uint32_t reserveBefore=0,reserveAfter=0;
    bool nativeFinalVerified=false;
};
enum class AmmoSupplyReason:std::uint8_t {
    None,InvalidConfig,InvalidInput,StaleInput,InvalidSource,SourceChanged,
    TrackingLost,Released,NeedNeutral,OutsidePouch,ReserveExhausted,
    HandUnavailable,ClaimLost,PendingOtherOwner,InvalidReservation,
    InvalidReceipt,CounterExhausted,Explicit
};
struct AmmoSupplyResult {
    AmmoSupplyReason reason=AmmoSupplyReason::None;
    std::optional<AmmoSupplyObject> held{};
    std::optional<HandInteractionKey> acquired{},released{};
};
struct AmmoSupplyResolution {
    bool accepted=false;
    std::optional<AmmoSupplyReservation> consumed{},releasedReservation{};
};
// Serialized alongside the one shared HandInteraction. Caller updates that
// arbiter with actual safety first. This policy releases only its exact token;
// it never resets the arbiter or transfers another feature's claim.
class AmmoSupply {
public:
    explicit AmmoSupply(AmmoSupplyConfig config)noexcept:config_(config){}
    AmmoSupply(const AmmoSupply&)=delete;
    AmmoSupply& operator=(const AmmoSupply&)=delete;
    AmmoSupplyResult Update(const AmmoSupplySample&,HandInteraction&)noexcept;
    // Safety/ownership only: no acquisition or retained geometry evidence.
    AmmoSupplyResult WaitHeld(const AmmoSupplySample& s,HandInteraction& h)noexcept{return UpdateCurrent(s,h,false);}
    std::optional<AmmoSupplyReservation> Reserve(const AmmoSupplySample&,HandInteraction&,
        const ReloadInsertionSeat&,const ManualReloadRequest& physicalRequest,std::uint64_t nativeCycle)noexcept;
    // Renderer contact may arrive from N-1. Its exact original sample/claims
    // must have been retained by Update, remain unexpired and still own both
    // current hands. Current safety/source is authoritative; old geometry is
    // never renewed or restamped. Update after renewing GunHold each packet.
    std::optional<AmmoSupplyReservation> ReserveFrom(const AmmoSupplySample& currentSafety,HandInteraction&,
        const AmmoSupplySample& originalEvidence,const ReloadInsertionSeat&,
        const ManualReloadRequest& physicalRequest,std::uint64_t nativeCycle)noexcept;
    // After successful native submission, the item belongs to the weapon.
    // Releases only its exact hand token; never resolves/refunds the reservation.
    bool ReleaseSubmitted(const HandInteractionSample&,HandInteraction&,const AmmoSupplyReservation&)noexcept;
    AmmoSupplyResolution Resolve(const HandInteractionSample& currentSafety,HandInteraction&,
        const AmmoSupplyReceipt&)noexcept;
    AmmoSupplyResolution Rebaseline(const HandInteractionSample& currentSafety,HandInteraction&,
        const AmmoSupplyRebaseline&)noexcept;
    AmmoSupplyResolution SettleTerminal(const HandInteractionSample&,HandInteraction&,
        const AmmoSupplyTerminalReceipt&)noexcept;
    // Drops presentation; pending native work stays quarantined indefinitely.
    // Explicit confirmed receipt or verified post-drain baseline is required to
    // clear it. IDs are not reset.
    AmmoSupplyResult Cancel(const HandInteractionSample&,HandInteraction&)noexcept;
    const std::optional<AmmoSupplyObject>& Held()const noexcept{return held_;}
    const std::optional<AmmoSupplyReservation>& Pending()const noexcept{return pending_;}
private:
    bool ConfigValid()const noexcept;
    bool SourceValid(const AmmoSupplySource&,std::int64_t now)const noexcept;
    bool Budget(const AmmoSupplySource&,std::uint32_t units,HandInteractionKey item={})const noexcept;
    AmmoSupplyResult UpdateCurrent(const AmmoSupplySample&,HandInteraction&,bool allowAcquire=true)noexcept;
    std::optional<AmmoSupplyReservation> ReserveCurrent(const HandInteractionSample&,HandInteraction&,
        const ReloadInsertionSeat&,const ManualReloadRequest&,std::uint64_t cycle)noexcept;
    AmmoSupplyResult Drop(const HandInteractionSample&,HandInteraction&,AmmoSupplyReason)noexcept;
    AmmoSupplyResult Snapshot(AmmoSupplyReason reason=AmmoSupplyReason::None)const noexcept{return {reason,held_,{}, {}};}
    AmmoSupplyConfig config_{};
    std::optional<AmmoSupplyObject> held_{};
    std::optional<AmmoSupplyReservation> pending_{};
    AmmoSupplySource source_{},settled_{};
    HandInteractionSample input_{};
    struct RetainedEvidence {AmmoSupplySample sample{};AmmoSupplyObject item{};HandClaim gun{};};
    std::array<std::optional<RetainedEvidence>,32> history_{};
    std::size_t historyNext_=0;
    std::uint64_t nextItem_=0,lastSeat_=0,lastRequest_=0,lastRetirement_=0;
    std::int64_t lastNow_=0,terminalCutoffNs_=0;
    bool seen_=false,armed_=false,blocked_=false,packetGripPressed_=false;
};
} // namespace fvr::interaction
