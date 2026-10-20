#include "Bf4ArmIk.h"

#include <algorithm>
#include <cmath>

namespace fvr::bf4 {

ArmBindJoints Bf4ArmBindJoints() {
    ArmBindJoints j;
    for (int side = 0; side < 2; ++side) {
        const float x = side == 0 ? 1.0f : -1.0f; // +x: the left arm
        j.shoulder[side] = {0.1641f * x, 1.4745f, -0.0396f};
        j.elbow[side] = {0.3729f * x, 1.2601f, -0.0125f};
        j.wrist[side] = {0.5089f * x, 1.1205f, 0.1609f};
    }
    return j;
}

namespace {

math::Vec3 MapPoint(const float* b, math::Vec3 p) {
    return {b[0] * p.x + b[1] * p.y + b[2] * p.z + b[3], b[4] * p.x + b[5] * p.y + b[6] * p.z + b[7],
            b[8] * p.x + b[9] * p.y + b[10] * p.z + b[11]};
}

bool Near(math::Vec3 a, math::Vec3 b, float tolerance) {
    const math::Vec3 d = a - b;
    return math::Dot(d, d) < tolerance * tolerance;
}

bool Usable(const float* b) {
    for (int k = 0; k < kArmBoneFloats; ++k)
        if (!std::isfinite(b[k]) || std::fabs(b[k]) > 1e5f) return false;
    return true;
}

bool SameBone(const float* a, const float* b) {
    for (int k = 0; k < kArmBoneFloats; ++k)
        if (std::fabs(a[k] - b[k]) > 2e-4f) return false;
    return true;
}

Mat4 FromRows(const float* b) {
    return {b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7], b[8], b[9], b[10], b[11], 0, 0, 0, 1};
}

bool IsIdentity(const Mat4& m) { return m == Identity4(); }

} // namespace

int FindArmChains(const float* palette, int bones, const ArmBindJoints& bind, ArmChain out[2]) {
    out[0] = out[1] = ArmChain{};
    if (!palette || bones <= 0) return 0;
    bones = std::min(bones, kArmMaxBones);
    // Distinct entries only: a duplicated matrix agrees with itself on every point.
    int uniq[kArmMaxBones];
    int n = 0;
    for (int i = 0; i < bones; ++i) {
        const float* b = palette + i * kArmBoneFloats;
        if (!Usable(b)) continue;
        bool seen = false;
        for (int k = 0; k < n && !seen; ++k) seen = SameBone(b, palette + uniq[k] * kArmBoneFloats);
        if (!seen) uniq[n++] = i;
    }
    int found = 0;
    for (int side = 0; side < 2; ++side) {
        math::Vec3 ps[kArmMaxBones], pe[kArmMaxBones], pw[kArmMaxBones];
        for (int k = 0; k < n; ++k) {
            const float* b = palette + uniq[k] * kArmBoneFloats;
            ps[k] = MapPoint(b, bind.shoulder[side]);
            pe[k] = MapPoint(b, bind.elbow[side]);
            pw[k] = MapPoint(b, bind.wrist[side]);
        }
        bool sPartner[kArmMaxBones]{}, wPartner[kArmMaxBones]{};
        for (int a = 0; a < n; ++a)
            for (int b = a + 1; b < n; ++b) {
                if (Near(ps[a], ps[b], kArmChainTolerance)) sPartner[a] = sPartner[b] = true;
                if (Near(pw[a], pw[b], kArmChainTolerance)) wPartner[a] = wPartner[b] = true;
            }
        // An upper arm (shoulder shared with another entry) and a forearm (wrist shared with
        // another entry, not the upper arm's shoulder) meeting at the elbow.
        for (int u = 0; u < n && !out[side].found; ++u) {
            if (!sPartner[u]) continue;
            for (int f = 0; f < n; ++f) {
                if (f == u || !wPartner[f] || !Near(pe[u], pe[f], kArmChainTolerance) ||
                    Near(ps[f], ps[u], kArmChainTolerance))
                    continue;
                out[side] = {ps[u], pe[u], pw[f], true};
                ++found;
                break;
            }
        }
    }
    return found;
}

