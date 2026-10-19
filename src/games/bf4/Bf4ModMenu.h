#pragma once
// The in-headset FrostbiteVR menu for BF4, built from the capability table so it never
// offers something that is disabled, and explains why in plain words. The XR host
// renders it and drives it with the laser pointer; changes come back as commands.
#include "fvr/game/Capability.h"
#include "fvr/interaction/ReloadPolicy.h"
#include "fvr/ui/ModMenu.h"

namespace fvr::bf4 {

struct ModMenuState {
    interaction::ReloadMode reloadMode = interaction::ReloadMode::ButtonPress;
    bool snapTurn = true;
    int theaterDistance = 1; // 0 near, 1 medium, 2 far
    bool headTracking = true; // campaign levels: head-tracked view instead of the screen
    int worldSettle = 2;      // developer: submits before a frame counts as head-tracked
    bool worldStereo = true;  // developer: BF4's own two-eye render in the world lane
};

enum class ModMenuAction : std::uint8_t {
    None, SetReloadMode, SetSnapTurn, SetTheaterDistance, Recenter, MarkFeedback, GameMenu, SetHeadTracking, SetWorldSettle,
    SetWorldStereo
};

struct ModMenuCommand {
    ModMenuAction action = ModMenuAction::None;
    int value = 0;
};

ui::ModMenuModel BuildModMenu(const game::CapabilityRegistry& caps, bool developer, const ModMenuState& state);

// Recompute availability notes after a change or a capability update.
void RefreshModMenu(ui::ModMenuModel& menu, const game::CapabilityRegistry& caps, bool developer);

ModMenuCommand InterpretChange(const ui::MenuItem& item);

} // namespace fvr::bf4
