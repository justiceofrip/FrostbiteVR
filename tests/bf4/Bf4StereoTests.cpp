// Engine-stereo route (BF4's own 3D Vision path through an emulated nvapi64.dll).
// A simulated engine loop follows the documented 3D Vision Direct pattern: Initialize,
// SetDriverMode(Direct), CreateHandleFromIUnknown, then per frame IsActivated ->
// SetActiveEye(L) draw, SetActiveEye(R) draw, Present. The REAL emulated driver, tracker,
// probe and AdapterCore run underneath; the fake platform records eye captures.
#include "TestHarness.h"

#include "EmuFixtures.h"
#include "../../src/games/bf4/stereo/NvapiCoreEmu.h"
#include "../../src/games/bf4/stereo/SideBySide.h"
#include "../../src/platform/InitGate.h"

#include <atomic>
#include <cstdio>
#include <cstring>
#include <string>
#include <filesystem>
#include <fstream>
#include <thread>

using namespace fvr;
using namespace fvr::bf4;
using namespace fvr::bf4::emu;
using namespace fvr::bf4::stereo;
namespace nv = fvr::bf4::nvapi;

TEST_CASE(NvapiEmulationAnswersLikeA3DVisionDriver) {
    NvapiStereoEmu emu;
    int device = 0;
    void* handle = nullptr;
    std::uint8_t flag = 9;
    CHECK(emu.Initialize() == nv::kOk);
    CHECK(emu.SetDriverMode(nv::kDriverModeDirect) == nv::kOk && emu.DirectMode());
    CHECK(emu.SetDriverMode(7) == nv::kError);
    CHECK(emu.IsEnabled(&flag) == nv::kOk && flag == 1);
    CHECK(emu.CreateHandle(&device, &handle) == nv::kOk && handle);
    CHECK(emu.IsActivated(handle, &flag) == nv::kOk && flag == 1);
    CHECK(emu.IsActivated(reinterpret_cast<void*>(0x1234), &flag) == nv::kError); // unknown handle

    std::vector<Eye> seen;
    emu.SetEyeCallback([&](Eye e) { seen.push_back(e); });
    CHECK(emu.SetActiveEye(handle, nv::kEyeLeft) == nv::kOk);
    CHECK(emu.SetActiveEye(handle, nv::kEyeRight) == nv::kOk);
    CHECK(emu.SetActiveEye(handle, 0) == nv::kError);
    CHECK((seen == std::vector<Eye>{Eye::Left, Eye::Right}) && emu.EyeSwitches() == 2);

    // NVIDIA's enum: 2 is LEFT, 1 is RIGHT (a swap here would invert depth).
    CHECK(EyeFromNvapi(2) == Eye::Left && EyeFromNvapi(1) == Eye::Right && EyeFromNvapi(3) == Eye::Mono);

    // Lockdown: report stereo off and stop forwarding eye switches.
    emu.SetSuppressed(true);
    CHECK(emu.IsActivated(handle, &flag) == nv::kOk && flag == 0);
    emu.SetActiveEye(handle, nv::kEyeLeft);
    CHECK(seen.size() == 2);

    CHECK(NvapiStereoEmu::Implements(nv::id::Stereo_SetActiveEye) && !NvapiStereoEmu::Implements(0x11111111));
    emu.NoteQuery(nv::id::Stereo_SetActiveEye, true);
    emu.NoteQuery(0x0150e828, true);
    CHECK(emu.Report().find("NvAPI_Stereo_SetActiveEye") != std::string::npos);
}

TEST_CASE(EyeSequenceCapturesAtEyeBoundaries) {
    EyeSequenceTracker t;
    // Frame: L pass, R pass, present. L is complete at the switch, R at present.
    CHECK(t.OnActiveEye(Eye::Left) == CaptureAction::None);
    CHECK(t.OnActiveEye(Eye::Right) == CaptureAction::CaptureLeft);
    auto p = t.OnPresent();
    CHECK(p.capture == CaptureAction::CaptureRight && p.pairComplete && p.pattern == FramePattern::SequentialLR);
    // Next frame starts from R without a new pass yet: switching to L must NOT capture
    // the post-present backbuffer as "R".
    CHECK(t.OnActiveEye(Eye::Left) == CaptureAction::None);
    CHECK(t.OnActiveEye(Eye::Right) == CaptureAction::CaptureLeft);
    CHECK(t.OnPresent().pairComplete);
    // HUD drawn in mono after both eyes: L and R still captured before the HUD lands.
    t.OnActiveEye(Eye::Left);
    CHECK(t.OnActiveEye(Eye::Right) == CaptureAction::CaptureLeft);
    CHECK(t.OnActiveEye(Eye::Mono) == CaptureAction::CaptureRight);
    p = t.OnPresent();
    CHECK(p.capture == CaptureAction::None && p.pairComplete && p.pattern == FramePattern::SequentialLR);
}

