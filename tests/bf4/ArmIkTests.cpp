// Arm IK for the first-person rig (Bf4ArmIk.h): chains found by the joints bones share,
// entries classified, shoulders moved to the body while the hands stay on the gun.
#include "TestHarness.h"

#include "../../src/games/bf4/Bf4ArmIk.h"

#include <array>
#include <cmath>
#include <cstring>
#include <vector>

using namespace fvr;
using namespace fvr::bf4;

namespace {

Mat4 Rotation(math::Vec3 axis, float radians) {
    math::Pose p;
    p.orientation = math::FromAxisAngle(axis, radians);
    return FromPose(p);
}

// A rigid S with S(j) = p, rotated by r about j.
Mat4 About(math::Vec3 j, math::Vec3 p, const Mat4& r) { return Mul(Translation(p), Mul(r, Translation(j * -1.0f))); }

// A rigid S with S(ja) = pa and S(jb) = pb (equal lengths), twisted about that segment.
Mat4 Segment(math::Vec3 ja, math::Vec3 jb, math::Vec3 pa, math::Vec3 pb, float twist) {
    const Mat4 align = RotationBetween(jb - ja, pb - pa);
    return About(ja, pa, Mul(Rotation(pb - pa, twist), align));
}

void Put(std::vector<float>& palette, const Mat4& m) {
    for (int k = 0; k < 12; ++k) palette.push_back(m[k]);
}

bool NearV(math::Vec3 a, math::Vec3 b, float eps = 1e-4f) { return math::Length(a - b) <= eps; }

math::Vec3 Map(const std::vector<float>& palette, int bone, math::Vec3 p) {
    const float* b = palette.data() + bone * 12;
    return {b[0] * p.x + b[1] * p.y + b[2] * p.z + b[3], b[4] * p.x + b[5] * p.y + b[6] * p.z + b[7],
            b[8] * p.x + b[9] * p.y + b[10] * p.z + b[11]};
}

struct Rig {
    std::vector<float> palette;
    math::Vec3 s[2], e[2], w[2]; // posed joints (pass world)
    // Bones: 0 spine, per side (base 1 / 8): +0 clavicle, +1 upper, +2 upper twist, +3 forearm,
    // +4 forearm twist, +5 hand, +6 finger; 15 a duplicate of the spine, 16 a weapon bone,
    // 17 a weapon copy of the left upper arm 5 cm off.
    static constexpr int kBones = 18;
};

Rig MakeRig() {
    const ArmBindJoints bind = Bf4ArmBindJoints();
    Rig rig;
    const Mat4 spine = About({0, 1.3f, 0}, {0.02f, 1.9f, 0.05f}, Rotation({0, 1, 0}, 0.3f));
    Put(rig.palette, spine);
    for (int side = 0; side < 2; ++side) {
        const float x = side == 0 ? -1.0f : 1.0f; // the left arm on the camera's left
        const float lu = math::Length(bind.elbow[side] - bind.shoulder[side]);
        const float lf = math::Length(bind.wrist[side] - bind.elbow[side]);
        rig.s[side] = {0.12f * x, 1.95f, 0.05f};
        rig.e[side] = rig.s[side] + math::Normalize({0.1f * x, -0.6f, -0.8f}) * lu;
        rig.w[side] = rig.e[side] + math::Normalize({-0.3f * x, 0.2f, -0.93f}) * lf;
        Put(rig.palette, About(bind.shoulder[side], rig.s[side], Rotation({0, 0, 1}, 0.35f * x)));
        Put(rig.palette, Segment(bind.shoulder[side], bind.elbow[side], rig.s[side], rig.e[side], 0.0f));
        Put(rig.palette, Segment(bind.shoulder[side], bind.elbow[side], rig.s[side], rig.e[side], 0.5f));
        Put(rig.palette, Segment(bind.elbow[side], bind.wrist[side], rig.e[side], rig.w[side], 0.2f));
        Put(rig.palette, Segment(bind.elbow[side], bind.wrist[side], rig.e[side], rig.w[side], 0.9f));
        const Mat4 hand = About(bind.wrist[side], rig.w[side], Rotation({1, 0.3f, 0}, 0.6f));
        Put(rig.palette, hand);
        // A finger: turned about a knuckle 9 cm past the wrist (bind space).
        const math::Vec3 knuckle = bind.wrist[side] + math::Vec3{0.09f * -x, 0, 0};
        Put(rig.palette, Mul(hand, About(knuckle, knuckle, Rotation({0, 0, 1}, 0.4f))));
    }
    Put(rig.palette, spine);
    Put(rig.palette, About({0, 0, 0}, {0.1f, 1.7f, -0.4f}, Rotation({1, 0, 0}, 0.2f)));
    std::vector<float> copy(rig.palette.begin() + 2 * 12, rig.palette.begin() + 3 * 12);
    copy[3] += 0.05f;
    rig.palette.insert(rig.palette.end(), copy.begin(), copy.end());
    return rig;
}

} // namespace

