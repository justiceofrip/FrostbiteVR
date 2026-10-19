#include "EyeSequence.h"

namespace fvr::bf4::stereo {

std::string_view FramePatternName(FramePattern p) noexcept {
    switch (p) {
    case FramePattern::Mono: return "mono";
    case FramePattern::SequentialLR: return "sequential-LR";
    case FramePattern::SequentialRL: return "sequential-RL";
    case FramePattern::Interleaved: return "interleaved";
    case FramePattern::Partial: return "partial";
    }
    return "unknown";
}

CaptureAction EyeSequenceTracker::CaptureFor(Eye eye) {
    if (eye == Eye::Left) return CaptureAction::CaptureLeft;
    if (eye == Eye::Right) return CaptureAction::CaptureRight;
    return CaptureAction::None;
}

CaptureAction EyeSequenceTracker::OnActiveEye(Eye eye) {
    if (eye == current_) {
        // Explicit re-selection of the same eye opens a block (engines often set the eye
        // at the start of every pass even when it did not change).
        if (!blockOpen_) {
            blockOpen_ = true;
            frameBlocks_ += eye == Eye::Left ? 'L' : eye == Eye::Right ? 'R' : 'M';
        }
        return CaptureAction::None;
    }
    // Leaving an eye whose pass was rendered this frame: its image is complete now.
    const CaptureAction action = blockOpen_ ? CaptureFor(current_) : CaptureAction::None;
    if (action == CaptureAction::CaptureLeft) haveLeft_ = true;
    if (action == CaptureAction::CaptureRight) haveRight_ = true;
    current_ = eye;
    blockOpen_ = true;
    frameBlocks_ += eye == Eye::Left ? 'L' : eye == Eye::Right ? 'R' : 'M';
    return action;
}

EyeSequenceTracker::PresentResult EyeSequenceTracker::OnPresent() {
    PresentResult r;
    if (blockOpen_) {
        r.capture = CaptureFor(current_);
        if (r.capture == CaptureAction::CaptureLeft) haveLeft_ = true;
        if (r.capture == CaptureAction::CaptureRight) haveRight_ = true;
    }
    // Classify on the L/R subsequence; mono blocks (HUD, post) are ignored for order.
    std::string lr;
    for (char c : frameBlocks_)
        if (c != 'M') lr += c;
    if (lr.empty()) r.pattern = FramePattern::Mono;
    else if (lr == "LR") r.pattern = FramePattern::SequentialLR;
    else if (lr == "RL") r.pattern = FramePattern::SequentialRL;
    else if (lr.find('L') != std::string::npos && lr.find('R') != std::string::npos) r.pattern = FramePattern::Interleaved;
    else r.pattern = FramePattern::Partial;

    r.pairComplete = haveLeft_ && haveRight_ &&
                     (r.pattern == FramePattern::SequentialLR || r.pattern == FramePattern::SequentialRL);
    ++frames_;
    ++counts_[static_cast<int>(r.pattern)];
    if (r.pairComplete) ++pairs_;
    if (recent_.size() < 240) recent_ += (frameBlocks_.empty() ? std::string("-") : frameBlocks_) + "|";
    frameBlocks_.clear();
    haveLeft_ = haveRight_ = false;
    blockOpen_ = false; // the next frame's first pass must open its own block
    return r;
}

void EyeSequenceTracker::Reset() {
    current_ = Eye::Mono;
    blockOpen_ = haveLeft_ = haveRight_ = false;
    frameBlocks_.clear();
}

FramePattern EyeSequenceTracker::Dominant() const {
    int best = 0;
    for (int i = 1; i < 5; ++i)
        if (counts_[i] > counts_[best]) best = i;
    return static_cast<FramePattern>(best);
}

bool EyeSequenceTracker::StereoWorking() const {
    const FramePattern d = Dominant();
    return pairs_ > 0 && (d == FramePattern::SequentialLR || d == FramePattern::SequentialRL);
}

} // namespace fvr::bf4::stereo
