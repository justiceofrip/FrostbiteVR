#pragma once
#include "Bc2Profile.h"
#ifdef FVR_BC2_CONTEXTUAL_TOOLS
#include "Bc2ContextualToolSource.h"
#endif
#include "Bc2PhysicalPump.h"
#include "Bc2BoltRuntimeGeometry.h"
#include "Bc2FixedContainerPresentation.h"
#include "Bc2EquipmentIdentity.h"
#include "Bc2MuzzleAdmission.h"
#include "Bc2ReloadProducerBinding.h"
#include "Bc2WeaponProfiles.h"
#include "Bc2SightContact.h"
#include "Bc2SightAdapter.h"
#include "Bc2ReloadPreview.h"
#include "Bc2MagazinePresentation.h"
#include "Bc2BodyAmmoRenderer.h"
#include "Bc2StowedAmmo.h"
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
#ifdef FVR_BC2_CHEST_KNIFE_DRAW_RETURN
#include "Bc2ChestKnifeRendering.h"
#include "Bc2SelectedKnifeRendering.h"
#endif
namespace fvr::bc2 {struct Bc2PhysicalFeedPresentation;struct Bc2PhysicalPistolSlidePresentation;}
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
// Subtree contacts retain the selected configuration from their original input.
// A newer observation may confirm it, but cannot supply or renew that source.
struct SightSelectionEvidence {
    const SightAdapterProfile* adapter=nullptr;
    std::shared_ptr<const SelectedMeshesSnapshot> selected;
    WeaponEquipmentIdentity equipment{};
    AuthoredGripOwner owner{};
    std::uint64_t rigFingerprint=0;
};
inline bool SightSelectionCurrent(const SightSelectionEvidence& evidence,const SelectedMeshesSnapshot* current,
    const WeaponEquipmentIdentity& equipment,const AuthoredGripOwner& owner,std::int64_t now)noexcept {
    if(!evidence.adapter||evidence.adapter->assembly!=SightAssemblyKind::Subtree||!current||
       !SightAdapterSelectedMesh(*evidence.adapter,evidence.selected.get(),evidence.equipment,
           evidence.owner,evidence.rigFingerprint,now)||
       evidence.equipment!=equipment||evidence.owner.soldier!=owner.soldier||evidence.owner.weak!=owner.weak||
       evidence.owner.actorGeneration!=owner.actorGeneration||evidence.owner.equipmentGeneration!=owner.equipmentGeneration||
       evidence.owner.space!=owner.space||evidence.owner.inputSequence>owner.inputSequence||owner.inputDeadlineNs<=now)return false;
    const auto& original=*evidence.selected;
    // Validate current proof separately from source admission: its sequence may
    // be newer than the captured input while the exact configuration is unchanged.
    return authored_grip_detail::Fresh(*current,now)&&current->owner==original.owner&&
        current->sequence>=original.sequence&&current->observedNs>=original.observedNs&&
        current->weaponData==original.weaponData&&current->inventory==original.inventory&&
        current->selectedSlot==original.selectedSlot&&current->stateTypeInfo==original.stateTypeInfo&&
        current->meshTypeInfo==original.meshTypeInfo&&current->weaponName==original.weaponName&&
        current->stateCount==original.stateCount&&current->soleConfiguredArray==original.soleConfiguredArray&&
        current->states==original.states&&current->configurationPathVerified&&
        current->configurationPathPointer==original.configurationPathPointer&&
        current->configurationPath==original.configurationPath;
}
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
    Bc2PumpTracking pump{};
    Bc2BoltTracking bolt{};
    // Same immutable Gameplay packet; never substitute a later global feed pose.
    // Family keys resolve immutable rows; render admission separately checks
    // the actual selected configuration, native equipment and measured rig.
    std::shared_ptr<const Bc2PhysicalFeedPresentation> feed;
    std::shared_ptr<const Bc2PhysicalPistolSlidePresentation> pistolSlide;
    Bc2FixedContainerTracking fixedContainer{};
    std::optional<BodyAmmoTracking> fixedBodyAmmo;
    std::optional<BodyAmmoTracking> feedBodyAmmo;
    interaction::InputFrame boltRawInput{}; // Before support-orientation guidance; same original generation.
    std::optional<interaction::AmmoSupplyVisualSample> bodyMagazine;
    ReloadTracking reload{}; // Explicit mode; defaults off, immutable preview.
    bool weaponActionsBlocked=false;
    std::shared_ptr<const BodyFreeRightEvidence> freeRight; // Actual released claim + native suppression + accepted hide.
    std::uint64_t equipmentGeneration=0;
    WeaponEquipmentIdentity nativeEquipment{};
    // Original selected configuration captured with this input packet. A newer
    // global observation confirms lifetime at output; it cannot replace this
    // source while an older immutable packet is being posed.
    std::shared_ptr<const SelectedMeshesSnapshot> selectedMeshes;
    std::optional<interaction::BodySlotAssignment> bodyHolsterSlot;
    interaction::BodyAnchorConfig bodyAnchors{};
    std::shared_ptr<const BodyInventoryDisplay> bodyInventoryDisplay;
    std::array<std::shared_ptr<const CarriedMeshesSnapshot>,8> bodyCarriedMeshes{};
    Bc2AmmoReserveLease stowedAmmoReserve{}; // Read-only availability; no synthetic GunHold.
    const MagazineEquipmentProfile* stowedAmmoMagazine=nullptr;
    std::optional<interaction::AmmoSupplyContact> stowedAmmoContact;
    // Same original packet for support-only diagnostics, independent of ammo.
    ReloadStateOwner controllerOwner{};
    interaction::HandInteractionSample controllerEvidence{};
