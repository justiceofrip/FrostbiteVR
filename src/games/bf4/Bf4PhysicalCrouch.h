#pragma once
// Physical crouching for BF4 (opt-in): when the player's head drops well below their
// standing height, the game's crouch is toggled on; standing up toggles it off again.
// Input-only: BF4's pad crouch (B) is a toggle, so the stance the game is in is believed,
// not read. The belief follows every press that changes it: our own pulses, the player's
// own crouch button, and the game's own stand-ups (sprint and jump leave a crouch).
//
// Standing height calibrates itself: the highest head height seen, sinking slowly
// (kSinkRate) while the body stands, so drift and a lowered stance are followed; it holds
// while the body is crouched (a long crouch never reads as standing). Seated players
// should leave this off.
#include <algorithm>
#include <cstdint>

namespace fvr::bf4 {

struct CrouchInput {
    bool enabled = false;
    bool usable = false;      // campaign play, input focused, head tracked
    double timeSeconds = 0;
    float headHeight = 0;     // metres, tracking space (only differences matter)
    bool crouchButton = false; // the player's own crouch (pad B) is held
    bool sprintButton = false; // sprint (left stick click): BF4 stands up
    bool jumpButton = false;   // jump (pad A): BF4 stands up
};

class PhysicalCrouch {
public:
    static constexpr float kCrouchDrop = 0.35f; // head this far below standing: crouch
    static constexpr float kStandDrop = 0.20f;  // back above this: stand
    static constexpr float kSinkRate = 0.01f;   // standing height sinks 1 cm/s
    static constexpr double kPulseSeconds = 0.12;

    // True while the crouch button should be pressed (a short toggle pulse).
    bool Update(const CrouchInput& in) {
        const float dt = lastTime_ > 0 ? static_cast<float>(std::clamp(in.timeSeconds - lastTime_, 0.0, 0.25)) : 0.0f;
        lastTime_ = in.timeSeconds;
        // The game's stance changes we can see: our pulse ending, the player's own toggle
        // (on its press), sprint or jump standing up.
        if (in.crouchButton && !buttonWas_ && !Pulsing(in.timeSeconds)) crouched_ = !crouched_;
        buttonWas_ = in.crouchButton;
        if (in.sprintButton || in.jumpButton) crouched_ = false;
        if (!in.usable) return false;
        if (!calibrated_ || in.headHeight > standing_) {
            standing_ = in.headHeight;
            calibrated_ = true;
        } else if (!bodyCrouched_) {
            standing_ = std::max(in.headHeight, standing_ - kSinkRate * dt);
        }
        // Act only when the body's posture changes (with hysteresis): a stance the player
        // chose with the button stays until they physically crouch or stand up.
        const float drop = standing_ - in.headHeight;
        const bool body = bodyCrouched_ ? drop > kStandDrop : drop >= kCrouchDrop;
        if (body != bodyCrouched_) {
            bodyCrouched_ = body;
            if (in.enabled && crouched_ != body && !Pulsing(in.timeSeconds)) {
                crouched_ = body;
                pulseUntil_ = in.timeSeconds + kPulseSeconds;
                ++toggles_;
            }
        }
        return Pulsing(in.timeSeconds);
    }
    bool Crouched() const { return crouched_; }
    float StandingHeight() const { return standing_; }
    std::uint64_t Toggles() const { return toggles_; }

private:
    bool Pulsing(double now) const { return now < pulseUntil_; }

    bool calibrated_ = false, crouched_ = false, bodyCrouched_ = false, buttonWas_ = false;
    float standing_ = 0;
    double lastTime_ = 0, pulseUntil_ = 0;
    std::uint64_t toggles_ = 0;
};

} // namespace fvr::bf4
