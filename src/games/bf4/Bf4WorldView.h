#pragma once
// BF4 world lane (head-tracked view in the headset): math and decisions only, unit-tested.
//
// Each XR frame the host asks for an eye pair at exact tracked poses with per-eye FOV
// tangents (a FrameLease). The adapter applies that lease's head pose to the game camera
// (Bf4ViewOverride, tagged with the lease), waits until a presented frame is known to carry
// it, then resamples the part of the game image each eye covers into that eye's texture and
// publishes the pair. Mono: one head-centred render feeds both eyes (no parallax), both from
// the same native frame. Stereo replaces the source with one render per eye in the same frame.
#include "fvr/math/StereoMath.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace fvr::bf4 {

// Source UV rectangle for one eye, inside the source region [bu0, bu1] (the whole image, or
// one half of a side-by-side frame). inside=false: part of the eye's view was not rendered
// (the resampler fills it black), i.e. the game FOV is narrower than the eye's.
struct EyeCrop {
    float u0 = 0, v0 = 0, u1 = 1, v1 = 1;
    float bu0 = 0, bu1 = 1;
    bool inside = true;
};

// tanX/tanY: half-angle tangents of the game's render. Eye tangents: left/down negative.
// shiftNdc: a constant horizontal image shift the render carries (BF4 split stereo shifts
// each eye's image by +-separation in NDC); the crop takes it back out.
inline EyeCrop EyeCropRect(const math::FovTangents& eye, float tanX, float tanY, float bu0 = 0, float bu1 = 1,
                           float shiftNdc = 0) {
    EyeCrop c;
    c.bu0 = bu0;
    c.bu1 = bu1;
    if (!(tanX > 0) || !(tanY > 0) || !(bu1 > bu0)) return {0, 0, 0, 0, bu0, bu1, false};
    const float w = bu1 - bu0;
    c.u0 = bu0 + w * (0.5f + eye.left / (2 * tanX) + shiftNdc / 2);
    c.u1 = bu0 + w * (0.5f + eye.right / (2 * tanX) + shiftNdc / 2);
    c.v0 = 0.5f - eye.up / (2 * tanY);
    c.v1 = 0.5f - eye.down / (2 * tanY);
    constexpr float kSlack = 1e-4f;
    c.inside = c.u0 >= bu0 - kSlack && c.u1 <= bu1 + kSlack && c.v0 >= -kSlack && c.v1 <= 1 + kSlack;
    return c;
}

// The vertical FOV (radians) the game must render so both eyes fit: the larger vertical
// tangent of either eye, or the horizontal one over the render's aspect, plus margin.
inline float RequiredVerticalFov(const math::FovTangents& left, const math::FovTangents& right, float aspect,
                                 float margin = 1.03f) {
    float ty = std::max({left.up, -left.down, right.up, -right.down});
    const float tx = std::max({-left.left, left.right, -right.left, right.right});
    if (aspect > 0) ty = std::max(ty, tx / aspect);
    return std::min(2.0f * std::atan(ty * margin), 3.0f);
}

// Both eye crops of one presented frame. Mono: the same head-centred image. Split stereo
// (BF4's own two-eye render): eye 0 (left) is the engine's eye 1 in the left half, shifted
// by +separation; eye 1 (right) is the right half, shifted by -separation. Halves keep the
// full frame's projection (anamorphic), so tanX is the full frame's.
inline void EyeCrops(const math::FovTangents (&fov)[2], float tanX, float tanY, bool split, float separation,
                     EyeCrop (&out)[2]) {
    if (!split) {
        out[0] = EyeCropRect(fov[0], tanX, tanY);
        out[1] = EyeCropRect(fov[1], tanX, tanY);
        return;
    }
    out[0] = EyeCropRect(fov[0], tanX, tanY, 0.0f, 0.5f, separation);
    out[1] = EyeCropRect(fov[1], tanX, tanY, 0.5f, 1.0f, -separation);
}

// Capture decision at Present for the lease the camera is carrying.
struct WorldCaptureInput {
    bool leaseHeld = false;
    std::uint64_t leasePoseId = 0;   // tag given to the view override with this lease's pose
    std::uint64_t appliedPoseId = 0; // what the latest view submit carried
    std::uint32_t appliedRun = 0;    // submits in a row that carried it
    std::uint32_t settle = 2;        // submits needed before the presented frame must show it
    std::uint64_t leaseAgeMs = 0;
    bool untracked = false;          // emulator only: no camera, publish the frame as it is
    std::uint32_t stableFrames = ~0u; // presents since the stereo/mono layout last changed
};
enum class WorldStep { None, Wait, Capture, Cancel };
inline constexpr std::uint64_t kWorldLeaseMaxMs = 140; // host requests live 150 ms

inline WorldStep DecideWorldStep(const WorldCaptureInput& in) {
    if (!in.leaseHeld) return WorldStep::None;
    if (in.untracked) return WorldStep::Capture;
    // A frame rendered across a mono/stereo switch could be either layout: wait it out.
    const bool layoutSettled = in.stableFrames >= std::max(1u, in.settle) + 2;
    if (in.leasePoseId && in.appliedPoseId == in.leasePoseId && in.appliedRun >= std::max(1u, in.settle) && layoutSettled)
        return WorldStep::Capture;
    return in.leaseAgeMs > kWorldLeaseMaxMs ? WorldStep::Cancel : WorldStep::Wait;
}

// World presentation with a way back: when the world lane publishes no pair for kNoPairMs
// (no camera, loading screen, host not asking), the game screen returns to the theater
// panel for kRetryMs before the next attempt, so the headset never stays black.
class WorldModeGate {
public:
    static constexpr std::uint64_t kNoPairMs = 2500, kRetryMs = 5000;
    bool Update(bool wanted, bool pairPublished, std::uint64_t nowMs) {
        if (!wanted) {
            world_ = false;
            return false;
        }
        if (backoffUntil_ && nowMs < backoffUntil_) return false;
        backoffUntil_ = 0;
        if (!world_) {
            world_ = true;
            lastProgressMs_ = nowMs;
        }
        if (pairPublished) lastProgressMs_ = nowMs;
        if (nowMs - lastProgressMs_ >= kNoPairMs) {
            world_ = false;
            backoffUntil_ = nowMs + kRetryMs;
            ++fallbacks_;
            return false;
        }
        return true;
    }
    bool World() const { return world_; }
    std::uint64_t Fallbacks() const { return fallbacks_; }

private:
    bool world_ = false;
    std::uint64_t lastProgressMs_ = 0, backoffUntil_ = 0, fallbacks_ = 0;
};

} // namespace fvr::bf4
