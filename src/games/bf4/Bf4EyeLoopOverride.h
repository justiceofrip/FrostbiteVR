#pragma once
// Room for two eyes in BF4's per-eye view array (bf4.exe build 179547, docs/BF4-STEREO.md).
//
// The two-eye render loop (+0x8CAF70, reached only through slot 2 = +0x8BE430 of its class,
// vtable +0x1D24258) appends one 0x1560-byte entry per eye to an array whose pointers sit
// at object +0x1570 (begin), +0x1578 (end of data), +0x1580 (end of storage), with inline
// storage for exactly ONE entry at +0x1590 and no bounds check. PC builds never render
// split-screen stereo, so a second eye writes 0x1560 bytes past the storage into the next
// fields: the crash seen on 2026-10-19 within seconds of switching stereo on.
//
// Like Bf4ViewOverride, nothing in game code is patched: the object is pointed at a copy of
// its vtable whose slot 2 is ours (one 8-byte write, restored exactly on Remove). For each
// call the replacement points the array at a buffer with room for kEntries (temporary
// override), calls the original, then puts the three pointers back exactly. The game's code
// resets the array at the start of every call and consumes it before returning, so between
// calls nothing reads the buffer.
//
// Portable C++: tests drive it with a fake object and vtable in-process.
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>

namespace fvr::bf4 {

inline constexpr std::size_t kEyeLoopVtableSlots = 16;
inline constexpr std::size_t kEyeLoopSlot = 2;
inline constexpr std::size_t kEyeArrayBegin = 0x1570, kEyeArrayEnd = 0x1578, kEyeArrayCapacity = 0x1580;
inline constexpr std::size_t kEyeArrayInline = 0x1590, kEyeEntryBytes = 0x1560;

// Slot 2's arguments as the function reads them (arg 2 unused; 5 is a float on the stack;
// 6 the view count; 7 the views; 8 a pointer; 9 and 10 passed on).
using EyeLoopFn = void (*)(void* self, std::uint64_t a2, std::uint64_t a3, float a4, float a5, std::uint32_t a6, void* a7,
                           void* a8, std::uint64_t a9, std::uint64_t a10);

class EyeLoopOverride {
public:
    enum class InstallResult { Ok, NoObject, UnexpectedSlot, NotInlineArray, AlreadyInstalled };
    struct Stats {
        std::uint64_t calls = 0, widened = 0, restored = 0, foreign = 0, maxEntries = 0;
    };
    static constexpr std::size_t kEntries = 8; // two eyes for up to four views

    EyeLoopOverride() = default;
    ~EyeLoopOverride();
    EyeLoopOverride(const EyeLoopOverride&) = delete;
    EyeLoopOverride& operator=(const EyeLoopOverride&) = delete;

    // The object's slot 2 must be `expectedSlot` and its array must use its inline storage.
    InstallResult Install(void* object, std::uintptr_t expectedSlot);
    // Detaches from the object only if it still points at our copy; never frees the copy.
    // Only call while the object is known alive (it is never destroyed during a level).
    bool Remove();
    bool Installed() const { return object_ != nullptr; }
    void* Object() const { return object_; }
    Stats GetStats() const;

private:
    static void Call(void* self, std::uint64_t a2, std::uint64_t a3, float a4, float a5, std::uint32_t a6, void* a7, void* a8,
                     std::uint64_t a9, std::uint64_t a10);
    void* object_ = nullptr;
    void* originalVtable_ = nullptr;
    void** copy_ = nullptr;       // leaked on purpose: a call may still be in flight
    std::uint8_t* buffer_ = nullptr; // leaked on purpose for the same reason
    mutable std::mutex mutex_;
    Stats stats_{};
};

} // namespace fvr::bf4
