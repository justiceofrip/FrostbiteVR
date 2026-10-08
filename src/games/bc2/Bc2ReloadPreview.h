#pragma once
#include "Bc2AmmoReserve.h"
#include "Bc2BeltAmmo.h"
#include "Bc2ReloadPalette.h"
#include "Bc2ReloadRound.h"
#include "Bc2SelectedMeshes1p.h"
#include <memory>
namespace fvr::bc2 {
// Carried has no native insertion authority and no cycle. Guided/Pending retain
// the actual held-cycle lease; a caller may never invent a cycle for visibility.
enum class ReloadPreviewPhase:std::uint8_t {Carried,Guided,Pending};
struct ReloadPreview {
    ReloadPreviewPhase phase=ReloadPreviewPhase::Carried;
    std::shared_ptr<const SelectedMeshesSnapshot> selectedMeshes;
    Bc2AmmoReserveLease reserve{};
    ReloadRoundLease native{};
    interaction::HandClaim shellClaim{},weaponClaim{};
    Bc2ReloadTargets targets{}; // Used only by Guided/Pending.
};
// Visibility authority for a genuinely started/kept manual native cycle.
// Independent of shell claims and the allThreeHeld insertion gate: pending
// native advance may animate an unowned prop while no carried preview exists.
struct ReloadShellControl {
    std::uint64_t cycle=0;
    Bc2AmmoReserveLease reserve{};
    interaction::HandClaim weaponClaim{};
    std::shared_ptr<const SelectedMeshesSnapshot> selectedMeshes;
};
struct ReloadTracking {
    bool enabled=false;
    ReloadStateOwner owner{};
    // Original immutable input evidence, including original observed/deadline.
    interaction::HandInteractionSample inputEvidence{};
    std::shared_ptr<const ReloadPreview> preview;
    std::shared_ptr<const ReloadShellControl> shellControl;
    std::shared_ptr<const BeltAmmoTracking> belt; // Reserve visual; optional held-cycle evidence, no shell claim or ammo authority.
};
struct ReloadRawContact {
    bool valid=false,nativeShellVisible=false;
    ReloadStateOwner owner{};
    RigIdentity rig{};
    std::uint64_t rigFingerprint=0;
    interaction::HandInteractionSample inputEvidence{};
    // Canonical metres; sampled together before any hand IK/preview. Do not
    // replace these with a guided pose or restamp them with a later generation.
    math::Matrix4 rawLeftWristWorldMeters{},weaponWorldMeters{};
    // Same-publication reference body, diagnostic mapping evidence only.
    math::Matrix4 trackingBodyWorldMeters{};
};
bool ReloadTrackingFresh(const ReloadTracking&,std::int64_t nowNs)noexcept;
bool ReloadPreviewFresh(const ReloadPreview&,const ReloadTracking&,std::int64_t nowNs)noexcept;
// Atomic publication uses this to fall back immediately after claim loss,
// native-cycle/phase/owner change or expiry, even when the pose mutex was busy.
bool ReloadPreviewRetained(const ReloadPreview& published,const ReloadTracking& current,std::int64_t nowNs)noexcept;
struct ReloadPackedPaletteChoice {
    std::span<const std::array<std::byte,64>> bytes;
    bool fallback=false;
};
// Final immutable Pack selection. Coherence comes from actual shot/getter
// identity checks. Invalid/missing base returns empty, requiring native source.
ReloadPackedPaletteChoice SelectReloadPackedPalette(const ReloadPreview&,const ReloadTracking*,
    std::int64_t nowNs,std::int64_t originalTargetDeadline,
    std::span<const std::array<std::byte,64>> posed,std::span<const std::array<std::byte,64>> base,
    bool coherentShotGuard)noexcept;
// Private suppression of the exact SPAS shell leaf during an active manual
// cycle WITHOUT a displayed physical item. Never changes animation or ammo.
struct ReloadShellHidePlan {
    RigIdentity rig{};
    ReloadTracking source{};
    std::uint32_t shell=0;
    std::vector<std::array<std::byte,64>> ordinary,hidden;
};
bool ReloadShellControlFresh(const ReloadTracking&,std::int64_t nowNs)noexcept;
bool ReloadBeltCompatible(const ReloadTracking&,std::int64_t nowNs)noexcept;
bool ReloadShellControlRetained(const ReloadTracking& source,const ReloadTracking& current,std::int64_t nowNs)noexcept;
std::optional<ReloadShellHidePlan> BuildReloadShellHidePalette(const ReloadTracking&,const RigSnapshot&,
    std::span<const std::array<std::byte,64>> ordinary,std::string_view asset,std::int64_t nowNs);
// Call before EACH actual native Pack, so cancellation between eyes restores
// ordinary bytes immediately and invalidates paired-hide evidence.
ReloadPackedPaletteChoice SelectReloadShellPackedPalette(const ReloadShellHidePlan&,const ReloadTracking* current,
    std::int64_t nowNs,bool coherentShotGuard)noexcept;
ReloadRawContact BuildReloadRawContact(const ReloadTracking&,const RigSnapshot&,
    std::string_view asset,const math::Matrix4& rawLeftWrist,const math::Matrix4& placedWeapon,
    float unitsPerMetre,std::int64_t nowNs,const math::Matrix4* trackingBody=nullptr);
std::optional<Bc2ReloadTargets> ResolveReloadPreviewTargets(const ReloadPreview&,
    const ReloadTracking&,const ReloadRawContact&,std::int64_t nowNs)noexcept;
// Evidence-backed SPAS static skin binding, not a generic selected-mesh bool.
// Every call still checks the current exact asset, named rig and claims/native
// lease. Only its exact owned shell may receive the visibility exception;
// final Pack checks exact current native source bytes and current ownership.
Bc2ReloadPalettePlan BuildReloadPreviewPalette(const ReloadPreview&,const ReloadTracking&,
    const ReloadRawContact&,const RigSnapshot&,const Bc2ReloadPaletteSample&,
    std::string_view asset,std::int64_t nowNs);
}
