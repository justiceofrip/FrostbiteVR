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

    // Manual chosen from the in-headset menu: the magazine gesture (virtual pad), with its
    // instructions as the note; the native reload binding is no longer pressed by B.
    core.SetReloadMode(interaction::ReloadMode::Manual);
    CHECK(core.ReloadMode().effective == interaction::ReloadMode::Manual && !core.ReloadMode().note.empty());
    for (int i = 0; i < 6; ++i) {
        auto in = turned(2.0 + i / 90.0);
        in.reloadButton = i >= 2;
        core.OnFrame(clock.Next(), in);
    }
    CHECK(platform.reloads == 1);

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

TEST_CASE(EmuControllerAimInTheHeadTrackedView) {
    FakeImage image({names::kGameRenderer, names::kRenderViewUpdate, names::kClientGameContext, names::kLevelName});
    FakePlatform platform;
    AdapterCore core(Session(true), X64(), image.Resolve(), platform);
    core.OnSwapchainAdopted(true);
    platform.Campaign();
    platform.aimKnown = true;
    // The aim loop itself: 30 degrees from the head is beyond the gun's reach when the gun
    // follows the aim (EmuGunFollowsAimKeepsTheBulletsOnTheBarrel).
    CHECK(core.SetMenuSetting("gun_aim", 0));
    Clock clock;
    auto frame = [&](double t, float handYaw, float stickX, float leftY) {
        auto in = Input(t);
        in.world.valid = true;
        in.world.poseId = 100 + static_cast<std::uint64_t>(t * 90);
        in.world.head = in.head;
        in.world.fov[0] = {-1.0f, 0.9f, 1.0f, -1.1f};
        in.world.fov[1] = {-0.9f, 1.0f, 1.0f, -1.1f};
        in.controls.focused = true;
        in.controls.generation = 1 + static_cast<std::uint64_t>(t * 90);
        in.controls.spaceGeneration = 1;
        in.controls.predictedNs = 1000000 + static_cast<std::int64_t>(t * 1e9);
        for (auto& h : in.controls.hands) h.active = interaction::Components;
        auto& right = in.controls.hands[1];
        right.aimTracked = right.gripTracked = true;
        right.aim.orientation = math::FromAxisAngle({0, 1, 0}, -handYaw); // positive = right
        right.stickX = stickX;
        in.controls.hands[0].gripTracked = true;
        in.controls.hands[0].stickY = leftY;
        platform.GameFrame();
        core.OnFrame(clock.Next(), in);
    };
    for (int i = 0; i < 3; ++i) frame(i / 90.0, 0.0f, 0, 0);
    CHECK(core.Stereo() == StereoMode::Mono);
    // Point the gun 30 degrees right of the body and walk forward.
    for (int i = 0; i < 200; ++i) frame(0.1 + i / 90.0, 0.5235988f, 0, 1.0f);
    CHECK(platform.mouseCalls > 0);
    CHECK(Near(WrapPi(platform.gameAim.yaw - 0.5f), 0.5235988f, 0.02f)); // game aim = body + controller
    const auto& r = platform.cameraLog.back();
    CHECK(r.size() == 1 && Near(r[0].parentYaw, 0.5f, 1e-3f)); // the view keeps the body heading
    const auto pad = core.Pad();
    CHECK(pad.thumbRX == 0 && pad.thumbRY == 0); // no stick look while the gun aims by mouse
    // Forward on the stick is turned back by the 30 degree aim offset: forward + left strafe.
    CHECK(pad.thumbLY > 25000 && pad.thumbLX < -12000);
    // The right stick turns the body: the view heading moves and the aim follows.
    for (int i = 0; i < 45; ++i) frame(2.5 + i / 90.0, 0.5235988f, 1.0f, 0);
    const float body = platform.cameraLog.back()[0].parentYaw;
    CHECK(body > 0.9f);
    for (int i = 0; i < 120; ++i) frame(3.0 + i / 90.0, 0.5235988f, 0, 0);
    CHECK(Near(WrapPi(platform.gameAim.yaw - body), 0.5235988f, 0.02f));
    // Developer sessions keep a trace of the aim updates (research `aimtrace`).
    const std::string trace = core.AimTraceCsv();
    CHECK(trace.rfind("tick_ms,sample,game_yaw,", 0) == 0 && std::count(trace.begin(), trace.end(), '\n') > 300);
    // Opening the front end releases it: no more mouse movement.
    platform.level = "Levels/FrontEnd/FrontEnd";
    const int calls = platform.mouseCalls;
    for (int i = 0; i < 5; ++i) frame(5.0 + i / 90.0, 1.0f, 0, 0);
    CHECK(platform.mouseCalls == calls);
}

