#pragma once
#include "Bc2ReloadRequestBridge.h"
#include "Bc2AmmoReserve.h"
#include "fvr/interaction/AmmoSupply.h"

namespace fvr::bc2 {
// Exact selected SPAS profile and fresh native reserve, independent of whether
// native ReloadHold is active. No offsets/read calls or inventory synthesis.
// Pool identity retains the exact native server item/equip generation, separate
// from the physical hand equip generation already retained by Bind owners.
std::optional<interaction::AmmoSupplySource> SpasAmmoSupplySource(
    const Bc2ReloadInteractionSample&,const Bc2AmmoReserveLease&)noexcept;
// Compatibility with already active native reload observations. Cycle is not
// required to construct a supply source; Reserve/bridge/submit still require it.
std::optional<interaction::AmmoSupplySource> SpasAmmoSupplySource(
    const Bc2ReloadInteractionSample&,const ReloadRoundLease&)noexcept;
bool SameAmmoReservation(const interaction::AmmoSupplyReservation&,const Bc2ReloadReservedItem&)noexcept;
// Only a successfully consumed bridge result plus its exact verified native
// completion evidence can produce an Applied receipt. Cancelled/timeout/submit
// results never clear a supply reservation. Caller passes the ORIGINAL mapping
// retained when Begin succeeded, even if the physical tracking epoch changed.
std::optional<interaction::AmmoSupplyReceipt> SpasAmmoSupplyReceipt(
    const interaction::AmmoSupplyReservation&,const Bc2ReloadOwnerMap& originalOwners,
    const Bc2ReloadBridgeResult&,const Bc2ReloadAckEvidence&,const ReloadRoundLease& currentNative,
    const Bc2AmmoReserveLease& currentReserve,std::int64_t nowNs)noexcept;
// Legacy same-sequence-domain convenience for an already active round observer.
std::optional<interaction::AmmoSupplyReceipt> SpasAmmoSupplyReceipt(
    const interaction::AmmoSupplyReservation&,const Bc2ReloadOwnerMap& originalOwners,
    const Bc2ReloadBridgeResult&,const Bc2ReloadAckEvidence&,const ReloadRoundLease& currentNative,
    std::int64_t nowNs)noexcept;
} // namespace fvr::bc2
