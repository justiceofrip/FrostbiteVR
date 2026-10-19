// Headless emulated BF4 sessions. A fake platform stands in for Win32/D3D11/MinHook and
// a synthetic .text image stands in for bf4.exe, with one unique pattern planted per
// binding. The REAL AdapterCore, BindingTable, SessionGuard, PointerInjector and reload
// router run unmodified. No game, GPU or headset.
#include "TestHarness.h"

#include "EmuFixtures.h"
#include "../../src/games/bf4/Bf4WorldView.h"
#include "fvr/runtime/ExactWrite.h"

#include <array>
#include <cstring>
#include <map>
#include <string>
#include <vector>

using namespace fvr;
using namespace fvr::bf4;
using fvrtest::Near;
using namespace fvr::bf4::emu;


TEST_CASE(EmuFrontEndTheaterAndMenuClick) {
    FakeImage image({});
    FakePlatform platform;
    AdapterCore core(Session(false), X64(), image.Resolve(), platform);
    core.OnSwapchainAdopted(true);
    Clock clock;
    // Developer off: Candidate features are not usable in a player session.
    for (int i = 0; i < 5; ++i) core.OnFrame(clock.Next(), Input(i / 90.0));
    CHECK(platform.frames.empty());

    FakePlatform devPlatform;
    AdapterCore dev(Session(true), X64(), image.Resolve(), devPlatform);
    dev.OnSwapchainAdopted(true);
    for (int i = 0; i < 90; ++i) {
        auto in = Input(i / 90.0);
        in.pointer.active = i >= 10 && i < 80;
        in.pointer.u = 0.5f;
        in.pointer.v = 0.25f;
        in.pointer.down = i >= 40 && i < 45;
        in.reloadButton = i == 50; // front end: must not reload
        dev.OnFrame(clock.Next(), in);
    }
    using T = PointerEvent::Type;
    std::vector<T> types;
    for (const auto& e : devPlatform.pointerLog) types.push_back(e.type);
    CHECK((types == std::vector<T>{T::Move, T::ButtonDown, T::ButtonUp, T::Deactivate}));
    CHECK(devPlatform.pointerLog[0].x == 960 && devPlatform.pointerLog[0].y == 270);
    CHECK(dev.Clicks() == 1);
    CHECK(devPlatform.frames.size() == 90 && devPlatform.frames.back().eye == -1);
    CHECK(devPlatform.cameraLog.empty() && devPlatform.reloads == 0);
    CHECK(dev.Stereo() == StereoMode::Off);
    CHECK(dev.Status().find("HeadTrackedCamera: disabled (needs GameRenderer.Instance unenrolled") != std::string::npos);
}

TEST_CASE(EmuCampaignNativeStereoRecenterReload) {
    FakeImage image(kAllBindings);
    FakePlatform platform;
    AdapterCore core(Session(true), X64(), image.Resolve(), platform);
    core.OnSwapchainAdopted(true);
    platform.Campaign();
    Clock clock;

    // Player stands turned 90 degrees left in their room; first pose sets the reference.
    auto turned = [](double t) {
        auto in = Input(t);
        const auto q = math::FromAxisAngle({0, 1, 0}, 1.5707963f);
        in.head.orientation = q;
        in.eyes[0].pose.orientation = q;
        in.eyes[1].pose.orientation = q;
        in.eyes[0].pose.position = math::Rotate(q, {-0.032f, 0, 0}) + in.head.position;
        in.eyes[1].pose.position = math::Rotate(q, {0.032f, 0, 0}) + in.head.position;
        return in;
    };
    for (int i = 0; i < 3; ++i) core.OnFrame(clock.Next(), turned(i / 90.0));
    CHECK(core.Stereo() == StereoMode::Native);
    CHECK(platform.cameraLog.size() == 3);
    const auto& pair = platform.cameraLog.back();
    CHECK(pair.size() == 2 && pair[0].eye == 0 && pair[1].eye == 1);
    // Recentered: the game camera looks straight ahead and eyes sit +-32 mm on its X axis.
    CHECK(Near(pair[0].local.forward.z, 1.0f) && Near(pair[0].local.trans.x, -0.032f) && Near(pair[1].local.trans.x, 0.032f));
    CHECK(Near(pair[0].fov.left, -0.95f) && Near(pair[1].fov.right, 0.95f));

    // Button reload: one request per press, none while held, none in menus.
    for (int i = 0; i < 20; ++i) {
        auto in = turned(1.0 + i / 90.0);
        in.reloadButton = i >= 2 && i < 12;
        core.OnFrame(clock.Next(), in);
    }
    CHECK(platform.reloads == 1);

    // Manual chosen from the in-headset menu: still button press, with the note.
    core.SetReloadMode(interaction::ReloadMode::Manual);
    CHECK(core.ReloadMode().effective == interaction::ReloadMode::ButtonPress && !core.ReloadMode().note.empty());
    for (int i = 0; i < 6; ++i) {
        auto in = turned(2.0 + i / 90.0);
        in.reloadButton = i >= 2;
        core.OnFrame(clock.Next(), in);
    }
    CHECK(platform.reloads == 2);

    // Recenter request edge counts once.
    for (int i = 0; i < 5; ++i) {
        auto in = Input(3.0 + i / 90.0);
        in.recenterRequested = true;
        core.OnFrame(clock.Next(), in);
    }
    CHECK(core.Stats().recenters == 1);
}