TEST_CASE(ArmIkFindsBothArmsAndClassifiesEveryBone) {
    const ArmBindJoints bind = Bf4ArmBindJoints();
    const Rig rig = MakeRig();
    ArmChain chains[2];
    CHECK(FindArmChains(rig.palette.data(), Rig::kBones, bind, chains) == 2);
    for (int side = 0; side < 2; ++side)
        CHECK(chains[side].found && NearV(chains[side].shoulder, rig.s[side]) && NearV(chains[side].elbow, rig.e[side]) &&
              NearV(chains[side].wrist, rig.w[side]));
    BoneClass cls[Rig::kBones];
    ClassifyBones(rig.palette.data(), Rig::kBones, bind, chains, kArmClassTolerance, cls);
    const ArmPart expected[7] = {ArmPart::Body, ArmPart::Upper, ArmPart::Upper, ArmPart::Fore,
                                 ArmPart::Fore, ArmPart::Hand,  ArmPart::None};
    for (int side = 0; side < 2; ++side)
        for (int k = 0; k < 7; ++k) {
            const BoneClass c = cls[1 + side * 7 + k];
            CHECK(c.part == expected[k] && (c.part == ArmPart::None || c.side == side));
        }
    CHECK(cls[0].part == ArmPart::None && cls[15].part == ArmPart::None && cls[16].part == ArmPart::None);
    CHECK(cls[17].part == ArmPart::None); // the weapon's copy 5 cm off is not the arm
}

TEST_CASE(ArmIkNeedsARealChain) {
    const ArmBindJoints bind = Bf4ArmBindJoints();
    const Rig rig = MakeRig();
    // Hands and fingers only (the right-hand mesh): no chain of its own.
    std::vector<float> hands(rig.palette.begin() + 13 * 12, rig.palette.begin() + 15 * 12);
    ArmChain chains[2];
    CHECK(FindArmChains(hands.data(), 2, bind, chains) == 0 && !chains[0].found && !chains[1].found);
    // Duplicated entries agree on every point but make no chain.
    std::vector<float> twins;
    for (int k = 0; k < 4; ++k) twins.insert(twins.end(), rig.palette.begin(), rig.palette.begin() + 12);
    CHECK(FindArmChains(twins.data(), 4, bind, chains) == 0);
    // Garbage (stale ring-buffer data) is skipped.
    std::vector<float> junk(12 * 3, 1e30f);
    CHECK(FindArmChains(junk.data(), 3, bind, chains) == 0);
    // The hand mesh is still classified against another mesh's chains.
    ArmChain full[2];
    FindArmChains(rig.palette.data(), Rig::kBones, bind, full);
    BoneClass cls[2];
    ClassifyBones(hands.data(), 2, bind, full, kArmClassTolerance, cls);
    CHECK(cls[0].part == ArmPart::Hand && cls[0].side == 1 && cls[1].part == ArmPart::None);
}