bool TurnsAboutNear(const float* hand, const float* bone, math::Vec3 pivotNear, float reach) {
    // T = hand^-1 x bone, in bind space: a screw motion (turn about an axis, slide along it).
    const Mat4 t = Mul(InverseRigid(FromRows(hand)), FromRows(bone));
    const math::Vec3 move{t[3], t[7], t[11]};
    const float c = std::clamp((t[0] + t[5] + t[10] - 1.0f) * 0.5f, -1.0f, 1.0f);
    const float angle = std::acos(c);
    if (angle < 1e-3f) return math::Length(move) < 0.01f; // no turn: it must move with the hand
    math::Vec3 axis{t[9] - t[6], t[2] - t[8], t[4] - t[1]};
    const float len = math::Length(axis);
    if (len < 1e-6f) return false; // half a turn: the axis is not recoverable this way
    axis = axis * (1.0f / len);
    const float slide = math::Dot(move, axis);
    if (std::fabs(slide) > 0.01f) return false;
    // The axis' point: (t_perp + cot(angle/2) axis x t_perp) / 2.
    const math::Vec3 across = move - axis * slide;
    const math::Vec3 point = (across + math::Cross(axis, across) * (1.0f / std::tan(angle * 0.5f))) * 0.5f;
    const math::Vec3 v = pivotNear - point;
    return math::Length(v - axis * math::Dot(v, axis)) < reach;
}

void ClassifyBones(const float* palette, int bones, const ArmBindJoints& bind, const ArmChain chains[2], float tolerance,
                   BoneClass* out, float fingerReach) {
    bones = std::min(bones, kArmMaxBones);
    int hand[2] = {-1, -1};
    if (fingerReach > 0)
        for (int side = 0; side < 2; ++side) hand[side] = FindHandBone(palette, bones, bind, chains[side], side, tolerance);
    for (int i = 0; i < bones; ++i) {
        out[i] = BoneClass{};
        const float* b = palette + i * kArmBoneFloats;
        if (!Usable(b)) continue;
        for (int side = 0; side < 2; ++side) {
            const ArmChain& c = chains[side];
            if (!c.found) continue;
            const bool s = Near(MapPoint(b, bind.shoulder[side]), c.shoulder, tolerance);
            const bool e = Near(MapPoint(b, bind.elbow[side]), c.elbow, tolerance);
            const bool w = Near(MapPoint(b, bind.wrist[side]), c.wrist, tolerance);
            ArmPart part = ArmPart::None;
            if (e && w) part = ArmPart::Fore;
            else if (e) part = ArmPart::Upper; // shoulder and elbow, or an elbow helper: with the upper arm
            else if (s) part = ArmPart::Body;
            else if (w) part = ArmPart::Hand;
            if (part != ArmPart::None) {
                out[i] = {part, static_cast<std::uint8_t>(side)};
                break;
            }
        }
        if (out[i].part != ArmPart::None || fingerReach <= 0) continue;
        // A finger: it turns relative to its hand about a pivot near the wrist (or, with no hand
        // bone in this palette, maps the wrist within reach).
        for (int side = 0; side < 2 && out[i].part == ArmPart::None; ++side) {
            if (!chains[side].found) continue;
            const bool finger =
                hand[side] >= 0
                    ? TurnsAboutNear(palette + hand[side] * kArmBoneFloats, b, bind.wrist[side], fingerReach)
                    : math::Length(MapPoint(b, bind.wrist[side]) - chains[side].wrist) < fingerReach * 0.5f;
            if (finger) out[i] = {ArmPart::Hand, static_cast<std::uint8_t>(side)};
        }
    }
}

bool IsBodyPalette(const float* palette, int bones, const ArmBindJoints& bind, const ArmChain chains[2], float tolerance) {
    if (!palette || bones < 1 || !chains[0].found || !chains[1].found) return false;
    const float* r = palette;
    if (!Usable(r)) return false;
    // Rigid: unit, orthogonal axes (columns: the bind axes in the pass world).
    const math::Vec3 ax{r[0], r[4], r[8]}, ay{r[1], r[5], r[9]}, az{r[2], r[6], r[10]};
    for (const math::Vec3& a : {ax, ay, az})
        if (std::fabs(math::Length(a) - 1.0f) > 0.02f) return false;
    if (std::fabs(math::Dot(ax, ay)) > 0.02f || std::fabs(math::Dot(ay, az)) > 0.02f || std::fabs(math::Dot(az, ax)) > 0.02f)
        return false;
    for (int i = 0; i < std::min(bones, kBodyLeadBones); ++i) {
        const float* b = palette + i * kArmBoneFloats;
        if (!Usable(b)) continue;
        for (int side = 0; side < 2; ++side)
            if (Near(MapPoint(b, bind.shoulder[side]), chains[side].shoulder, tolerance) ||
                Near(MapPoint(b, bind.elbow[side]), chains[side].elbow, tolerance) ||
                Near(MapPoint(b, bind.wrist[side]), chains[side].wrist, tolerance))
                return false; // a mesh with an arm's part
    }
    if (ay.y < std::cos(kBodyMaxTiltDeg * 0.017453293f)) return false;
    const math::Vec3 neckBind = (bind.shoulder[0] + bind.shoulder[1]) * 0.5f;
    const math::Vec3 neck = (chains[0].shoulder + chains[1].shoulder) * 0.5f;
    return Near(MapPoint(r, neckBind), neck, kBodyNeckReach);
}