TEST_CASE(EmuHolstersAndMagazineReloadInACampaign) {
    FakeImage image({names::kGameRenderer, names::kRenderViewUpdate, names::kClientGameContext, names::kLevelName});
    FakePlatform platform;
    AdapterCore core(Session(true, interaction::ReloadMode::Manual), X64(), image.Resolve(), platform);
    core.OnSwapchainAdopted(true);
    platform.Campaign();
    CHECK(core.ReloadMode().effective == interaction::ReloadMode::Manual); // magazine gesture via the pad
    Clock clock;
    std::uint64_t generation = 1;
    math::Vec3 left{-0.25f, 1.20f, -0.30f}, right{0.20f, 1.30f, -0.40f};
    float leftGrip = 0, rightGrip = 0, rightTrigger = 0;
    bool b = false;
    auto frame = [&] {
        auto in = Input(0);
        in.controls.focused = in.controls.headValid = true;
        in.controls.generation = ++generation;
        in.controls.spaceGeneration = 1;
        in.controls.predictedNs = static_cast<std::int64_t>(generation) * 11000000;
        in.controls.head = in.head;
        in.controls.head.orientation = {0, 0, 0, 1};
        for (auto& h : in.controls.hands) {
            h.active = interaction::Components;
            h.gripTracked = h.aimTracked = true;
            h.grip.orientation = h.aim.orientation = {0, 0, 0, 1};
        }
        in.controls.hands[0].grip.position = left;
        in.controls.hands[1].grip.position = right;
        in.controls.hands[0].squeeze = leftGrip;
        in.controls.hands[1].squeeze = rightGrip;
        in.controls.hands[1].trigger = rightTrigger;
        if (b) in.controls.hands[1].held |= interaction::Secondary;
        core.OnFrame(clock.Next(), in);
    };
    for (int i = 0; i < 3; ++i) frame();
    // Holding the gun with the right grip is not BF4's grenade button any more.
    rightGrip = 1;
    rightTrigger = 1;
    frame();
    CHECK(!(core.Pad().buttons & interaction::pad::RightShoulder) && core.Pad().rightTrigger > 200);
    rightGrip = 0;
    frame();
    // Right hip: the sidearm key reaches the game, with a pulse on the right controller.
    right = core.Gestures().SlotPosition("hip_right");
    frame();
    rightGrip = 1;
    frame();
    CHECK(!platform.keyPresses.empty() && platform.keyPresses.back() == gamekey::Secondary);
    CHECK(!platform.haptics.empty() && platform.haptics.back().first == 1);
    for (int i = 0; i < 12; ++i) frame(); // ~130 ms: the key goes up again
    CHECK(platform.keys[0] == 0 && platform.keys[1] == 0);
    rightGrip = 0;
    right = {0.20f, 1.30f, -0.40f};
    frame();
    // B drops the magazine: the trigger no longer reaches the game.
    b = true;
    frame();
    b = false;
    frame();
    CHECK(core.Pad().rightTrigger == 0 && core.Gestures().MagazineOut(Bf4Gun::Secondary));
    // Left hand: belt, then the magazine well -> the pad's reload (X), fire back.
    left = core.Gestures().SlotPosition("belt_front_left");
    frame();
    leftGrip = 1;
    frame();
    CHECK(!(core.Pad().buttons & interaction::pad::LeftShoulder)); // the grab is not BF4's spot button
    left = BodyGestures::Magwell({right, {0, 0, 0, 1}});
    frame();
    CHECK((core.Pad().buttons & interaction::pad::X) && core.Pad().rightTrigger > 200);
    CHECK(core.Gestures().Stats().seats == 1);
    // The mod menu takes the input: no keys, the pad goes neutral, the state waits.
    leftGrip = 0;
    frame();
    CHECK(core.Status().find("gestures gun=sidearm") != std::string::npos);
}

