#pragma once
#include "Bc2MagazineInteraction.h"
#include "Bc2ReloadPreview.h"
#include "Bc2MagazineDetachEvidence.h"
#include "Bc2MagazineResourceAdapter.h"
namespace fvr::bc2 {
// Free replacement carry was measured relative to this exact sampled weapon.
// Reusing its local pose with a newer weapon would move the loading hand/prop.
// This is immutable presentation evidence, not a new native/input lease.
struct MagazineCarryFrame {
 interaction::HandInteractionOwner owner{};interaction::HandInteractionKey item{};
 interaction::HandClaimToken handClaim{},gunClaim{};
 std::uint64_t inputSequence=0,cycle=0;
 std::int64_t observedNs=0,deadlineNs=0;
 math::Matrix4 weaponWorldMeters{};
};
using MagazineReplacementFrame=MagazineCarryFrame;
struct MagazineTracking {
 MagazineFamilyEvidence family{};
 bool enabled=false;ReloadStateOwner owner{};interaction::HandInteractionSample inputEvidence{};
 std::shared_ptr<const SelectedMeshesSnapshot> selected;
 Bc2AmmoReserveLease reserve{};std::uint64_t cycle=0;
 std::optional<interaction::MagazinePropTarget> target;
 std::shared_ptr<const MagazineDetachAuthorization> detach;
 std::optional<MagazineReplacementFrame> replacementFrame;
 std::optional<MagazineCarryFrame> removalFrame;
 // Visual-only reuse of a previously committed Removed pose. Original
 // native/input/target leases stay unchanged; this current suppression may
 // neither generate contact nor acknowledge a return.
 std::optional<HolsterSuppressionReceipt> retainedVisualSuppression;
 // Resource-backed gameplay carries exact ammunition custody separately from
 // the legacy reload/animation leases. Null preserves the legacy consumer.
 std::shared_ptr<const MagazineResourcePresentation> resource;
};
bool MagazineTrackingFresh(const MagazineTracking&,std::int64_t now)noexcept;
bool MagazineTargetFresh(const MagazineTracking&,std::int64_t now)noexcept;
// Visual-only admission. Never use for raw contact, supply, or native work.
bool MagazinePaletteTargetFresh(const MagazineTracking&,std::int64_t now)noexcept;
bool MagazineTargetRetained(const MagazineTracking& original,const MagazineTracking& current,std::int64_t now)noexcept;
// Diagnostic only. A reason never grants contact or extends a source lifetime.
// Publication/getter refusals share this bounded vocabulary so a failed builder
// packet is not silently replaced with an unexplained empty contact.
enum class MagazineRawContactReason:unsigned {
 NotProduced,Accepted,HandRoleUnavailable,RetainedVisualSuppression,TrackingNotFresh,
 AssetMismatch,RigBindingUnavailable,RigFingerprintMismatch,RigOwnerMismatch,
 InvalidUnits,WristNotRigid,WeaponNotRigid,MagazineHidden,MagazineNotRigid,
 MagazineInverseUnavailable,ChildHidden,ChildNotRigid,ChildPositionMismatch,ChildAngleMismatch,
 GuardMissing,GuardOwnerMismatch,GuardNotFresh,FrameUnavailable,ContactOwnerMismatch,
 FrameInputMismatch,InputNewerThanGuard,InputInFuture,InputExpired,GuardChanged,Count
};
inline constexpr std::array<const char*,unsigned(MagazineRawContactReason::Count)> MagazineRawContactReasonNames{
 "not_produced","accepted","hand_role_unavailable","retained_visual_suppression","tracking_not_fresh",
 "asset_mismatch","rig_binding_unavailable","rig_fingerprint_mismatch","rig_owner_mismatch",
 "invalid_units","wrist_not_rigid","weapon_not_rigid","magazine_hidden","magazine_not_rigid",
 "magazine_inverse_unavailable","child_hidden","child_not_rigid","child_position_mismatch","child_angle_mismatch",
 "guard_missing","guard_owner_mismatch","guard_not_fresh","frame_unavailable","contact_owner_mismatch",
 "frame_input_mismatch","input_newer_than_guard","input_in_future","input_expired","guard_changed"};
struct MagazineRawContactEvidence {
 MagazineRawContactReason reason=MagazineRawContactReason::NotProduced;
 std::uint32_t magazineBone=~0u,childBone=~0u;
 // Only calculations already required by the rigid-assembly gate are retained.
 std::optional<float> relativeDistanceMeters,relativeAngleRadians;
};
struct MagazineRawContact:ReloadRawContact {
 bool nativeMagazineAttached=false;
 // Reachable measured support contact, using this packet's anatomical wrist.
 std::optional<float> supportContactDistanceMeters;
 MagazineRawContactEvidence evidence{};
};
inline MagazineRawContact RejectedMagazineRawContact(MagazineRawContactReason reason)noexcept {
 MagazineRawContact out;out.evidence.reason=reason;return out;
}
// Free carry only: retain the basis of the exact raw packet that produced the target.
std::optional<MagazineCarryFrame> MagazineRemovalFrame(const MagazineTracking&,const MagazineRawContact&)noexcept;
MagazineRawContact BuildMagazineRawContact(const MagazineTracking&,const RigSnapshot&,std::string_view asset,
 const math::Matrix4& rawLeftWrist,const math::Matrix4& placedWeapon,float unitsPerMetre,std::int64_t now,
 const math::Matrix4* trackingBody=nullptr);
// Contact admission after the immutable rig frame has passed its native owner,
// equipment and selected-mesh checks. The frame and raw input keep their own
// original deadlines; a newer tracking guard cannot renew either one.
MagazineRawContact AdmitMagazineRawContact(const MagazineTracking* guard,const ReloadStateOwner&,
 const MagazineRawContact* frame,std::uint64_t frameInput,std::int64_t frameDeadlineNs,std::int64_t now)noexcept;
struct MagazinePresentationBinding {
 std::uint64_t fingerprint=0;std::uint32_t weapon=0,magazine=0,wrist=0;
 std::array<std::uint32_t,15> fingers{};
 std::array<std::uint32_t,MagazineAssemblyLimit> assembly{};unsigned assemblyCount=0;
 // An exact-bind co-located descendant inherits an explicitly authored
 // member's transform. It still receives the full current-frame rigidity and
 // visibility checks, and participates in every pose/hide write.
 std::array<unsigned,MagazineAssemblyLimit> assemblyProfileMember{};
 const MagazineGeometryProfile* geometry=nullptr; // Immutable profile used to derive these exact roles.
 bool operator==(const MagazinePresentationBinding&)const=default;
};
namespace magazine_presentation_detail {
 // Structural fixture helper; production additionally requires captured hash.
 std::optional<MagazinePresentationBinding> Derive(const RigSnapshot&);
 // Explicit geometry variant for structural/offline calibration. It grants no native authority.
 std::optional<MagazinePresentationBinding> Derive(const RigSnapshot&,const MagazineGeometryProfile&,bool completeCoincidentDescendants=false);
}
std::optional<MagazinePresentationBinding> BindMagazinePresentation(const RigSnapshot&,std::string_view asset);
std::optional<MagazinePresentationBinding> BindMagazinePresentation(const RigSnapshot&,const MagazineEquipmentProfile&);
struct MagazinePresentationPlan {
 std::optional<MagazinePresentationBinding> binding;
 std::vector<interaction::BoneWrite> writes;
 std::optional<math::Matrix4> wristTarget;
 bool hideMagazine=false;
};
// Root composes these writes with torso/weapon/right-hand, solving the left arm
// to EXACT wristTarget before the final immutable BuildRigPosePlan. No native
// animation write, hidden-bone exception, or ammunition authority is granted.
MagazinePresentationPlan BuildMagazinePresentation(const RigSnapshot&,const MagazinePresentationBinding&,
 const MagazineTracking&,const math::Matrix4& placedWeapon,float unitsPerMetre,std::int64_t now);
std::optional<std::vector<std::array<std::byte,64>>> HideMagazinePackedPalette(
 const RigSnapshot&,const MagazinePresentationBinding&,std::span<const std::array<std::byte,64>> ordinary);
// BEFORE EACH native Pack: original target/mesh/owner/cycle/claim deadlines must
// remain valid. Resource-backed poses retain their exact native admission time,
// separately from these pose lifetimes. coherentShotGuard proves exact source.
ReloadPackedPaletteChoice SelectMagazinePackedPalette(const MagazineTracking& original,const MagazineTracking* current,
 std::int64_t now,std::span<const std::array<std::byte,64>> posed,
 std::span<const std::array<std::byte,64>> ordinary,bool coherentShotGuard)noexcept;
// Diagnostic only; never participates in palette selection or native authority.
enum class MagazineFallbackReason:unsigned {
 SourceReadFailed,SourceChanged,CurrentMissing,ShotIncoherent,OldExpired,
 CurrentExpired,RetentionMismatch,InvalidPalette,ShotMissing,Count
};
inline constexpr std::uint64_t MagazineFallbackBit(MagazineFallbackReason r)noexcept{return 1ull<<unsigned(r);}
inline constexpr std::array<const char*,unsigned(MagazineFallbackReason::Count)> MagazineFallbackNames{
 "source_read_failed","source_changed","current_missing","shot_incoherent","old_expired",
 "current_expired","retention_mismatch","invalid_palette","shot_missing"};
std::uint64_t ClassifyMagazinePaletteFallback(const MagazineTracking&,const MagazineTracking*,
 std::int64_t,std::span<const std::array<std::byte,64>>,bool coherentShotGuard)noexcept;
} // namespace fvr::bc2
