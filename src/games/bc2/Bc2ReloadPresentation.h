#pragma once
#include "Bc2ReloadInteraction.h"
#include "Bc2Rig.h"
namespace fvr::bc2 {
// Shared captured shell origin; the belt and held preview use the same prop center.
std::optional<math::Matrix4> SpasShellBoneAtCenter(const math::Matrix4& centerWorld,float units)noexcept;
struct Bc2ReloadPresentationBinding {
    std::uint64_t fingerprint=0;
    std::uint32_t weapon=0,shell=0,wrist=0;
    std::array<std::uint32_t,15> fingers{};
};
namespace bc2_reload_detail {
// Structural derivation is separately testable with synthetic rigs. Production
// must enter through BindBc2ReloadPresentation's exact captured-rig gate.
std::optional<Bc2ReloadPresentationBinding> DerivePresentationBinding(const RigSnapshot&);
bool KnownHiddenShell(const RigSnapshot&,const Bc2ReloadPresentationBinding&)noexcept;
// A private-copy exception for exactly the bound shell. Never changes native
// bytes or the generic planner; every other hidden leaf remains excluded.
std::optional<RigPosePlan> BuildShellVisibilityPalette(const RigSnapshot&,
    const Bc2ReloadPresentationBinding&,std::span<const interaction::BoneWrite>,
    std::optional<std::uint32_t> ownedShell);
}
std::optional<Bc2ReloadPresentationBinding> BindBc2ReloadPresentation(
    const RigSnapshot&,std::string_view asset,std::string_view selectedMesh);
struct Bc2ReloadPresentationObservation {
    bool enabled=false,selectedMeshIdentityVerified=false,sectionOwnershipVerified=false,shellSectionVisible=false;
    std::string_view assetName,meshPath;
    interaction::ReloadInsertionIdentity identity{};
    std::uint64_t inputSequence=0,nativeCycle=0;
    std::int64_t nowNs=0;
    float unitsPerMetre=1;
    math::Matrix4 placedWeaponWorld{}; // Canonical native unit scale, same rig publication.
    // Explicit carried-ammo path. Caller validates a genuine idle reserve lease;
    // zero cycle authorizes visual placement only, never insertion/completion.
    bool carried=false;
    // Set only by the exact-owner AmmoObject preview adapter after its real
    // mesh/rig/claim/native-lease checks. Default callers retain hidden rejection.
    bool allowOwnedShellVisibility=false;
};
enum class Bc2ReloadPresentationReason : std::uint8_t {
    None,Disabled,UnsupportedAsset,StaleOwnership,InvalidBinding,VisibilityUnverified,
    NativeShellHidden,InvalidGeometry
};
struct Bc2ReloadPresentationPlan {
    Bc2ReloadPresentationReason reason=Bc2ReloadPresentationReason::None;
    std::vector<interaction::BoneWrite> writes;
    // Exact native bytes and palette identity only. No game memory is written.
    std::optional<RigPosePlan> palette;
    std::optional<std::uint32_t> ownedShellVisibility;
};
// Places the actual jntWpn_7 shell plus captured row58 hand shape independently
// of the weapon root. Root must solve the left arm to the SAME wrist target and
// compose these writes once into its private immutable palette. Only a verified
// owned ammo preview may replace the bound shell's native collapsed pose in that
// private copy. No native source or unrelated hidden leaf can be changed.
Bc2ReloadPresentationPlan BuildBc2ReloadPresentation(const RigSnapshot&,
    const Bc2ReloadPresentationBinding&,const Bc2ReloadPresentationObservation&,
    const Bc2ReloadTargets&);
}
