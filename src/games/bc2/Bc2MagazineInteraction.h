#pragma once
#include "Bc2AmmoReserve.h"
#include "Bc2MagazineEquipmentProfile.h"
#include "Bc2SelectedCarriedWeapon.h"
#include "Bc2MagazineReloadCycle.h"
#include "Bc2SelectedMeshes1p.h"
#include "Bc2WeaponMode.h"
#include "fvr/interaction/DetachableMagazine.h"
#include <memory>
namespace fvr::bc2 {
interaction::DetachableMagazineConfig Xm8MagazineConfig()noexcept;
bool Xm8MagazineSelected(const SelectedMeshesSnapshot&,const ReloadStateOwner&,std::string_view,std::int64_t)noexcept;
// Physical GunHold may own the exact native carried item, or a separately
// verified PersistentWeapon alias. Choose from actual hand identity/evidence;
// optional launcher selection must not be a prerequisite for ordinary reload.
struct MagazineFamilyBinding {
 ReloadStateOwner owner{};interaction::HandInteractionKey weapon{};
 std::uint32_t inventory=0,launcher=0,launcherSlot=0;
 const MagazineEquipmentProfile* profile=&Xm8MagazineEquipment();
 WeaponEquipmentIdentity equipment{}; // Exact direct-item identity; empty only for the verified unequal alias route.
 bool operator==(const MagazineFamilyBinding&)const=default;
};
struct MagazineFamilyEvidence {
 MagazineFamilyBinding binding{};std::int64_t observedNs=0,deadlineNs=0;bool verified=false;
 std::optional<SelectedCarriedWeaponEvidence> carried;
};
std::optional<MagazineFamilyEvidence> ResolveMagazineFamily(const WeaponModeMemory&,
 const ReloadStateOwner&,const interaction::HandInteractionSample&,interaction::HandInteractionKey);
std::optional<MagazineFamilyEvidence> ResolveMagazineFamily(const WeaponModeMemory&,
 const ReloadStateOwner&,const interaction::HandInteractionSample&,interaction::HandInteractionKey,const MagazineEquipmentProfile&);
bool MagazineSelected(const SelectedMeshesSnapshot&,const ReloadStateOwner&,std::string_view,
 const MagazineEquipmentProfile&,std::int64_t)noexcept;
bool MagazineFamilyFresh(const MagazineFamilyEvidence&,const ReloadStateOwner&,
 const interaction::HandInteractionOwner&,interaction::HandInteractionKey,std::int64_t now)noexcept;
struct Bc2MagazineOwnerMap: Bc2ReloadOwnerMap {
 MagazineFamilyEvidence family{};
 // Observation clocks advance independently of stable cycle ownership.
 bool operator==(const Bc2MagazineOwnerMap& b)const noexcept {
  return static_cast<const Bc2ReloadOwnerMap&>(*this)==static_cast<const Bc2ReloadOwnerMap&>(b)&&family.binding==b.family.binding;
 }
};
interaction::ManualReloadOwner MagazinePhysicalOwner(const Bc2ReloadOwnerMap&)noexcept;
interaction::ManualReloadOwner MagazineNativeOwner(const Bc2ReloadOwnerMap&)noexcept;
std::optional<Bc2MagazineOwnerMap> BindMagazineOwners(const interaction::HandInteractionOwner&,
    interaction::HandInteractionKey,const Bc2AmmoReserveLease&,std::uint64_t cycle,std::int64_t now,const MagazineFamilyEvidence&)noexcept;
std::optional<interaction::AmmoSupplySource> MagazineSupply(const Bc2MagazineOwnerMap&,
    const Bc2AmmoReserveLease&,std::uint64_t trackingEpoch,std::int64_t now,unsigned pendingUnits=0)noexcept;
// Render-only availability: a full loaded weapon still has reserve ammunition.
// Never use this in reservation, native Start, or refill acknowledgement paths.
std::optional<interaction::AmmoSupplySource> MagazineBodySupply(const Bc2MagazineOwnerMap&,
 const Bc2AmmoReserveLease&,std::uint64_t,std::int64_t)noexcept;
std::optional<interaction::AmmoSupplySource> Xm8MagazineSupply(const Bc2MagazineOwnerMap&,
    const Bc2AmmoReserveLease&,std::uint64_t trackingEpoch,std::int64_t now,unsigned pendingUnits=0)noexcept;
std::optional<interaction::MagazineNativeObservation> MagazineGateObservation(const Bc2MagazineOwnerMap&,
    const ReloadMagazineGateAcknowledgement&,const interaction::ManualReloadRequest& physical,std::int64_t now)noexcept;
std::optional<ReloadMagazineNativeRequest> MagazineSeatRequest(const Bc2MagazineOwnerMap&,
    const ReloadMagazineLease&,const interaction::AmmoSupplyReservation&,
    const interaction::ManualReloadRequest& physical,std::int64_t now)noexcept;
std::optional<interaction::AmmoSupplyReceipt> MagazineSupplyReceipt(const Bc2MagazineOwnerMap&,
    const interaction::AmmoSupplyReservation&,const ReloadMagazineLease& before,
    const ReloadMagazineAckEvidence&,const Bc2AmmoReserveLease& current,std::int64_t now)noexcept;
} // namespace fvr::bc2
