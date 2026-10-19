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

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
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
inline constexpr std::size_t kViewAspectOffset = 0xC4;
inline constexpr std::size_t kViewHorizontalFovOffset = 0x250;
inline constexpr std::uint32_t kViewFovDirtyBits = 0xF;  // what the FOV setter ORs in

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
                  float fovY = 0);
    void Clear();
    Stats GetStats() const;

    static constexpr std::chrono::milliseconds kMaxAge{100};

private:
    static void Submit(void* renderer, float a, float b, std::uint32_t viewCount, std::uint8_t* views, void* sync,
                       void* extra);
    void* object_ = nullptr;
    void* originalVtable_ = nullptr;
    RendererSubmitFn original_ = nullptr;
    void** copy_ = nullptr; // [locator, slots...]; leaked on purpose: a call may still be in flight
    mutable std::mutex mutex_;
    frostbite::LinearTransform local_{};
    bool keepGamePitch_ = false, havePose_ = false;
    std::uint64_t poseId_ = 0;
    float fovY_ = 0;
    std::chrono::steady_clock::time_point poseTime_{};
    Stats stats_{};
};

} // namespace fvr::bf4
