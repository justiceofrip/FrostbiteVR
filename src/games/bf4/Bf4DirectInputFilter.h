#pragma once
// BF4 re-enumerates its DirectInput game controllers on device changes (and at start), and for
// each one it runs the classic IsXInputDevice check: a WMI scan of every PnP device on its main
// thread (live, 2026-10-20: 5.5 s for this PC's 309 devices; a 4.5 s in-level freeze caught by
// the watchdog; VR runtimes and virtual-pad buses add and remove devices often). While the
// adapter runs, a game-controller enumeration by the game (DirectInput's own EnumDevices,
// below the Steam overlay's hook) now reports no devices, so the scan never starts. BF4 reads
// pads through XInput (the virtual pad, a real Xbox pad), which is unaffected; DirectInput-only
// devices (flight sticks, wheels) are not offered to the game meanwhile.

#include <cstdint>
#include <string>

namespace fvr::bf4 {

class DirectInputFilter {
public:
    // Puts the filter in front of EnumDevices (wide and narrow): the class vtable entry, whose
    // previous value (dinput8's or an overlay's) it calls. Remove puts that back while the
    // entry still holds ours (exact restore).
    static bool Install();
    static void Remove();
    static void SetEnabled(bool enabled);
    struct Stats {
        std::uint64_t calls = 0;     // game-controller enumerations
        std::uint64_t skipped = 0;   // ... answered with no devices
        std::uint64_t passed = 0;    // ... passed on (filter off, or other device classes)
    };
    static Stats GetStats();
    static std::string Describe();
    // Pure: is this enumeration one the filter answers (game controllers)?
    static bool Filters(std::uint32_t devType);
};

} // namespace fvr::bf4
