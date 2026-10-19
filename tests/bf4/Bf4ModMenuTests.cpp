// In-headset FrostbiteVR menu, end to end through the adapter core: menu button opens it,
// the host's laser (u, v, down) clicks a row on the VrPanel, the setting changes, and the
// game cursor is never driven while the menu owns the laser.
#include "TestHarness.h"

#include "EmuFixtures.h"
#include "../../src/games/bf4/Bf4ModMenu.h"
#include "fvr/ui/ModMenuPanel.h"

using namespace fvr;
using namespace fvr::bf4;
using namespace fvr::bf4::emu;

TEST_CASE(ModMenuReflectsCapabilitiesInPlainWords) {
    game::CapabilityRegistry caps;
    // Button reload not bound yet (true today): the menu says so in plain words.
    CHECK(BuildModMenu(caps, false, {}).Find("reload_mode")->note.find("game's own reload key") != std::string::npos);
    caps.Set(game::Feature::ButtonReload, game::FeatureState::Enabled, "test");
    auto menu = BuildModMenu(caps, false, {});
    CHECK(menu.Find("reload_mode")->note.empty()); // working button press: nothing to explain
    CHECK(!menu.Find("holsters")->enabled && menu.Find("holsters")->note == "Coming in a later update.");
    CHECK(menu.Find("snap_turn")->enabled && !menu.Find("theater_distance")->enabled); // distance: not wired to the host yet
}

TEST_CASE(EmuModMenuOpensAndChangesReloadMode) {
    FakeImage image({});
    FakePlatform platform;
    AdapterCore core(Session(true), X64(), image.Resolve(), platform); // developer: candidates usable
    core.OnSwapchainAdopted(true);
    Clock clock;
    auto in = Input(0);
    in.pointer = {true, 0.5f, 0.5f, false};
    for (int i = 0; i < 5; ++i) core.OnFrame(clock.Next(), in); // laser on the game screen
    CHECK(!platform.pointerLog.empty() && !core.ModMenuOpen());

    in.menuButton = true; // host MenuControl toggle edge
    core.OnFrame(clock.Next(), in);
    in.menuButton = false;
    CHECK(core.ModMenuOpen() && platform.panelsPublished == 1);
    CHECK(!platform.pointerLog.empty() && platform.pointerLog.back().type == PointerEvent::Type::Deactivate); // game cursor released

    // Aim at the "Reloading" row and click (fresh press + release).
    const ui::Widget* row = core.ModMenuPanel().Find(1);
    CHECK(row != nullptr);
    in.pointer.u = (row->rect.x + row->rect.w * 0.5f) / 1280.0f;
    in.pointer.v = (row->rect.y + row->rect.h * 0.5f) / 720.0f;
    const std::size_t gameEvents = platform.pointerLog.size();
    for (bool down : {false, true, true, false}) {
        in.pointer.down = down;
        core.OnFrame(clock.Next(), in);
    }
    CHECK(core.MenuSettings().reloadMode == interaction::ReloadMode::Manual);
    CHECK(core.ReloadMode().requested == interaction::ReloadMode::Manual &&
          core.ReloadMode().effective == interaction::ReloadMode::Manual); // the magazine gesture (virtual pad)
    CHECK(platform.pointerLog.size() == gameEvents); // the game never saw those clicks
    CHECK(core.ModMenuPanel().Find(1)->text.find(L"Manual") != std::wstring::npos);
    CHECK(core.Status().find("mod_menu=open") != std::string::npos);

    in.menuButton = true; // close again
    core.OnFrame(clock.Next(), in);
    CHECK(!core.ModMenuOpen());
}