TEST_CASE(EmuControllerAimHoldsAcrossLeaseGaps) {
    // Live, the world lane holds a lease on most frames but not all: aiming must not
    // re-anchor the body heading (a view jump by the controller's offset) at every gap.
    FakeImage image({names::kGameRenderer, names::kRenderViewUpdate, names::kClientGameContext, names::kLevelName});
    FakePlatform platform;
    AdapterCore core(Session(true), X64(), image.Resolve(), platform);
    core.OnSwapchainAdopted(true);
    platform.Campaign();
    platform.aimKnown = true;
    CHECK(core.SetMenuSetting("gun_aim", 0)); // 30 degrees from the head is beyond the gun's reach
    Clock clock;
    int n = 0;
    auto frame = [&](float handYaw) {
        const double t = n / 90.0;
        auto in = Input(t);
        in.world.valid = n % 3 != 2; // every third frame has no lease
        in.world.poseId = 100 + static_cast<std::uint64_t>(n);
        in.world.head = in.head;
        in.world.fov[0] = {-1.0f, 0.9f, 1.0f, -1.1f};
        in.world.fov[1] = {-0.9f, 1.0f, 1.0f, -1.1f};
        in.controls.focused = true;
        in.controls.generation = 1 + static_cast<std::uint64_t>(n);
        in.controls.spaceGeneration = 1;
        in.controls.predictedNs = 1000000 + static_cast<std::int64_t>(t * 1e9);
        for (auto& h : in.controls.hands) h.active = interaction::Components;
        auto& right = in.controls.hands[1];
        right.aimTracked = right.gripTracked = true;
        right.aim.orientation = math::FromAxisAngle({0, 1, 0}, -handYaw);
        in.controls.hands[0].gripTracked = true;
        platform.GameFrame();
        core.OnFrame(clock.Next(), in);
        ++n;
    };
    for (int i = 0; i < 3; ++i) frame(0);
    std::vector<float> body;
    for (int i = 0; i < 240; ++i) {
        frame(0.5235988f);
        const auto& r = platform.cameraLog.back();
        if (!r.empty() && std::isfinite(r[0].parentYaw)) body.push_back(r[0].parentYaw);
    }
    CHECK(body.size() > 100);
    for (const float b : body) CHECK(Near(b, body.front(), 1e-4f)); // one heading throughout
    CHECK(Near(WrapPi(platform.gameAim.yaw - body.front()), 0.5235988f, 0.02f));
    CHECK(core.Status().find("controller_aim=engaged") != std::string::npos);
}

