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
// - Two-handed aim: while the left grip is held in front of the gun (along the right
//   controller's aim), the gun aims along the line from the right grip to the left grip,
//   which is steadier than one wrist (a rifle held with both hands).
// - Gun follows aim: the aim stays where the render can turn all the way to it (see
//   LimitAimToRender), so the barrel never leaves the bullets' line.
#include "Bf4WorldView.h"
#include "fvr/interaction/AimFrame.h"
#include "fvr/math/Vec.h"

#include <algorithm>
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
// The axes of a camera with this aim and no roll (BF4's own camera keeps its right row level).
inline CameraAxes AimAxes(const AimAngle& a) {
    const float sy = std::sin(a.yaw), cy = std::cos(a.yaw), sp = std::sin(a.pitch), cp = std::cos(a.pitch);
    const float right[3] = {cy, 0, sy}, back[3] = {-sy * cp, -sp, cy * cp};
    CameraAxes x;
    for (int k = 0; k < 3; ++k) {
        x.row[0][k] = right[k];
        x.row[2][k] = back[k];
    }
    x.row[1][0] = back[1] * right[2] - back[2] * right[1]; // up = back x right
    x.row[1][1] = back[2] * right[0] - back[0] * right[2];
    x.row[1][2] = back[0] * right[1] - back[1] * right[0];
    return x;
}
// Gun follows aim: GunAimRotation turns the render only as far toward the game's aim as the
// FOV budget allows, and beyond it the gun stopped short while the bullets went on along the
// aim (live, 2026-10-19: 15 degrees up with the head level, the barrel a third of the way).
// This brings an aim back toward the head, along the turn from the head to it, until the
// render can turn all the way: the gun then stops at that edge with the bullets on its line,
// and looking toward a target brings it into reach. `head`: the head camera's axes in the
// aim's frame. kReachSlack keeps the head's motion until the render, and the game's lag
// behind the aim, inside the budget. True when the aim was brought back.
inline constexpr float kReachSlack = 0.05f;
inline bool LimitAimToRender(const CameraAxes& head, AimAngle& aim, const math::FovTangents& left,
                             const math::FovTangents& right, float aspect, float maxFovY = kGunAimFovBudget - kReachSlack) {
    const float budget = std::max(RequiredVerticalFov(left, right, aspect), maxFovY);
    auto fits = [&](const AimAngle& a) {
        return RequiredVerticalFovRotated(RotationBetween(head, AimAxes(a)), left, right, aspect) <= budget;
    };
    if (fits(aim)) return false;
    const CameraAxes target = AimAxes(aim);
    auto toward = [&](float t) {
        const CameraAxes c = TurnToward(head, target, t);
        return CameraAim({c.row[2][0], c.row[2][1], c.row[2][2]});
    };
    float lo = 0, hi = 1;
    for (int i = 0; i < 12; ++i) {
        const float mid = 0.5f * (lo + hi);
        if (fits(toward(mid))) lo = mid;
        else hi = mid;
    }
    aim = toward(lo);
    return true;
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
    bool snapTurn = false;  // turn in fixed steps (comfort) instead of smoothly
    // Two-handed aim: the left grip is held (and not carrying a magazine or grenade); the
    // grip positions in tracking space.
    bool twoHanded = false;
    bool supportGrip = false;
    math::Vec3 rearGrip{}, frontGrip{};
    // Gun follows aim (LimitAimToRender): the head camera's axes in the reference's frame,
    // the eyes' FOV tangents and the render's aspect.
    bool limitToRender = false;
    CameraAxes head;
    math::FovTangents eyes[2];
    float aspect = 0;
};

struct MotionAimOutput {
    bool engaged = false;
    float bodyYaw = 0;      // heading the headset view is anchored to
    int mouseX = 0, mouseY = 0; // relative counts to send this frame
    float moveRotation = 0; // radians to turn the left stick by (RotateMovement)
    bool twoHanded = false; // the left hand steadies the aim (its grip is not a pad button)
};

class MotionAim {
public:
    static constexpr float kTurnRate = 2.6f;      // rad/s at full stick (~150 deg/s)
    static constexpr int kMaxCounts = 600;        // per frame and axis
    // Rad per count until learned. Deliberately high: a game more sensitive than the
    // estimate makes every step overshoot, a less sensitive one only makes steering slower
    // until the gain is learned (simulated: no overshoot for true gains 0.0002-0.003).
    static constexpr float kInitialGain = 0.003f;
    // Snap turning: one step per push past kSnapPush; the stick must come back under
    // kSnapRearm before the next one.
    static constexpr float kSnapAngle = 0.5235988f; // 30 degrees
    static constexpr float kSnapPush = 0.7f, kSnapRearm = 0.3f;
    // Two-handed aim: the front hand 12-80 cm from the rear one, within 35 degrees of the
    // right controller's aim to start (60 to keep).
    static constexpr float kTwoHandMin = 0.12f, kTwoHandMax = 0.80f;
    static constexpr float kTwoHandStart = 0.6108652f, kTwoHandKeep = 1.0471976f;

