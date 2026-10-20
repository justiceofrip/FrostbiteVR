#pragma once
// BF4 draws its first-person meshes (arms, weapon) in a pass of their own, inside the G-buffer
// pass, with their own view constants (live, 2026-10-20, research `drawprobe`): the render
// camera's rotation, a first-person camera position near the origin of a first-person space,
// and a projection at the game's own FOV (cot(gameFov/2); 45 degrees in the prologue) instead of
// the render's. The meshes are posed in camera space, so on screen the gun is fixed in place
// and drawn for the game's FOV, whatever the head-tracked render uses.
//
// Alternate eyes (3D): each frame is rendered from one eye, the eyes in turn. The eye is chosen
// here, on the render thread, per frame (OnPresent): every main view of the frame (its constants
// at the render's projection scale) and its first-person pass get a camera moved half the eye
// distance sideways, so all of a frame's passes agree. (Choosing it on the main thread per
// submit failed live: BF4 draws the first-person rig into the G-buffer before the frame's main
// view, and which submit a frame shows is not predictable, so the rig came out at the other eye.)
// BF4's deferred lighting still reconstructs with the head's camera: 3 cm off, not visible.
//
// This listener (Bf4ContextHooks.h) gives that pass constants of our own when BF4 binds them
// (b2, viewConstants, vertex and pixel stage): the render's projection (true size), and the
// view composed with a camera-space rigid transform M, so the whole first-person rig moves
// by M. BF4's own memory, buffers and arrays are never written: the bind proceeds with our
// buffer in that one slot. Unmatched binds pass through untouched.
//
// Arms from the body (gun in the hand, option; Bf4ArmIk.h): inside that pass each bone palette
// BF4 binds (vertex b0, at least kArmMinPaletteBytes) is read, the arms' shoulders, elbows and
// wrists found in it, and the bind proceeds with a copy of ours whose arm bones put the
// shoulders at the body (the head's yaw, level) and turn the upper arms and forearms to reach
// the hands, which stay on the gun. One solve per pass from the pass's reference arms (the first
// palette holding them, replaced by a later one that continues the previous pass's arms when
// the first does not: the weapon's palettes carry copies posed apart); every palette's bones
// are classified against it, so the same bone moves alike in every mesh.

#include "Bf4ArmIk.h"
#include "Bf4ContextHooks.h"
#include "Bf4Mat4.h"
#include "fvr/math/Vec.h"

#include <d3d11_1.h>
#include <wrl/client.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <map>
#include <mutex>
#include <string>

