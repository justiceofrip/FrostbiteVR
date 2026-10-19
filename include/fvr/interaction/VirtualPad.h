#pragma once
// VR controllers -> one virtual XInput-style gamepad, for games that support a pad natively
// (BF4 imports XINPUT9_1_0): locomotion, fire, aim and button reload without native input
// bindings, and the game's own controller settings stay the remapping layer.
// Values match XINPUT_GAMEPAD so a Windows sink copies them verbatim; no Windows headers.
// Policy only: no hooks, no native state. Engine-agnostic (the layout is per game).
#include "fvr/interaction/ControllerInput.h"

#include <cstdint>
#include <vector>

namespace fvr::interaction {

namespace pad {
inline constexpr std::uint16_t DpadUp = 0x0001, DpadDown = 0x0002, DpadLeft = 0x0004, DpadRight = 0x0008;
inline constexpr std::uint16_t Start = 0x0010, Back = 0x0020, LeftThumb = 0x0040, RightThumb = 0x0080;
inline constexpr std::uint16_t LeftShoulder = 0x0100, RightShoulder = 0x0200;
inline constexpr std::uint16_t A = 0x1000, B = 0x2000, X = 0x4000, Y = 0x8000;
} // namespace pad

struct PadState {
    std::uint16_t buttons = 0;
    std::uint8_t leftTrigger = 0, rightTrigger = 0;
    std::int16_t thumbLX = 0, thumbLY = 0, thumbRX = 0, thumbRY = 0;
    bool operator==(const PadState&) const = default;
};

// A VR control that acts as a digital pad button. Grip (Squeeze) is analog in the
// framework, so it counts as pressed above the layout's threshold.
enum class VrControl : std::uint8_t { Primary, Secondary, StickClick, Squeeze };

struct PadBinding {
    std::uint8_t hand = 1; // 0 left, 1 right
    VrControl control = VrControl::Primary;
    std::uint16_t padButton = 0;
};

struct PadLayout {
    std::vector<PadBinding> buttons; // always-on digital bindings
    PadBinding reload;               // separate: only while button reload is the effective mode
    float stickDeadzone = 0.15f;     // radial, rescaled so the edge of the zone reads 0
    float triggerDeadzone = 0.05f;
    float squeezeThreshold = 0.6f;
};

struct PadOptions {
    bool reloadEnabled = true;      // false while Manual reload owns the reload gesture
    std::uint16_t extraButtons = 0; // timed pulses, e.g. Start from the mod menu
};

// Neutral (all zero) when the input is not valid/focused: a lost controller never leaves a
// stick deflected or a trigger held in the game.
PadState MapToPad(const InputFrame& input, const PadLayout& layout, const PadOptions& options) noexcept;

} // namespace fvr::interaction
