#pragma once
#include "Bc2AmmoSupply.h"
#include "Bc2ReloadPresentation.h"
#include "Bc2SelectedMeshes1p.h"
#include "fvr/interaction/AmmoSupplyVisual.h"
#include <memory>
namespace fvr::bc2 {
struct BeltAmmoTracking {
    ReloadStateOwner owner{};
    interaction::AmmoSupplyVisualSample visual{};
    Bc2AmmoReserveLease reserve{};
    std::shared_ptr<const SelectedMeshesSnapshot> meshes;
    std::optional<ReloadRoundLease> heldCycle; // Exact original lease; no insertion/ammo authority.
};
bool SpasBeltAmmoFresh(const BeltAmmoTracking&,std::int64_t now)noexcept;
bool SpasBeltAmmoRetained(const BeltAmmoTracking&,const BeltAmmoTracking&,std::int64_t now)noexcept;
struct BeltAmmoPalette {
    BeltAmmoTracking source{};RigIdentity rig{};std::uint32_t shell=0;
    std::vector<std::array<std::byte,64>> ordinary,posed;
};
// Only the measured native SPAS idle shell leaf is reusable. The XM8 leaf is
// still needed by the rifle/held original/replacement. No extra instance exists.
inline constexpr bool IndependentBeltMagazineInstance=false;
namespace belt_ammo_detail {
// Structural test seam; production entry below additionally binds the exact
// captured SPAS rig, selected mesh, native owner and existing supply source.
std::optional<std::vector<std::array<std::byte,64>>> PlaceIdleShell(const RigSnapshot&,
    const Bc2ReloadPresentationBinding&,std::span<const std::array<std::byte,64>> ordinary,
    const math::Matrix4& centerWorld,float units);
}
std::optional<BeltAmmoPalette> BuildSpasBeltAmmoPalette(const BeltAmmoTracking&,const RigSnapshot&,
    std::span<const std::array<std::byte,64>> ordinary,const interaction::InputFrame&,
    const math::Matrix4& eyeBase,std::string_view asset,std::int64_t now);
struct BeltAmmoPaletteChoice {std::span<const std::array<std::byte,64>> bytes;bool fallback=true;};
BeltAmmoPaletteChoice SelectSpasBeltAmmoPalette(const BeltAmmoPalette&,const BeltAmmoTracking* current,
    std::int64_t now,bool coherentNativeSource)noexcept;
// Renderer supplies its CURRENT shell-control choice as fallback, not an old
// cached ordinary palette. A held-cycle overlay needs an actually retained hide.
BeltAmmoPaletteChoice SelectSpasBeltOverlay(const BeltAmmoPalette&,const BeltAmmoTracking* current,
    std::int64_t now,bool coherentNativeSource,std::span<const std::array<std::byte,64>> currentBase,
    bool currentCycleHidden)noexcept;
}