namespace fvr::bf4 {

// BF4's viewConstants (shader census, every vertex shader, b2): byte offsets.
struct ViewConstantsLayout {
    static constexpr std::uint32_t kSize = 368;
    static constexpr std::uint32_t kScreenSize = 16, kViewMatrix = 48, kProjMatrix = 112, kViewProjMatrix = 176,
                                   kCrViewProjMatrix = 240, kProjectionK = 304, kCameraPos = 320;
};

// Matrices as stored there: Bf4Mat4.h (row-major, column vectors, translation in column 3).
// Where BF4 holds the gun's grip at hip in its first-person camera space (live, prologue rifle:
// receiver at (0.068, -0.045, -0.287), barrel along -Z within 3 degrees; the grip ~6 cm below
// and behind it): the VR hand takes this place.
math::Pose GunGripAtRest();
// The grip at rest (GunGripAtRest) in BF4's right hand bone's bind space: live (sessions 61 and
// 63, prologue rifle at hip), BF4's first-person view x the hand's palette entry at rest, inverted,
// x the grip at rest. The right hand holds every gun's grip, so wherever BF4 holds the gun (the
// underbarrel launcher turned 28 deg, sprinting 62 deg, reloads 5-8 deg, other weapons) the
// grip it holds is that hand x this, idle within 0.1 cm and 0.3 deg of the rifle's grip at rest.
Mat4 GripInRightHandBind();

class FirstPersonView : public ContextHooks::Listener {
public:
    struct Frame {
        bool enabled = false;
        float gameFovY = 0;          // BF4's own vertical FOV (the first-person pass is projected with it)
        float renderKx = 0, renderKy = 0; // the head-tracked render's projection scale (cot terms)
        Mat4 transform = Identity4(); // camera-space transform M applied to the first-person meshes
        // Gun in the hand: M = hand x grip-at-rest^-1, the hand taken from the record whose
        // render rotation is the one the pass is drawn with (`transform` is then unused).
        bool handMode = false;
        struct Hand {
            float rotation[9]{}; // render camera axes as view rows
            float gameRotation[9]{}; // the game camera's, the same way
            math::Pose hand{};   // the right hand relative to the head (camera space)
            bool valid = false;
            math::Pose body{};   // arms from the body: the torso relative to the head, when bodyValid
            bool bodyValid = false; // (else the head's yaw, level: BodyInHead)
            math::Pose leftHand{}; // ...the left controller's grip relative to the head, when leftValid
            bool leftValid = false, leftFree = false; // ...and whether the hand is off the gun
        };
        std::array<Hand, 6> hands{};
        math::Pose gripAtRest = GunGripAtRest();
        // Keep the gun on the hand while BF4 animates it in camera space for aiming down
        // sights: the receiver's hip pose is held while the game's FOV is zoomed and the rig
        // moved back by the difference (hip bob, sway and recoil stay).
        bool holdThroughZoom = true;
        // Alternate eyes: this frame's camera at one eye (chosen per frame), `halfEye` metres
        // from the head along its right axis.
        bool alternateEyes = false;
        float halfEye = 0.032f;
        // Gun in the hand: the arms reach the gun from the body (above).
        bool armIk = false;
        // Gun in the hand: hold the grip BF4's right hand holds (GripInRightHandBind, from the
        // arms' palettes, smoothed) instead of the rifle's grip at rest.
        bool handAnchor = true;
        // Gun in the hand: BF4's own body mesh in this pass (IsBodyPalette) is drawn collapsed.
        // BF4 puts it out of view; moved with the gun it came into view (legs in front of the
        // player, seen 2026-10-20).
        bool hideBody = true;
    };
    struct Stats {
        std::uint64_t binds = 0;     // slot-2 binds seen
        std::uint64_t matched = 0;   // ... holding the first-person view
        std::uint64_t replaced = 0;  // ... given our constants
        std::uint64_t unreadable = 0; // ... whose contents could not be read
        std::uint64_t handMatched = 0, handNearest = 0, handMissing = 0; // hand mode: how the pose was found
        std::uint64_t byRender = 0, byGame = 0; // ...by the render camera's rotation or the game camera's
        float nearestDiff = 0;                  // the last search's smallest rotation difference
        std::uint64_t passes = 0, weaponFound = 0, zoomHeld = 0; // receiver tracking
        float weapon[3]{};                      // the receiver in camera space (last pass)
        float weaponShift = 0;                  // how far the hold moved it back (m)
        std::uint64_t shiftedViews = 0, shiftedFrames = 0; // alternate eyes: main views moved to the eye, frames
        float lastKy = 0;            // the matched pass's projection scale
        float cameraPos[3]{};        // the matched pass's first-person camera position
        // Arms from the body: palettes read in the pass, rewritten (bound as ours), bones moved,
        // passes with both arms solved, reference changes within a pass, and the last shoulder
        // slides.
        std::uint64_t armPalettes = 0, armRewritten = 0, armBones = 0, armSolved = 0, armSwitches = 0;
        float armSlide[2]{};
        // ...and the render thread's time in it (microseconds, totals): reading BF4's palettes
        // (its upload ring: write-combined memory), finding/solving/rewriting, uploading ours.
        // The grip from BF4's right hand: passes it was found in, binds that used it, and how far
        // the grip it gave was from the rifle's grip at rest (last bind: cm, degrees).
        std::uint64_t anchorFound = 0, anchorUsed = 0, anchorJumps = 0; // ...and hands not taken (jumps)
        // Recoil kicks: BF4's right hand stepping back along the gun faster than kKickSpeed
        // (live, session 75: idle and walking never reached 4 mm a pass at ~120 passes/s; ten
        // single shots made 17 such passes, a 2.5 s burst 50), at most one per kKickGap.
        std::uint64_t kicks = 0;
        std::uint64_t bodyHidden = 0; // BF4's body mesh binds drawn collapsed (hideBody)
        // Diagnostics: the last kicks' backward step (mm), sideways step (mm) and pass gap (ms).
        float kickLog[4][3]{};
        float anchorCm = 0, anchorDeg = 0;
        double armReadUs = 0, armWorkUs = 0, armUploadUs = 0;
        bool armStreamingReads = false; // the reads use SSE4.1 streaming loads
    };

