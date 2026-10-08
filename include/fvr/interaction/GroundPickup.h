#pragma once
#include "fvr/interaction/BodyInventory.h"
#include "fvr/interaction/HandInteraction.h"
#include <array>
#include <cstdint>
#include <optional>

namespace fvr::interaction {
// A ground entity is never an owned BodyItemKey. All generations are supplied
// by a verified adapter lifecycle, not synthesized from a reused pointer/name.
struct WorldPickupKey {
    std::uint64_t world=0,worldGeneration=0,entity=0,entityGeneration=0;
    bool operator==(const WorldPickupKey&)const=default;
};
struct PickupActor {
    std::uint64_t world=0,worldGeneration=0,actor=0,actorGeneration=0;
    bool operator==(const PickupActor&)const=default;
};
struct PickupWindow {
    std::uint64_t sequence=0;
    std::int64_t observedNs=0,deadlineNs=0;
    bool operator==(const PickupWindow&)const=default;
};
struct PickupAmmo {
    std::uint64_t kind=0,rounds=0;
    bool operator==(const PickupAmmo&)const=default;
};
// Unique ammunition kinds, total owned rounds across all verified native
// copies/pools. Shared reserves appear once. This is read-only evidence, not an
// instruction to assign counts. Automatic native grants need a different,
// separately proved policy; this transaction accepts conservation only.
struct PickupAmmoLedger {
    std::array<PickupAmmo,16> entries{};std::uint32_t count=0;
    bool operator==(const PickupAmmoLedger&)const=default;
};
struct PickupBundle {
    BodyItemKey item{};
    // Immutable observed contents identity includes weapon, attachments and
    // loaded/chamber state; it is not an asset hash or an ammunition authority.
    HandInteractionKey contents{};
    std::uint32_t slot=0,memberCount=0;
    // Linked subweapons can change raw entry count without adding a carried gun.
    std::array<HandInteractionKey,8> members{};
    bool operator==(const PickupBundle&)const=default;
};
struct PickupInventory {
    PickupActor actor{};
    std::uint64_t inventory=0,generation=0,revision=0;
    PickupWindow source{};
    std::uint32_t capacity=0,count=0;
    std::array<PickupBundle,32> bundles{};
    PickupAmmoLedger ammo{};
    bool operator==(const PickupInventory&)const=default;
};
struct WorldPickupLease {
    WorldPickupKey key{};
    HandInteractionKey interaction{},contents{};
    PickupWindow source{};
    PickupAmmoLedger ammo{};
    bool operator==(const WorldPickupLease&)const=default;
};
struct GroundPickupCapabilities {
    bool worldIdentity=false,linkedInventory=false,ammunitionLedger=false;
    bool reversiblePreview=false,targetedExchange=false,dropReceipt=false;
    bool retirementReceipt=false;
    constexpr bool Ready()const noexcept {
        return worldIdentity&&linkedInventory&&ammunitionLedger&&reversiblePreview&&
            targetedExchange&&dropReceipt&&retirementReceipt;
    }
};
struct GroundPickupPreviewRequest {
    std::uint64_t id=0;PickupActor actor{};WorldPickupKey worldItem{};
    HandClaimToken claim{};PickupWindow source{};
};
struct GroundPickupPreviewReceipt {
    std::uint64_t request=0;PickupActor actor{};WorldPickupKey worldItem{};
    HandClaimToken claim{};PickupWindow source{};
    // Actual reversible world concealment + both private eye copies + native
    // action suppression. These are adapter receipts, never gesture outcomes.
    bool worldConcealed=false,pairedHeld=false,actionsSuppressed=false;
};
struct GroundPickupSwapRequest {
    std::uint64_t id=0,preview=0;PickupActor actor{};
    WorldPickupKey incoming{};HandInteractionKey contents{};
    std::uint64_t inventory=0,inventoryGeneration=0,revision=0;
    std::uint32_t nativeSlot=0;BodySlotId holster=0;BodyItemKey outgoing{};
    PickupWindow source{};
};
struct PickupSlotBinding {
    BodySlotId holster=0;BodyItemKey item{};std::uint32_t nativeSlot=0;
    std::uint64_t inventoryRevision=0;PickupWindow source{};
};
enum class PickupDispatch:std::uint8_t {NotStarted,Accepted,Unknown};
struct GroundPickupNoDispatchReceipt {
    std::uint64_t request=0;PickupActor actor{};
    bool noNativeExchangeStarted=false,nativeCallsDrained=false;
};
struct GroundPickupExchangeReceipt {
    std::uint64_t request=0;PickupActor actor{};
    WorldPickupKey consumed{};HandInteractionKey incomingContents{};
    BodyItemKey acquired{},droppedOwned{};
    WorldPickupLease droppedWorld{};
    PickupInventory after{};
    // Must originate at the native transaction/observer boundary. A sent Use
    // button, item disappearance or changed HUD alone proves neither field.
    bool incomingRetired=false,nativeCallsDrained=false;
};
enum class PickupCleanup:std::uint8_t {RestoreWorld,HolsterAcquired};
struct GroundPickupCleanupReceipt {
    std::uint64_t preview=0,exchange=0;PickupActor actor{};
    WorldPickupKey worldItem{};BodyItemKey acquired{};BodySlotId holster=0;
    PickupWindow source{};PickupCleanup operation=PickupCleanup::RestoreWorld;
    // Released includes the exact shared-hand claim. Restored means the normal
    // owned presentation's input mask (a holstered gun still cannot fire).
    bool temporaryReleased=false,pairedPresentation=false,actionsRestored=false;
};
struct GroundPickupRetirementReceipt {
    PickupActor oldActor{};std::uint64_t preview=0,exchange=0;
    bool actorGenerationRetired=false,nativeCallsDrained=false,temporaryReleased=false;
};
enum class GroundPickupPhase:std::uint8_t {
    Idle,PreparingPreview,Temporary,AwaitingDispatch,AwaitingNative,
    ReconcileRequired,RestoringWorld,HolsteringAcquired,Complete,Cancelled
};
enum class GroundPickupReason:std::uint8_t {
    None,Unavailable,InvalidEvidence,HandBusy,PreviewPending,ChangedEvidence,
    InvalidSlot,StaleReceipt,ExchangeUnproved,Expired,UnknownDispatch
};

// Serialized portable transaction only. No geometry, engine calls, item creation,
// native ammunition writes, hiding or action injection. The adapter must recheck
// the immutable request immediately before dispatch. No runtime adapter exists
// in BC2 yet. Reconciliation can remain pending, but this class never authorizes
// fire suppression beyond a currently valid actual presentation receipt.
class GroundPickup {
public:
    explicit GroundPickup(GroundPickupCapabilities capabilities={}):capabilities_(capabilities){}
    std::optional<GroundPickupPreviewRequest> Begin(const PickupInventory&,const WorldPickupLease&,
        const HandClaim& currentClaim,std::int64_t nowNs)noexcept;
    bool Preview(const GroundPickupPreviewReceipt&,std::int64_t nowNs)noexcept;
    std::optional<GroundPickupSwapRequest> Stow(const PickupInventory&,const WorldPickupLease&,
        const HandClaim&,const PickupSlotBinding&,std::int64_t nowNs)noexcept;
    bool DispatchAllowed(std::uint64_t request,const PickupInventory&,const WorldPickupLease&,
        const HandClaim&,std::int64_t nowNs)noexcept;
    bool Dispatched(std::uint64_t request,PickupDispatch,std::int64_t nowNs)noexcept;
    bool Exchange(const GroundPickupExchangeReceipt&,std::int64_t nowNs)noexcept;
    // Fresh non-execution + unchanged inventory/source proof is required to
    // leave an ambiguous dispatch. Timeout is never interpreted as rollback.
    bool ProveNotStarted(const GroundPickupNoDispatchReceipt&,const PickupInventory&,
        const WorldPickupLease&,std::int64_t nowNs)noexcept;
    void Cancel()noexcept;
    void Tick(std::int64_t nowNs)noexcept;
    bool Cleanup(const GroundPickupCleanupReceipt&,std::int64_t nowNs)noexcept;
    bool Retire(const GroundPickupRetirementReceipt&)noexcept;
    bool Reset()noexcept;
    GroundPickupPhase Phase()const noexcept{return phase_;}
    GroundPickupReason Reason()const noexcept{return reason_;}
    const std::optional<GroundPickupSwapRequest>& Pending()const noexcept{return swap_;}
    const std::optional<GroundPickupPreviewRequest>& PreviewRequest()const noexcept{return preview_;}
    bool PresentationAuthority(const PickupActor&,std::int64_t nowNs)const noexcept;
    std::optional<BodyItemKey> Acquired()const noexcept{return acquired_;}
private:
    bool Clock(std::int64_t)noexcept;
    bool SameBaseline(const PickupInventory&,const WorldPickupLease&,std::int64_t)const noexcept;
    static bool Fresh(PickupWindow,std::int64_t)noexcept;
    static bool Inventory(const PickupInventory&)noexcept;
    static bool World(const WorldPickupLease&)noexcept;
    static bool Ledger(const PickupAmmoLedger&)noexcept;
    static bool Conserved(const PickupAmmoLedger&,const PickupAmmoLedger&,
        const PickupAmmoLedger&,const PickupAmmoLedger&)noexcept;
    GroundPickupCapabilities capabilities_{};
    GroundPickupPhase phase_=GroundPickupPhase::Idle;
    GroundPickupReason reason_=GroundPickupReason::None;
    std::uint64_t serial_=0;
    std::int64_t lastNow_=0,exchangeObservedNs_=0;
    PickupInventory before_{};WorldPickupLease incoming_{};HandClaim claim_{};
    std::optional<GroundPickupPreviewRequest> preview_;
    std::optional<GroundPickupPreviewReceipt> presentation_;
    std::optional<GroundPickupSwapRequest> swap_;
    std::optional<BodyItemKey> acquired_;
};
}
