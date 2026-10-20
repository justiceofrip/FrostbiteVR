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
    float gameFov = 0;            // the game's own vertical FOV (0: unknown); a change arms the guard
    float turn = 0;         // right stick X, -1..1 (body turning)
    float dt = 0;           // seconds since the last update
    bool snapTurn = false;  // turn in fixed steps (comfort) instead of smoothly
    float snapAngle = 0.5235988f; // radians per step (the player's choice; 30 degrees by default)
    float turnRate = 2.6f;        // smooth turning, rad/s at full stick (~150 deg/s by default)
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
    float renderBudget = kGunAimFovBudget; // the render's widest FOV (the player's reach choice)
    // Gun in the hand: the game fires from the eye, the drawn barrel starts at the hand. With
    // converge > 0 (metres) the aim goes from the eye to the barrel's line at that distance, so
    // the shots cross it there instead of running parallel a hand's distance away. Positions
    // in tracking space.
    float converge = 0;
    math::Vec3 eye{}, muzzle{};
};

struct MotionAimOutput {
    bool engaged = false;
    float bodyYaw = 0;      // heading the headset view is anchored to
    int mouseX = 0, mouseY = 0; // relative counts to send this frame
    float moveRotation = 0; // radians to turn the left stick by (RotateMovement)
    bool twoHanded = false; // the left hand steadies the aim (its grip is not a pad button)
    // The direction the gun is drawn along, in the reference's frame: the one-hand aim or the
    // two-handed line (before any convergence; the game aims along it, or through its line).
    AimAngle barrel{};
    bool barrelValid = false;
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
                bodyYaw_ = WrapPi(bodyYaw_ + (in.turn > 0 ? in.snapAngle : -in.snapAngle));
                snapArmed_ = false;
                ++snaps_;
            } else if (std::fabs(in.turn) < kSnapRearm) {
                snapArmed_ = true;
            }
        } else {
            bodyYaw_ = WrapPi(bodyYaw_ + in.turn * in.turnRate * in.dt);
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
        if (in.limitToRender &&
            LimitAimToRender(in.head, aim, in.eyes[0], in.eyes[1], in.aspect, in.renderBudget - kReachSlack))
            ++limited_;
        out.barrel = aim;
        out.barrelValid = true;
        if (in.converge > 0) aim = ConvergedAim(in, aim);
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
        // What the game may be applying now: the recent pushes the way it moved (its input
        // delay is one to three frames, and the newest counts may already pull back).
        const float landingX = Landing(gameX, gainX_, flightX_), landingY = Landing(gameY, gainY_, flightY_);
        GuardOvershoot(WrapPi(desired_.yaw - in.game.yaw), targetX, gameX, landingX, anchorX_, guardX_, gainX_);
        GuardOvershoot(desired_.pitch - in.game.pitch, targetY, gameY, landingY, anchorY_, guardY_, gainY_);
        out.mouseX = Counts(WrapPi(desired_.yaw - in.game.yaw) - flightX, gainX_);
        out.mouseY = Counts(desired_.pitch - in.game.pitch - flightY, gainY_);
        flightX_ = {out.mouseX, flightX_[0], flightX_[1], flightX_[2]};
        flightY_ = {out.mouseY, flightY_[0], flightY_[1], flightY_[2]};
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
    std::uint64_t ForeignTurns() const { return foreign_; } // windows the game turned by itself
    std::uint64_t SignFlips() const { return flips_; }       // probes that found inverted look
    bool TwoHandedNow() const { return twoHanded_; }
    // The aim from the eye through the barrel's line `converge` metres from the muzzle (all in
    // the reference's frame); the barrel's own aim when the geometry is degenerate.
    static AimAngle ConvergedAim(const MotionAimInput& in, const AimAngle& barrel) {
        const math::Vec3 dir{std::sin(barrel.yaw) * std::cos(barrel.pitch), std::sin(barrel.pitch),
                             -std::cos(barrel.yaw) * std::cos(barrel.pitch)};
        const math::Quat toReference = math::Conjugate(in.reference.orientation);
        const math::Vec3 eye = math::Rotate(toReference, in.eye - in.reference.position);
        const math::Vec3 muzzle = math::Rotate(toReference, in.muzzle - in.reference.position);
        const math::Vec3 to = muzzle + dir * in.converge - eye;
        const float length = math::Length(to);
        if (!(length > 0.5f)) return barrel;
        return {std::atan2(to.x, -to.z), std::asin(std::fmax(-1.0f, std::fmin(1.0f, to.y / length)))};
    }
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
    // `target` and `game`: how far each moved since the last update; `pushed`: how far the
    // counts the game is applying now should move it at the current estimate.
    void GuardOvershoot(float error, float target, float game, float pushed, float anchor, Guard& g, float& gain) {
        if (std::fabs(error) < 0.01f) return;
        // BF4 changes its mouse sensitivity when its view zooms (aiming down sights, scopes,
        // vehicles): the guard is armed only for a moment after the game's FOV changed, or
        // while nothing is learned yet. A death camera's sway passed every other test (live,
        // session 17: three firings grew the yaw gain 5.8x).
        if (judgeQuiet_ > 0 || (guardArmed_ == 0 && anchor > 0)) {
            g = {};
            return;
        }
        const int now = error > 0 ? 1 : -1;
        if (g.sign && now != g.sign) {
            // An overshoot is the game going past the target the way our counts pushed it, by
            // no more than they can explain. A game turning by itself (its death camera,
            // live 2026-10-19: guards grew the yaw gain to the clamp, 35x) is not one.
            const bool ours = game * pushed > 0 && std::fabs(game) <= (kMaxGuardStep + 1) * std::fabs(pushed) + 0.01f;
            if (std::fabs(target) > std::fabs(game) || !ours) {
                // The target crossed the game, not the game the target (live, 2026-10-19:
                // letting go of a two-handed grip grew the pitch gain sevenfold this way), or
                // the game moved on its own: no overshoot; a new swing starts here.
                g = {};
            } else if (g.previous > 0 && g.peak > kOvershoot * g.previous) {
                // Never past kMaxGuardStep times what learning last confirmed (live: guards
                // between probes of a death camera still grew the yaw gain sixfold).
                const float cap = anchor > 0 ? std::fmin(kMaxGain, kMaxGuardStep * anchor) : kMaxGain;
                gain = std::copysign(std::fmin(cap, std::fabs(gain) * std::fmin(kMaxGuardStep, 1 + g.peak / g.previous)), gain);
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
    static float Landing(float game, float gain, const std::array<int, 4>& flight) {
        float same = 0;
        for (const int c : flight) {
            const float m = gain * static_cast<float>(c);
            if (m * game > 0) same += m;
        }
        return same;
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
        probe_ = Probe::None;
        ResetWindow();
    }
    // Gain = angle the game turned / counts sent, accumulated over a window of game
    // samples (the window absorbs the input-to-view delay).
    void Learn(const MotionAimInput& in) {
        if (in.gameSample == lastSample_) return;
        lastSample_ = in.gameSample;
        if (haveGame_) {
            const float dYaw = WrapPi(in.game.yaw - lastGame_.yaw), dPitch = in.game.pitch - lastGame_.pitch;
            const long long cx = pendingX_, cy = pendingY_;
            pendingX_ = pendingY_ = 0;
            if (judgeQuiet_ > 0) --judgeQuiet_;
            if (guardArmed_ > 0) --guardArmed_;
            if (in.gameFov > 0) {
                if (lastFov_ > 0 && std::fabs(in.gameFov / lastFov_ - 1) > 0.02f) guardArmed_ = kGuardArmSamples;
                lastFov_ = in.gameFov;
            }
            if (probe_ != Probe::None) Probing(dYaw, dPitch);
            else Window(dYaw, dPitch, cx, cy);
        }
        lastGame_ = in.game;
        haveGame_ = true;
    }
    // The game turning against the counts (one-signed counts in the window, compared with
    // what the game has applied of them by now, kLag samples later) is either inverted look
    // or the game turning by itself (its death camera, live 2026-10-19: taken for the mouse,
    // it flipped the pitch gain and collapsed both). No window tells them apart; stopping
    // does: Probing keeps quiet for a moment and watches. Still: the counts did it, the sign
    // flips. Turning on: the game's own camera, nothing is learned and aiming stays quiet.
    enum class Probe : std::uint8_t { None, Settle, Measure };
    static constexpr int kLag = 2, kProbeSettle = 4, kProbeMeasure = 12;
    static constexpr float kAgainst = 0.05f, kProbeStill = 0.01f;
    static bool Against(float angle, long long applied, long long appliedAbs, float gain) {
        const float expected = gain * static_cast<float>(applied);
        return std::fabs(expected) >= kAgainst && std::fabs(angle) >= kAgainst && angle * expected < 0 &&
               static_cast<double>(std::llabs(applied)) >= 0.8 * static_cast<double>(appliedAbs);
    }
    void Probing(float dYaw, float dPitch) {
        muted_ = std::max(muted_, 2); // nothing is sent while the game is watched
        ++probeSamples_;
        if (probe_ == Probe::Settle) { // counts already sent land first
            if (probeSamples_ >= kProbeSettle) {
                probe_ = Probe::Measure;
                probeSamples_ = 0;
                probeYaw_ = probePitch_ = 0;
            }
            return;
        }
        probeYaw_ += dYaw;
        probePitch_ += dPitch;
        if (probeSamples_ < kProbeMeasure) return;
        probe_ = Probe::None;
        if (std::fabs(probeYaw_) >= kProbeStill || std::fabs(probePitch_) >= kProbeStill) {
            ++foreign_; // the game's own camera: quiet a while, longer each time
            judgeQuiet_ = kJudgeQuietSamples;
            muted_ = kMuteFrames << std::min(ignoredInARow_, 3);
            ++ignoredInARow_;
            ++mutes_;
        } else { // our counts turned it the other way: inverted look on that axis
            if (probeAgainst_[0]) gainX_ = -gainX_;
            if (probeAgainst_[1]) gainY_ = -gainY_;
            ++flips_;
            muted_ = 0;
        }
        outlierX_ = outlierY_ = 0;
        ResetWindow();
    }
    void ResetWindow() {
        windowYaw_ = windowPitch_ = 0;
        windowCountsX_ = windowCountsY_ = 0;
        windowAppliedX_ = windowAppliedY_ = windowAppliedAbsX_ = windowAppliedAbsY_ = 0;
        lagX_ = lagY_ = {};
        windowFrames_ = 0;
    }
    void Window(float dYaw, float dPitch, long long cx, long long cy) {
        windowYaw_ += dYaw;
        windowPitch_ += dPitch;
        windowCountsX_ += cx;
        windowCountsY_ += cy;
        windowAppliedX_ += lagX_[0];
        windowAppliedY_ += lagY_[0];
        windowAppliedAbsX_ += std::llabs(lagX_[0]);
        windowAppliedAbsY_ += std::llabs(lagY_[0]);
        lagX_ = {lagX_[1], cx};
        lagY_ = {lagY_[1], cy};
        if (++windowFrames_ >= 20) {
                // Counts sent and no turn at all: the game is not listening to the mouse
                // right now (its pause menu moves a cursor with it instead). Quiet for a
                // while, longer each time it happens again, until the game turns again.
                // 200 counts: the cautious first gain sends few counts per frame.
                const bool turned = std::fabs(windowYaw_) >= 0.005f || std::fabs(windowPitch_) >= 0.005f;
                const bool sent = std::llabs(windowCountsX_) + std::llabs(windowCountsY_) >= 200;
                const bool againstX = Against(windowYaw_, windowAppliedX_, windowAppliedAbsX_, gainX_);
                const bool againstY = Against(windowPitch_, windowAppliedY_, windowAppliedAbsY_, gainY_);
                if (againstX || againstY) {
                    probe_ = Probe::Settle;
                    probeSamples_ = 0;
                    probeAgainst_ = {againstX, againstY};
                    muted_ = std::max(muted_, 2);
                    ResetWindow();
                    return;
                }
                if (sent && !turned) {
                    muted_ = kMuteFrames << std::min(ignoredInARow_, 3);
                    ++ignoredInARow_;
                    ++mutes_;
                } else {
                    if (turned) ignoredInARow_ = 0;
                    if (judgeQuiet_ == 0) {
                        // The anchor moves only with what a window measured, never with a guard.
                        const float wx = Adapt(windowYaw_, windowCountsX_, gainX_, trustX_, outlierX_);
                        const float wy = Adapt(windowPitch_, windowCountsY_, gainY_, trustY_, outlierY_);
                        trustX_ += wx;
                        trustY_ += wy;
                        if (wx > 0 && trustX_ >= kLearnedTrust) anchorX_ = std::fabs(gainX_);
                        if (wy > 0 && trustY_ >= kLearnedTrust) anchorY_ = std::fabs(gainY_);
                    }
                }
                const bool learnedX = trustX_ >= kLearnedTrust, learnedY = trustY_ >= kLearnedTrust;
                // BF4 turns as far per count on both axes (live: 0.00140 / -0.00140, aiming
                // down sights 0.00084 / -0.00084): an axis not learned yet starts from the
                // other's gain, kSeedMargin above it (the cautious side), not from the first
                // estimate. Small vertical moves (the gun's reach keeps them small) may never
                // teach the pitch gain and steered at half speed (live, 2026-10-19).
                if (learnedX && !learnedY) {
                    gainY_ = std::copysign(kSeedMargin * std::fabs(gainX_), gainY_);
                    anchorY_ = std::fabs(gainY_);
                }
                if (learnedY && !learnedX) {
                    gainX_ = std::copysign(kSeedMargin * std::fabs(gainY_), gainX_);
                    anchorX_ = std::fabs(gainX_);
                }
                ResetWindow();
        }
    }
    // An axis counts as learned once its windows carried this much trust (0.1 rad of turning).
    static constexpr float kSeedMargin = 1.2f, kLearnedTrust = 0.5f;
    // Plausible gains (rad per count) and the largest change one window may make once an axis
    // is learned (aiming down sights changes BF4's by 0.6x / 1.67x); a bigger one needs the
    // next window to agree.
    static constexpr float kMinGain = 5e-5f, kMaxGain = 0.02f, kMaxLearnStep = 2.5f;

    // Returns the trust the window carried (0: it taught nothing). `pendingRatio`: an
    // unconfirmed large change of the previous window (0: none).
    static float Adapt(float angle, long long counts, float& gain, float trust, float& pendingRatio) {
        // Enough signal: 200 counts, or fewer with a clear turn (an estimate that is too high
        // sends few counts and would otherwise never relearn).
        const bool enough = std::llabs(counts) >= 200 || (std::llabs(counts) >= 40 && std::fabs(angle) >= 0.05f);
        if (!enough || std::fabs(angle) < 0.01f) return 0;
        float measured = angle / static_cast<float>(counts);
        const float magnitude = std::fabs(measured);
        if (magnitude < kMinGain || magnitude > kMaxGain) return 0;
        const float ratio = measured / gain;
        if (ratio <= 0) return 0; // the sign changes only after a probe (Probing)
        if (trust >= kLearnedTrust) {
            if (ratio > kMaxLearnStep || ratio < 1 / kMaxLearnStep) {
                const bool confirmed = pendingRatio > 0 && std::fabs(ratio / pendingRatio - 1) < 0.3f;
                pendingRatio = ratio;
                if (!confirmed) return 0;
                measured = gain * std::clamp(ratio, 1 / kMaxLearnStep, kMaxLearnStep);
            } else {
                pendingRatio = 0;
            }
        }
        // Trust a window in proportion to how far the game turned in it (0.2 rad: fully), not
        // to the counts sent (those depend on the estimate itself).
        const float weight = std::fmin(1.0f, std::fabs(angle) / 0.2f);
        gain += weight * (measured - gain);
        if (std::fabs(gain) < kMinGain) gain = measured;
        gain = std::copysign(std::clamp(std::fabs(gain), kMinGain, kMaxGain), gain);
        return weight;
    }

    bool engaged_ = false, haveGame_ = false;
    float bodyYaw_ = 0, lastHandYaw_ = 0;
    float gainX_ = kInitialGain, gainY_ = -kInitialGain; // mouse down looks down: negative pitch per count
    float trustX_ = 0, trustY_ = 0; // what each axis's windows have taught so far (Adapt)
    float outlierX_ = 0, outlierY_ = 0; // an unconfirmed large change per axis (Adapt)
    float anchorX_ = 0, anchorY_ = 0;   // the gains learning last confirmed (0: none yet)
    int judgeQuiet_ = 0;                // game samples left without learning or guards
    int guardArmed_ = 0;                // game samples left with the guard armed (a FOV change)
    float lastFov_ = 0;
    static constexpr int kGuardArmSamples = 180; // ~1.5 s
    static constexpr int kJudgeQuietSamples = 240; // ~2 s after the game turned by itself
    std::uint64_t foreign_ = 0, flips_ = 0; // probes that found the game's own camera / inverted look
    Probe probe_ = Probe::None;
    int probeSamples_ = 0;
    float probeYaw_ = 0, probePitch_ = 0;
    std::array<bool, 2> probeAgainst_{};
    long long windowAppliedX_ = 0, windowAppliedY_ = 0, windowAppliedAbsX_ = 0, windowAppliedAbsY_ = 0;
    std::array<long long, kLag> lagX_{}, lagY_{}; // counts of the last kLag samples, not applied yet
    AimAngle lastGame_{}, desired_{}, lastDesired_{}, lastGuardGame_{};
    bool haveDesired_ = false;
    std::uint64_t lastSample_ = 0;
    long long pendingX_ = 0, pendingY_ = 0, windowCountsX_ = 0, windowCountsY_ = 0;
    std::array<int, 4> flightX_{}, flightY_{}; // counts sent in the last four updates, newest first
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