    ~FirstPersonView() override;
    bool Install(ID3D11DeviceContext* immediate);
    void Remove();
    bool Installed() const { return installed_; }
    void SetFrame(const Frame& frame);
    Stats GetStats() const;
    std::string Describe() const;

    // Pure: is this viewConstants block the first-person pass's (perspective, projected at the
    // game's FOV, not at the render's)?
    static bool IsFirstPersonBlock(const std::uint8_t* block, float gameFovY, float renderKy);
    // Pure, gun in the hand: the hand of the first-person block among the frame's records, or
    // null. The block is the first-person pass's when it is a rigid perspective view not
    // projected at the render's scale whose rotation is exactly a recorded game camera's (live:
    // BF4's first-person view takes the game camera's rotation; its FOV differs with aiming
    // down sights, so the FOV alone does not tell it).
    // Exact (< 2e-3) or, failing that, the nearest within kShakeTolerance: BF4 adds recoil and
    // camera shake to that view after the submit (live: frames drawn while firing showed its own
    // gun until this tolerance). `exact` reports which.
    static constexpr float kShakeTolerance = 0.08f; // matrix elements, ~4.5 deg
    // `shaped`: the block has the first-person pass's shape (whatever its rotation).
    static const Frame::Hand* FirstPersonHand(const std::uint8_t* block, const Frame& frame, bool* exact = nullptr,
                                              bool* shaped = nullptr);
    // Pure: the block with the render's projection and its view composed with M (viewProj,
    // camera-relative viewProj, projection terms and camera position kept consistent).
    static void Rewrite(std::uint8_t* block, float renderKx, float renderKy, const Mat4& m);
    // Pure: the hand of the record whose render (or game) camera rotation is the block's
    // (exact match, else the nearest within 0.05, else none). `how`: 0 exact, 1 nearest, 2
    // none; `game`: matched by the game camera's rotation; `diff`: the smallest difference.
    static const Frame::Hand* HandFor(const std::uint8_t* block, const Frame& frame, int& how, bool& game, float& diff);

