#pragma once
#include "fvr/engine/PeImage.h"
#include "Bc2WeaponModeFamilies.h"
#include <cstdint>
#include <optional>
namespace fvr::bc2 {
struct WeaponModeMemory {
    void* context=nullptr;
    bool (*read)(void*,std::uint32_t,void*,std::size_t)=nullptr;
    bool (*type)(void*,std::uint32_t,const char*)=nullptr;
};
struct WeaponModeCommand {
    std::uint32_t action=0,targetWeapon=0,targetSlot=0,inventory=0,persistent=0;
    const WeaponModeFamilyProfile* family=nullptr;bool selectedSecondary=false;
};
// Read-only candidate for the exact inspected scoped-XM8/XM320 pair. Native
// eligibility, ammo, equipment animation and selection remain authoritative.
// Invoke no native function and never write the equipped pointer.
std::optional<WeaponModeCommand> ResolveWeaponMode(const WeaponModeMemory&,
    std::uint32_t soldier,std::uint32_t currentWeapon);
// Read-only evidence seam. A returned command is not native dispatch admission.
std::optional<WeaponModeCommand> ObserveWeaponModeFamily(const WeaponModeMemory&,
    std::uint32_t soldier,std::uint32_t currentWeapon,const WeaponModeFamilyProfile&);
struct WeaponModeCandidates {
    std::uint32_t selector=0,update=0,edgeReader=0,booleanReader=0,scalarReader=0;
};
// Validate static action IDs, cache readers and the original selector/update
// call graph before enabling the separate opt-in mode command.
std::optional<WeaponModeCandidates> DiscoverWeaponMode(std::span<const std::byte>,
    const engine::PeImage&);
}
