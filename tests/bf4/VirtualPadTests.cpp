// Virtual gamepad: VR controller samples -> XInput-style pad (mapping policy), and the
// adapter core's use of it (reload gating, neutral while the mod menu owns input).
#include "TestHarness.h"

#include "EmuFixtures.h"
#include "../../src/games/bf4/Bf4PadLayout.h"

using namespace fvr;
using namespace fvr::bf4;
using namespace fvr::bf4::emu;
namespace pad = fvr::interaction::pad;

namespace {
interaction::InputFrame Controls() {
    interaction::InputFrame f;
    f.generation = 1;
    f.spaceGeneration = 1;
    f.predictedNs = 1000;
    f.focused = true;
    for (auto& h : f.hands) h.active = interaction::Components;
    return f;
}
} // namespace

TEST_CASE(VirtualPadMapsBf4DefaultLayout) {
    const auto layout = DefaultPadLayout();
    auto f = Controls();
    f.hands[0].stickX = 1.0f;   // full right on the left stick
    f.hands[0].stickY = 0.10f;  // inside the radial deadzone on its own
    f.hands[1].trigger = 1.0f;  // fire
    f.hands[0].trigger = 0.03f; // under the trigger deadzone
    f.hands[1].held = interaction::Primary | interaction::Secondary; // right A + B
    f.hands[0].squeeze = 0.8f;  // left grip
    auto p = interaction::MapToPad(f, layout, {});
    CHECK(p.thumbLX > 32000 && p.thumbLY > 0 && p.thumbRX == 0);
    CHECK(p.rightTrigger == 255 && p.leftTrigger == 0);
    CHECK(p.buttons == (pad::A | pad::X | pad::LeftShoulder)); // jump, reload, spot

    interaction::PadOptions manual;
    manual.reloadEnabled = false; // Manual reload owns the gesture: right B no longer reloads
    CHECK(interaction::MapToPad(f, layout, manual).buttons == (pad::A | pad::LeftShoulder));

    auto small = Controls();
    small.hands[1].stickX = 0.1f; // deadzone
    CHECK(interaction::MapToPad(small, layout, {}).thumbRX == 0);

    f.focused = false; // tracking/focus lost: neutral, never a stuck stick or trigger
    f.hands = {};
    CHECK(interaction::MapToPad(f, layout, {}) == interaction::PadState{});
    auto bad = Controls();
    bad.hands[0].stickX = 2.0f; // invalid sample
    CHECK(interaction::MapToPad(bad, layout, {}) == interaction::PadState{});
}

TEST_CASE(EmuVirtualPadFollowsReloadModeAndMenu) {
    FakeImage image({});
    FakePlatform platform;
    AdapterCore core(Session(true), X64(), image.Resolve(), platform);
    core.OnSwapchainAdopted(true);
    Clock clock;
    auto in = Input(0);
    in.controls = Controls();
    in.controls.hands[1].held = interaction::Secondary; // right B: reload
    in.controls.hands[0].stickY = 1.0f;                  // walk forward
    core.OnFrame(clock.Next(), in);
    CHECK(platform.padActive && (platform.pad.buttons & pad::X) && platform.pad.thumbLY > 32000);
    CHECK(core.Status().find("pad=active") != std::string::npos);

    core.SetReloadMode(interaction::ReloadMode::Manual); // magazine gesture: B drops the magazine
    core.OnFrame(clock.Next(), in);
    CHECK(!(platform.pad.buttons & pad::X));
    core.SetReloadMode(interaction::ReloadMode::ButtonPress);
    core.OnFrame(clock.Next(), in);
    CHECK(platform.pad.buttons & pad::X);

    in.menuButton = true; // mod menu opens: the pad goes neutral
    core.OnFrame(clock.Next(), in);
    in.menuButton = false;
    CHECK(core.ModMenuOpen() && platform.padActive && platform.pad == interaction::PadState{});

    // "Game menu (pause)" from the mod menu: closes it and presses Start briefly.
    const ui::Widget* game = nullptr;
    for (const auto& w : core.ModMenuPanel().Widgets())
        if (w.text.find(L"Game menu") != std::wstring::npos) game = &w;
    CHECK(game != nullptr);
    in.controls.hands[1].held = 0;
    in.pointer = {true, (game->rect.x + game->rect.w * 0.5f) / 1280.0f, (game->rect.y + game->rect.h * 0.5f) / 720.0f, false};
    for (bool down : {false, true, false}) {
        in.pointer.down = down;
        core.OnFrame(clock.Next(), in);
    }
    CHECK(!core.ModMenuOpen() && (platform.pad.buttons & pad::Start));
    in.pointer = {};
    for (int i = 0; i < 12; ++i) core.OnFrame(clock.Next(), in);
    CHECK(!(platform.pad.buttons & pad::Start)); // released again

    platform.punkBuster = true; // lockdown: the pad is handed back
    for (int i = 0; i < 300 && !core.Detached(); ++i) core.OnFrame(clock.Next(), in);
    CHECK(core.Detached() && platform.detaches == 1 && !platform.padActive);
    CHECK(core.Status().find("pad=off") != std::string::npos);
}
