#pragma once
// Decides when the (single, mono) backbuffer holds a finished eye image.
//
// With real 3D Vision the driver gives each eye its own backbuffer. Our emulated driver
// does not, so both eye passes land in the same buffer. The engine tells us which eye it
// is drawing via SetActiveEye; when it leaves an eye (switches eye, or presents), that
// eye's image is complete and must be copied out before the next pass overwrites it.
//
// It also classifies the engine's per-frame pattern, which is the key unknown the probe
// answers: "LR"/"RL" (sequential: capture works), interleaved (needs render-target
// redirection), partial or mono.
#include "NvapiStereoEmu.h"

#include <cstdint>
#include <string>
#include <string_view>

namespace fvr::bf4::stereo {

enum class CaptureAction : std::uint8_t { None, CaptureLeft, CaptureRight };

enum class FramePattern : std::uint8_t { Mono, SequentialLR, SequentialRL, Interleaved, Partial };
std::string_view FramePatternName(FramePattern p) noexcept;

class EyeSequenceTracker {
public:
    CaptureAction OnActiveEye(Eye eye);

    struct PresentResult {
        CaptureAction capture = CaptureAction::None;
        bool pairComplete = false;
        FramePattern pattern = FramePattern::Mono;
    };
    PresentResult OnPresent();

    void Reset();

    std::uint64_t Frames() const { return frames_; }
    std::uint64_t Pairs() const { return pairs_; }
    std::uint64_t Count(FramePattern p) const { return counts_[static_cast<int>(p)]; }
    FramePattern Dominant() const;
    bool StereoWorking() const; // sequential pattern dominates and pairs are arriving
    const std::string& RecentSequence() const { return recent_; } // e.g. "LR|LR|LMR|"

private:
    static CaptureAction CaptureFor(Eye eye);
    Eye current_ = Eye::Mono;
    bool blockOpen_ = false; // the current eye has been explicitly started this frame
    bool haveLeft_ = false, haveRight_ = false;
    std::string frameBlocks_; // "L", "R", "M" per eye block this frame
    std::string recent_;
    std::uint64_t frames_ = 0, pairs_ = 0;
    std::uint64_t counts_[5] = {};
};

} // namespace fvr::bf4::stereo