int FindHandBone(const float* palette, int bones, const ArmBindJoints& bind, const ArmChain& chain, int side,
                 float tolerance) {
    if (!chain.found || side < 0 || side > 1) return -1;
    bones = std::min(bones, kArmMaxBones);
    int best = -1;
    float bestDistance = tolerance;
    for (int i = 0; i < bones; ++i) {
        const float* b = palette + i * kArmBoneFloats;
        if (!Usable(b) || Near(MapPoint(b, bind.elbow[side]), chain.elbow, tolerance)) continue;
        const float d = math::Length(MapPoint(b, bind.wrist[side]) - chain.wrist);
        if (d < bestDistance) {
            bestDistance = d;
            best = i;
        }
    }
    return best;
}

namespace {
// The elbow of an arm from shoulder `s` reaching `wt` (lengths lu, lf), on the side of `hint`
// (else below); `slide`: how far the shoulder had to move along the line to reach.
struct Reach {
    math::Vec3 shoulder, elbow;
    float slide = 0;
};
Reach SolveReach(math::Vec3 s, math::Vec3 hint, math::Vec3 wt, float lu, float lf) {
    Reach r;
    const math::Vec3 d = wt - s;
    float len = math::Length(d);
    const math::Vec3 n = len > 1e-4f ? d * (1.0f / len) : math::Vec3{0, 0, -1};
    const float maxReach = (lu + lf) * 0.999f, minReach = std::fabs(lu - lf) + 0.01f;
    if (len > maxReach) r.slide = len - maxReach;
    else if (len < minReach) r.slide = len - minReach;
    r.shoulder = s + n * r.slide;
    len -= r.slide;
    const float a = (lu * lu - lf * lf + len * len) / (2.0f * len);
    const float h = std::sqrt(std::max(0.0f, lu * lu - a * a));
    math::Vec3 pole = hint - s;
    pole = pole - n * math::Dot(pole, n);
    if (math::Length(pole) < 1e-4f) {
        pole = math::Vec3{0, -1, 0} - n * math::Dot(math::Vec3{0, -1, 0}, n);
        if (math::Length(pole) < 1e-4f) pole = math::Cross(n, math::Vec3{1, 0, 0});
    }
    r.elbow = r.shoulder + n * a + math::Normalize(pole) * h;
    return r;
}

constexpr float kShoulderShift = 0.08f; // the most a shoulder follows the hand (m)

// `fraction` of a rotation matrix's turn (about the same axis).
Mat4 ScaledRotation(const Mat4& r, float fraction) {
    if (fraction >= 1.0f) {
        Mat4 out = r;
        out[3] = out[7] = out[11] = 0;
        return out;
    }
    const float trace = r[0] + r[5] + r[10];
    const float angle = std::acos(std::clamp((trace - 1.0f) * 0.5f, -1.0f, 1.0f));
    const math::Vec3 axis{r[9] - r[6], r[2] - r[8], r[4] - r[1]};
    const float len = math::Length(axis);
    if (angle < 1e-5f || len < 1e-6f) return Identity4();
    math::Pose p;
    p.orientation = math::FromAxisAngle(axis * (1.0f / len), angle * std::max(fraction, 0.0f));
    return FromPose(p);
}

// `fraction` of the smallest rotation taking `from` onto `to`.
Mat4 PartialRotation(math::Vec3 from, math::Vec3 to, float fraction) {
    const math::Vec3 a = math::Normalize(from), b = math::Normalize(to);
    const math::Vec3 axis = math::Cross(a, b);
    const float s = math::Length(axis);
    if (s < 1e-6f || fraction <= 0) return Identity4();
    math::Pose p;
    p.orientation = math::FromAxisAngle(axis * (1.0f / s), std::atan2(s, math::Dot(a, b)) * fraction);
    return FromPose(p);
}
} // namespace

