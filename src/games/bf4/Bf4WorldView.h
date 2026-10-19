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
#include "fvr/math/Vec.h"

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
// (BF4's own two-eye render): eye 0 (left) is the engine's eye 1 in the left half, eye 1
// (right) the right half. In BF4's right-handed view space (w = -z) the engine's +s per
// eye shifts the image by -s (left eye -separation, right eye +separation); tanX is the
// eye half's own projection (its aspect).
inline void EyeCrops(const math::FovTangents (&fov)[2], float tanX, float tanY, bool split, float separation,
                     EyeCrop (&out)[2]) {
    if (!split) {
        out[0] = EyeCropRect(fov[0], tanX, tanY);
        out[1] = EyeCropRect(fov[1], tanX, tanY);
        return;
    }
    out[0] = EyeCropRect(fov[0], tanX, tanY, 0.0f, 0.5f, -separation);
    out[1] = EyeCropRect(fov[1], tanX, tanY, 0.5f, 1.0f, separation);
}

// Capture decision at Present for the lease the camera is carrying.
// Aim reticle (controller aiming): where the game's own aim crosses a mono render with
// half-angle tangents tanX/tanY, as frame uv; scale converts frame uv to tangent units (the
// reticle keeps its angular size whatever the render FOV).
struct Reticle {
    bool show = false;
    float u = 0.5f, v = 0.5f;
    float scaleX = 1, scaleY = 1;
};
inline Reticle ReticleAt(float aimTanX, float aimTanY, float tanX, float tanY) {
    Reticle r;
    if (!(tanX > 0 && tanY > 0) || !std::isfinite(aimTanX) || !std::isfinite(aimTanY)) return r;
    r.u = 0.5f + aimTanX / (2 * tanX);
    r.v = 0.5f - aimTanY / (2 * tanY);
    r.scaleX = 2 * tanX;
    r.scaleY = 2 * tanY;
    r.show = r.u > 0 && r.u < 1 && r.v > 0 && r.v < 1;
    return r;
}

// Gesture markers (the magazine pouch, the magazine well) drawn into the eye images. Each
// eye places a tracking-space point by its own pose and FOV, so a marker sits at its true
// depth even though the world behind it is one mono render. u,v: the eye image (0..1).
struct EyeMarker {
    bool show = false;
    float u = 0, v = 0;
    std::uint32_t kind = 0; // 0 amber (take from here), 1 green (put it here)
};
inline EyeMarker MarkerInEye(const math::Pose& eye, const math::FovTangents& fov, const math::Vec3& point,
                             std::uint32_t kind) {
    EyeMarker m;
    const math::Vec3 local = math::Rotate(math::Conjugate(eye.orientation), point - eye.position);
    const float depth = -local.z, w = fov.right - fov.left, h = fov.up - fov.down;
    if (!(depth > 0.05f) || !(w > 0) || !(h > 0)) return m;
    m.u = (local.x / depth - fov.left) / w;
    m.v = (fov.up - local.y / depth) / h;
    m.kind = kind;
    m.show = m.u > -0.05f && m.u < 1.05f && m.v > -0.05f && m.v < 1.05f;
    return m;
}
// Per eye: up to two markers, the eye image's extent in tangent units (marker size), and
// the comfort vignette (its FOV tangents place the centre of the view).
struct EyeOverlay {
    EyeMarker markers[2];
    float spanX = 1, spanY = 1;
    math::FovTangents fov{-1, 1, 1, -1};
    float vignette = 0; // 0 none .. 1 full
};

// Comfort vignette: darkens the edge of view while the player moves by stick or turns
// smoothly (artificial motion the inner ear does not feel). Fast in, slower out.
class ComfortVignette {
public:
    static constexpr float kRise = 6.0f, kFall = 2.5f; // per second
    // motion: 0..1 (stick deflection past the dead zone, or smooth turning).
    float Update(bool enabled, float motion, float dt) {
        const float target = enabled ? std::clamp(motion, 0.0f, 1.0f) : 0.0f;
        const float rate = target > level_ ? kRise : kFall;
        const float step = rate * std::clamp(dt, 0.0f, 0.1f);
        level_ = target > level_ ? std::min(target, level_ + step) : std::max(target, level_ - step);
        return level_;
    }
    float Level() const { return level_; }

private:
    float level_ = 0;
};

// Depth stereo: both eyes from the one mono frame, each shifted by its own parallax using
// the frame's depth buffer (Bf4DepthCapture). A point at view distance z appears in an eye
// offset b (world units, + right) at head tangent - b/z, so each eye samples the frame at
// its own tangent + b/z. In frame uv (which spans 2 tanX across) the shift is
// shift/z with shift = b / (2 tanX).
inline float LinearDepth(float d, float nearZ, float farZ, bool reversed) {
    const float denom = reversed ? nearZ + d * (farZ - nearZ) : farZ - d * (farZ - nearZ);
    return denom > 0 ? nearZ * farZ / denom : farZ;
}
struct DepthStereoParams {
    bool enabled = false;
    float shift[2] = {0, 0}; // per eye, frame uv x world units (left negative)
    float nearZ = 0.1f, farZ = 1000;
    bool reversed = false;   // the buffer clears to 0 (reversed depth)
    float minDepth = 0.4f;   // nearer than this counts as this far (the weapon: about where a VR gun is held)
};
inline DepthStereoParams DepthStereoFor(float halfEye, float tanX, float nearZ, float farZ, float clearValue) {
    DepthStereoParams p;
    if (!(halfEye > 0 && tanX > 0 && nearZ > 0 && farZ > nearZ)) return p;
    p.enabled = true;
    p.shift[0] = -halfEye / (2 * tanX);
    p.shift[1] = halfEye / (2 * tanX);
    p.nearZ = nearZ;
    p.farZ = farZ;
    p.reversed = clearValue < 0.5f;
    return p;
}

inline constexpr std::uint64_t kWorldLeaseMaxMs = 140; // host requests live 150 ms
// --frame-channels 2: requests live 200 ms (one waits behind the other); the margin covers
// the time between the host's request and the adapter taking it at a frame start.
inline constexpr std::uint64_t kWorldPipelinedLeaseMaxMs = 170;
struct WorldCaptureInput {
    bool leaseHeld = false;
    std::uint64_t leasePoseId = 0;   // tag given to the view override with this lease's pose
    std::uint64_t appliedPoseId = 0; // what the latest view submit carried
    std::uint32_t appliedRun = 0;    // submits in a row that carried it
    std::uint32_t settle = 2;        // submits needed before the presented frame must show it
    std::uint64_t leaseAgeMs = 0;
    std::uint64_t leaseMaxMs = kWorldLeaseMaxMs;
    bool untracked = false;          // emulator only: no camera, publish the frame as it is
    std::uint32_t stableFrames = ~0u; // presents since the stereo/mono layout last changed
};
enum class WorldStep { None, Wait, Capture, Cancel };

inline WorldStep DecideWorldStep(const WorldCaptureInput& in) {
    if (!in.leaseHeld) return WorldStep::None;
    if (in.untracked) return WorldStep::Capture;
    // A frame rendered across a mono/stereo switch could be either layout: wait it out.
    const bool layoutSettled = in.stableFrames >= std::max(1u, in.settle) + 2;
    if (in.leasePoseId && in.appliedPoseId == in.leasePoseId && in.appliedRun >= std::max(1u, in.settle) && layoutSettled)
        return WorldStep::Capture;
    return in.leaseAgeMs > in.leaseMaxMs ? WorldStep::Cancel : WorldStep::Wait;
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
