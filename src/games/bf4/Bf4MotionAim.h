#pragma once
// Right-controller aiming for BF4 through the game's own mouse look (input only, no game
// memory written). Pure logic; the platform sends the mouse movement and reads back the
// game's aim from the view it submits.
//
// - The headset view is anchored to a body heading (bodyYaw) that only the player turns
//   (right stick); it no longer follows the game's aim.
// - The game's aim (its camera yaw/pitch) is steered every frame toward bodyYaw + the right
//   controller's direction, by relative mouse counts. The counts-per-radian gain of each
//   axis (sensitivity, inversion, aim-down-sights scaling) is learned from what the game
//   actually did with the counts sent, over a sliding window.
// - Movement: the game moves relative to its aim, so the left stick is turned by the
//   aim's offset from the body to keep walking relative to the body.
#include "fvr/interaction/AimFrame.h"

#include <array>
#include <cmath>
#include <cstdint>

namespace fvr::bf4 {

// Yaw/pitch of a BF4 camera (right-handed rows: right, up, +Z behind). Yaw 0 looks down
// world -Z; positive yaw turns right (toward +X); positive pitch looks up.
struct AimAngle {
    float yaw = 0, pitch = 0;
};
inline AimAngle CameraAim(const math::Vec3& backRow) {
    const float fx = -backRow.x, fy = -backRow.y, fz = -backRow.z;
    return {std::atan2(fx, -fz), std::asin(std::fmax(-1.0f, std::fmin(1.0f, fy)))};
}
inline float WrapPi(float a) {
    constexpr float kTau = 6.283185307179586f;
    a = std::fmod(a + 3.14159265f, kTau);
    if (a < 0) a += kTau;
    return a - 3.14159265f;
}

struct MotionAimInput {
    bool engage = false;    // world lane, campaign play, feature on
    bool aimTracked = false;
    math::Pose reference;   // upright recentre reference (tracking space)
    math::Pose aim;         // right controller aim pose (tracking space)
    bool gameValid = false; // the game's aim was read this frame
    AimAngle game;          // the game's current aim
    std::uint64_t gameSample = 0; // increments with each new game reading (a view submit)
    float turn = 0;         // right stick X, -1..1 (body turning)
    float dt = 0;           // seconds since the last update
};

struct MotionAimOutput {
    bool engaged = false;
    float bodyYaw = 0;      // heading the headset view is anchored to
    int mouseX = 0, mouseY = 0; // relative counts to send this frame
    float moveRotation = 0; // radians to turn the left stick by (RotateMovement)
};

class MotionAim {
public:
    static constexpr float kTurnRate = 2.6f;      // rad/s at full stick (~150 deg/s)
    static constexpr int kMaxCounts = 600;        // per frame and axis
    static constexpr float kInitialGain = 0.0006f; // rad per count until learned