TEST_CASE(EmuControllerAimSettlesWithLongerInputDelays) {
    // The game reads mouse input one to three frames late depending on its threading and
    // frame rate: aiming must settle on the controller without swinging past it.
    for (const int delay : {1, 2, 3}) {
        FakeImage image({names::kGameRenderer, names::kRenderViewUpdate, names::kClientGameContext, names::kLevelName});
        FakePlatform platform;
        platform.aimDelay = delay;
        AdapterCore core(Session(true), X64(), image.Resolve(), platform);
        core.OnSwapchainAdopted(true);
        platform.Campaign();
        platform.aimKnown = true;
        CHECK(core.SetMenuSetting("gun_aim", 0)); // the swings go beyond the gun's reach
        Clock clock;
        int n = 0;
        auto frame = [&](float handYaw) {
            auto in = Input(n / 90.0);
            in.world.valid = true;
            in.world.poseId = 100 + static_cast<std::uint64_t>(n);
            in.world.head = in.head;
            in.world.fov[0] = {-1.0f, 0.9f, 1.0f, -1.1f};
            in.world.fov[1] = {-0.9f, 1.0f, 1.0f, -1.1f};
            in.controls.focused = true;
            in.controls.generation = 1 + static_cast<std::uint64_t>(n);
            in.controls.spaceGeneration = 1;
            in.controls.predictedNs = 1000000 + static_cast<std::int64_t>(n) * 11000000;
            for (auto& h : in.controls.hands) h.active = interaction::Components;
            auto& right = in.controls.hands[1];
            right.aimTracked = right.gripTracked = true;
            right.aim.orientation = math::FromAxisAngle({0, 1, 0}, -handYaw);
            platform.GameFrame();
            core.OnFrame(clock.Next(), in);
            ++n;
        };
        for (int i = 0; i < 3; ++i) frame(0);
        const float body = 0.5f; // the game's starting yaw becomes the body heading
        float worstAfter = 0;
        for (int i = 0; i < 240; ++i) {
            frame(0.5235988f);
            const float error = std::fabs(WrapPi(platform.gameAim.yaw - body - 0.5235988f));
            if (i >= 120) worstAfter = std::max(worstAfter, error); // after two seconds
        }
        CHECK(worstAfter < 0.02f); // settled, and stays there
    }
}

TEST_CASE(EmuControllerAimRecoversWhenTheGameIsMoreSensitiveThanLearned) {
    // Learned while aiming down sights, then the game turns three times more per count
    // (sights off): the steering must not swing out of control while it re-learns.
    for (const int delay : {1, 2}) {
        FakeImage image({names::kGameRenderer, names::kRenderViewUpdate, names::kClientGameContext, names::kLevelName});
        FakePlatform platform;
        platform.aimDelay = delay;
        platform.mouseGainX = 0.0004f;
        AdapterCore core(Session(true), X64(), image.Resolve(), platform);
        core.OnSwapchainAdopted(true);
        platform.Campaign();
        platform.aimKnown = true;
        Clock clock;
        int n = 0;
        auto frame = [&](float handYaw) {
            auto in = Input(n / 90.0);
            in.world.valid = true;
            in.world.poseId = 100 + static_cast<std::uint64_t>(n);
            in.world.head = in.head;
            in.world.fov[0] = {-1.0f, 0.9f, 1.0f, -1.1f};
            in.world.fov[1] = {-0.9f, 1.0f, 1.0f, -1.1f};
            in.controls.focused = true;
            in.controls.generation = 1 + static_cast<std::uint64_t>(n);
            in.controls.spaceGeneration = 1;
            in.controls.predictedNs = 1000000 + static_cast<std::int64_t>(n) * 11000000;
            for (auto& h : in.controls.hands) h.active = interaction::Components;
            auto& right = in.controls.hands[1];
            right.aimTracked = right.gripTracked = true;
            right.aim.orientation = math::FromAxisAngle({0, 1, 0}, -handYaw);
            platform.GameFrame();
            core.OnFrame(clock.Next(), in);
            ++n;
        };
        for (int i = 0; i < 3; ++i) frame(0);
        // Learn: sweep the hand so the game turns and the gain is measured.
        for (int i = 0; i < 600; ++i) frame(0.5f * std::sin(i / 40.0f));
        // Sights off while the hand holds still, then a quick turn to -0.35 rad (~0.25 s).
        // (A target teleported in the same frame as the change would overshoot by about
        // 1 rad at three frames of delay whatever the guard does: the counts go out before
        // the game shows any of them. Teleports at an unchanged gain: Bf4CoreTests.)
        const float from = 0.5f * std::sin(599 / 40.0f);
        for (int i = 0; i < 30; ++i) frame(from);
        platform.mouseGainX *= 3.0f;
        platform.gameFov = 0.95f; // leaving the sights: BF4 widens its view as it raises the sensitivity
        float worstPast = 0, worstLate = 0;
        for (int i = 0; i < 360; ++i) {
            frame(i < 22 ? from + (-0.35f - from) * float(i + 1) / 22.0f : -0.35f);
            const float past = WrapPi(-0.35f + 0.5f - platform.gameAim.yaw); // > 0: beyond the target
            worstPast = std::max(worstPast, past);
            if (i >= 240) worstLate = std::max(worstLate, std::fabs(past));
        }
        CHECK(worstPast < 0.2f);   // never swung far past the controller
        CHECK(worstLate < 0.02f);  // and settled on it
    }
}