TEST_CASE(EyeSequenceClassifiesPatterns) {
    auto run = [](const char* frame, int frames) {
        EyeSequenceTracker t;
        for (int i = 0; i < frames; ++i) {
            for (const char* c = frame; *c; ++c) t.OnActiveEye(*c == 'L' ? Eye::Left : *c == 'R' ? Eye::Right : Eye::Mono);
            t.OnPresent();
        }
        return t;
    };
    CHECK(run("RL", 10).Dominant() == FramePattern::SequentialRL && run("RL", 10).StereoWorking());
    const auto interleaved = run("LRLR", 10);
    CHECK(interleaved.Dominant() == FramePattern::Interleaved && interleaved.Pairs() == 0 && !interleaved.StereoWorking());
    CHECK(run("L", 10).Dominant() == FramePattern::Partial);
    CHECK(run("", 10).Dominant() == FramePattern::Mono);
    CHECK(run("LR", 3).RecentSequence() == "LR|LR|LR|");
}

TEST_CASE(StereoProbeVerdictsAndNextSteps) {
    EyeSequenceTracker none;
    ProbeFacts f;
    f.gpuVendorId = 0x1002;
    CHECK(Classify(f, none) == ProbeVerdict::NoNvapi);
    CHECK(NextStep(ProbeVerdict::NoNvapi, f).find("Pretend GPU is NVIDIA") != std::string::npos);
    f.shimLoaded = true;
    CHECK(Classify(f, none) == ProbeVerdict::NvapiNoStereo);
    f.stereoHandleCreated = true;
    f.eyeSwitches = 40;
    EyeSequenceTracker good;
    for (int i = 0; i < 20; ++i) { good.OnActiveEye(Eye::Left); good.OnActiveEye(Eye::Right); good.OnPresent(); }
    CHECK(Classify(f, good) == ProbeVerdict::Working);
    const auto report = BuildProbeReport(f, good, "nvapi ...\n");
    CHECK(report.find("verdict: working") != std::string::npos && report.find("pairs=20") != std::string::npos);
}

TEST_CASE(NvapiCoreAnswersLikeOneGpuOnA3DVisionDriver) {
    // Probe 1 on the real game: these were queried right after NvAPI_Initialize.
    const NvapiCoreEmu core;
    char s[nv::kShortString]{};
    std::uint32_t ver = 0, n = 0;
    CHECK(core.GetDriverAndBranchVersion(&ver, s) == nv::kOk && ver == 42531 && std::string(s) == "r425_00");
    void* gpus[nv::kMaxGpus]{};
    CHECK(core.EnumPhysicalGpus(gpus, &n) == nv::kOk && n == 1 && gpus[0] == NvapiCoreEmu::PhysicalGpu());
    CHECK(core.EnumLogicalGpus(gpus, &n) == nv::kOk && n == 1 && gpus[0] == NvapiCoreEmu::LogicalGpu());
    CHECK(core.GetFullName(NvapiCoreEmu::PhysicalGpu(), s) == nv::kOk && std::string(s) == "GeForce GTX 1080");
    CHECK(core.GetFullName(nullptr, s) == nv::kInvalidHandle);
    std::uint32_t dev = 0, sub = 0, rev = 0, ext = 0;
    CHECK(core.GetPciIdentifiers(NvapiCoreEmu::PhysicalGpu(), &dev, &sub, &rev, &ext) == nv::kOk &&
          dev == 0x1B8010DEu && ext == 0x1B80u);
    // Versioned structs: V1 (28 bytes) filled as one AFR group; a wrong size is refused untouched.
    int device = 0;
    std::uint32_t sli[8] = {nv::MakeVersion(28, 1), 9, 9, 9, 9, 9, 9, 0xABCD};
    CHECK(core.GetCurrentSliState(&device, sli) == nv::kOk && sli[1] == 1 && sli[2] == 1 && sli[3] == 0);
    CHECK(sli[7] == 0xABCD); // V1 never writes past its 28 bytes
    std::uint32_t bad[8] = {nv::MakeVersion(20, 1)};
    CHECK(core.GetCurrentSliState(&device, bad) == nv::kIncompatibleStructVersion);
    unsigned char drv[140]{};
    const std::uint32_t drvVer = nv::MakeVersion(140, 1);
    std::memcpy(drv, &drvVer, 4);
    CHECK(core.GetDisplayDriverVersion(nullptr, drv) == nv::kOk && std::string(reinterpret_cast<char*>(drv + 12)) == "r425_00");
    void* display = nullptr;
    CHECK(core.EnumNvidiaDisplayHandle(0, &display) == nv::kOk && display == NvapiCoreEmu::Display());
    CHECK(core.EnumNvidiaDisplayHandle(1, &display) == nv::kEndEnumeration);
    CHECK(NvapiCoreEmu::Implements(nv::id::D3D_GetCurrentSLIState) && !NvapiCoreEmu::Implements(0x33c7358cu));
}

