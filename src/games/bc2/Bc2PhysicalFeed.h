#pragma once
#include "Bc2FeedEquipmentAdapter.h"
#include "Bc2FeedEquipmentProfile.h"
#include "Bc2ReloadPreview.h"
#include "Bc2SelectedMeshes1p.h"
#include "Bc2EquipmentIdentity.h"
#include "Bc2FeedAmmoSupply.h"
#include <memory>
#include <iosfwd>

namespace fvr::bc2 {
// Mod-owned local poses. This is presentation intent, never native cover,
// chamber, ammunition, or firing-state evidence. Each selected chain bone
// remains independently articulated; the pouch is a separate object.
struct Bc2PhysicalFeedPresentation {
    ReloadStateOwner owner{};
    RigIdentity rig{};
    interaction::HandInteractionSample input{};
    interaction::HandInteractionKey weapon{};
    interaction::FeedSupplyKey supply{};
    std::uint64_t lifetime=0,revision=0,rigFingerprint=0,registrationKey=0;
    math::Matrix4 weaponFromPouch{},weaponFromCoverHinge{};
    std::array<math::Matrix4,MaxBc2FeedChainBones> weaponFromChain{};
    unsigned chainCount=0;
    // Bits: pouch=1, cover=2, then active chain bones. Cover is always attached.
    // Carried parts use their retained original basis; attached parts use the
    // current placed weapon. Neither release nor a pending receipt erases it.
    std::uint16_t carriedPartsMask=0;
    math::Matrix4 pouchCarryWeaponWorldMeters{},chainCarryWeaponWorldMeters{};
    math::Matrix4 originalWeaponWorldMeters{};
    std::optional<math::Matrix4> mechanismWristWorldMeters;
    std::optional<interaction::HandClaim> mechanismClaim;
    std::optional<interaction::AmmoSupplyObject> supplyObject;
    interaction::FeedPhysicalOperation operation=interaction::FeedPhysicalOperation::None;
    interaction::FeedInteractionTargetKind targetKind=interaction::FeedInteractionTargetKind::None;
    float coverRadians=0;
    bool blocksFire=true;
};
// Serialized Gameplay publishes; renderer reads a bounded immutable copy.
std::shared_ptr<const Bc2PhysicalFeedPresentation> ReadBc2PhysicalFeedPresentation(
    const ReloadStateOwner&,std::int64_t nowNs)noexcept;
struct Bc2PhysicalFeedApi {
    void* context=nullptr;
    std::optional<FeedEquipmentNativeView> (*read)(void*,const ReloadStateOwner&,std::int64_t)noexcept=nullptr;
    bool (*submit)(void*,const FeedEquipmentSubmission&)noexcept=nullptr;
    std::optional<interaction::FeedAmmunitionReceipt> (*receipt)(void*,std::uint64_t,std::uint64_t)noexcept=nullptr;
    std::optional<FeedEquipmentNativeView> (*readAt)(void*,const ReloadStateOwner&,std::int64_t,std::int64_t)noexcept=nullptr;
    FeedEquipmentSubmitResult (*submitObserved)(void*,const FeedEquipmentSubmission&,bool noEntryOnly)noexcept=nullptr;
    bool (*publishGate)(void*,const FeedEquipmentGate&)noexcept=nullptr;
    std::optional<FeedEquipmentGate> (*readGate)(void*,const interaction::AmmoResourceIdentity&)noexcept=nullptr;
    std::optional<FeedEquipmentActorRetirement> (*retirement)(void*,std::uint64_t afterEvent)noexcept=nullptr;
};
struct Bc2PhysicalFeedSample {
    ReloadStateOwner owner{};
    // Current native identity is separate from the reusable weapon address.
    // Missing identity suspends interaction; a fresh binding also pins custody.
    std::optional<WeaponEquipmentIdentity> equipment;
    std::optional<AmmoResourceBinding> binding;
    interaction::HandInteractionSample input{};
    interaction::HandInteractionKey weapon{};
    std::shared_ptr<const SelectedMeshesSnapshot> selected;
    ReloadRawContact raw{};
    // Same current controller packet, in the shared recentered body frame.
    std::optional<math::Matrix4> bodyFromHand;
    // Exact selected authored rig/configuration evidence from the same frame.
    std::string_view asset{},configurationPath{},meshPath{};
    std::uint64_t frameSequence=0,rigFingerprint=0;
    bool selectedRigVerified=false;
    bool grip=false,reloadPressed=false,cancel=false;
};
struct Bc2PhysicalFeedResult {
    bool selected=false,blocksFire=false,ownsHand=false,pending=false,submitRefused=false;
    // An idle unfinished mechanism may be stowed. Only a live hand operation
    // or an unresolved native request prevents an equipment transition.
    bool blocksEquipment=false;
    interaction::FeedEquipmentStatus status=interaction::FeedEquipmentStatus::Unbound;
    interaction::FeedInteractionStatus interactionStatus{};
    interaction::FeedReloadStatus reloadStatus{};
    interaction::HandInteractionReason handReason=interaction::HandInteractionReason::None;
    std::shared_ptr<const Bc2PhysicalFeedPresentation> presentation;
    std::optional<Bc2FeedBodyAmmo> bodyAmmo;
    std::optional<interaction::AmmoSupplyVisualSample> bodyVisual;
};
class Bc2PhysicalFeed {
public:
    explicit Bc2PhysicalFeed(Bc2PhysicalFeedApi api={})noexcept;
    ~Bc2PhysicalFeed();
    Bc2PhysicalFeed(const Bc2PhysicalFeed&)=delete;
    Bc2PhysicalFeed& operator=(const Bc2PhysicalFeed&)=delete;
    Bc2PhysicalFeedResult Tick(const Bc2PhysicalFeedSample&,interaction::HandInteraction&,std::uint64_t& intent)noexcept;
    void Suspend(const interaction::HandInteractionSample&,interaction::HandInteraction&)noexcept;
    void Report(std::ostream&)const;
    // Suppression only: caller must independently revalidate live native
    // equipment/actor. An expired pose grants no rendering or ammo authority.
    bool BlocksFire(const ReloadStateOwner&,const WeaponEquipmentIdentity&,
        const std::optional<AmmoResourceBinding>& freshBinding={})const noexcept;
    std::optional<FeedEquipmentGate> RetainedGate(const ReloadStateOwner&,const WeaponEquipmentIdentity&,
        const std::optional<AmmoResourceBinding>& freshBinding={})const noexcept;
private:
    struct State;
    std::unique_ptr<State> state_;
};
}