    MotionAimOutput Update(const MotionAimInput& in) {
        MotionAimOutput out;
        if (!in.engage || !in.gameValid) {
            engaged_ = false;
            return out;
        }
        if (!engaged_) { // the body starts where the game is looking
            engaged_ = true;
            bodyYaw_ = in.game.yaw;
            Reset();
        }
        bodyYaw_ = WrapPi(bodyYaw_ + in.turn * kTurnRate * in.dt);
        out.engaged = true;
        out.bodyYaw = bodyYaw_;
        out.moveRotation = WrapPi(in.game.yaw - bodyYaw_);
        Learn(in);
        if (!in.aimTracked) return out;
        if (muted_ > 0) { // the game stopped turning (pause menu, cutscene): stay quiet a while
            --muted_;
            return out;
        }
        const auto hand = interaction::ControllerAim(in.reference, in.aim, lastHandYaw_);
        if (!hand) return out;
        lastHandYaw_ = hand->yaw;
        desired_ = {WrapPi(bodyYaw_ + hand->yaw), hand->pitch};
        out.mouseX = Counts(WrapPi(desired_.yaw - in.game.yaw), gainX_);
        out.mouseY = Counts(desired_.pitch - in.game.pitch, gainY_);
        pendingX_ += out.mouseX;
        pendingY_ += out.mouseY;
        return out;
    }
    float GainX() const { return gainX_; }
    float GainY() const { return gainY_; }
    bool Engaged() const { return engaged_; }
    std::uint64_t Mutes() const { return mutes_; }
    static constexpr int kMuteFrames = 60;
    AimAngle Desired() const { return desired_; }

private:
    // Proportional step with a 0.7 margin against the one-frame input delay.
    static int Counts(float error, float gain) {
        const float c = 0.7f * error / gain;
        return static_cast<int>(std::lround(std::fmax(-float(kMaxCounts), std::fmin(float(kMaxCounts), c))));
    }
    void Reset() {
        gainX_ = kInitialGain;
        gainY_ = -kInitialGain; // mouse down looks down: negative pitch per positive count
        haveGame_ = false;
        muted_ = 0;
        pendingX_ = pendingY_ = 0;
        windowCountsX_ = windowCountsY_ = 0;
        windowYaw_ = windowPitch_ = 0;
        windowFrames_ = 0;
    }
    // Gain = angle the game turned / counts sent, accumulated over a window of game
    // samples (the window absorbs the input-to-view delay).
    void Learn(const MotionAimInput& in) {
        if (in.gameSample == lastSample_) return;
        lastSample_ = in.gameSample;
        if (haveGame_) {
            windowYaw_ += WrapPi(in.game.yaw - lastGame_.yaw);
            windowPitch_ += in.game.pitch - lastGame_.pitch;
            windowCountsX_ += pendingX_;
            windowCountsY_ += pendingY_;
            pendingX_ = pendingY_ = 0;
            if (++windowFrames_ >= 20) {
                // Plenty of counts and no turn at all: the game is not listening to the
                // mouse right now (menus move a cursor with it instead).
                if (std::llabs(windowCountsX_) + std::llabs(windowCountsY_) >= 1500 && std::fabs(windowYaw_) < 0.005f &&
                    std::fabs(windowPitch_) < 0.005f) {
                    muted_ = kMuteFrames;
                    ++mutes_;
                }
                Adapt(windowYaw_, windowCountsX_, gainX_);
                Adapt(windowPitch_, windowCountsY_, gainY_);
                windowYaw_ = windowPitch_ = 0;
                windowCountsX_ = windowCountsY_ = 0;
                windowFrames_ = 0;
            }
        }
        lastGame_ = in.game;
        haveGame_ = true;
    }
    static void Adapt(float angle, long long counts, float& gain) {
        if (std::llabs(counts) < 200 || std::fabs(angle) < 0.01f) return; // not enough signal
        const float measured = angle / static_cast<float>(counts);
        const float magnitude = std::fabs(measured);
        if (magnitude < 1e-6f || magnitude > 0.05f) return;
        // Trust a window in proportion to how much it moved (2000 counts: fully). Follows
        // sign flips too (inverted look).
        const float weight = std::fmin(1.0f, static_cast<float>(std::llabs(counts)) / 2000.0f);
        gain += weight * (measured - gain);
        if (std::fabs(gain) < 1e-6f) gain = measured;
    }

    bool engaged_ = false, haveGame_ = false;
    float bodyYaw_ = 0, lastHandYaw_ = 0;
    float gainX_ = kInitialGain, gainY_ = -kInitialGain;
    AimAngle lastGame_{}, desired_{};
    std::uint64_t lastSample_ = 0;
    long long pendingX_ = 0, pendingY_ = 0, windowCountsX_ = 0, windowCountsY_ = 0;
    float windowYaw_ = 0, windowPitch_ = 0;
    int windowFrames_ = 0;
    int muted_ = 0;
    std::uint64_t mutes_ = 0;
};

} // namespace fvr::bf4