TEST_CASE(EmuAlternateEyeFallback) {
    FakeImage image({names::kGameRenderer, names::kRenderViewUpdate, names::kStereoActiveEye, names::kClientGameContext,
                     names::kLevelName});
    FakePlatform platform;
    AdapterCore core(Session(true), X64(), image.Resolve(), platform);
    core.OnSwapchainAdopted(true);
    platform.Campaign();
    Clock clock;
    for (int i = 0; i < 6; ++i) core.OnFrame(clock.Next(), Input(i / 90.0));
    CHECK(core.Stereo() == StereoMode::AlternateEye);
    std::vector<int> eyes;
    for (const auto& f : platform.frames) eyes.push_back(f.eye);
    CHECK((eyes == std::vector<int>{0, 1, 0, 1, 0, 1}));
    CHECK(core.Caps().Get(game::Feature::StereoNative).reason.find("Renderer.StereoEyeLoop unenrolled") != std::string::npos);
    // Back to the front end: camera control stops and the next run restarts on the left eye.
    platform.level = "";
    core.OnFrame(clock.Next(), Input(1.0));
    CHECK(core.Stereo() == StereoMode::Off);
    platform.Campaign();
    core.OnFrame(clock.Next(), Input(1.1));
    CHECK(platform.frames.back().eye == 0);
}

TEST_CASE(EmuLockdownReleasesHeldClickAndStops) {
    FakeImage image(kAllBindings);
    FakePlatform platform;
    AdapterCore core(Session(true), X64(), image.Resolve(), platform);
    core.OnSwapchainAdopted(true);
    Clock clock;
    auto held = [](double t) {
        auto in = Input(t);
        in.pointer.active = true;
        in.pointer.u = in.pointer.v = 0.5f;
        in.pointer.down = true;
        return in;
    };
    core.OnFrame(clock.Next(), held(0.0));
    core.OnFrame(clock.Next(), held(0.01));
    platform.levelKnown = true;
    platform.level = "Levels/MP_Siege/MP_Siege"; // joined a multiplayer server mid-click
    core.OnFrame(clock.Next(), held(0.02));
    using T = PointerEvent::Type;
    CHECK(platform.pointerLog.size() == 4 && platform.pointerLog[2].type == T::ButtonUp &&
          platform.pointerLog[3].type == T::Deactivate);
    CHECK(core.Detached() && platform.detaches == 1);
    const auto framesAtLock = platform.frames.size();
    for (int i = 0; i < 10; ++i) core.OnFrame(clock.Next(), held(1.0 + i));
    CHECK(platform.frames.size() == framesAtLock && platform.detaches == 1);
    CHECK(core.Status().find("GameMenuPointer: disabled (locked: non-campaign level") != std::string::npos);
}

TEST_CASE(EmuPunkBusterScanCadence) {
    FakeImage image({});
    FakePlatform platform;
    AdapterCore core(Session(true), X64(), image.Resolve(), platform);
    core.OnSwapchainAdopted(true);
    Clock clock;
    for (int i = 0; i < 90; ++i) core.OnFrame(clock.Next(), Input(i / 90.0)); // ~1 s
    CHECK(platform.moduleScans == 1);
    platform.punkBuster = true;
    int framesUntilLock = 0;
    while (!core.Detached() && framesUntilLock < 400) {
        core.OnFrame(clock.Next(), Input(2.0));
        ++framesUntilLock;
    }
    CHECK(core.Detached() && framesUntilLock <= 2000 / 11 + 1);
    CHECK(core.Guard().Reason().find("PunkBuster") != std::string::npos);
}