    void OnMap(ID3D11DeviceContext*, ID3D11Resource*, D3D11_MAP, void* data) override;
    void OnUpdate(ID3D11DeviceContext*, ID3D11Resource*, const D3D11_BOX*, const void* source) override;
    void OnBindConstants(ID3D11DeviceContext*, ContextHooks::Stage, UINT startSlot, UINT numBuffers, ID3D11Buffer** buffers,
                         UINT* first, UINT* count) override;
    void OnDraw(ID3D11DeviceContext*, const char* kind, UINT count, UINT instances) override;
    // Pure: a mesh transform that can be the weapon's (rigid, unscaled, row 3 = 0 0 0 1) within
    // `radius` of the first-person camera.
    static bool WeaponPart(const Mat4& world, const float camera[3], float radius = 1.5f);
    // Pure: is this block a main view (rigid perspective at the render's projection scale)?
    static bool IsMainViewBlock(const std::uint8_t* block, float renderKy);
    // Pure: the camera-space move from the head's camera to eye 0 (left) or 1 (right).
    static Mat4 EyeShift(int eye, float halfEye);
    // Pure: the body frame in the head's camera space from the render camera's axes (view
    // rows in BF4's y-up world): the head's yaw, level, at the eye.
    static Mat4 BodyInHead(const float rows[9]);
    static constexpr std::uint32_t kArmMinPaletteBytes = 1024; // >= 21 bones: a skinned mesh's palette
    static constexpr float kArmNearCamera = 0.6f;      // a palette's arms count when the shoulder is this near the camera (m)
    static constexpr float kArmShoulderFollow = 0.35f; // the shoulder's share of the hand's move (ArmSolveInput)
    static constexpr float kArmClavicleShare = 0.5f;   // the clavicle's share of the upper arm's swing
    static constexpr float kArmWristBehindGrip = 0.06f; // a controller's grip to BF4's wrist joint, back along it (m)
    static constexpr float kArmHandMoveSeconds = 0.15f; // a hand leaving or taking the gun gets there in this time
    static constexpr float kAnchorSeconds = 0.06f;      // the right hand's grip is smoothed over this (s)
    static constexpr int kAnchorStalePasses = 30;       // without it this many passes: back to the grip at rest
    static constexpr float kKickSpeed = 0.45f;          // m/s back along the gun (4 mm in a 120 Hz pass)
    static constexpr float kKickGap = 0.05f;            // s between two kicks (full auto: up to 20 a second)
    static constexpr float kKickMinGap = 0.004f;        // s: steps are measured over at least this
    static constexpr float kKickMaxStep = 0.03f;        // m: a longer step is a jump, not a kick
    static constexpr float kAnchorJump = 0.10f;         // m: a hand this far from the smoothed one is not taken...
    static constexpr int kAnchorJumpPasses = 10;        // ...for this many passes in a row
    // Alternate eyes, each Present (render thread): the eye the frame just drawn was rendered
    // from (-1: none moved), read before OnPresent; OnPresent then turns to the other eye.
    int FrameEye() const;
    void OnPresent();
    // Second view: BF4 renders once per eye in a frame; each render's first-person pass (and
    // with `mainViews` its main views, when the camera itself is not at the eye) is seen from
    // `eye` (0 left, 1 right; half eye distance `Frame::halfEye`). -1: back to the frame's own
    // eye (alternate eyes or none).
    void SetCallEye(int eye, bool mainViews = true) {
        callEyeMain_.store(mainViews);
        callEye_.store(eye);
    }
    // Draws seen on the immediate context so far (second view: draws per render).
    std::uint64_t Draws() const { return draws_.load(); }
    unsigned long DrawThread() const { return drawThread_.load(); } // the thread of the latest draw
    // Research: the call stack at draw number `n` of the next frame (counted from a Present).
    void RequestDrawStack(std::uint32_t n) { drawStackAt_.store(n); }
    bool TakeDrawStack(std::string& out);
    // Developer research: first-person draws with this index count are drawn with an all-zero
    // palette (collapsed, unseen), to tell the pass's meshes apart. 0: none.
    void SetHideIndexCount(UINT count) { hideCount_.store(count); }
    // Developer research: in first-person draws with this index count (gun in the hand), the
    // palette entries [lo, hi) are zeroed (vertices using them collapse toward the origin), to
    // tell which bones a mesh's parts use. 0: none.
    void SetMaskIndexCount(UINT count, int lo, int hi) {
        maskLo_.store(lo);
        maskHi_.store(hi);
        maskCount_.store(count);
    }

private:
    // The block (rewritten) bound in place of BF4's, in slot i: false when ours could not be filled.
    bool Upload(ID3D11DeviceContext* context, const std::uint8_t* block, ID3D11Buffer** buffers, UINT i, UINT* first,
                UINT* count);
    // Arms from the body: a palette bound at b0 in the pass, rewritten and bound as ours.
    void RewriteArms(ID3D11DeviceContext* context, const void* mapped, std::uint32_t size, ID3D11Buffer** buffers,
                     UINT* first, UINT* count);
    struct Source {
        void* mapped = nullptr;         // what Map returned last (BF4's ring buffer is mapped no-overwrite)
        std::uint32_t size = 0;
    };
    bool installed_ = false;
    ID3D11DeviceContext* immediate_ = nullptr;
    Microsoft::WRL::ComPtr<ID3D11Buffer> ours_;
    std::array<std::uint8_t, ViewConstantsLayout::kSize> lastSource_{};
    bool haveLast_ = false;
    mutable std::mutex mutex_;
    Frame frame_;
    Stats stats_;
    std::map<const void*, Source> sources_; // constant buffers by the address Map gave for them
    // Receiver tracking over the first-person pass (render thread only): the largest rigid
    // mesh drawn in it is the weapon's receiver.
    void EndPass();
    bool inPass_ = false;
    int eye_ = 0;               // alternate eyes: this frame's (render thread)
    bool shiftedThisFrame_ = false;
    Mat4 passView_{};
    float passCamera_[3]{};
    Mat4 pendingWorld_{};
    bool havePending_ = false;
    Mat4 bestWorld_{};
    UINT bestCount_ = 0;
    Mat4 weapon_{};          // the receiver in camera space, last completed pass
    bool weaponValid_ = false;
    Mat4 weaponRest_{};      // ...at hip, held while zoomed
    bool restValid_ = false;
    float hipFov_ = 0;       // the widest game FOV seen (hip)
    int hipPasses_ = 0;      // passes at hip FOV in a row
    // Arms from the body (render thread only).
    Microsoft::WRL::ComPtr<ID3D11Buffer> oursPalette_;
    Microsoft::WRL::ComPtr<ID3D11Buffer> zeroPalette_; // all zero: a skinned draw bound with it collapses
    bool hideBodyPass_ = false;
    std::atomic<UINT> hideCount_{0};
    std::atomic<int> callEye_{-1};
    std::atomic<bool> callEyeMain_{true};
    std::atomic<std::uint64_t> draws_{0};
    std::atomic<unsigned long> drawThread_{0};
    std::atomic<std::uint32_t> frameDraws_{0}, drawStackAt_{0};
    std::string drawStack_; // under mutex_
    bool drawStackReady_ = false;
    std::atomic<UINT> maskCount_{0};
    std::atomic<int> maskLo_{0}, maskHi_{0};
    Microsoft::WRL::ComPtr<ID3D11Buffer> maskPalette_;
    std::array<std::uint8_t, 4096> lastPalette_{}; // research: the palette last bound in the pass
    std::uint32_t lastPaletteBytes_ = 0;
    // Research overrides bind b0 for one draw; BF4's binding (recorded at its bind) is put back
    // before the next draw unless BF4 binds b0 again first.
    void ResearchBind(ID3D11DeviceContext* context, ID3D11Buffer* buffer);
    Microsoft::WRL::ComPtr<ID3D11Buffer> gameB0_;
    UINT gameB0First_ = 0, gameB0Count_ = 0;
    bool gameB0Ranged_ = false, researchB0_ = false, researchBinding_ = false;
    ArmBindJoints armBind_ = Bf4ArmBindJoints();
    bool palettePass_ = false; // this pass reads palettes (gun in the hand)
    bool armPass_ = false;     // ...and rewrites them (arms from the body)
    bool armSolved_ = false;   // armCorr_ is this pass's
    ArmChain armRef_[2]{}, armPrev_[2]{}; // this pass's reference arms, the previous pass's
    // The arms each palette of the pass poses (within 1 mm: one candidate) and how many palettes
    // did: the most voted is the reference (ties: the nearest to the previous pass's).
    struct ArmCandidate {
        ArmChain chain;
        int votes = 0;
    };
    static constexpr int kArmCandidates = 8;
    ArmCandidate armVotes_[2][kArmCandidates]{};
    int armCandidates_[2]{};
    ArmCorrections armCorr_{};
    Mat4 armHeadFromPass_{}, armBody_{};
    math::Vec3 armLeftTarget_{};  // the free left hand's wrist (head space)
    Mat4 armLeftTurn_ = Identity4(); // ...and its turn since it let go (head space)
    float armLeftBlend_ = 0;      // 0 on the gun .. 1 at the controller
    // The left controller's turn relative to the gun while it steadies it (two-handed): the
    // free hand keeps that relation, so it turns as its controller turns from the gun.
    Mat4 armLeftGrip_ = Identity4();
    std::int64_t armLastPassTick_ = 0;
    // The grip from BF4's right hand (GripInRightHandBind): this pass's hand bone (first-person
    // camera space), the smoothed one in use, and for how many passes it was not found.
    Mat4 anchorPass_{}, anchor_{}, anchorLastRaw_{};
    // This pass's right hand bones (first-person camera space) with the right arm each palette
    // posed: at the pass's end the one of the pass's arm nearest the hand in use is taken.
    struct AnchorCandidate {
        Mat4 hand;
        ArmChain arm;
    };
    static constexpr int kAnchorCandidates = 8;
    AnchorCandidate anchorCandidates_[kAnchorCandidates]{};
    int anchorCandidateCount_ = 0;
    bool anchorPassValid_ = false, anchorValid_ = false;
    int anchorMisses_ = 0, anchorJumps_ = 0;
    std::int64_t anchorTick_ = 0, kickTick_ = 0, kickRawTick_ = 0;
};

} // namespace fvr::bf4
