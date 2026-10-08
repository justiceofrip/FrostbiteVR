#pragma once
#include "fvr/interaction/GroundPickup.h"
#include "fvr/interaction/TemporaryHeldFire.h"
namespace fvr::bc2 {
// Discovery only. Interact27 is not a targeted pickup/drop transaction. Current
// BodyNativeInventory describes carried items and cannot issue world leases.
// No live feature flag may enable this until every native seam has proof.
inline constexpr interaction::GroundPickupCapabilities GroundPickupCapabilities()noexcept{return {};}
static_assert(!GroundPickupCapabilities().Ready());
// Neither an external active ground weapon nor reversible native slot borrowing
// has a verified BC2 binding. Rendering a held prop never enables firing.
inline constexpr interaction::TemporaryFireCapabilities TemporaryHeldFireCapabilities()noexcept{return {};}
static_assert(!TemporaryHeldFireCapabilities().Ready(interaction::TemporaryNativeEquip::ExternalActiveEntity));
static_assert(!TemporaryHeldFireCapabilities().Ready(interaction::TemporaryNativeEquip::BorrowedNativeSlot));
}