TEST_CASE(EmuWrongArchNeverRuns) {
    FakeImage image(kAllBindings);
    FakePlatform platform;
    BuildIdentity b;
    b.verdict = BuildVerdict::WrongArch;
    b.label = "bf4_x86.exe";
    AdapterCore core(Session(true), b, image.Resolve(), platform);
    core.OnSwapchainAdopted(true);
    Clock clock;
    core.OnFrame(clock.Next(), Input(0));
    CHECK(core.Detached() && platform.frames.empty() && platform.moduleScans == 0);
}

// ---- exact-write restoration -------------------------------------------------------------
namespace {
struct FakeMemory final : runtime::IMemory {
    std::array<std::uint8_t, 64> bytes{};
    bool Read(std::uint64_t a, void* out, std::size_t n) override {
        if (a + n > bytes.size()) return false;
        std::memcpy(out, &bytes[a], n);
        return true;
    }
    bool Write(std::uint64_t a, const void* in, std::size_t n) override {
        if (a + n > bytes.size()) return false;
        std::memcpy(&bytes[a], in, n);
        return true;
    }
    float F(std::uint64_t a) { float f; std::memcpy(&f, &bytes[a], 4); return f; }
    void SetF(std::uint64_t a, float f) { std::memcpy(&bytes[a], &f, 4); }
};
} // namespace

TEST_CASE(ExactWriteRestoresOnEveryOutcome) {
    FakeMemory mem;
    mem.SetF(0, 1.2f); // game FOV
    mem.SetF(8, 3.0f);
    {
        runtime::ExactWriteJournal j(mem);
        CHECK(j.Write(0, 1.6f) && j.Write(0, 1.7f)); // overlapping writes
        CHECK(j.Write(8, 9.0f));
        CHECK(!j.Write(62, 0.0f)); // out of range: refused, nothing recorded
        CHECK(Near(mem.F(0), 1.7f) && j.Pending() == 3);
    } // scope exit = early return / exception path
    CHECK(Near(mem.F(0), 1.2f) && Near(mem.F(8), 3.0f));

    runtime::ExactWriteJournal j(mem);
    j.Write(0, 1.6f);
    mem.SetF(0, 1.4f); // the game wrote its own value afterwards
    const auto r = j.RestoreAll();
    CHECK(r.leftForeign == 1 && r.restored == 0 && Near(mem.F(0), 1.4f));
}

TEST_CASE(EmuMonoWorldLaneCameraOnlyForHostLeases) {
    // Camera bindings without any stereo path: the head-tracked mono view (world lane).
    FakeImage image({names::kGameRenderer, names::kRenderViewUpdate, names::kClientGameContext, names::kLevelName});
    FakePlatform platform;
    AdapterCore core(Session(true), X64(), image.Resolve(), platform);
    core.OnSwapchainAdopted(true);
    platform.Campaign();
    Clock clock;
    for (int i = 0; i < 3; ++i) core.OnFrame(clock.Next(), Input(i / 90.0));
    CHECK(core.Stereo() == StereoMode::Mono);
    // No eye-pair request from the host yet: the camera stays the game's, frames ask for the world lane.
    CHECK(platform.cameraLog.empty());
    CHECK(!platform.frames.empty() && platform.frames.back().world && platform.frames.back().worldSettle == 2);

    auto in = Input(0.1);
    in.world.valid = true;
    in.world.poseId = 41;
    in.world.head = in.head;
    in.world.fov[0] = {-1.0f, 0.9f, 1.0f, -1.1f};
    in.world.fov[1] = {-0.9f, 1.0f, 1.0f, -1.1f};
    core.OnFrame(clock.Next(), in);
    CHECK(platform.cameraLog.size() == 1);
    const auto& r = platform.cameraLog.back();
    CHECK(r.size() == 1 && r[0].eye == -1 && r[0].poseId == 41);
    CHECK(Near(r[0].renderFovY, RequiredVerticalFov(in.world.fov[0], in.world.fov[1], 1920.0f / 1080.0f), 1e-5f));
    CHECK(Near(r[0].local.forward.z, 1.0f)); // the first pose is the reference: straight ahead

    // Back in the front end: no world lane, no camera.
    platform.level = "Levels/FrontEnd/FrontEnd";
    core.OnFrame(clock.Next(), in);
    CHECK(core.Stereo() == StereoMode::Off && platform.cameraLog.size() == 1 && !platform.frames.back().world);
}
