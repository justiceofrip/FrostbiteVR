#pragma once
#include "Bc2Profile.h"
#include "Bc2EquipmentIdentity.h"
#include "Bc2ReloadProducerBinding.h"
#include "Bc2WeaponProfiles.h"
#include "Bc2SightContact.h"
#include "Bc2SightAdapter.h"
#include "Bc2ReloadPreview.h"
#include "Bc2MagazinePresentation.h"
#include "Bc2BodyAmmoRenderer.h"
#include "Bc2BodyEquipment.h"
#include "Bc2PersistentBodyEquipment.h"
#include "Bc2MagazinePhysicalProbe.h"
#include "Bc2WeaponVisibility.h"
#include "Bc2BodyHolster.h"
#include "Bc2BodyHolsterObservation.h"
#include "fvr/interaction/ControllerInput.h"
#include "fvr/interaction/SupportGrip.h"
#include "fvr/interaction/SightFlip.h"
#include <ostream>
namespace fvr::bc2::rigPublication {
using WeaponSightContact=bc2::WeaponSightContact;
using ResolveOwner=bool(*)(unsigned soldier,unsigned& weak)noexcept;
bool Install(std::span<const std::byte>,const engine::PeImage&,std::uintptr_t,ResolveOwner,bool pulse,bool hands);
// Private rendering instruction; native selection and source animation remain
// authoritative. All matrices are weapon-local, translations in metres.
struct SightPreview {
    const SightAdapterProfile* adapter=nullptr;
    bool valid=false;std::uint64_t token=0,generation=0;
    unsigned weapon=0;std::int64_t deadline=0;
    math::Matrix4 rear{},front{},wrist{};
    math::Vec3 palmPointWrist{},graspPoint{};float angle=0;
    std::uint64_t physicalItem=0,grabGeneration=0;
    interaction::SightFlipPhase phase=interaction::SightFlipPhase::Idle;
    float gestureAngle=0,nativeProgress=0,rawProgress=0;bool nativeObserved=false,rawObserved=false;
    std::uint64_t rawGeneration=0;
};
struct Tracking {
    interaction::InputFrame input{};
    unsigned soldier=0,weak=0,weapon=0;
    std::uint64_t ownerGeneration=0;
    std::int64_t deadline=0;
    float bodyYaw=0;
    math::Vec3 consumed{},actorPosition{};
    math::Matrix4 eyeBase{};bool eyeBaseValid=false;
    const Bc2WeaponProfile* weaponProfile=nullptr; // Static registry lifetime; never crosses IPC.
    std::array<char,64> assetName{};bool supportHolding=false;std::uint64_t supportToken=0;
    SightPreview sightPreview{};
    // Verified shared physical family authorizes only a held sight preview.
    // Raw weapon remains calibration, support, shot and publication identity.
    unsigned sightPhysicalItem=0;
    const SightAdapterProfile* sightAdapter=nullptr;bool sightSecondary=false;
    MagazineTracking magazine{};
    std::optional<interaction::AmmoSupplyVisualSample> bodyMagazine;
    ReloadTracking reload{}; // Explicit mode; defaults off, immutable preview.
    bool weaponActionsBlocked=false;
    std::shared_ptr<const BodyFreeRightEvidence> freeRight; // Actual released claim + native suppression + accepted hide.
    std::uint64_t equipmentGeneration=0;
    WeaponEquipmentIdentity nativeEquipment{};
    std::optional<interaction::BodySlotAssignment> bodyHolsterSlot;
    interaction::BodyAnchorConfig bodyAnchors{};
    std::shared_ptr<const BodyInventoryDisplay> bodyInventoryDisplay;
    std::array<std::shared_ptr<const CarriedMeshesSnapshot>,8> bodyCarriedMeshes{};

};
// Read-only shot candidate from the same immutable pose as the rendered gun.
// flashBone is name/topology validated, but firing semantics still need native
// acceptance before a consumer can use this to modify a projectile.
struct NativeSightState {
 bool valid=false;
 math::Matrix4 rear{},front{}; // Raw native weapon-local metres; NEVER contact proof.
 math::Matrix4 rawHand{};bool rawHandValid=false;unsigned physicalItem=0; // Pre-IK, in placed weapon space.
};
struct WeaponShotFrame {
 math::Matrix4 nativeWeapon{},trackedWeapon{},nativeFlash{},trackedFlash{};
 std::uint64_t generation=0;std::int64_t deadline=0;unsigned flashBone=0;
 float unitsPerMetre=1;std::uint64_t ownerGeneration=0,space=0;
 interaction::SupportGripContact support{};
 WeaponSightContact sight{};
 NativeSightState nativeSight{}; // Same raw item/owner/space/generation/deadline as this frame.
 MagazineRawContact magazine{};
 ReloadRawContact reload{}; // Original pre-IK input generation/times and rig identity.
 // Returned only through the publication's original configured-mesh/input lease.
 // Experimental authored contact, never a native aim/muzzle/holster capability.
 bool authoredSupportReference=false;
};
std::optional<WeaponShotFrame> ReadWeaponShotFrame(unsigned soldier,unsigned weak,unsigned weapon)noexcept;
// Contact-only capability: pending native ammo transfer can block firing while
// a free hand regrips the fore-end. Contains no muzzle or native sight data.
struct WeaponSupportFrame {
 interaction::SupportGripContact support{};
 std::uint64_t generation=0;std::int64_t deadline=0;
 std::uint64_t ownerGeneration=0,space=0;
 bool authoredSupportReference=false;
};
std::optional<WeaponSupportFrame> ReadWeaponSupportFrame(unsigned soldier,unsigned weak,unsigned weapon)noexcept;
// Body transaction ownership observation only; it may describe hidden geometry.
// Never use this getter for firing, support, or EmptyHands acknowledgement.
std::optional<WeaponShotFrame> ReadBodyWeaponFrame(unsigned soldier,unsigned weak,unsigned weapon)noexcept;
ReloadRawContact ReadReloadContact(const ReloadStateOwner&)noexcept;
MagazineRawContact ReadMagazineContact(const ReloadStateOwner&)noexcept;
interaction::SupportGripContact ReadSupportContact(unsigned soldier,unsigned weak,unsigned weapon,std::uint64_t owner,std::uint64_t space)noexcept;
WeaponSightContact ReadSightContact(unsigned soldier,unsigned weak,unsigned weapon,std::uint64_t owner,std::uint64_t space)noexcept;
std::optional<ReloadProducerOwner> ReadReloadProducerOwner()noexcept;
std::uint64_t ReadReticleEquipmentGeneration()noexcept; // Diagnostic only; zero on missing current ownership.
void PublishNativeEye(unsigned soldier,unsigned weak,const math::Matrix4&)noexcept;
std::optional<math::Matrix4> ReadEyeBase(unsigned soldier,unsigned weak,std::uint64_t space)noexcept;
void PublishTracking(const Tracking&)noexcept;
// Original current input/native source only; never renews a visual lease.
std::optional<BodyAmmoRenderSource> ReadBodyAmmoRenderSource(std::int64_t now)noexcept;
std::optional<BodyHolsteredRenderSource> ReadHolsteredBodyRenderSource(std::int64_t now)noexcept;
std::optional<BodyCarriedRenderBatch> ReadCarriedBodyRenderSource(std::int64_t now)noexcept;
// Disabled by default; original request/input/mesh lease is never renewed here.
void PublishWeaponVisibility(const WeaponVisibilityRequest&)noexcept;
void PublishOrdinaryEquipment(const std::optional<OrdinaryEquipmentRequest>&)noexcept;
std::optional<OrdinaryEquipmentPair> ReadOrdinaryEquipmentPair(const ReloadStateOwner&,std::uint64_t request,std::int64_t nowNs)noexcept;
std::optional<WeaponVisibilityReceipt> ReadWeaponVisibilityReceipt(const ReloadStateOwner&,std::uint64_t request,std::int64_t nowNs)noexcept;
void RetargetWeapon(unsigned animation,void* nativeWorld)noexcept;
BodyHolsterPackCounters ReadBodyHolsterPackCounters()noexcept;
MagazinePackCounters ReadMagazinePackCounters()noexcept;
std::optional<MagazineDetachPairReceipt> ReadMagazineDetachPair(const ReloadStateOwner&,std::int64_t nowNs)noexcept;
void Start()noexcept;
bool Stop()noexcept;
void Report(std::ostream&);
}