TEST_CASE(EmuBf4SplitStereoStaysOffAsUnsupported) {
    // Live 2026-10-19: two views overrun BF4's one-view world renderer (BF4-STEREO.md), so
    // the developer item is shown disabled and nothing switches the route on.
    FakeImage image({names::kGameRenderer, names::kRenderViewUpdate, names::kClientGameContext, names::kLevelName});
    FakePlatform platform;
    AdapterCore core(Session(true), X64(), image.Resolve(), platform);
    core.OnSwapchainAdopted(true);
    CHECK(!core.SetMenuSetting("world_stereo", 1)); // disabled item: refused
    core.SetWorldStereo(true);
    CHECK(!core.MenuSettings().worldStereo);
    CHECK(core.SetMenuSetting("depth_stereo", 1)); // the depth route is the stereo option
    CHECK(core.MenuSettings().depthStereo);
}

TEST_CASE(EmuWalkTowardTheGunLeavesTheStickAlone) {
    // Vehicles: the left stick is throttle and steering, so "Walk toward: Gun" must hand it
    // to the game unturned while the gun aims off the body heading.
    FakeImage image({names::kGameRenderer, names::kRenderViewUpdate, names::kClientGameContext, names::kLevelName});
    FakePlatform platform;
    AdapterCore core(Session(true), X64(), image.Resolve(), platform);
    core.OnSwapchainAdopted(true);
    platform.Campaign();
    platform.aimKnown = true;
    Clock clock;
    int n = 0;
    auto input = [&] {
        auto in = Input(n / 90.0);
        in.world.valid = true;
        in.world.poseId = 100 + static_cast<std::uint64_t>(n);
        in.world.head = in.head;
        in.world.fov[0] = {-1.0f, 0.9f, 1.0f, -1.1f};
        in.world.fov[1] = {-0.9f, 1.0f, 1.0f, -1.1f};
        in.controls.focused = true;
        in.controls.generation = 1 + static_cast<std::uint64_t>(n);
        in.controls.spaceGeneration = 1;
        in.controls.predictedNs = 1000000 + static_cast<std::int64_t>(n) * 11000000;
        for (auto& h : in.controls.hands) h.active = interaction::Components;
        auto& right = in.controls.hands[1];
        right.aimTracked = right.gripTracked = true;
        right.aim.orientation = math::FromAxisAngle({0, 1, 0}, -0.5235988f); // gun 30 degrees right
        in.controls.hands[0].stickY = 1.0f;
        ++n;
        return in;
    };
    auto frame = [&](ControllerSnapshot in) {
        platform.GameFrame();
        core.OnFrame(clock.Next(), in);
    };
    for (int i = 0; i < 200; ++i) frame(input());
    CHECK(core.Pad().thumbLX < -12000); // body-relative walking: the stick is turned back
    // Menu: "Walk toward" -> Gun.
    auto in = input();
    in.menuButton = true;
    frame(in);
    const ui::Widget* walk = nullptr;
    for (const auto& w : core.ModMenuPanel().Widgets())
        if (w.text.find(L"Walk toward") != std::wstring::npos) walk = &w;
    CHECK(walk != nullptr && walk->text.find(L"Body") != std::wstring::npos);
    const float u = (walk->rect.x + walk->rect.w * 0.5f) / 1280.0f, v = (walk->rect.y + walk->rect.h * 0.5f) / 720.0f;
    for (bool down : {false, true, false}) {
        in = input();
        in.pointer = {true, u, v, down};
        frame(in);
    }
    CHECK(core.MenuSettings().walkWithBody == false);
    in = input();
    in.menuButton = true; // close
    frame(in);
    for (int i = 0; i < 30; ++i) frame(input());
    CHECK(!core.ModMenuOpen() && core.Pad().thumbLX == 0 && core.Pad().thumbLY > 32000);
}