TEST_CASE(ArmIkMovesShouldersToTheBodyAndKeepsHandsOnTheGun) {
    const ArmBindJoints bind = Bf4ArmBindJoints();
    Rig rig = MakeRig();
    ArmSolveInput in;
    FindArmChains(rig.palette.data(), Rig::kBones, bind, in.chains);
    // The rig as drawn: the first-person view (camera at the pass origin's eye) and the gun's move.
    in.headFromPass = Mul(Mul(Translation({0.05f, -0.1f, -0.05f}), Rotation({0, 1, 0}, -0.6f)),
                          Translation({0, -1.9f, 0}));
    // The torso goes back to the body: the gun was turned ~35 degrees right of it.
    in.body = Rotation({0, 1, 0}, 0.6f);
    const ArmCorrections c = SolveArms(in);
    CHECK(c.solved[0] && c.solved[1]);
    BoneClass cls[Rig::kBones];
    ClassifyBones(rig.palette.data(), Rig::kBones, bind, in.chains, kArmClassTolerance, cls);
    const std::vector<float> before = rig.palette;
    const int changed = ApplyArmCorrections(rig.palette.data(), Rig::kBones, cls, in.headFromPass, c);
    CHECK(changed == 10); // clavicle, 2 upper, 2 fore per arm
    for (int side = 0; side < 2; ++side) {
        const int b = 1 + side * 7;
        auto head = [&](int bone, math::Vec3 p) { return TransformPoint(in.headFromPass, Map(rig.palette, bone, p)); };
        const math::Vec3 wrist = TransformPoint(in.headFromPass, rig.w[side]);
        // At the body, slid along the line to the hand when that is out of reach.
        const math::Vec3 atBody = TransformPoint(in.body, TransformPoint(in.headFromPass, rig.s[side]));
        const math::Vec3 shoulder = atBody + math::Normalize(wrist - atBody) * c.slide[side];
        // Joined at every joint, the shoulder at the body, the wrist where the gun put it.
        CHECK(NearV(head(b + 0, bind.shoulder[side]), shoulder) && NearV(head(b + 1, bind.shoulder[side]), shoulder) &&
              NearV(head(b + 2, bind.shoulder[side]), shoulder));
        CHECK(NearV(head(b + 1, bind.elbow[side]), c.elbow[side]) && NearV(head(b + 2, bind.elbow[side]), c.elbow[side]) &&
              NearV(head(b + 3, bind.elbow[side]), c.elbow[side]) && NearV(head(b + 4, bind.elbow[side]), c.elbow[side]));
        CHECK(NearV(head(b + 3, bind.wrist[side]), wrist) && NearV(head(b + 4, bind.wrist[side]), wrist) &&
              NearV(head(b + 5, bind.wrist[side]), wrist));
        CHECK(c.slide[side] >= 0.0f && c.slide[side] < 0.05f);
        // The hand and its finger are untouched (the gun holds them).
        CHECK(std::memcmp(&rig.palette[(b + 5) * 12], &before[(b + 5) * 12], 2 * 12 * sizeof(float)) == 0);
        // The elbow stays on BF4's side: below the shoulder-wrist line, as drawn.
        const math::Vec3 drawnElbow = TransformPoint(in.body, TransformPoint(in.headFromPass, rig.e[side]));
        const math::Vec3 n = math::Normalize(wrist - shoulder);
        auto off = [&](math::Vec3 p) { const math::Vec3 d = p - shoulder; return d - n * math::Dot(d, n); };
        CHECK(math::Dot(off(c.elbow[side]), off(drawnElbow)) > 0);
    }
    // Spine, its duplicate, the weapon bone and the weapon's copy are left alone.
    for (int bone : {0, 15, 16, 17}) CHECK(std::memcmp(&rig.palette[bone * 12], &before[bone * 12], 12 * sizeof(float)) == 0);
}

