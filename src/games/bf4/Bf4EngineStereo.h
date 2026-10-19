#pragma once
// BF4's own split-screen stereo, driven for the world lane (docs/BF4-STEREO.md, analysed
// build only). Static facts:
//  - The render device (0x610 bytes, vtable +0x1DD1870) lives in global +0x2738080; its
//    settings are at +0x80: StereoDepth +0x44, StereoConvergenceScale +0x48,
//    StereoSeparationScale +0x4C, StereoEnable +0x7D, StereoSplitScreenEnable +0x7E (read
//    only from user.cfg on PC; read in code only by the per-frame function below).
//  - Every frame, per output (outputs at device +0x38, stride 0x138, count +0x30), the
//    stereo function +0xC12FF0 sets mode +0x7C = 1 and on +0x80 = 1 when both flags are
//    set, convergence +0x84 = ConvergenceScale * 0.1 and separation +0x88 = Depth * 0.01 *
//    SeparationScale.
//  - The two-eye render loop +0x8CAF70 then renders each view twice in the same frame:
//    eye 1 into the left half, eye 0 into the right half of the backbuffer, with view
//    +0xD0 = +separation (eye 1) / -separation (eye 0) and +0xD4 = convergence. The
//    projection builder +0xB7EA10 adds s to row 2 x and s*c to row 3 x, so per eye
//    x_ndc = x*P00/z + s + s*c/z: a camera offset of -s*c/P00 along the view's right axis
//    (eye 1 left, eye 0 right) plus a constant image shift of s, which the crop removes.
// So the half eye distance is separation * convergence * tan(horizontal half FOV); the
// controller keeps it equal to the headset's (closed loop on SeparationScale, measured
// from the output the engine actually used) with a fixed 10 m convergence.
//
// Every write goes through an exact-write journal and is undone on Disable / destruction,
// leaving any value the game wrote meanwhile untouched.
#include "fvr/runtime/ExactWrite.h"

#include <cstdint>

namespace fvr::bf4 {

struct EngineStereoLayout {
    std::uint64_t deviceGlobal = 0;  // address of the global holding the render device
    std::uint64_t deviceVtable = 0;  // expected vtable of that device
};

struct EngineStereoReading {
    bool valid = false;
    std::uint32_t mode = 0; // 1 = split screen
    bool on = false;
    float convergence = 0, separation = 0;
    bool SplitActive() const { return valid && mode == 1 && on && separation > 0 && convergence > 0; }
    // Half eye distance in world units for a render with horizontal half-FOV tangent tanX.
    float HalfEyeDistance(float tanX) const { return separation * convergence * tanX; }
};

class EngineStereo {
public:
    enum class Status { Off, On, Refused, Foreign };
    static constexpr float kConvergenceMeters = 10.0f;

    explicit EngineStereo(runtime::IMemory& memory) : memory_(memory), journal_(memory) {}
    ~EngineStereo() { Disable(); }

    // Checks the device and its settings, then switches split-screen stereo on.
    Status Enable(const EngineStereoLayout& layout, float halfEyeDistance, float tanX);
    // Keeps the engine's half eye distance at `halfEyeDistance` (world units) for a render
    // whose horizontal half-FOV tangent is tanX. Rate-limited; call once per frame.
    void Update(float halfEyeDistance, float tanX);
    EngineStereoReading Read();
    void Disable();
    Status State() const { return status_; }
    std::uint64_t Adjustments() const { return adjustments_; }

    // The SeparationScale that yields `halfEyeDistance` given the separation the engine
    // produced for `currentScale` (its own factors included).
    static float NextSeparationScale(float currentScale, float measuredHalf, float targetHalf);

private:
    runtime::IMemory& memory_;
    runtime::ExactWriteJournal journal_;
    std::uint64_t settings_ = 0, outputs_ = 0;
    float scale_ = 0;
    std::uint32_t framesSinceWrite_ = 0;
    std::uint64_t adjustments_ = 0;
    Status status_ = Status::Off;
};

} // namespace fvr::bf4
