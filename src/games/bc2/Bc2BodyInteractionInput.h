#pragma once
#include "Bc2BodyInventory.h"
namespace fvr::bc2 {
// Preserve ordinary body suspension exactly. Only its precise cause is exposed
// to the selected-tool adapter; this is not a holster/equip permission.
inline void SetBodyInteractionInput(BodyDrawSample& s,bool aimValid,bool active,bool sightIdle,
    std::uint32_t actions)noexcept {
    using namespace interaction;
    const auto conflicting=actions&(Use|NextWeapon|PreviousWeapon|Fire);
    const bool other=!aimValid||!active||!sightIdle;
    s.interaction=other||conflicting?BodyInteractionState::Suspended:BodyInteractionState::Available;
    s.cancelOnlyForFire=!other&&conflicting==Fire;
}
}