TEST_CASE(ArmIkReproducesTheRigWhenTheGunIsAtRest) {
    const ArmBindJoints bind = Bf4ArmBindJoints();
    Rig rig = MakeRig();
    ArmSolveInput in;
    FindArmChains(rig.palette.data(), Rig::kBones, bind, in.chains);
    in.headFromPass = Translation({0, -1.9f, 0});
    in.body = Identity4(); // the gun where BF4 holds it relative to the body
    const ArmCorrections c = SolveArms(in);
    BoneClass cls[Rig::kBones];
    ClassifyBones(rig.palette.data(), Rig::kBones, bind, in.chains, kArmClassTolerance, cls);
    const std::vector<float> before = rig.palette;
    ApplyArmCorrections(rig.palette.data(), Rig::kBones, cls, in.headFromPass, c);
    for (int side = 0; side < 2; ++side) CHECK(NearV(c.elbow[side], TransformPoint(in.headFromPass, rig.e[side]), 1e-3f));
    float worst = 0;
    for (std::size_t k = 0; k < before.size(); ++k) worst = std::fmax(worst, std::fabs(rig.palette[k] - before[k]));
    CHECK(worst < 2e-3f);
}

TEST_CASE(ArmIkShouldersFollowTheArmsPartway) {
    const ArmBindJoints bind = Bf4ArmBindJoints();
    const Rig base = MakeRig();
    ArmSolveInput in;
    FindArmChains(base.palette.data(), Rig::kBones, bind, in.chains);
    in.headFromPass = Translation({0, -1.9f, 0});
    in.body = Identity4();
    for (int side = 0; side < 2; ++side) { // the hands raised 20 cm (the gun aimed up)
        in.wristTarget[side] = TransformPoint(in.headFromPass, base.w[side]) + math::Vec3{0, 0.2f, 0};
        in.hasWristTarget[side] = true;
    }
    const ArmCorrections still = SolveArms(in);
    in.shoulderFollow = 0.5f;
    in.clavicleShare = 0.5f; // the clavicle turns with the arm too; still joined at the shoulder
    const ArmCorrections follow = SolveArms(in);
    CHECK(still.solved[0] && still.solved[1] && follow.solved[0] && follow.solved[1]);
    for (int side = 0; side < 2; ++side) {
        Rig rig = base;
        BoneClass cls[Rig::kBones];
        ClassifyBones(rig.palette.data(), Rig::kBones, bind, in.chains, kArmClassTolerance, cls);
        ApplyArmCorrections(rig.palette.data(), Rig::kBones, cls, in.headFromPass, follow);
        const int b = 1 + side * 7;
        auto head = [&](int bone, math::Vec3 p) { return TransformPoint(in.headFromPass, Map(rig.palette, bone, p)); };
        const math::Vec3 shoulder = head(b + 1, bind.shoulder[side]);
        // Raised with the arm, still joined to the clavicle, the elbow and the raised wrist.
        CHECK(shoulder.y > TransformPoint(in.headFromPass, base.s[side]).y + 0.01f);
        CHECK(NearV(head(b + 0, bind.shoulder[side]), shoulder));
        CHECK(NearV(head(b + 1, bind.elbow[side]), follow.elbow[side]) && NearV(head(b + 3, bind.elbow[side]), follow.elbow[side]));
        CHECK(NearV(head(b + 3, bind.wrist[side]), in.wristTarget[side]) && NearV(head(b + 5, bind.wrist[side]), in.wristTarget[side]));
    }
}