TEST_CASE(EmuNextLeasePoseReachesTheCameraInTheCapturingFrame) {
    // After a Present captures the pose it carried, the next queued request's pose goes to
    // the camera in that same frame, so the very next submit carries it.
    FakeImage image({names::kGameRenderer, names::kRenderViewUpdate, names::kClientGameContext, names::kLevelName});
    FakePlatform platform;
    AdapterCore core(Session(true), X64(), image.Resolve(), platform);
    core.OnSwapchainAdopted(true);
    platform.Campaign();
    Clock clock;
    auto in = Input(0);
    in.world.valid = true;
    in.world.poseId = 7;
    in.world.head = in.head;
    in.world.fov[0] = {-1.0f, 0.9f, 1.0f, -1.1f};
    in.world.fov[1] = {-0.9f, 1.0f, 1.0f, -1.1f};
    for (int i = 0; i < 3; ++i) core.OnFrame(clock.Next(), in);
    CHECK(core.Stereo() == StereoMode::Mono && core.Stats().earlyPoses == 0); // same lease: once per frame
    const std::size_t before = platform.cameraLog.size();
    platform.nextWorld = in.world; // this Present captured pose 7; pose 8 waits in the queue
    platform.nextWorld.poseId = 8;
    platform.nextWorld.head.position.x += 0.05f;
    core.OnFrame(clock.Next(), in);
    CHECK(platform.cameraLog.size() == before + 2);
    CHECK(platform.cameraLog[before][0].poseId == 7 && platform.cameraLog[before + 1][0].poseId == 8);
    CHECK(core.Stats().earlyPoses == 1);
    platform.nextWorld = {}; // nothing queued: no second camera write
    core.OnFrame(clock.Next(), in);
    CHECK(platform.cameraLog.size() == before + 3);
}

namespace {
// A campaign frame on the world lane with a full controller sample (hands tracked, head valid).
struct LaneFrame {
    ControllerSnapshot in;
    explicit LaneFrame(double t, std::uint64_t generation) : in(Input(t)) {
        in.world.valid = true;
        in.world.poseId = 100 + generation;
        in.world.head = in.head;
        in.world.fov[0] = {-1.0f, 0.9f, 1.0f, -1.1f};
        in.world.fov[1] = {-0.9f, 1.0f, 1.0f, -1.1f};
        auto& c = in.controls;
        c.focused = c.headValid = true;
        c.head = c.referenceHead = in.head;
        c.generation = generation;
        c.spaceGeneration = 1;
        c.predictedNs = 1000000 + static_cast<std::int64_t>(t * 1e9);
        for (auto& h : c.hands) {
            h.active = interaction::Components;
            h.gripTracked = h.aimTracked = true;
            h.grip.position = {0.2f, 1.3f, -0.3f};
            h.aim = h.grip;
        }
    }
};
} // namespace

