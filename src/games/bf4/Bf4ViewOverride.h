#pragma once
// Head-tracked main view for bf4.exe build 179547 (RE task R3, docs/BF4-CAMERA-RE.md).
//
// The game submits each frame's views through GameRenderer vtable slot 5
// (renderer, float, float, viewCount, viewBlocks*, sync*, ptr), which copies them into the
// renderer before returning. The override never patches game code: it points the renderer
// object at a copy of its vtable whose slot 5 is ours (one 8-byte heap write, restored
// exactly). Per call it writes the head-tracked transform into view block 0, calls the
// original slot 5, then restores the block's bytes (a temporary override).
//
// Portable C++: tests drive it with a fake renderer object and vtable in-process.
#include "../../engines/frostbite/Fb3CameraMath.h"
#include "Bf4WorldView.h"

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <mutex>

namespace fvr::bf4 {

// Layout facts from the static analysis of build 179547.
inline constexpr std::size_t kRendererVtableSlots = 23;  // slot 23 is no longer code
inline constexpr std::size_t kRendererSubmitSlot = 5;    // +0x83F0E0
inline constexpr std::size_t kViewTransformBytes = 0x90; // setter +0xB8A0A0 block
inline constexpr std::size_t kViewCopyOffset = 0x40;     // callers store the transform twice
inline constexpr std::size_t kViewDirtyOffset = 0xE0;
inline constexpr std::uint32_t kViewDirtyBits = 0xB;     // what the setter ORs in
// Projection fields of the same view struct (FOV setter +0xB89EE0, aspect setter +0xB89D60,
// both read by the view setup +0x848B00 after it copies the block): vertical FOV in radians,
// aspect, and the horizontal FOV the setters derive as 2*atan(tan(fov/2)*aspect).
inline constexpr std::size_t kViewFovOffset = 0xB4;
// The view's "default" FOV: view update bit 4 (+0xB8C910) derives
// +0x258 = tan(fov/2) / tan(defaultFov/2), the scale BF4 uses to keep the first-person
// weapon the same size on screen at any FOV. Writing it with the FOV makes that scale 1,
// so the gun is drawn at true size in the headset (it was ~3.5x at 111 degrees).
inline constexpr std::size_t kViewDefaultFovOffset = 0xB8;
inline constexpr std::size_t kViewAspectOffset = 0xC4;
// Near and far planes (setters +0xB89F40 / +0xB89EC0; read live as 0.06 and 20000).
inline constexpr std::size_t kViewNearOffset = 0xBC;
inline constexpr std::size_t kViewFarOffset = 0xC0;
inline constexpr std::size_t kViewHorizontalFovOffset = 0x250;
inline constexpr std::uint32_t kViewFovDirtyBits = 0xF;  // what the FOV setter ORs in
// A submitted view block: three 0x4E0-byte views (+0x000 main, +0x4E0, +0x9C0 secondary with
// an enable byte at +0xEA0) and trailing fields (the per-frame pipeline `+0x828090`).
inline constexpr std::size_t kViewBlockBytes = 0x1020;

using RendererSubmitFn = void (*)(void* renderer, float a, float b, std::uint32_t viewCount, std::uint8_t* views,
                                  void* sync, void* extra);

// Frostbite LinearTransform rows (right, up, forward, translation) as four padded Vec3.
frostbite::LinearTransform ReadViewTransform(const std::uint8_t* rows /* 64 bytes */);
void WriteViewTransform(std::uint8_t* rows /* 64 bytes */, const frostbite::LinearTransform& t); // keeps w lanes

// The game's camera with the headset pose `local` (relative to the recentred reference,
// Frostbite convention) applied on top. keepGamePitch=false keeps only the game's yaw so
// the horizon follows the headset (mouse/stick pitch stays with aim, not the view).
frostbite::LinearTransform HeadTrackedView(const frostbite::LinearTransform& game,
                                           const frostbite::LinearTransform& local, bool keepGamePitch);
// The same with the parent heading given explicitly (controller aiming: the view follows
// the body heading, not the game's aim). BF4 camera convention: yaw 0 looks down -Z,
// positive yaw turns right; the game camera's position is kept.
frostbite::LinearTransform HeadTrackedViewAtYaw(const frostbite::LinearTransform& game,
                                                const frostbite::LinearTransform& local, float yaw);

// The direction a camera with back row `aimBack` looks along, seen from camera `view`
// (BF4 rows: right, up, +Z behind), as image-plane tangents. False when it points sideways
// or behind.
bool AimInView(const math::Vec3& aimBack, const frostbite::LinearTransform& view, float& tanX, float& tanY);

class ViewOverride {
public:
    enum class InstallResult { Ok, NoObject, UnexpectedSubmit, AlreadyInstalled };
    enum class RemoveResult { Restored, ObjectGone, ForeignVtable, NotInstalled };
    struct Stats {
        std::uint64_t submits = 0, applied = 0, stale = 0, restored = 0, foreign = 0;
        std::uint64_t notRigid = 0; // block 0 did not hold a camera transform: left alone
        std::uint64_t fovApplied = 0;
        // The pose the latest submit carried and how many submits in a row carried it: a
        // presented frame is known to show pose `poseId` once enough submits did (the
        // render thread runs at most a frame or two behind the main thread).
        std::uint64_t poseId = 0;
        std::uint32_t poseRun = 0;
        float gameFovY = 0;     // the block's own FOV before the override (radians)
        float renderedFovY = 0; // what the latest applied submit rendered with
        // The game's own camera (+0x40, the transform the renderer inverts into its view)
        // before any override: its +Z row (behind the camera) gives the game's aim.
        math::Vec3 gameBack{};
        std::uint64_t gameSamples = 0;
        // That aim in the view the latest applied submit rendered (tangents, +x right, +y up):
        // the aim reticle's place in the frame.
        bool aimInView = false;
        float aimTanX = 0, aimTanY = 0;
        float nearZ = 0, farZ = 0; // the main view's planes (depth stereo linearises with them)
        // Largest difference between the block's two transform copies (+0x00 and +0x40) as
        // the game submitted them (latest rigid submit): 0 when they are the same camera.
        float copiesDiffer = 0;
        // Gun follows aim: the latest applied submit's render-from-head rotation (enabled when
        // the camera was turned toward the aim) and how far toward it the FOV budget allowed.
        FrameRotation frameRotation;
        float gunFraction = 0;
    };
    // Gun follows aim (world lane): render with the camera turned toward the game's aim at
    // the head's position, latched per pose id, with a FOV that still covers both eyes.
    struct GunAimSettings {
        bool enabled = false;
        math::FovTangents eye[2]{{-1, 1, 1, -1}, {-1, 1, 1, -1}};
        float aspect = 0; // the engine's view aspect (0: the block's own)
        float budget = kGunAimFovBudget; // widest render the turn may need (radians)
    };

