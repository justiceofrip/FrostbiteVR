#pragma once
// Arm IK for BF4's first-person rig with the gun in the hand (Bf4FirstPersonView.h). The whole
// rig follows the controller there, so the arms' shoulders swing with the gun (live: turned
// 60 degrees right, the support arm's sleeve crossed the lower view). Here the shoulders go back
// to the body, the hands stay on the gun, and the upper arms and forearms are turned to join
// them. Pure: it works on the bone palettes BF4 binds for its first-person skinned meshes (b0
// `boneVectors`: 3 float4 rows per bone, a rigid transform S from bind space to the pass's
// world), so the rewritten palette is all a draw needs; nothing in the game is written.
//
// Live (draw-probe palettes, sessions 58-61, prologue, 2026-10-20; scratch fp_rig.py): the arms'
// skeleton has these bind-space joints (the points two bones map alike in every captured
// pose): shoulders (+-0.1641, 1.4745, -0.0396), elbows (+-0.3729, 1.2601, -0.0125), wrists
// (+-0.5089, 1.1205, 0.1609); +x is the left arm. Twist bones fix their segment's two joints
// (the twist axis runs through them). Palettes order bones per mesh (the same bone sits at
// different indices in different meshes), and the weapon's palettes carry their own copies of
// the arm bones, posed alike or a few cm apart (reloads) or stale, so every entry is classified
// by the joints it maps:
//   upper arm: that arm's shoulder and elbow; forearm: elbow and wrist; body (clavicle):
//   shoulder only; hand: wrist only. Fingers and the weapon's bones map none: they stay put.

#include "Bf4Mat4.h"

#include <cstdint>

namespace fvr::bf4 {

struct ArmBindJoints {
    math::Vec3 shoulder[2], elbow[2], wrist[2]; // [0] left, [1] right; bind space
};
ArmBindJoints Bf4ArmBindJoints();

// One arm's joints in some space (found: all three located).
struct ArmChain {
    math::Vec3 shoulder{}, elbow{}, wrist{};
    bool found = false;
};

enum class ArmPart : std::uint8_t { None, Body, Upper, Fore, Hand };
struct BoneClass {
    ArmPart part = ArmPart::None;
    std::uint8_t side = 0;
};

constexpr int kArmMaxBones = 64;
constexpr int kArmBoneFloats = 12;          // 3 rows of 4
constexpr float kArmChainTolerance = 1e-3f; // entries "agree" on a joint within 1 mm
constexpr float kArmClassTolerance = 5e-3f; // ...an entry belongs to a chain within 5 mm

// The arms' joints as this palette poses them (pass world). An arm is found when a point at
// least two distinct entries map its elbow to is shared by an entry with a shoulder point
// (the upper arm) and by another with a wrist point (the forearm). Returns the arms found.
int FindArmChains(const float* palette, int bones, const ArmBindJoints& bind, ArmChain out[2]);

// Each entry's part against the chains: the joints it maps within `tolerance` of them. With
// `fingerReach` > 0, an entry mapping none but the wrist within that distance is a finger and
// moves with the hand (live: the fingers map it 0.6-10 cm from the hand's wrist).
void ClassifyBones(const float* palette, int bones, const ArmBindJoints& bind, const ArmChain chains[2],
                   float tolerance, BoneClass* out, float fingerReach = 0);
constexpr float kArmFingerReach = 0.10f;

// The solve, in head space (the render camera's: x right, y up, -z forward).
struct ArmSolveInput {
    ArmChain chains[2];      // pass world (found ones are solved)
    Mat4 headFromPass{};     // where the rig is drawn: move x first-person view
    Mat4 body{};             // head space: takes the rig's torso from the gun to the body
    math::Vec3 wristTarget[2]{}; // head space, when set (else the wrist stays where the gun puts it)
    bool hasWristTarget[2]{};
    // How far the wrist goes toward its target (0: stays on the gun, 1: at the target), so a hand
    // leaving or taking the gun moves there over a few frames instead of jumping.
    float wristBlend[2] = {1, 1};
    // With a wrist target: the hand also turns about the wrist by this rotation (head space,
    // scaled by the blend), e.g. as its controller turned since it let go of the gun.
    Mat4 handTurn[2] = {Identity4(), Identity4()};
    // How much of the hand's move (from where BF4's rig holds it, placed with the body) the
    // shoulder follows, up to 8 cm, the clavicle turning about the neck's base (the shoulders'
    // midpoint): raised hands shrug it, a reach brings it forward (0: it stays at the body).
    // With both arms only.
    float shoulderFollow = 0;
    // How much of the upper arm's swing (from BF4's pose to the solved one) the clavicle takes
    // about the shoulder joint, so the skin blending the two turns less (0: none).
    float clavicleShare = 0;
};
struct ArmCorrections {
    // Per arm and part: head-space transforms applied after headFromPass (identity for None
    // and, with no wrist target, Hand).
    Mat4 part[2][5]{};
    bool solved[2]{};
    float slide[2]{};        // how far the shoulder moved toward (+) or away from (-) the hand to reach
    math::Vec3 elbow[2]{};   // the solved elbow (head space)
};
// Shoulders at body x shoulder-as-drawn; the elbow on the side BF4's own elbow is, placed with
// the body; out of reach (beyond 99.9 % of the arm's length, or nearer than its fold) the
// shoulder slides along the shoulder-hand line.
ArmCorrections SolveArms(const ArmSolveInput& in);

// The palette with each classified entry S replaced by C S, C = headFromPass^-1 x D x
// headFromPass (D: its part's correction): drawn with headFromPass it lands at D x (as drawn).
// Returns how many entries changed.
int ApplyArmCorrections(float* palette, int bones, const BoneClass* classes, const Mat4& headFromPass,
                        const ArmCorrections& corrections);

} // namespace fvr::bf4