TEST_CASE(EmuGameMenuInALevelGoesToTheScreen) {
    // BF4's own menu in a level (pause) shows the game's cursor (live, 2026-10-19): the view
    // goes to the screen for the laser and the gun stops steering the game's cursor; back to
    // the head-tracked view when it closes.
    FakeImage image({names::kGameRenderer, names::kRenderViewUpdate, names::kClientGameContext, names::kLevelName});
    FakePlatform platform;
    AdapterCore core(Session(true), X64(), image.Resolve(), platform);
    core.OnSwapchainAdopted(true);
    platform.Campaign();
    platform.aimKnown = true;
    Clock clock;
    std::uint64_t n = 0;
    auto frame = [&](bool cursor, bool pointerTrigger = false) {
        LaneFrame l(n / 90.0, ++n);
        l.in.controls.hands[1].aim.orientation = math::FromAxisAngle({0, 1, 0}, -0.2f); // 0.2 rad right
        if (pointerTrigger) {
            l.in.pointer.active = true;
            l.in.controls.hands[1].trigger = 1.0f;
        }
        auto f = clock.Next();
        f.gameCursor = cursor;
        platform.GameFrame();
        core.OnFrame(f, l.in);
    };
    for (int i = 0; i < 60; ++i) frame(false);
    CHECK(core.Stereo() == StereoMode::Mono && !core.GameMenuOnScreen() && platform.mouseCalls > 0);
    // A cursor for a frame or two is no menu.
    frame(true);
    frame(true);
    frame(false);
    CHECK(core.Stereo() == StereoMode::Mono && !core.GameMenuOnScreen());
    for (int i = 0; i < 6; ++i) frame(true);
    CHECK(core.GameMenuOnScreen() && core.Stereo() == StereoMode::Off && !platform.frames.back().world);
    const int calls = platform.mouseCalls;
    for (int i = 0; i < 20; ++i) frame(true, true);
    CHECK(platform.mouseCalls == calls);         // no mouse look into the menu
    CHECK(platform.pad.rightTrigger == 0);       // the trigger at the screen clicks, not fires
    CHECK(core.Status().find("game_menu=on") != std::string::npos);
    for (int i = 0; i < 6; ++i) frame(false);
    CHECK(!core.GameMenuOnScreen() && core.Stereo() == StereoMode::Mono && core.Stats().gameMenus == 1);
    for (int i = 0; i < 30; ++i) frame(false);
    CHECK(platform.mouseCalls > calls); // aiming again
    // The front end's menus are on the screen anyway: not counted as an in-level menu.
    platform.level = "Levels/FrontEnd/FrontEnd";
    for (int i = 0; i < 10; ++i) frame(true);
    CHECK(!core.GameMenuOnScreen() && core.Stats().gameMenus == 1);
}

TEST_CASE(EmuBothSticksHeldRecenterWithoutReachingTheGame) {
    // BF4's host runs without --roomscale, so the adapter takes the both-sticks hold itself;
    // neither click reaches the game meanwhile (live, 2026-10-19: the right one drew the knife).
    FakeImage image({names::kGameRenderer, names::kRenderViewUpdate, names::kClientGameContext, names::kLevelName});
    FakePlatform platform;
    AdapterCore core(Session(true), X64(), image.Resolve(), platform);
    core.OnSwapchainAdopted(true);
    platform.Campaign();
    Clock clock;
    std::uint64_t n = 0;
    bool leaked = false;
    auto frame = [&](float headYaw, bool left, bool right) {
        LaneFrame l(n / 90.0, ++n);
        const auto q = math::FromAxisAngle({0, 1, 0}, headYaw);
        l.in.head.orientation = l.in.world.head.orientation = l.in.controls.head.orientation = q;
        if (left) l.in.controls.hands[0].held |= interaction::StickClick;
        if (right) l.in.controls.hands[1].held |= interaction::StickClick;
        core.OnFrame(clock.Next(), l.in);
        leaked = leaked || (platform.pad.buttons & (interaction::pad::LeftThumb | interaction::pad::RightThumb));
    };
    for (int i = 0; i < 5; ++i) frame(0, false, false);
    // Turned 0.5 rad left; the view follows the head until the recenter.
    for (int i = 0; i < 5; ++i) frame(0.5f, false, false);
    CHECK(Near(platform.cameraLog.back()[0].local.forward.x, std::sin(0.5f), 1e-3f)); // back row of a left turn
    frame(0.5f, false, true); // the right stick a frame early
    for (int i = 0; i < 100; ++i) frame(0.5f, true, true); // 1.1 s
    frame(0.5f, true, false);
    for (int i = 0; i < 5; ++i) frame(0.5f, false, false);
    CHECK(!leaked);
    CHECK(core.Stats().recenters == 1 && core.Stats().recenterGestures == 1);
    CHECK(Near(platform.cameraLog.back()[0].local.forward.z, 1.0f, 1e-3f)); // straight ahead again
    // A lone click still reaches the game (sprint).
    for (int i = 0; i < 12; ++i) frame(0.5f, true, false);
    CHECK(platform.pad.buttons & interaction::pad::LeftThumb);
}

