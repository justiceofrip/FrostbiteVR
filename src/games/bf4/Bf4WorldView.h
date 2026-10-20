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

// Gun follows aim. BF4 draws its first-person gun at rest relative to the camera that renders
// the frame (live, 2026-10-19), so with the camera at the head the gun stays in front of the
// face while bullets go along the game's aim. Rendering the frame with the camera turned to
// the aim (at the head's position) puts the barrel on the bullets' line; each eye ray is then
// rotated into that render, which reproduces the head's view exactly (a pure rotation).
//
// A camera orientation as BF4 stores it: its world axes as rows (right, up, back; the camera
// looks along -back).
struct CameraAxes {
    float row[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
};
// Maps a head-camera direction (x right, y up, -z forward) to the render camera's.
struct FrameRotation {
    bool enabled = false;
    float m[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
};
inline float Dot3(const float (&a)[3], const float (&b)[3]) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
// Render-from-head rotation for cameras with axes `head` and `render`: m[i][j] = render_i . head_j.
inline FrameRotation RotationBetween(const CameraAxes& head, const CameraAxes& render) {
    FrameRotation f;
    f.enabled = true;
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) f.m[i][j] = Dot3(render.row[i], head.row[j]);
    return f;
}
// The render camera's axes from the head's and a render-from-head rotation (inverse of the above).
inline CameraAxes ApplyRotation(const CameraAxes& head, const FrameRotation& f) {
    CameraAxes o;
    for (int i = 0; i < 3; ++i)
        for (int k = 0; k < 3; ++k) o.row[i][k] = f.m[i][0] * head.row[0][k] + f.m[i][1] * head.row[1][k] + f.m[i][2] * head.row[2][k];
    return o;
}
// The render tangents of a head-space tangent direction; false when it falls behind the render.
inline bool RotateTangent(const FrameRotation& f, float tx, float ty, float& fx, float& fy) {
    const float d[3] = {tx, ty, -1};
    const float o0 = Dot3(f.m[0], d), o1 = Dot3(f.m[1], d), o2 = Dot3(f.m[2], d);
    if (!(o2 < -1e-3f)) return false;
    fx = o0 / -o2;
    fy = o1 / -o2;
    return true;
}
// The vertical FOV the render needs so both eyes fit after the rotation. Each eye counts by
// the ellipse inscribed in its tangent rectangle: the lens shows about that (runtimes mask the
// rectangle's corners), and rotated corners would stretch the render far more (modelled for
// Quest-like eyes: 20 degrees sideways needs 2.07 rad with the ellipse, 2.40 with corners).
// Unrotated it equals RequiredVerticalFov. 3 rad when part of an eye would fall behind.
inline float RequiredVerticalFovRotated(const FrameRotation& f, const math::FovTangents& left, const math::FovTangents& right,
                                        float aspect, float margin = 1.03f) {
    if (!f.enabled) return RequiredVerticalFov(left, right, aspect, margin);
    constexpr int kPoints = 32;
    float ty = 0;
    for (const math::FovTangents* e : {&left, &right}) {
        const float cx = 0.5f * (e->left + e->right), cy = 0.5f * (e->up + e->down);
        const float ax = 0.5f * (e->right - e->left), ay = 0.5f * (e->up - e->down);
        for (int k = 0; k < kPoints; ++k) {
            const float a = 6.2831853f * float(k) / kPoints;
            float fx = 0, fy = 0;
            if (!RotateTangent(f, cx + ax * std::cos(a), cy + ay * std::sin(a), fx, fy)) return 3.0f;
            ty = std::max(ty, std::fabs(fy));
            if (aspect > 0) ty = std::max(ty, std::fabs(fx) / aspect);
        }
    }
    return std::min(2.0f * std::atan(ty * margin), 3.0f);
}
// The head's axes turned toward the aim's by the fraction t of the rotation between them
// (about the one axis that takes the head's onto the aim's).
inline CameraAxes TurnToward(const CameraAxes& head, const CameraAxes& aim, float t) {
    // Q = sum_i aim_i (x) head_i: the world rotation taking each head axis onto the aim's.
    float q[3][3] = {};
    for (int i = 0; i < 3; ++i)
        for (int r = 0; r < 3; ++r)
            for (int c = 0; c < 3; ++c) q[r][c] += aim.row[i][r] * head.row[i][c];
    const float cosA = std::clamp((q[0][0] + q[1][1] + q[2][2] - 1) * 0.5f, -1.0f, 1.0f);
    const float angle = std::acos(cosA);
    if (angle < 1e-5f) return head;
    const float s = std::sin(angle);
    if (s < 1e-3f) return t >= 1 ? aim : head; // ~180 degrees: no stable axis
    const float k[3] = {(q[2][1] - q[1][2]) / (2 * s), (q[0][2] - q[2][0]) / (2 * s), (q[1][0] - q[0][1]) / (2 * s)};
    const float a = angle * std::clamp(t, 0.0f, 1.0f), ca = std::cos(a), sa = std::sin(a);
    CameraAxes o;
    for (int i = 0; i < 3; ++i) { // Rodrigues: v cos a + (k x v) sin a + k (k.v)(1 - cos a)
        const float* v = head.row[i];
        const float kv = k[0] * v[0] + k[1] * v[1] + k[2] * v[2];
        const float cr[3] = {k[1] * v[2] - k[2] * v[1], k[2] * v[0] - k[0] * v[2], k[0] * v[1] - k[1] * v[0]};
        for (int n = 0; n < 3; ++n) o.row[i][n] = v[n] * ca + cr[n] * sa + k[n] * kv * (1 - ca);
    }
    return o;
}
// The render orientation for the gun to follow the aim: as far toward the aim as the FOV
// budget allows (the frame must still cover both eyes; a wider frame is less sharp). Beyond
// it (the head looks far from where the gun points) the gun follows only part of the way.
struct GunAim {
    FrameRotation rotation; // render-from-head
    float fovY = 0;         // vertical FOV to render (radians)
    float fraction = 0;     // how far toward the aim: 1 = all the way
};
// What the eye resampler needs to rotate each eye's rays into the render.
struct FrameView {
    FrameRotation rotation;
    float tanX = 0, tanY = 0; // the render's half-angle tangents
    float halfEye = 0;        // world units (depth stereo's per-eye offset, rotated too)
};
// Budget: 2.1 rad (~120 deg vertical). For Quest-like eyes (head-only 1.88 rad) the gun follows
// fully up to about 20 degrees sideways or 7 down; the centre is then up to ~22 % less sharp.
inline constexpr float kGunAimFovBudget = 2.1f;
inline GunAim GunAimRotation(const CameraAxes& head, const CameraAxes& aim, const math::FovTangents& left,
                             const math::FovTangents& right, float aspect, float maxFovY = kGunAimFovBudget) {
    const float budget = std::max(RequiredVerticalFov(left, right, aspect), maxFovY);
    auto at = [&](float t, GunAim& g) {
        g.rotation = RotationBetween(head, TurnToward(head, aim, t));
        g.fovY = RequiredVerticalFovRotated(g.rotation, left, right, aspect);
        g.fraction = t;
        return g.fovY <= budget;
    };
    GunAim best;
    if (at(1.0f, best)) return best;
    float lo = 0, hi = 1;
    for (int i = 0; i < 12; ++i) {
        const float mid = 0.5f * (lo + hi);
        GunAim g;
        if (at(mid, g)) lo = mid;
        else hi = mid;
    }
    at(lo, best);
    return best;
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
// HUD corners: BF4 draws its ammo panel and minimap in the frame's bottom corners, which a
// 16:9 frame tall enough for the headset puts outside both eyes. Each is copied into an inset
// inside the visible lower corners, at the same view tangents in both eyes (so at infinity).
// Source: frame uv (u0, v0, u1, v1), measured on a 16:9 campaign frame (2026-10-19 session
// snapshot: ammo panel u 0.87-0.99 v 0.84-0.98; minimap with its objective text u 0.01-0.14
// v 0.71-0.98); target: view tangents (left, top, right, bottom), same aspect as the source.
struct HudInset {
    float src[4];
    float tan[4];
};
inline constexpr HudInset kAmmoInset{{0.865f, 0.835f, 0.995f, 0.985f}, {0.38f, -0.64f, 0.82f, -0.93f}};
inline constexpr HudInset kMinimapInset{{0.008f, 0.705f, 0.142f, 0.985f}, {-0.82f, -0.50f, -0.43f, -0.96f}};

// Per eye: up to two markers, the eye image's extent in tangent units (marker size), the
// comfort vignette and the HUD corners (its FOV tangents place both in the view).
struct EyeOverlay {
    EyeMarker markers[2];
    float spanX = 1, spanY = 1;
    math::FovTangents fov{-1, 1, 1, -1};
    float vignette = 0; // 0 none .. 1 full
    bool hudInsets = false;
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