ArmCorrections SolveArms(const ArmSolveInput& in) {
    ArmCorrections c;
    for (auto& side : c.part)
        for (auto& m : side) m = Identity4();
    // The neck's base: midway between the shoulders placed with the body (both arms needed).
    const bool both = in.chains[0].found && in.chains[1].found;
    const math::Vec3 neck =
        both ? (TransformPoint(in.body, TransformPoint(in.headFromPass, in.chains[0].shoulder)) +
                TransformPoint(in.body, TransformPoint(in.headFromPass, in.chains[1].shoulder))) * 0.5f
             : math::Vec3{};
    for (int side = 0; side < 2; ++side) {
        const ArmChain& ch = in.chains[side];
        if (!ch.found) continue;
        const math::Vec3 s0 = TransformPoint(in.headFromPass, ch.shoulder), e0 = TransformPoint(in.headFromPass, ch.elbow),
                         w0 = TransformPoint(in.headFromPass, ch.wrist);
        const float lu = math::Length(e0 - s0), lf = math::Length(w0 - e0);
        if (!(lu > 0.05f && lf > 0.05f && lu < 1.0f && lf < 1.0f)) continue; // not an arm
        const float blend = std::clamp(in.wristBlend[side], 0.0f, 1.0f);
        const math::Vec3 wt = in.hasWristTarget[side] ? w0 + (in.wristTarget[side] - w0) * blend : w0;
        // The torso with the body; then the clavicle turns about the neck's base so the shoulder
        // follows part of the hand's move from where BF4's rig holds it (up: a shrug; forward:
        // reaching), at most kShoulderShift.
        Mat4 torso = in.body;
        if (both && in.shoulderFollow > 0) {
            const math::Vec3 sb = TransformPoint(torso, s0);
            math::Vec3 shift = (wt - TransformPoint(torso, w0)) * std::min(in.shoulderFollow, 1.0f);
            const float len = math::Length(shift);
            if (len > kShoulderShift) shift = shift * (kShoulderShift / len);
            const Mat4 turn = RotationBetween(sb - neck, sb + shift - neck);
            torso = Mul(Translation(neck), Mul(turn, Mul(Translation(neck * -1.0f), torso)));
        }
        const math::Vec3 sb = TransformPoint(torso, s0), eb = TransformPoint(torso, e0);
        const Reach r = SolveReach(sb, eb, wt, lu, lf);
        const math::Vec3 ss = r.shoulder, e = r.elbow;
        const float slide = r.slide;
        const Mat4 body = Mul(Translation(ss - sb), torso);
        const math::Vec3 upperFrom = TransformPoint(body, e0) - ss, upperTo = e - ss;
        const Mat4 upper = Mul(Translation(ss), Mul(RotationBetween(upperFrom, upperTo), Mul(Translation(ss * -1.0f), body)));
        // The clavicle takes part of that swing about the shoulder joint: the skin blending the two
        // turns less (live: aiming up, the full swing smeared the shoulder).
        const Mat4 clavicle =
            in.clavicleShare > 0
                ? Mul(Translation(ss), Mul(PartialRotation(upperFrom, upperTo, std::min(in.clavicleShare, 1.0f)),
                                           Mul(Translation(ss * -1.0f), body)))
                : body;
        const Mat4 fore = Mul(Translation(e),
                              Mul(RotationBetween(TransformPoint(upper, w0) - e, wt - e), Mul(Translation(e * -1.0f), upper)));
        Mat4 hand = Identity4();
        if (in.hasWristTarget[side] && blend > 0) {
            const Mat4 turn = ScaledRotation(in.handTurn[side], blend);
            hand = Mul(Translation(wt), Mul(turn, Translation(w0 * -1.0f)));
        }
        if (!AllFinite(clavicle) || !AllFinite(upper) || !AllFinite(fore) || !AllFinite(hand)) continue;
        c.part[side][static_cast<int>(ArmPart::Body)] = clavicle;
        c.part[side][static_cast<int>(ArmPart::Upper)] = upper;
        c.part[side][static_cast<int>(ArmPart::Fore)] = fore;
        c.part[side][static_cast<int>(ArmPart::Hand)] = hand;
        c.solved[side] = true;
        c.slide[side] = slide;
        c.elbow[side] = e;
    }
    return c;
}

int ApplyArmCorrections(float* palette, int bones, const BoneClass* classes, const Mat4& headFromPass,
                        const ArmCorrections& corrections) {
    bones = std::min(bones, kArmMaxBones);
    const Mat4 back = InverseRigid(headFromPass);
    Mat4 in[2][5];
    bool use[2][5]{};
    for (int side = 0; side < 2; ++side)
        for (int part = 1; part < 5; ++part) {
            const Mat4& d = corrections.part[side][part];
            use[side][part] = corrections.solved[side] && !IsIdentity(d);
            if (use[side][part]) in[side][part] = Mul(back, Mul(d, headFromPass));
        }
    int changed = 0;
    for (int i = 0; i < bones; ++i) {
        const BoneClass& k = classes[i];
        const int part = static_cast<int>(k.part);
        if (k.part == ArmPart::None || k.side > 1 || !use[k.side][part]) continue;
        float* b = palette + i * kArmBoneFloats;
        const Mat4 s = Mul(in[k.side][part], FromRows(b));
        for (int r = 0; r < 3; ++r)
            for (int col = 0; col < 4; ++col) b[r * 4 + col] = s[r * 4 + col];
        ++changed;
    }
    return changed;
}

} // namespace fvr::bf4
