#include "StereoProbe.h"

#include <cstdio>

namespace fvr::bf4::stereo {

ProbeVerdict Classify(const ProbeFacts& f, const EyeSequenceTracker& t) {
    if (!f.shimLoaded) return ProbeVerdict::NoNvapi;
    if (!f.stereoHandleCreated || f.eyeSwitches == 0) return ProbeVerdict::NvapiNoStereo;
    if (t.StereoWorking()) return ProbeVerdict::Working;
    if (t.Dominant() == FramePattern::Interleaved) return ProbeVerdict::Interleaved;
    return ProbeVerdict::Partial;
}

std::string_view VerdictName(ProbeVerdict v) noexcept {
    switch (v) {
    case ProbeVerdict::NoNvapi: return "no-nvapi";
    case ProbeVerdict::NvapiNoStereo: return "nvapi-no-stereo";
    case ProbeVerdict::Interleaved: return "interleaved";
    case ProbeVerdict::Partial: return "partial";
    case ProbeVerdict::Working: return "working";
    }
    return "unknown";
}

std::string NextStep(ProbeVerdict v, const ProbeFacts& f) {
    const bool nvidia = f.gpuVendorId == 0x10DE;
    switch (v) {
    case ProbeVerdict::NoNvapi:
        return nvidia || f.vendorSpoofed
                   ? "BF4 never loaded NVAPI. Its 3D path may be switched off in Options > Video; look for a 3D/Stereoscopic option, then probe again."
                   : "BF4 never loaded NVAPI, likely because the GPU is not NVIDIA. Turn on 'Pretend GPU is NVIDIA' in the launcher and probe again.";
    case ProbeVerdict::NvapiNoStereo:
        return "BF4 talks to NVAPI but did not start stereo rendering. Check Options > Video for a Stereoscopic 3D toggle; the query list below shows how far it got.";
    case ProbeVerdict::Interleaved:
        return "BF4 renders both eyes but interleaves them within a frame. Next step: redirect each eye's render target instead of copying the backbuffer.";
    case ProbeVerdict::Partial:
        return "BF4 switches eyes but only one eye pass is visible per frame. Next step: capture on draw boundaries instead of eye switches.";
    case ProbeVerdict::Working:
        return "Engine stereo works: separate left and right images every frame. Check the side-by-side snapshot; next step is showing them per eye in the headset.";
    }
    return {};
}

std::string BuildProbeReport(const ProbeFacts& f, const EyeSequenceTracker& t, const std::string& nvapiReport) {
    const ProbeVerdict v = Classify(f, t);
    std::string s = "FrostbiteVR stereo probe\n";
    s += "verdict: " + std::string(VerdictName(v)) + "\n";
    s += "next: " + NextStep(v, f) + "\n";
    char line[200];
    std::snprintf(line, sizeof(line), "gpu_vendor=0x%04x spoofed=%d shim=%d handle=%d direct=%d eye_switches=%llu\n",
                  f.gpuVendorId, f.vendorSpoofed, f.shimLoaded, f.stereoHandleCreated, f.directMode,
                  static_cast<unsigned long long>(f.eyeSwitches));
    s += line;
    std::snprintf(line, sizeof(line), "frames=%llu pairs=%llu mono=%llu LR=%llu RL=%llu interleaved=%llu partial=%llu\n",
                  static_cast<unsigned long long>(t.Frames()), static_cast<unsigned long long>(t.Pairs()),
                  static_cast<unsigned long long>(t.Count(FramePattern::Mono)),
                  static_cast<unsigned long long>(t.Count(FramePattern::SequentialLR)),
                  static_cast<unsigned long long>(t.Count(FramePattern::SequentialRL)),
                  static_cast<unsigned long long>(t.Count(FramePattern::Interleaved)),
                  static_cast<unsigned long long>(t.Count(FramePattern::Partial)));
    s += line;
    s += "first frames: " + t.RecentSequence() + "\n";
    s += nvapiReport;
    return s;
}

} // namespace fvr::bf4::stereo
