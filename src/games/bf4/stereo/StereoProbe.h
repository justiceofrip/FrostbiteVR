#pragma once
// Turns what the shim and tracker saw into one plain verdict + next step. This is the
// report the player gets after a probe session, and what goes into feedback.
#include "EyeSequence.h"

#include <string>

namespace fvr::bf4::stereo {

enum class ProbeVerdict : std::uint8_t {
    NoNvapi,          // BF4 never loaded our nvapi64.dll
    NvapiNoStereo,    // loaded, but never created a stereo handle / never switched eyes
    Interleaved,      // eyes interleaved within a frame: capture needs RT redirection
    Partial,          // only one eye per frame: implicit eye continuation, needs a fallback
    Working           // sequential L/R pairs captured
};

struct ProbeFacts {
    bool shimLoaded = false;
    bool stereoHandleCreated = false;
    bool directMode = false;
    std::uint64_t eyeSwitches = 0;
    bool vendorSpoofed = false;
    unsigned gpuVendorId = 0; // as the real adapter reports it
};

ProbeVerdict Classify(const ProbeFacts& facts, const EyeSequenceTracker& tracker);
std::string_view VerdictName(ProbeVerdict v) noexcept;
std::string NextStep(ProbeVerdict v, const ProbeFacts& facts);
std::string BuildProbeReport(const ProbeFacts& facts, const EyeSequenceTracker& tracker, const std::string& nvapiReport);

} // namespace fvr::bf4::stereo