#ifdef FVR_BC2_CHEST_KNIFE_DRAW_RETURN
    std::optional<ChestKnifeRenderSource> chestKnife;
    std::optional<SelectedKnifeRenderSource> selectedKnife;
#endif

};
// The native publisher supplies its QPC deadline converted to nanoseconds.
// This predicate is shared with offline rig/contact integration; it never
// manufactures tracking readiness or extends native/input/presentation leases.
inline bool MagazineTrackingAdmitted(const Tracking& next,std::int64_t inputDeadlineNs,std::int64_t now)noexcept {
 const auto& owner=next.magazine.owner;
 return interaction::ValidInput(next.input)&&next.input.focused&&next.input.headValid&&
  next.input.hands[0].gripTracked&&next.input.hands[1].gripTracked&&next.input.hands[1].aimTracked&&
  owner.soldier==next.soldier&&owner.weak==next.weak&&owner.weapon==next.weapon&&
  owner.actorGeneration==next.ownerGeneration&&owner.space==next.input.spaceGeneration&&
  next.magazine.inputEvidence.sequence==next.input.generation&&
  next.magazine.inputEvidence.deadlineNs<=inputDeadlineNs&&
  (next.magazine.target?MagazinePaletteTargetFresh(next.magazine,now):MagazineTrackingFresh(next.magazine,now));
}
// Read-only shot candidate from the same immutable pose as the rendered gun.
// flashBone is name/topology validated, but firing semantics still need native
// acceptance before a consumer can use this to modify a projectile.
struct NativeSightState {
 bool valid=false;
 math::Matrix4 rear{},front{}; // Raw native weapon-local metres; NEVER contact proof.
 math::Matrix4 rawHand{};bool rawHandValid=false;unsigned physicalItem=0; // Pre-IK, in placed weapon space.
};
// Bounded observation of the exact immutable rendered frame. String views refer
// only to the compiled registry. These fields grant no native firing authority.
struct ShotAimEvidence {
 WeaponEquipmentIdentity equipment{};
 math::Pose reference{},effectiveGrip{},effectiveAim{};
 // Prepared input before support guidance; diagnostics may synthesize it.
 // Presence proves the same source packet, not physical headset ownership.
 math::Pose preSupportReference{},preSupportGrip{},preSupportAim{};
 std::uint64_t preSupportGeneration=0,preSupportSpace=0;
 math::Matrix4 body{};float bodyYaw=0;
 std::array<char,512> configurationPath{};bool configurationPathVerified=false;
 std::uint64_t configurationSequence=0;std::int64_t configurationDeadlineNs=0;
 std::string_view meshPath{},bindingDigest{},axisEvidence{};
 std::uint64_t rigFingerprint=0;
 math::Vec3 modelForward{},modelUp{};
 bool authoredBinding=false,authoredAxes=false,preSupportInputAvailable=false;
 // Exact configured mesh and rig identity, independent of primary-hand/aim authority.
 bool selectedRigVerified=false;
};
// Current-input body base using the original bounded native eye/stance source.
// Does not reuse or restamp the previous published tracked body frame.
std::optional<math::Matrix4> BuildCurrentEyeBase(const Tracking&)noexcept;
struct WeaponShotFrame {
 math::Matrix4 nativeWeapon{},trackedWeapon{},nativeFlash{},trackedFlash{};
 std::uint64_t generation=0;std::int64_t deadline=0;unsigned flashBone=0;
 float unitsPerMetre=1;std::uint64_t ownerGeneration=0,space=0;
 interaction::SupportGripContact support{};
 WeaponSightContact sight{};
 NativeSightState nativeSight{}; // Same raw item/owner/space/generation/deadline as this frame.
 MagazineRawContact magazine{};
 Bc2PumpRawContact pump{};
 Bc2BoltControllerContact bolt{};
 Bc2FixedContainerRawContact fixedContainer{};
 ReloadRawContact reload{}; // Original pre-IK input generation/times and rig identity.
 // Returned only through the publication's original configured-mesh/input lease.
 // Experimental authored contact, never a native aim/muzzle/holster capability.
 bool authoredSupportReference=false;
 // Observation-only controller mapping for input diagnostics. Original source
 // input/owner/pose lifetime; this cannot grant support or native authority.
 ReloadRawContact controllerMapping{};
 math::Matrix4 targetWristWorldMeters{};
 // Independent pre-IK anatomical wrist used by free/mechanism hand poses.
 // Keeps the original owner, rig and input lease; never support-calibrated.
 ReloadRawContact mechanismControllerMapping{};
#ifdef FVR_BC2_CHEST_KNIFE_DRAW_RETURN
 std::optional<ChestKnifeRenderReceipt> chestKnife;
 std::optional<SelectedKnifeRenderReceipt> selectedKnife;
#endif
 std::optional<ShotAimEvidence> aimEvidence;
 // Separate explicit write enrollment; observation metadata grants no authority.
 std::optional<MuzzleBinding> muzzleBinding;
};
std::optional<WeaponShotFrame> ReadWeaponShotFrame(unsigned soldier,unsigned weak,unsigned weapon)noexcept;
// Contact-only capability: pending native ammo transfer can block firing while
// a free hand regrips the fore-end. Contains no muzzle or native sight data.
struct WeaponSupportFrame {
 interaction::SupportGripContact support{};
 std::uint64_t generation=0;std::int64_t deadline=0;
 std::uint64_t ownerGeneration=0,space=0;
 bool authoredSupportReference=false;
 ReloadRawContact controllerMapping{};
 math::Matrix4 targetWristWorldMeters{};
};
std::optional<WeaponSupportFrame> ReadWeaponSupportFrame(unsigned soldier,unsigned weak,unsigned weapon)noexcept;
// Body transaction ownership observation only; it may describe hidden geometry.
// Never use this getter for firing, support, or EmptyHands acknowledgement.
std::optional<WeaponShotFrame> ReadBodyWeaponFrame(unsigned soldier,unsigned weak,unsigned weapon)noexcept;
#ifdef FVR_BC2_CONTEXTUAL_TOOLS
// Settled tracked wrist/weapon observation; independent of muzzle names.
std::optional<ContextualToolAttachment> ReadContextualToolAttachment(const ReloadStateOwner&)noexcept;
#endif
ReloadRawContact ReadReloadContact(const ReloadStateOwner&)noexcept;
Bc2PumpRawContact ReadPumpContact(const ReloadStateOwner&)noexcept;
Bc2BoltControllerContact ReadBoltContact(const ReloadStateOwner&)noexcept;
Bc2BoltPackCounters ReadBoltPackCounters()noexcept;
Bc2PumpPackCounters ReadPumpPackCounters()noexcept;
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
std::optional<FixedContainerRenderSource> ReadFixedContainerRenderSource(std::int64_t now)noexcept;
std::optional<BodyHolsteredRenderSource> ReadHolsteredBodyRenderSource(std::int64_t now)noexcept;
std::optional<StowedAmmoRenderSource> ReadStowedAmmoRenderSource(std::int64_t now)noexcept;
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
// Finite M95 diagnostic:1 preflight,2 fire,3 released,4 done,5 failed. Zero disabled.
void PublishM95ShotCapturePhase(unsigned)noexcept;
void Start()noexcept;
bool Stop()noexcept;
// Independent scalar atomic counters only; not a coherent pose/pair snapshot.
void ReportLiveCounters(std::ostream&);
void Report(std::ostream&);
}
