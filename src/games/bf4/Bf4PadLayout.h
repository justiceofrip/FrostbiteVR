#pragma once
// BF4's default controller scheme expressed as a VR -> virtual pad layout. BF4 reads the
// pad itself (XINPUT9_1_0), so its own Options > Controls stay the remapping layer; this
// only decides which VR control presses which pad button.
//
// BF4 default pad (PC, "Default" scheme): LS move / click sprint, RS look / click melee,
// LT aim, RT fire, A jump, B crouch (hold prone), X reload / interact, Y switch weapon,
// LB spot, RB grenade, Start pause, Back scoreboard. Unverified in-game until the first
// controller session (capability VirtualGamepad stays Candidate until then).
#include "fvr/interaction/VirtualPad.h"

namespace fvr::bf4 {

inline interaction::PadLayout DefaultPadLayout() {
    using interaction::VrControl;
    namespace pad = interaction::pad;
    interaction::PadLayout l;
    l.buttons = {
        {1, VrControl::Primary, pad::A},          // right A -> jump
        {0, VrControl::Primary, pad::B},          // left X  -> crouch
        {0, VrControl::Secondary, pad::Y},        // left Y  -> switch weapon
        {1, VrControl::Squeeze, pad::RightShoulder}, // right grip -> grenade
        {0, VrControl::Squeeze, pad::LeftShoulder},  // left grip  -> spot
        {1, VrControl::StickClick, pad::RightThumb}, // right stick click -> melee
        {0, VrControl::StickClick, pad::LeftThumb},  // left stick click  -> sprint
    };
    l.reload = {1, VrControl::Secondary, pad::X}; // right B -> reload (weapon hand)
    return l;
}

} // namespace fvr::bf4
