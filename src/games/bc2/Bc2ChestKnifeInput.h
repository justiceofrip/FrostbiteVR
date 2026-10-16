#pragma once
#include "fvr/interaction/ControllerInput.h"
#include <cmath>
namespace fvr::bc2 {
// Raw input validity for continued held custody/display. The coordinator also
// requires current body interaction eligibility; policy/native gates still own
// menu, original source lease, exact selected knife, claim and operation.
inline bool ChestKnifeControlInputCurrent(const interaction::InputFrame& input)noexcept {
    using namespace interaction;
    if(!ValidInput(input)||!input.focused||!input.headValid)return false;
    constexpr unsigned required=Stick|Trigger|Squeeze|Primary|Secondary|StickClick;
    for(const auto& hand:input.hands)
        if((hand.active&required)!=required||!hand.gripTracked)return false;
    return input.hands[1].aimTracked;
}
// Initial chest draw retains quiet-input arming. Continued motion recognition
// has a separate action-conflict gate below.
inline bool ChestKnifeControlsNeutral(const interaction::InputFrame& input)noexcept {
    if(!ChestKnifeControlInputCurrent(input))return false;
    for(const auto& hand:input.hands)
        if(hand.held||hand.trigger>.1f||std::hypot(hand.stickX,hand.stickY)>.1f)return false;
    return true;
}
// Left secondary = crouch, left click alone = sprint, right primary = jump.
// Axes are locomotion; Gameplay separately rejects an actual weapon flick.
// Both-click recenter, Use, reload/equip buttons and triggers remain conflicts.
inline bool ChestKnifeActionInputCurrent(const interaction::InputFrame& input)noexcept {
    using namespace interaction;
    if(!ChestKnifeControlInputCurrent(input))return false;
    return !(input.hands[0].held&~unsigned(Secondary|StickClick))&&
        !(input.hands[1].held&~unsigned(Primary))&&
        input.hands[0].trigger<=.1f&&input.hands[1].trigger<=.1f&&input.hands[0].squeeze<=.35f;
}

}