    ViewOverride() = default;
    ~ViewOverride(); // detaches the replacement from this instance; never touches the object
    ViewOverride(const ViewOverride&) = delete;
    ViewOverride& operator=(const ViewOverride&) = delete;

    // `expectedSubmit` is the resolved RenderView.Update binding: slot 5 of the object's
    // vtable must be exactly that function. One instance per process.
    InstallResult Install(void* renderer, std::uintptr_t expectedSubmit);
    // Puts the original vtable pointer back only while `currentRenderer` (the global, read
    // again by the caller) is still our object and it still points at our copy; a freed or
    // replaced object is never touched.
    RemoveResult Remove(void* currentRenderer);
    bool Installed() const { return object_ != nullptr; }
    void* Object() const { return object_; }

    // Pose for the next submits; it expires after maxAge so a stopped producer can never
    // leave a stale view behind.
    // poseId != 0 tags the pose (world lane lease); fovY > 0 also replaces the vertical FOV
    // (radians) for these submits, exactly as the game's FOV setter would.
    void SetLocal(const frostbite::LinearTransform& local, bool keepGamePitch, std::uint64_t poseId = 0,
                  float fovY = 0, float parentYaw = std::numeric_limits<float>::quiet_NaN());
    void Clear();
    Stats GetStats() const;
    // Developer research: which of the block's two transform copies get the head pose
    // (bit 0: +0x00, bit 1: +0x40; default both). The restore is exact either way.
    void SetCopyMask(std::uint32_t mask) { copyMask_.store(mask & 3u); }
    void SetGunAim(const GunAimSettings& settings);
    std::uint32_t CopyMask() const { return copyMask_.load(); }
    // Developer research (read-only): the next submit copies block 0 as the game submitted it
    // (before the override writes anything); TakeBlockDump hands the copy over once.
    void RequestBlockDump();
    bool TakeBlockDump(std::array<std::uint8_t, kViewBlockBytes>& out, std::uint32_t& viewCount);

    static constexpr std::chrono::milliseconds kMaxAge{100};

private:
    static void Submit(void* renderer, float a, float b, std::uint32_t viewCount, std::uint8_t* views, void* sync,
                       void* extra);
    void* object_ = nullptr;
    std::atomic<std::uint32_t> copyMask_{3};
    GunAimSettings gunAim_;        // under mutex_
    std::uint64_t gunLatchPose_ = 0;
    GunAim gunLatched_;
    void* originalVtable_ = nullptr;
    RendererSubmitFn original_ = nullptr;
    void** copy_ = nullptr; // [locator, slots...]; leaked on purpose: a call may still be in flight
    mutable std::mutex mutex_;
    frostbite::LinearTransform local_{};
    bool keepGamePitch_ = false, havePose_ = false;
    std::uint64_t poseId_ = 0;
    float fovY_ = 0;
    float parentYaw_ = std::numeric_limits<float>::quiet_NaN();
    bool dumpRequested_ = false, dumpReady_ = false; // RequestBlockDump
    std::uint32_t dumpViewCount_ = 0;
    std::array<std::uint8_t, kViewBlockBytes> dump_{};
    std::chrono::steady_clock::time_point poseTime_{};
    Stats stats_{};
};

} // namespace fvr::bf4