    MotionAimOutput Update(const MotionAimInput& in) {
        MotionAimOutput out;
        if (!in.engage || !in.gameValid) {
            engaged_ = false;
            return out;
        }
        if (!engaged_) { // the body starts where the game is looking
            engaged_ = true;
            bodyYaw_ = in.game.yaw;
            Reset(); // the learned gain stays: the game's mouse sensitivity has not changed
        }
        if (in.snapTurn) {
            if (snapArmed_ && std::fabs(in.turn) >= kSnapPush) {
                bodyYaw_ = WrapPi(bodyYaw_ + (in.turn > 0 ? kSnapAngle : -kSnapAngle));
                snapArmed_ = false;
                ++snaps_;
            } else if (std::fabs(in.turn) < kSnapRearm) {
                snapArmed_ = true;
            }
        } else {
            bodyYaw_ = WrapPi(bodyYaw_ + in.turn * kTurnRate * in.dt);
            snapArmed_ = std::fabs(in.turn) < kSnapRearm;
        }
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
        AimAngle aim{hand->yaw, hand->pitch};
        twoHanded_ = TwoHanded(in, aim);
        out.twoHanded = twoHanded_;
        if (in.limitToRender && LimitAimToRender(in.head, aim, in.eyes[0], in.eyes[1], in.aspect)) ++limited_;
        desired_ = {WrapPi(bodyYaw_ + aim.yaw), aim.pitch};
        // What moved since the last update: the target (hand, snap turn, the second hand
        // taking over or letting go) or the game. The guard only judges swings the game made.
        const float targetX = haveDesired_ ? WrapPi(desired_.yaw - lastDesired_.yaw) : 0.0f;
        const float targetY = haveDesired_ ? desired_.pitch - lastDesired_.pitch : 0.0f;
        const float gameX = haveDesired_ ? WrapPi(in.game.yaw - lastGuardGame_.yaw) : 0.0f;
        const float gameY = haveDesired_ ? in.game.pitch - lastGuardGame_.pitch : 0.0f;
        lastDesired_ = desired_;
        lastGuardGame_ = in.game;
        haveDesired_ = true;
        // Counts sent in the last frames may not show in the game's aim yet (its input is a
        // frame or two behind): count them as done, or the loop overshoots and, with a
        // longer delay, oscillates.
        const float flightX = gainX_ * static_cast<float>(flightX_[0] + flightX_[1]);
        const float flightY = gainY_ * static_cast<float>(flightY_[0] + flightY_[1]);
        ++updates_;
        GuardOvershoot(WrapPi(desired_.yaw - in.game.yaw), targetX, gameX, guardX_, gainX_);
        GuardOvershoot(desired_.pitch - in.game.pitch, targetY, gameY, guardY_, gainY_);
        out.mouseX = Counts(WrapPi(desired_.yaw - in.game.yaw) - flightX, gainX_);
        out.mouseY = Counts(desired_.pitch - in.game.pitch - flightY, gainY_);
        flightX_ = {out.mouseX, flightX_[0]};
        flightY_ = {out.mouseY, flightY_[0]};
        pendingX_ += out.mouseX;
        pendingY_ += out.mouseY;
        return out;
    }
    float GainX() const { return gainX_; }
    float GainY() const { return gainY_; }
    bool Engaged() const { return engaged_; }
    std::uint64_t Mutes() const { return mutes_; }
    std::uint64_t Snaps() const { return snaps_; }
    std::uint64_t OscillationGuards() const { return guards_; }
    std::uint64_t ReachLimited() const { return limited_; } // updates whose aim was brought back
    bool TwoHandedNow() const { return twoHanded_; }
    static constexpr int kMuteFrames = 60;
    AimAngle Desired() const { return desired_; }

private:
    // The line from the rear grip to the front grip, in the reference's frame, replaces the
    // one-hand aim when it runs along it (both hands on one gun).
    bool TwoHanded(const MotionAimInput& in, AimAngle& aim) const {
        if (!in.twoHanded || !in.supportGrip) return false;
        const math::Vec3 d = math::Rotate(math::Conjugate(in.reference.orientation), in.frontGrip - in.rearGrip);
        const float length = math::Length(d);
        if (!(length >= kTwoHandMin && length <= kTwoHandMax)) return false;
        const AimAngle line{std::atan2(d.x, -d.z), std::asin(std::fmax(-1.0f, std::fmin(1.0f, d.y / length)))};
        // Angle between the line and the one-hand aim (both as directions).
        auto dir = [](const AimAngle& a) {
            return math::Vec3{std::sin(a.yaw) * std::cos(a.pitch), std::sin(a.pitch), -std::cos(a.yaw) * std::cos(a.pitch)};
        };
        const math::Vec3 u = dir(line), v = dir(aim);
        const float c = std::fmax(-1.0f, std::fmin(1.0f, u.x * v.x + u.y * v.y + u.z * v.z));
        if (std::acos(c) > (twoHanded_ ? kTwoHandKeep : kTwoHandStart)) return false;
        aim = line;
        return true;
    }
    // Overshoot: the aim error swung past zero by more than kOvershoot of the swing before
    // it. The game turns more per count than the estimate says (e.g. leaving aim-down-sights
    // raises its sensitivity), so the estimate grows by the overshoot ratio at once (smaller
    // steps; at most kMaxGuardStep a time); the regular learning then settles it on the
    // measured value. A sign change the target made (it moved more than the game in that
    // update) is no overshoot. tools/models/aim_guard_model.py: this keeps the protection of
    // judging every sign change and removes most false alarms of moving targets. Simulated: a
    // threefold sensitivity jump stays within ~0.1 rad of overshoot for 1-2 frames of delay,
    // where waiting for repeated swings let it run away at 3 frames.
    struct Guard {
        int sign = 0;
        float peak = 0;      // largest |error| of the current swing
        float previous = -1; // the swing before it (-1: none to compare with)
    };
    static constexpr float kOvershoot = 0.3f;
    static constexpr float kMaxGuardStep = 4.0f; // one guard grows the estimate at most this much
    // `target` and `game`: how far each moved since the last update.
    void GuardOvershoot(float error, float target, float game, Guard& g, float& gain) {
        if (std::fabs(error) < 0.01f) return;
        const int now = error > 0 ? 1 : -1;
        if (g.sign && now != g.sign) {
            if (std::fabs(target) > std::fabs(game)) {
                // The target crossed the game, not the game the target (live, 2026-10-19:
                // letting go of a two-handed grip grew the pitch gain sevenfold this way):
                // no overshoot; a new swing starts here.
                g = {};
            } else if (g.previous > 0 && g.peak > kOvershoot * g.previous) {
                gain = std::fmax(-0.05f, std::fmin(0.05f, gain * std::fmin(kMaxGuardStep, 1 + g.peak / g.previous)));
                g.previous = -1;
                ++guards_;
            } else {
                g.previous = g.peak;
            }
            g.peak = 0;
        }
        g.sign = now;
        g.peak = std::fmax(g.peak, std::fabs(error));
    }
    // Proportional step. With two frames of counts treated as in flight, half the remaining
    // error per frame settles without overshoot for game input delays of one to three
    // frames and a gain off by up to 40 % (simulated before it was chosen).
    static constexpr float kStep = 0.5f;
    static int Counts(float error, float gain) {
        const float c = kStep * error / gain;
        return static_cast<int>(std::lround(std::fmax(-float(kMaxCounts), std::fmin(float(kMaxCounts), c))));
    }
    void Reset() {
        haveGame_ = false;
        muted_ = 0;
        pendingX_ = pendingY_ = 0;
        flightX_ = flightY_ = {};
        guardX_ = guardY_ = {};
        haveDesired_ = false;
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
                // Counts sent and no turn at all: the game is not listening to the mouse
                // right now (its pause menu moves a cursor with it instead). Quiet for a
                // while, longer each time it happens again, until the game turns again.
                // 200 counts: the cautious first gain sends few counts per frame.
                const bool turned = std::fabs(windowYaw_) >= 0.005f || std::fabs(windowPitch_) >= 0.005f;
                if (std::llabs(windowCountsX_) + std::llabs(windowCountsY_) >= 200 && !turned) {
                    muted_ = kMuteFrames << std::min(ignoredInARow_, 3);
                    ++ignoredInARow_;
                    ++mutes_;
                } else if (turned) {
                    ignoredInARow_ = 0;
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
        // Trust a window in proportion to how far the game turned in it (0.2 rad: fully), not
        // to the counts sent (those depend on the estimate itself). Follows sign flips too
        // (inverted look).
        const float weight = std::fmin(1.0f, std::fabs(angle) / 0.2f);
        gain += weight * (measured - gain);
        if (std::fabs(gain) < 1e-6f) gain = measured;
    }

    bool engaged_ = false, haveGame_ = false;
    float bodyYaw_ = 0, lastHandYaw_ = 0;
    float gainX_ = kInitialGain, gainY_ = -kInitialGain; // mouse down looks down: negative pitch per count
    AimAngle lastGame_{}, desired_{}, lastDesired_{}, lastGuardGame_{};
    bool haveDesired_ = false;
    std::uint64_t lastSample_ = 0;
    long long pendingX_ = 0, pendingY_ = 0, windowCountsX_ = 0, windowCountsY_ = 0;
    std::array<int, 2> flightX_{}, flightY_{}; // counts sent in the last two updates
    Guard guardX_{}, guardY_{};
    std::uint64_t updates_ = 0, guards_ = 0, limited_ = 0;
    float windowYaw_ = 0, windowPitch_ = 0;
    int windowFrames_ = 0;
    int muted_ = 0;
    int ignoredInARow_ = 0; // windows in a row the game ignored the mouse (mute backoff)
    std::uint64_t mutes_ = 0;
    bool snapArmed_ = false; // a stick already pushed when aiming engages is not a turn
    bool twoHanded_ = false;
    std::uint64_t snaps_ = 0;
};

} // namespace fvr::bf4
