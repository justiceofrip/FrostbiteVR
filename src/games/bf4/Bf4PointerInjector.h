#pragma once
// Turns the host's game-screen pointer stream (u, v, down) into an ordered list of
// cursor events in client pixels. Platform-neutral so its guarantees are unit-tested:
//   * a Move precedes any button event at a new position
//   * ButtonUp is always emitted before the pointer deactivates (no stuck button)
//   * nothing is emitted while idle
//   * click lock: while the button is held the cursor stays where it went down, unless
//     the laser moves more than kDragFraction of the screen (then it is a real drag), so
//     the hand's jitter while pulling the trigger never turns a click into a drag (BF4's
//     campaign screen scrolls on drags and then ignores the click)
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

    static constexpr float kDragFraction = 0.06f; // a live click on Resume drifted 63 px of 1920

private:
    bool active_ = false;
    bool down_ = false;
    bool dragging_ = false;
    int downX_ = 0, downY_ = 0;
    int x_ = -1, y_ = -1;
    std::uint64_t clicks_ = 0;
};

} // namespace fvr::bf4