TEST_CASE(EmuGunFollowsAimKeepsTheBulletsOnTheBarrel) {
    // The hand 30 degrees up with the head level. Gun follows aim: the game's aim stops where
    // the render can turn all the way to it (the barrel on the bullets' line); wide reach lets
    // that render grow wider, so it stops farther up. Off: the aim follows the hand and the
    // gun stays in front of the face.
    const float up = 0.5235988f;
    const math::FovTangents eyeL{-1.0f, 0.9f, 1.0f, -1.1f}, eyeR{-0.9f, 1.0f, 1.0f, -1.1f};
    AimAngle normal{0, up}, wide{0, up};
    CHECK(LimitAimToRender(CameraAxes{}, normal, eyeL, eyeR, 1920.0f / 1080.0f));
    CHECK(LimitAimToRender(CameraAxes{}, wide, eyeL, eyeR, 1920.0f / 1080.0f, kGunAimFovBudgetWide - kReachSlack));
    CHECK(wide.pitch > normal.pitch + 0.03f);
    for (const int choice : {1, 2, 0, 3}) { // On, Wide reach, Off, In the hand
        const bool follow = choice == 1 || choice == 2;
        const AimAngle expected = choice == 2 ? wide : normal;
        FakeImage image({names::kGameRenderer, names::kRenderViewUpdate, names::kClientGameContext, names::kLevelName});
        FakePlatform platform;
        AdapterCore core(Session(true), X64(), image.Resolve(), platform);
        core.OnSwapchainAdopted(true);
        platform.Campaign();
        platform.aimKnown = true;
        CHECK(core.SetMenuSetting("gun_aim", choice));
        Clock clock;
        for (std::uint64_t n = 0; n < 300; ++n) {
            LaneFrame l(n / 90.0, n + 1);
            l.in.controls.hands[1].aim.orientation = math::FromAxisAngle({1, 0, 0}, up);
            platform.GameFrame();
            core.OnFrame(clock.Next(), l.in);
        }
        CHECK(Near(WrapPi(platform.gameAim.yaw - 0.5f), 0.0f, 0.01f));
        if (follow) {
            CHECK(expected.pitch > 0.03f && expected.pitch < up - 0.1f);
            CHECK(Near(platform.gameAim.pitch, expected.pitch, 0.01f));
            CHECK(core.Status().find("reach_limited=0 ") == std::string::npos);
        } else {
            CHECK(Near(platform.gameAim.pitch, up, 0.01f));
            CHECK(core.Status().find("reach_limited=0 ") != std::string::npos);
        }
        // No aim dot while the barrel shows the aim (the player's choice); without the gun
        // following, the dot is the only sign of where shots go. The menu can switch it on.
        CHECK(!platform.frames.empty() && platform.frames.back().reticle == (choice == 0));
        // In the hand: no turned render; each world request carries the right hand relative to
        // the head (grip position, aim orientation) for the first-person pass.
        CHECK(!platform.cameraLog.empty() && platform.cameraLog.back().size() == 1);
        const CameraRequest& r = platform.cameraLog.back()[0];
        CHECK(r.gunInHand == (choice == 3) && r.handValid == (choice == 3) && r.gunFollowsAim == follow);
        if (choice == 3) {
            CHECK(Near(r.handInHead.position.x, 0.2f, 1e-4f) && Near(r.handInHead.position.y, -0.4f, 1e-4f) &&
                  Near(r.handInHead.position.z, -0.3f, 1e-4f));
            const math::Vec3 barrel = math::Rotate(r.handInHead.orientation, {0, 0, -1}); // the aim ray, 30 deg up
            CHECK(Near(barrel.y, std::sin(up), 1e-4f) && Near(barrel.z, -std::cos(up), 1e-4f));
        }
        CHECK(core.SetMenuSetting("aim_dot", 1));
        LaneFrame l(300 / 90.0, 301);
        l.in.controls.hands[1].aim.orientation = math::FromAxisAngle({1, 0, 0}, up);
        platform.GameFrame();
        core.OnFrame(clock.Next(), l.in);
        CHECK(platform.frames.back().reticle);
    }
}
