#pragma once
#include "fvr/interaction/ActionPolicy.h"
#include <optional>
namespace fvr::bc2 {
struct PumpActionGates {bool blockActions=false,cancelReload=false;};
// An unavailable observation blocks actions but cannot create owed cycle debt
// or discard an already held shell. Native reload admission remains separate.
inline PumpActionGates EvaluatePumpActionGates(bool selected,bool physicalBlocksFire,
    bool physicalCycleDebt,std::optional<bool> nativeBlocksFire)noexcept {
    return {selected&&(physicalBlocksFire||!nativeBlocksFire||*nativeBlocksFire),
        selected&&(physicalCycleDebt||nativeBlocksFire.value_or(false))};
}
// Manual native debt blocks changes to the held weapon and shell operations.
// Idle pump observation leaves ordinary shell-start pulses and body input intact.
inline void ApplyPumpActionGate(interaction::ActionOutput& actions,bool debt)noexcept {
    if(!debt)return;
    constexpr auto mask=interaction::Fire|interaction::Reload|interaction::Use|
        interaction::NextWeapon|interaction::PreviousWeapon;
    actions.held&=~mask;actions.pressed&=~mask;
}
// Consumers see Fire first so it can cancel loading. The outgoing shot waits
// for exact resource retirement; the genuine shell-start Reload pulse survives.
inline void ApplyPumpResourceHandoffGate(interaction::ActionOutput& actions,bool resourceBusy)noexcept {
    if(resourceBusy){actions.held&=~interaction::Fire;actions.pressed&=~interaction::Fire;}
}
}
