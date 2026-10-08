#pragma once
#include "fvr/interaction/GroundPickup.h"
#include "fvr/interaction/ControllerInput.h"
#include "fvr/interaction/TrackingMath.h"

namespace fvr::interaction {
enum class TemporaryNativeEquip:std::uint8_t {ExternalActiveEntity,BorrowedNativeSlot};
struct TemporaryFireCapabilities {
    bool externalEquip=false,borrowedEquip=false,exactFireTarget=false;
    bool nativeAmmoAuthority=false,rawTrackedAim=false,reversibleRetirement=false;
    constexpr bool Ready(TemporaryNativeEquip mode)const noexcept {
        const bool equip=mode==TemporaryNativeEquip::ExternalActiveEntity?externalEquip:
            mode==TemporaryNativeEquip::BorrowedNativeSlot?borrowedEquip:false;
        return equip&&exactFireTarget&&nativeAmmoAuthority&&rawTrackedAim&&reversibleRetirement;
    }
};
struct TemporaryFireTarget {
    HandInteractionKey weapon{},clientFiring{},serverFiring{},ammunitionAuthority{};
    bool operator==(const TemporaryFireTarget&)const=default;
};
struct PermanentLoadoutKey {
    std::uint64_t inventory=0,generation=0,assignmentRevision=0;
    bool operator==(const PermanentLoadoutKey&)const=default;
};
// A native equip can temporarily project a gun into a borrowed engine slot.
// This never creates a third permanent holster assignment. The adapter must
// prove displaced contents ownership/restoration rather than just saving a raw
// pointer. External and borrowed modes require different native evidence.
struct TemporaryEquipProjection {
    TemporaryNativeEquip mode=TemporaryNativeEquip::ExternalActiveEntity;
    PermanentLoadoutKey permanent{};
    std::uint64_t nativeInventoryRevision=0;
    HandInteractionKey restoration{};
    std::optional<BodyItemKey> displaced;
    std::uint32_t borrowedSlot=0;
    bool assignmentsUnchanged=false,displacedStillOwned=false;
    bool operator==(const TemporaryEquipProjection&)const=default;
};
struct TemporaryHeldIdentity {
    HandInteractionKey lease{}; // New generation on native target/binding change.
    PickupActor actor{};WorldPickupKey originalWorldItem{};
    HandInteractionOwner controlOwner{};
    HandInteractionKey heldItem{};TemporaryFireTarget target{};
    // Native readback receipt for Fire cleared across the equip boundary before
    // this target becomes active; the first gate update is too late to prove it.
    HandInteractionKey activationClear{};
    TemporaryEquipProjection projection{};
    std::uint64_t inputEpoch=0,activationInput=0;
    std::int64_t activatedNs=0;
    bool operator==(const TemporaryHeldIdentity&)const=default;
};
struct TemporaryHeldNativeReceipt {
    TemporaryHeldIdentity identity{};PickupWindow source{};
    // Exact native current weapon/fire target and authoritative ammo owner have
    // been observed together. None follows from a controller pose or sent input.
    std::uint64_t ammunitionRevision=0;
    bool currentFireTarget=false,ammunitionBound=false,nativePlaying=false;
    bool heldPresentationPaired=false,permanentWeaponSuppressed=false;
    bool activationTriggerCleared=false,triggerRouteOwned=false;
};
struct TemporaryHeldAimReceipt {
    HandInteractionKey lease{};TemporaryFireTarget target{};HandClaimToken claim{};
    std::uint64_t inputEpoch=0,space=0;PickupWindow inputSource{};
    math::Pose rawGrip{},rawAim{};math::Matrix4 trackedMuzzle{};
    // A verified adapter's normal aim/muzzle route, using raw controller source
    // and weapon calibration. Render IK or temporary prop pose is not authority.
    bool nativeAimBound=false;
};
struct TemporaryHeldFireSample {
    InputFrame input{};HandInteractionSample hand{};
    std::uint64_t inputEpoch=0;
    std::optional<HandClaim> gunClaim; // Current actual shared arbiter claim.
    std::optional<TemporaryHeldNativeReceipt> native;
    std::optional<TemporaryHeldAimReceipt> aim;
};
enum class TemporaryFireReason:std::uint8_t {
    None,Disabled,MissingEvidence,InvalidNative,InvalidInput,InvalidAim,
    IdentityChanged,AwaitingRelease,StalePacket,Expired,ClockReversed
};
struct TemporaryFireCommand {
    TemporaryHeldIdentity identity{};
    std::uint64_t inputSequence=0,ammunitionRevision=0;
    std::int64_t deadlineNs=0;
    math::Matrix4 trackedMuzzle{};
    bool triggerHeld=false,triggerPressed=false;
};
struct TemporaryFireResult {
    std::optional<TemporaryFireCommand> command;
    // Informational revocation only; never dispatch an old-owner release into
    // a new native weapon. Native adapter cleanup must validate its own owner.
    std::optional<TemporaryHeldIdentity> revoked;
    TemporaryFireReason reason=TemporaryFireReason::MissingEvidence;
    bool armed=false;
};
// Level-trigger permission for the existing native input boundary, never a shot
// operation. Native cadence, ammunition, projectile creation and recoil remain
// native. Pickup/identity transition always outputs false and then requires a
// distinct fresh trigger<=0.1 packet; only a later >=0.75 packet can request Fire.
class TemporaryHeldFire {
public:
    explicit TemporaryHeldFire(TemporaryFireCapabilities capabilities={}):capabilities_(capabilities){}
    TemporaryFireResult Update(const TemporaryHeldFireSample&)noexcept;
    TemporaryFireResult Suspend()noexcept;
private:
    bool ValidNative(const TemporaryHeldNativeReceipt&,std::int64_t)const noexcept;
    bool ValidInputSource(const TemporaryHeldFireSample&)const noexcept;
    bool ValidAim(const TemporaryHeldFireSample&)const noexcept;
    TemporaryFireResult Reject(TemporaryFireReason)noexcept;
    TemporaryFireCapabilities capabilities_{};
    std::optional<TemporaryHeldIdentity> identity_;
    std::optional<HandClaimToken> lastClaim_;
    std::optional<TemporaryHeldNativeReceipt> lastNative_;
    PickupWindow lastInput_{};math::Pose lastGrip_{},lastAim_{};
    float lastTrigger_=0;
    std::int64_t lastNow_=0;
    bool armed_=false,held_=false,needsTransition_=true;
};

// This settlement is a native adapter boundary, not synthesized by the fire
// policy. Firing changes contents/rounds, so the original GroundPickup baseline
// cannot be reused. Actual post-fire snapshots and observed native consumption
// must settle before establishing a new holster-commit baseline. Raw engine slot
// borrowing must also be restored/projected without changing permanent ownership.
struct TemporaryHeldSettlementReceipt {
    TemporaryHeldIdentity identity{};
    PickupInventory permanentAfter{};
    WorldPickupLease heldAfter{};
    PickupAmmoLedger nativeConsumed{};
    HandInteractionKey nativeConsumptionReceipt{};
    bool triggerCleared=false,nativeCallsDrained=false,projectionSettled=false;
};
// Validates read-only accounting/identity for a future adapter handoff. It does
// not mutate GroundPickup or claim any native restoration happened.
bool ValidateTemporarySettlement(const TemporaryHeldNativeReceipt& original,
    const PickupInventory& permanentBefore,const WorldPickupLease& heldBefore,
    const TemporaryHeldSettlementReceipt& settled,std::int64_t nowNs)noexcept;
}
