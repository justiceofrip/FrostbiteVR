#include "fvr/interaction/VirtualPad.h"

#include <algorithm>
#include <cmath>

namespace fvr::interaction {

namespace {
// Radial deadzone, then rescale (deadzone..1) -> (0..1) and to the int16 pad range.
void MapStick(float x, float y, float deadzone, std::int16_t& outX, std::int16_t& outY) {
    const float magnitude = std::sqrt(x * x + y * y);
    if (!std::isfinite(magnitude) || magnitude <= deadzone) {
        outX = outY = 0;
        return;
    }
    const float scaled = std::min(1.0f, (magnitude - deadzone) / (1.0f - deadzone)) / magnitude;
    outX = static_cast<std::int16_t>(std::clamp(x * scaled, -1.0f, 1.0f) * 32767.0f);
    outY = static_cast<std::int16_t>(std::clamp(y * scaled, -1.0f, 1.0f) * 32767.0f);
}

std::uint8_t MapTrigger(float value, float deadzone) {
    if (!std::isfinite(value) || value <= deadzone) return 0;
    return static_cast<std::uint8_t>(std::clamp((value - deadzone) / (1.0f - deadzone), 0.0f, 1.0f) * 255.0f + 0.5f);
}

bool Pressed(const ControllerState& hand, VrControl control, float squeezeThreshold) {
    switch (control) {
    case VrControl::Primary: return hand.held & Primary;
    case VrControl::Secondary: return hand.held & Secondary;
    case VrControl::StickClick: return hand.held & StickClick;
    case VrControl::Squeeze: return (hand.active & Squeeze) && hand.squeeze >= squeezeThreshold;
    }
    return false;
}
} // namespace

PadState MapToPad(const InputFrame& input, const PadLayout& layout, const PadOptions& options) noexcept {
    PadState out;
    if (!input.focused || !ValidInput(input)) return out;
    const ControllerState& left = input.hands[0];
    const ControllerState& right = input.hands[1];
    if (left.active & Stick) MapStick(left.stickX, left.stickY, layout.stickDeadzone, out.thumbLX, out.thumbLY);
    if (right.active & Stick) MapStick(right.stickX, right.stickY, layout.stickDeadzone, out.thumbRX, out.thumbRY);
    if (left.active & Trigger) out.leftTrigger = MapTrigger(left.trigger, layout.triggerDeadzone);
    if (right.active & Trigger) out.rightTrigger = MapTrigger(right.trigger, layout.triggerDeadzone);
    for (const PadBinding& b : layout.buttons)
        if (b.hand < 2 && Pressed(input.hands[b.hand], b.control, layout.squeezeThreshold)) out.buttons |= b.padButton;
    if (options.reloadEnabled && layout.reload.padButton && layout.reload.hand < 2 &&
        Pressed(input.hands[layout.reload.hand], layout.reload.control, layout.squeezeThreshold))
        out.buttons |= layout.reload.padButton;
    out.buttons |= options.extraButtons;
    return out;
}

} // namespace fvr::interaction