TEST_CASE(ArmIkAFreeHandTakesItsFingers) {
    // The left hand off the gun goes to its controller; its fingers (no joint of the chain, the
    // wrist within reach) go with it. Halfway through the blend, the wrist is halfway there.
    const ArmBindJoints bind = Bf4ArmBindJoints();
    const Rig base = MakeRig();
    ArmSolveInput in;
    FindArmChains(base.palette.data(), Rig::kBones, bind, in.chains);
    in.headFromPass = Translation({0, -1.9f, 0});
    in.body = Identity4();
    const math::Vec3 wrist = TransformPoint(in.headFromPass, base.w[0]);
    in.wristTarget[0] = wrist + math::Vec3{-0.15f, -0.1f, 0.05f};
    in.hasWristTarget[0] = true;
    BoneClass cls[Rig::kBones];
    ClassifyBones(base.palette.data(), Rig::kBones, bind, in.chains, kArmClassTolerance, cls);
    CHECK(cls[7].part == ArmPart::None); // without finger reach the finger stays
    ClassifyBones(base.palette.data(), Rig::kBones, bind, in.chains, kArmClassTolerance, cls, kArmFingerReach);
    CHECK(cls[7].part == ArmPart::Hand && cls[7].side == 0 && cls[14].part == ArmPart::Hand && cls[14].side == 1);
    CHECK(cls[0].part == ArmPart::None && cls[16].part == ArmPart::None && cls[17].part == ArmPart::None);
    for (float blend : {1.0f, 0.5f}) {
        in.wristBlend[0] = blend;
        const ArmCorrections c = SolveArms(in);
        CHECK(c.solved[0]);
        Rig rig = base;
        ApplyArmCorrections(rig.palette.data(), Rig::kBones, cls, in.headFromPass, c);
        auto head = [&](const Rig& r, int bone, math::Vec3 p) { return TransformPoint(in.headFromPass, Map(r.palette, bone, p)); };
        const math::Vec3 target = wrist + (in.wristTarget[0] - wrist) * blend;
        CHECK(NearV(head(rig, 4, bind.wrist[0]), target) && NearV(head(rig, 6, bind.wrist[0]), target));
        // The finger keeps its place on the hand: moved as the hand moved.
        const math::Vec3 tip = bind.wrist[0] + math::Vec3{-0.12f, 0, 0};
        CHECK(NearV(head(rig, 7, tip) - head(base, 7, tip), target - wrist));
        // The right hand stays on the gun.
        CHECK(NearV(head(rig, 13, bind.wrist[1]), TransformPoint(in.headFromPass, base.w[1])));
    }
}

TEST_CASE(ArmIkSlidesTheShoulderWhenTheGunIsOutOfReach) {
    const ArmBindJoints bind = Bf4ArmBindJoints();
    Rig rig = MakeRig();
    ArmSolveInput in;
    FindArmChains(rig.palette.data(), Rig::kBones, bind, in.chains);
    in.headFromPass = Translation({0, -1.9f, 0});
    in.body = Translation({0, 0, 0.6f}); // the body 60 cm behind the gun: beyond the arms' reach
    const ArmCorrections c = SolveArms(in);
    CHECK(c.solved[0] && c.solved[1] && c.slide[0] > 0.1f && c.slide[1] > 0.1f);
    BoneClass cls[Rig::kBones];
    ClassifyBones(rig.palette.data(), Rig::kBones, bind, in.chains, kArmClassTolerance, cls);
    ApplyArmCorrections(rig.palette.data(), Rig::kBones, cls, in.headFromPass, c);
    for (int side = 0; side < 2; ++side) {
        const int b = 1 + side * 7;
        const float lu = math::Length(bind.elbow[side] - bind.shoulder[side]);
        const float lf = math::Length(bind.wrist[side] - bind.elbow[side]);
        const math::Vec3 s = TransformPoint(in.headFromPass, Map(rig.palette, b + 1, bind.shoulder[side]));
        const math::Vec3 w = TransformPoint(in.headFromPass, Map(rig.palette, b + 3, bind.wrist[side]));
        CHECK(NearV(w, TransformPoint(in.headFromPass, rig.w[side])));
        CHECK(fvrtest::Near(math::Length(w - s), (lu + lf) * 0.999f, 1e-3f));
        CHECK(NearV(TransformPoint(in.headFromPass, Map(rig.palette, b + 0, bind.shoulder[side])), s));
    }
}
