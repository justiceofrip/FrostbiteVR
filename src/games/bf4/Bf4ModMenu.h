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
    bool worldStereo = false; // developer: BF4's own two-eye render (freezes the game so far)
    bool controllerAim = true; // campaign: the gun follows the right controller
    bool holsters = true;      // campaign: body holster gestures (Bf4BodyGestures.h)
    bool aimDot = true;        // head-tracked view + controller aim: reticle where the gun aims
    bool vignette = true;      // head-tracked view: darken the edges while moving by stick
    bool twoHandedAim = true;  // controller aim: the left grip in front of the gun steadies it
};

enum class ModMenuAction : std::uint8_t {
    None, SetReloadMode, SetSnapTurn, SetTheaterDistance, Recenter, MarkFeedback, GameMenu, SetHeadTracking, SetWorldSettle,
    SetWorldStereo, SetControllerAim, SetHolsters, SetAimDot, SetVignette, SetTwoHandedAim
};

struct ModMenuCommand {
    ModMenuAction action = ModMenuAction::None;
    int value = 0;
};

// BF4's reload modes: Manual is the magazine gesture (Bf4BodyGestures.h), input-only, so it
// needs the virtual pad rather than a native manual-reload binding.
interaction::ReloadModeResolution ResolveBf4ReloadMode(interaction::ReloadMode requested,
                                                       const game::CapabilityRegistry& caps, bool developer);

ui::ModMenuModel BuildModMenu(const game::CapabilityRegistry& caps, bool developer, const ModMenuState& state);

// Recompute availability notes after a change or a capability update.
void RefreshModMenu(ui::ModMenuModel& menu, const game::CapabilityRegistry& caps, bool developer);

ModMenuCommand InterpretChange(const ui::MenuItem& item);

} // namespace fvr::bf4
