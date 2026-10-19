#pragma once
// Turns the host's game-screen pointer stream (u, v, down) into an ordered list of
// cursor events in client pixels. Platform-neutral so its guarantees are unit-tested:
//   * a Move precedes any button event at a new position
//   * ButtonUp is always emitted before the pointer deactivates (no stuck button)
//   * nothing is emitted while idle
// The Windows sink only translates events to hooks + window messages.
#include "Bf4FrameworkGlue.h"

#include <cstdint>
#include <vector>

namespace fvr::bf4 {

struct PointerEvent {
    enum class Type : std::uint8_t { Move, ButtonDown, ButtonUp, Deactivate };
    Type type = Type::Move;
    int x = 0, y = 0; // client pixels
};

class PointerInjector {
public:
    // Appends this frame's events to `out` (cleared first).
    void Update(const GamePointer& pointer, int clientWidth, int clientHeight, std::vector<PointerEvent>& out);
    // Release + deactivate if active (lockdown, menu closed, surface lost).
    void ForceRelease(std::vector<PointerEvent>& out);

    bool Active() const { return active_; }
    bool ButtonDown() const { return down_; }
    std::uint64_t Clicks() const { return clicks_; }

private:
    bool active_ = false;
    bool down_ = false;
    int x_ = -1, y_ = -1;
    std::uint64_t clicks_ = 0;
};

} // namespace fvr::bf4