TEST_CASE(SideBySideSnapshotIsAValidBmp) {
    // 4x2 red left eye (RGBA), 4x2 blue right eye (BGRA), downscale 2 -> 4x1 output.
    std::vector<std::uint8_t> red(4 * 2 * 4), blue(4 * 2 * 4);
    for (int i = 0; i < 8; ++i) {
        red[i * 4 + 0] = 255; red[i * 4 + 3] = 255;
        blue[i * 4 + 0] = 255; blue[i * 4 + 3] = 255; // BGRA: B first
    }
    const ImageView left{red.data(), 4, 2, 16, true}, right{blue.data(), 4, 2, 16, false};
    CHECK(Mean(left).r > 0.99f && Mean(right).b > 0.99f && Mean(right).r < 0.01f);
    // Process temp dir (TEMP on G: per AGENTS.md on Windows, /tmp on Linux).
    const std::string path = (std::filesystem::temp_directory_path() / "fvr-sbs-test.bmp").string();
    CHECK(WriteSideBySideBmp(path, left, right, 2));
    std::ifstream in(path, std::ios::binary);
    std::vector<unsigned char> b((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    in.close();
    CHECK(b.size() == 54 + 12 && b[0] == 'B' && b[1] == 'M');
    if (b.size() != 54 + 12) return; // the byte checks below would read past the end
    CHECK(b[18] == 4 && b[22] == 1 && b[28] == 24);           // 4x1, 24-bit
    CHECK(b[54] == 0 && b[55] == 0 && b[56] == 255);           // first pixel: red (BGR order)
    CHECK(b[54 + 6] == 255 && b[54 + 8] == 0);                 // third pixel: blue
    std::remove(path.c_str());
    CHECK(!WriteSideBySideBmp(path, left, ImageView{blue.data(), 2, 2, 8, false}, 1)); // size mismatch
}

TEST_CASE(InitGateReentrancyAndConcurrency) {
    platform::InitGate gate;
    CHECK(gate.Enter(1) == platform::InitGate::Decision::RunInit);
    CHECK(gate.Enter(1) == platform::InitGate::Decision::Reentrant); // init creates a device -> calls back in
    std::atomic<int> result{-1};
    std::thread other([&] { result = static_cast<int>(gate.Enter(2)); }); // must wait for init
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    CHECK(result == -1);
    gate.Finish();
    other.join();
    CHECK(result == static_cast<int>(platform::InitGate::Decision::AlreadyDone));
    CHECK(gate.Enter(3) == platform::InitGate::Decision::AlreadyDone);
}

namespace {
// The simulated BF4 render loop on top of the emulated driver.
struct SimEngine {
    NvapiStereoEmu& nv;
    void* handle = nullptr;
    int device = 0;
    explicit SimEngine(NvapiStereoEmu& n) : nv(n) {
        nv.Initialize();
        nv.SetDriverMode(nv::kDriverModeDirect);
        nv.CreateHandle(&device, &handle);
    }
    void Frame(FakePlatform& platform, AdapterCore& core, Clock& clock, double t, const char* passes = "LR") {
        std::uint8_t on = 0;
        nv.IsActivated(handle, &on);
        if (!on) {
            platform.backbuffer = "mono";
        } else {
            for (const char* c = passes; *c; ++c) {
                nv.SetActiveEye(handle, *c == 'L' ? nv::kEyeLeft : *c == 'R' ? nv::kEyeRight : nv::kEyeMono);
                platform.backbuffer = *c == 'L' ? "red" : *c == 'R' ? "blue" : "hud";
            }
        }
        core.OnFrame(clock.Next(), Input(t));
    }
};
} // namespace

TEST_CASE(EmuEngineStereoTheater3DEndToEnd) {
    FakeImage image({}); // no bf4.exe bindings at all: this route needs none
    FakePlatform platform;
    SessionInfo session = Session(false);
    session.stereo = StereoSetting::Probe;
    session.stereoRoute = StereoRoute::Nvapi; // the 3D Vision (eye-switch) route
    AdapterCore core(session, X64(), image.Resolve(), platform);
    core.OnSwapchainAdopted(true);
    NvapiStereoEmu nv;
    nv.SetEyeCallback([&](Eye e) { core.OnStereoEye(e); });
    platform.shim = &nv; // the game loaded our nvapi64.dll
    SimEngine engine(nv);
    Clock clock;
    for (int i = 0; i < 500; ++i) engine.Frame(platform, core, clock, i / 90.0);

    CHECK(core.Stereo() == StereoMode::Theater3D);
    CHECK(core.StereoTracker().Pairs() == 500);
    bool eyesCorrect = !platform.captures.empty();
    for (const auto& [eye, content] : platform.captures) eyesCorrect = eyesCorrect && content == (eye == 0 ? "red" : "blue");
    CHECK(eyesCorrect && platform.captures.size() == 1000);
    int left = 0, right = 0;
    for (const auto& f : platform.frames) { left += f.eye == 0; right += f.eye == 1; }
    CHECK(left > 400 && left == right); // published as eye pairs once stereo is confirmed
    CHECK(platform.snapshots == 1);     // exactly one side-by-side snapshot
    CHECK(platform.reports.count("stereo-probe.txt") && platform.reports["stereo-probe.txt"].find("verdict: working") != std::string::npos);
    CHECK(core.Caps().Get(game::Feature::StereoDriver).state == game::FeatureState::Candidate);

    // Multiplayer evidence: the shim is told to report "stereo off" and the engine drops
    // back to one pass on its own.
    platform.punkBuster = true;
    for (int i = 0; i < 300 && !core.Detached(); ++i) engine.Frame(platform, core, clock, 10.0 + i / 90.0);
    CHECK(core.Detached() && platform.suppressed);
    std::uint8_t on = 1;
    nv.IsActivated(engine.handle, &on);
    CHECK(on == 0);
}

TEST_CASE(EmuEngineStereoOffIgnoresShim) {
    FakeImage image({});
    FakePlatform platform;
    AdapterCore core(Session(true), X64(), image.Resolve(), platform); // stereo Off by default
    core.OnSwapchainAdopted(true);
    NvapiStereoEmu nv;
    nv.SetEyeCallback([&](Eye e) { core.OnStereoEye(e); });
    platform.shim = &nv;
    SimEngine engine(nv);
    Clock clock;
    for (int i = 0; i < 50; ++i) engine.Frame(platform, core, clock, i / 90.0);
    CHECK(platform.captures.empty() && platform.snapshots == 0 && platform.reports.empty());
    CHECK(core.Stereo() == StereoMode::Off);
    CHECK(core.Caps().Get(game::Feature::StereoDriver).reason == "stereo off in launcher settings");
}

TEST_CASE(EmuInterleavedEngineStaysMonoAndSaysWhy) {
    FakeImage image({});
    FakePlatform platform;
    SessionInfo session = Session(false);
    session.stereo = StereoSetting::Probe;
    session.stereoRoute = StereoRoute::Nvapi; // the 3D Vision (eye-switch) route
    AdapterCore core(session, X64(), image.Resolve(), platform);
    core.OnSwapchainAdopted(true);
    NvapiStereoEmu nv;
    nv.SetEyeCallback([&](Eye e) { core.OnStereoEye(e); });
    platform.shim = &nv;
    SimEngine engine(nv);
    Clock clock;
    for (int i = 0; i < 460; ++i) engine.Frame(platform, core, clock, i / 90.0, "LRLR");
    CHECK(core.Stereo() == StereoMode::Off && platform.snapshots == 0);
    for (const auto& f : platform.frames) CHECK(f.eye == -1);
    CHECK(platform.reports["stereo-probe.txt"].find("verdict: interleaved") != std::string::npos);
}

namespace {
// Synthetic split-screen backbuffer: textured noise in the left half; the right half is the
// same view shifted by `disparity` px (engine SBS stereo) or unrelated noise (a mono frame).
void FillSplit(FakePlatform& p, int disparity, bool stereoFrame, bool flat = false) {
    const int w = 1024, h = 256;
    p.pixels.assign(static_cast<std::size_t>(w) * h * 4, 0);
    auto noise = [](int x, int y, int seed) {
        unsigned v = static_cast<unsigned>(x * 73856093) ^ static_cast<unsigned>(y * 19349663) ^ static_cast<unsigned>(seed * 83492791);
        v ^= v >> 13; v *= 0x5bd1e995u; v ^= v >> 15;
        return static_cast<std::uint8_t>(v & 0xFF);
    };
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const bool right = x >= w / 2;
            const int lx = right ? x - w / 2 : x;
            // Blocky noise (4 px cells) so a small sampled shift still lands on the same cell.
            const std::uint8_t v = flat ? 0 : right ? (stereoFrame ? noise((lx + disparity) / 4, y / 4, 1) : noise(lx / 4, y / 4, 2))
                                                     : noise(lx / 4, y / 4, 1);
            std::uint8_t* px = &p.pixels[(static_cast<std::size_t>(y) * w + x) * 4];
            px[0] = px[1] = px[2] = v;
            px[3] = 255;
        }
    p.pixelWidth = w;
    p.pixelHeight = h;
}
} // namespace

TEST_CASE(EmuSplitScreenStereoEndToEnd) {
    // RenderDevice.StereoEnable + StereoSplitScreenEnable: no shim, no vendor report.
    FakeImage image({});
    FakePlatform platform;
    SessionInfo session = Session(false);
    session.stereo = StereoSetting::Probe;
    AdapterCore core(session, X64(), image.Resolve(), platform);
    core.OnSwapchainAdopted(true);
    Clock clock;
    FillSplit(platform, 0, false, /*flat=*/true); // loading screen first: undecidable
    for (int i = 0; i < 200; ++i) core.OnFrame(clock.Next(), Input(i / 90.0));
    CHECK(core.Stereo() == StereoMode::Off);
    CHECK(core.Stereo3D().lastSplit.verdict == stereo::SplitVerdict::Undetermined && platform.snapshots == 0);
    FillSplit(platform, 6, true); // menu: engine side-by-side stereo
    for (int i = 0; i < 700; ++i) core.OnFrame(clock.Next(), Input(3.0 + i / 90.0));
    CHECK(core.Stereo3D().lastSplit.verdict == stereo::SplitVerdict::Stereo);
    CHECK(core.Stereo() == StereoMode::Theater3D);
    CHECK(core.Caps().Get(game::Feature::StereoDriver).state == game::FeatureState::Candidate);
    int pairs = 0;
    bool halfWidth = true;
    for (const auto& f : platform.frames)
        if (f.eye == 0) { ++pairs; halfWidth = halfWidth && f.width == 960; }
    CHECK(pairs > 300 && halfWidth);
    CHECK(platform.snapshots == 1 && platform.splitCaptures > 300);
    CHECK(platform.reports["stereo-probe.txt"].find("route: split-screen") != std::string::npos &&
          platform.reports["stereo-probe.txt"].find("verdict: working") != std::string::npos);
    CHECK(platform.shim == nullptr && !platform.spoofed); // nothing NVIDIA-specific was needed
}

TEST_CASE(EmuSplitScreenMonoFrameIsNotStereo) {
    FakeImage image({});
    FakePlatform platform;
    SessionInfo session = Session(false);
    session.stereo = StereoSetting::On;
    AdapterCore core(session, X64(), image.Resolve(), platform);
    core.OnSwapchainAdopted(true);
    Clock clock;
    FillSplit(platform, 0, false); // the engine ignored the setting: two different halves
    for (int i = 0; i < 700; ++i) core.OnFrame(clock.Next(), Input(i / 90.0));
    CHECK(core.Stereo3D().lastSplit.verdict == stereo::SplitVerdict::Mono);
    CHECK(core.Stereo() == StereoMode::Off && platform.splitCaptures == 0); // never shown as stereo
    for (const auto& f : platform.frames) CHECK(f.eye == -1);
    CHECK(core.Caps().Get(game::Feature::StereoDriver).reason == "split-screen: backbuffer is split-mono");
}