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
    int snapDegrees = 30;     // snap turning's step: 30, 45 or 90 degrees
    int turnSpeed = 1;        // smooth turning: 0 slow (90 deg/s), 1 normal (150), 2 fast (240)
    int theaterDistance = 1; // 0 near, 1 medium, 2 far
    bool headTracking = true; // campaign levels: head-tracked view instead of the screen
    int worldSettle = 1;      // developer: submits before a frame counts as head-tracked (1: label-checked clean with native eyes, 2026-10-20)
    bool worldStereo = false; // BF4's own split two-eye render: unsupported, always false
    bool controllerAim = true; // campaign: the gun follows the right controller
    bool holsters = true;      // campaign: body holster gestures (Bf4BodyGestures.h)
    bool aimDot = false;       // controller aim: reticle where the gun aims (off: the barrel shows it, player 2026-10-19)
    bool vignette = true;      // head-tracked view: darken the edges while moving by stick
    bool twoHandedAim = true;  // controller aim: the left grip in front of the gun steadies it
    bool walkWithBody = true;  // controller aim: the left stick walks relative to the body (else the gun)
    bool physicalCrouch = false; // crouch for real to crouch in game (Bf4PhysicalCrouch.h; standing play)
    bool depthStereo = false;    // head-tracked view: 3D from the game's depth buffer (Bf4DepthCapture.h)
    bool alternateEyes = false;  // ...or each frame rendered from one eye, the eyes in turn (Bf4EyeResampler.h)
    // Head-tracked view: BF4 draws the world once per eye each frame, each from its own eye
    // (Bf4WorldRenderOverride.h). The menu's only 3D route (on by default: the player's goal);
    // the stand-ins above (depth reprojection, alternate eyes) are no longer offered.
    bool nativeEyes = true;
    bool hudInsets = true;       // head-tracked view: the corner HUD copied into the visible corners
    bool gunFollowsAim = false;  // head-tracked view: render along the aim so the barrel is on the bullets' line
    bool gunReachWide = false;   // ...and let that render grow wider (less sharp) so the gun follows farther
    bool gunInHand = true;       // ...or (default) draw BF4's first-person rig at the right controller (no turned render)
    bool armIk = true;           // ...with the shoulders kept at the body and the arms reaching the gun (Bf4ArmIk.h)
    bool freeLeftHand = false;   // ...and the left hand at its controller when off the gun (experimental: parts of
                                 // the hand can stay on the gun, stretched; seen by the player 2026-10-20)
};

enum class ModMenuAction : std::uint8_t {
    None, SetReloadMode, SetSnapTurn, SetTheaterDistance, Recenter, MarkFeedback, GameMenu, SetHeadTracking, SetWorldSettle,
    SetWorldStereo, SetControllerAim, SetHolsters, SetAimDot, SetVignette, SetTwoHandedAim, SetWalkDirection,
    SetPhysicalCrouch, SetDepthStereo, SetHudInsets, SetGunFollowsAim, SetTurnSpeed, SetArms
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
