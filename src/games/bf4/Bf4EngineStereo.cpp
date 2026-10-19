#include "Bf4EngineStereo.h"

#include <algorithm>
#include <cmath>

namespace fvr::bf4 {

namespace {
constexpr std::uint64_t kSettings = 0x80, kOutputCount = 0x30, kOutputs = 0x38;
constexpr std::uint64_t kDepth = 0x44, kConvergenceScale = 0x48, kSeparationScale = 0x4C;
constexpr std::uint64_t kStereoEnable = 0x7D, kSplitScreen = 0x7E;
constexpr std::uint64_t kFullscreenHeight = 0x28, kFullscreenWidth = 0x2C;
constexpr std::uint64_t kMode = 0x7C, kOn = 0x80, kConvergence = 0x84, kSeparation = 0x88;
constexpr float kDepthValue = 100.0f; // separation = 1.0 * SeparationScale before engine factors
} // namespace

float EngineStereo::NextSeparationScale(float currentScale, float measuredHalf, float targetHalf) {
    if (!(currentScale > 0) || !(measuredHalf > 0) || !(targetHalf > 0)) return currentScale;
    const float ratio = std::clamp(targetHalf / measuredHalf, 0.1f, 10.0f);
    return currentScale * ratio;
}

EngineStereo::Status EngineStereo::Enable(const EngineStereoLayout& layout, float halfEyeDistance, float tanX) {
    if (status_ == Status::On) return status_;
    std::uint64_t device = 0, vtable = 0, settings = 0, outputs = 0;
    std::uint32_t outputCount = 0, width = 0, height = 0;
    std::uint8_t enable = 0xFF, split = 0xFF;
    const bool read = memory_.Read(layout.deviceGlobal, &device, 8) && device && memory_.Read(device, &vtable, 8) &&
                      memory_.Read(device + kSettings, &settings, 8) && settings &&
                      memory_.Read(device + kOutputCount, &outputCount, 4) && memory_.Read(device + kOutputs, &outputs, 8) &&
                      outputs && memory_.Read(settings + kStereoEnable, &enable, 1) &&
                      memory_.Read(settings + kSplitScreen, &split, 1) && memory_.Read(settings + kFullscreenWidth, &width, 4) &&
                      memory_.Read(settings + kFullscreenHeight, &height, 4);
    // The device must be the analysed class and its settings must read as RenderDevice
    // settings (booleans where booleans are, a sane display size), or nothing is written.
    if (!read || vtable != layout.deviceVtable || outputCount < 1 || outputCount > 8 || enable > 1 || split > 1 ||
        width > 16384 || height > 16384 || !(halfEyeDistance > 0) || !(tanX > 0)) {
        status_ = Status::Refused;
        return status_;
    }
    settings_ = settings;
    outputs_ = outputs;
    scale_ = halfEyeDistance / (kConvergenceMeters * tanX); // separation for the ideal factors
    const float convergenceScale = kConvergenceMeters * 10.0f;
    const std::uint8_t one = 1;
    const bool written = journal_.Write(settings_ + kDepth, kDepthValue) &&
                         journal_.Write(settings_ + kConvergenceScale, convergenceScale) &&
                         journal_.Write(settings_ + kSeparationScale, scale_) && journal_.Write(settings_ + kStereoEnable, one) &&
                         journal_.Write(settings_ + kSplitScreen, one);
    if (!written) {
        journal_.RestoreAll();
        status_ = Status::Refused;
        return status_;
    }
    framesSinceWrite_ = 0;
    status_ = Status::On;
    return status_;
}

EngineStereoReading EngineStereo::Read() {
    EngineStereoReading r;
    if (!outputs_) return r;
    std::uint8_t on = 0;
    r.valid = memory_.Read(outputs_ + kMode, &r.mode, 4) && memory_.Read(outputs_ + kOn, &on, 1) &&
              memory_.Read(outputs_ + kConvergence, &r.convergence, 4) && memory_.Read(outputs_ + kSeparation, &r.separation, 4);
    r.on = on != 0;
    return r;
}

void EngineStereo::Update(float halfEyeDistance, float tanX) {
    if (status_ != Status::On) return;
    // Our values must still be in place; if the game rewrote one, hand everything back.
    float scale = 0;
    std::uint8_t split = 0;
    if (!memory_.Read(settings_ + kSeparationScale, &scale, 4) || scale != scale_ ||
        !memory_.Read(settings_ + kSplitScreen, &split, 1) || split != 1) {
        journal_.RestoreAll();
        status_ = Status::Foreign;
        return;
    }
    if (++framesSinceWrite_ < 30 || !(halfEyeDistance > 0) || !(tanX > 0)) return;
    const EngineStereoReading r = Read();
    if (!r.SplitActive()) return;
    const float measured = r.HalfEyeDistance(tanX);
    if (std::fabs(measured - halfEyeDistance) <= 0.02f * halfEyeDistance) return;
    const float next = NextSeparationScale(scale_, measured, halfEyeDistance);
    if (next == scale_ || !journal_.Write(settings_ + kSeparationScale, next)) return;
    scale_ = next;
    framesSinceWrite_ = 0;
    ++adjustments_;
}

void EngineStereo::Disable() {
    journal_.RestoreAll();
    if (status_ == Status::On) status_ = Status::Off;
    settings_ = outputs_ = 0;
}

} // namespace fvr::bf4
