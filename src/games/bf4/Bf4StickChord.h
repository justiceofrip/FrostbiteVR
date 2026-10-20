#pragma once
// Both stick clicks held together are FrostbiteVR's recenter gesture (one second), not the
// game's buttons: BF4's pad sprints on the left click and melees on the right, and the
// gesture drew the knife live (2026-10-19). A lone click still reaches the game: once it has
// been held kSoloSeconds without the other stick, or as a kTapSeconds tap when released
// sooner. A chord holds both back until both sticks are released.
#include <cstdint>

namespace fvr::bf4 {

class StickChord {
public:
    static constexpr double kSoloSeconds = 0.08; // how long a click waits for the other stick
    static constexpr double kTapSeconds = 0.06;  // a quicker click still reaches the game this long

    // left/right: stick clicks held now; now: seconds. Returns the clicks the game may see
    // this frame (bit 0 left, bit 1 right).
    unsigned Update(bool left, bool right, double now) {
        if (left && right && !chord_) {
            chord_ = true;
            ++chords_;
        } else if (chord_ && !left && !right) {
            chord_ = false;
        }
        const bool down[2] = {left, right};
        unsigned out = 0;
        for (int i = 0; i < 2; ++i) {
            Stick& s = sticks_[i];
            if (down[i]) {
                if (!s.held) s = {true, now, false, false, s.tapUntil};
                if (chord_) {
                    s.chorded = true;
                    s.passed = false;
                } else if (!s.chorded && now - s.since >= kSoloSeconds) {
                    s.passed = true;
                }
            } else if (s.held) {
                if (!s.passed && !s.chorded) s.tapUntil = now + kTapSeconds; // a quick click
                s.held = s.passed = false;
            }
            if (s.passed || (!chord_ && now < s.tapUntil)) out |= 1u << i;
        }
        return out;
    }
    std::uint64_t Chords() const { return chords_; }

private:
    struct Stick {
        bool held = false;
        double since = 0;
        bool passed = false;  // reaching the game while held
        bool chorded = false; // part of a chord since it was pressed
        double tapUntil = -1;
    };
    Stick sticks_[2];
    bool chord_ = false;
    std::uint64_t chords_ = 0;
};

} // namespace fvr::bf4
