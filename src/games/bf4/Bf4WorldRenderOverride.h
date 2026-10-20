#pragma once
// A second view for BF4: its world render run twice per frame, once per eye (bf4.exe build
// 179547, docs/BF4-STEREO.md).
//
// BF4's world renderer (vtable +0x1DF4B60, 21 slots, no RTTI) renders the frame's views in slot
// 2 (+0xCDE440: this, args, two pointers passed on; args +0x14 the view count, +0x18 the
// 0x1560-byte per-view entries). On PC it keeps per-view state for exactly one view (a
// one-element view-renderer array at +0x88, one 0x34000-byte view block), so two views in one
// call overrun it (the split-stereo crash, 2026-10-19). Instead of a second view in the same
// call, this runs the one-view call twice: hooks get to move the camera to an eye before each
// run and to keep the first eye's image before the second run draws over it. The game's
// simulation runs once; only rendering repeats.
//
// The call returns with its per-view render still running as jobs: it hands the caller two
// job handles (its 3rd and 4th arguments are 24-byte outputs; the render thread waits on them
// later with +0x63C940). A second run started before those finish rebuilds the view state
// under the first run's jobs (live, session 87: six doubled frames, then a job read a
// half-built per-view object). So between the runs the first run's two handles are waited on
// with the game's own wait (`SetJobWait`); the second run then fills them for the game.
//
// Slot 2 only builds the frame's render data (main thread; live, session 92: no draw during it).
// The world is drawn on the render thread by slot 3 (+0xCE56F0, this and a draw context; it
// waits for slot 2's jobs, then draws: live, session 93, draw stacks through the eye loop's slot
// 3 +0x8BEC40). Running slot 3 twice draws the frame's world twice from the same data: the hooks
// move the GPU camera to each eye (Bf4FirstPersonView) and keep the first eye's image.
//
// Like Bf4EyeLoopOverride, nothing in game code is patched: the object is pointed at a copy of
// its vtable whose slots 2 and 3 are ours (one 8-byte write, restored exactly on Remove).
//
// Portable C++: tests drive it with a fake object and vtable in-process.
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>

namespace fvr::bf4 {

inline constexpr std::size_t kWorldRendererVtableSlots = 21;
inline constexpr std::size_t kWorldRenderSlot = 2;
inline constexpr std::size_t kWorldDrawSlot = 3;
inline constexpr std::size_t kWorldRenderArgsViewCount = 0x14;

using WorldRenderFn = void (*)(void* self, void* args, void* a3, void* a4);
using WorldDrawFn = void (*)(void* self, void* context, void* a3, void* a4); // a3/a4 forwarded, unused
using JobWaitFn = void (*)(void* handle); // bf4.exe +0x63C940: help run jobs until the handle's are done

class WorldRenderOverride {
public:
    // Called on the render thread, inside the wrapped call.
    class Hooks {
    public:
        virtual ~Hooks() = default;
        // How many runs this call gets (1: as BF4 calls it; 2: one per eye). Asked once per call.
        virtual int Runs(std::uint32_t viewCount) = 0;
        // Around each run (index 0 .. runs-1).
        virtual void BeforeRun(int index, int runs) = 0;
        virtual void AfterRun(int index, int runs) = 0;
        // The same for the draw (slot 3): how many draws this call gets, and around each.
        virtual int Draws() { return 1; }
        virtual void BeforeDraw(int, int) {}
        virtual void AfterDraw(int, int) {}
    };
    enum class InstallResult { Ok, NoObject, UnexpectedSlot, AlreadyInstalled };
    struct Stats {
        std::uint64_t calls = 0, doubled = 0, foreign = 0, waits = 0, drawCalls = 0, drawsDoubled = 0;
        double drawUs[2] = {0, 0}; // render-thread time of the last draw call's runs
        double runUs[2] = {0, 0}; // render-thread time of the last call's runs
    };

    WorldRenderOverride() = default;
    ~WorldRenderOverride();
    WorldRenderOverride(const WorldRenderOverride&) = delete;
    WorldRenderOverride& operator=(const WorldRenderOverride&) = delete;

    // The object's slot 2 must be `expectedSlot` (and slot 3 `expectedDraw` when given). `hooks`
    // must outlive the override.
    InstallResult Install(void* object, std::uintptr_t expectedSlot, Hooks* hooks, std::uintptr_t expectedDraw = 0);
    // Detaches from the object only if it still points at our copy; never frees the copy.
    bool Remove();
    // The object is gone or no longer ours (a level change replaced it): dropped without
    // touching its memory; a call through the old copy just forwards.
    void Forget();
    bool Installed() const { return object_ != nullptr; }
    // The vtable pointer the object carries while it is ours (our copy's first slot).
    const void* OurVtable() const { return copy_ ? static_cast<const void*>(copy_ + 1) : nullptr; }
    // The game's wait for a job handle, called on the first run's two handles before the second.
    void SetJobWait(JobWaitFn wait) { wait_.store(wait); }
    void* Object() const { return object_; }
    Stats GetStats() const;

private:
    static void Call(void* self, void* args, void* a3, void* a4);
    static void Draw(void* self, void* context, void* a3, void* a4);
    void* object_ = nullptr;
    void* originalVtable_ = nullptr;
    void** copy_ = nullptr; // leaked on purpose: a call may still be in flight
    Hooks* hooks_ = nullptr;
    std::atomic<JobWaitFn> wait_{nullptr};
    mutable std::mutex mutex_;
    Stats stats_{};
};

} // namespace fvr::bf4
