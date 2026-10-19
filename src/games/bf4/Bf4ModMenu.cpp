#include "Bf4ModMenu.h"

#include <algorithm>

namespace fvr::bf4 {

using game::Feature;
using ui::MenuItem;
using ui::MenuItemKind;

namespace {
std::string Roadmap(const game::CapabilityRegistry& caps, Feature f, bool dev) {
    if (caps.IsUsable(f, dev)) return {};
    return "Coming in a later update.";
}
} // namespace

interaction::ReloadModeResolution ResolveBf4ReloadMode(interaction::ReloadMode requested,
                                                       const game::CapabilityRegistry& caps, bool developer) {
    auto r = interaction::ResolveReloadMode(requested, caps, developer);
    if (requested == interaction::ReloadMode::Manual && r.effective != interaction::ReloadMode::Manual &&
        caps.IsUsable(Feature::VirtualGamepad, developer)) {
        r.effective = interaction::ReloadMode::Manual;
        r.note = "B drops the magazine. Take one from your belt (front left) and push it into the gun. Hold B to "
                 "reload at once.";
    }
    return r;
}

ui::ModMenuModel BuildModMenu(const game::CapabilityRegistry& caps, bool developer, const ModMenuState& state) {
    ui::ModMenuModel menu("FrostbiteVR");
    menu.Add({"reload_mode", "Reloading", MenuItemKind::Choice, {"Button press", "Manual (magazine by hand)"},
              static_cast<int>(state.reloadMode), true, ""});
    // Shown but disabled until the XR host consumes them (never offer a dead switch).
    menu.Add({"snap_turn", "Snap turning", MenuItemKind::Toggle, {}, state.snapTurn ? 1 : 0, false, "Coming in a later update."});
    menu.Add({"theater_distance", "Screen distance", MenuItemKind::Choice, {"Near", "Medium", "Far"},
              state.theaterDistance, false, "Coming in a later update."});
    menu.Add({"view_mode", "In campaign levels", MenuItemKind::Choice, {"Screen", "Head-tracked view"},
              state.headTracking ? 1 : 0, true, ""});
    menu.Add({"controller_aim", "Aim with", MenuItemKind::Choice, {"Right stick", "Right controller"},
              state.controllerAim ? 1 : 0, true, "Head-tracked view only. Right stick turns your body."});
    if (developer)
        menu.Add({"world_stereo", "3D depth (experimental)", MenuItemKind::Choice, {"Off", "On"},
                  state.worldStereo ? 1 : 0, true, "BF4's own side-by-side render. So far it freezes the game."});
    if (developer)
        menu.Add({"world_settle", "Head-tracking frame sync (developer)", MenuItemKind::Choice,
                  {"1 frame", "2 frames", "3 frames", "4 frames"}, std::clamp(state.worldSettle, 1, 4) - 1, true,
                  "Raise it if the world swims when you turn your head."});
    menu.Add({"game_menu", "Game menu (pause)", MenuItemKind::Action, {}, 0, true, ""});
    menu.Add({"recenter", "Recenter view", MenuItemKind::Action, {}, 0, true,
              "Or hold both thumbsticks down for one second."});
    menu.Add({"holsters", "Body holsters", MenuItemKind::Choice, {"Off", "On"}, state.holsters ? 1 : 0, true, ""});
    menu.Add({"mark_feedback", "Mark this moment for feedback", MenuItemKind::Action, {}, 0, true,
              "Adds a marker to the log so your report points here."});
    menu.Add({"about", "Campaign only. Multiplayer turns VR off.", MenuItemKind::Label, {}, 0, false, ""});
    RefreshModMenu(menu, caps, developer);
    return menu;
}

void RefreshModMenu(ui::ModMenuModel& menu, const game::CapabilityRegistry& caps, bool developer) {
    if (MenuItem* reload = menu.Find("reload_mode")) {
        const auto requested = reload->value == 1 ? interaction::ReloadMode::Manual : interaction::ReloadMode::ButtonPress;
        reload->note = ResolveBf4ReloadMode(requested, caps, developer).note;
    }
    if (MenuItem* holsters = menu.Find("holsters")) {
        holsters->enabled = caps.IsUsable(Feature::VirtualGamepad, developer); // input-only: game keys
        holsters->note = holsters->enabled ? "Right hip: sidearm. Over the right shoulder: primary. Left hip: grenade "
                                             "(let go to throw). Chest, left hand: knife."
                                           : Roadmap(caps, Feature::Holsters, developer);
    }
    if (MenuItem* view = menu.Find("view_mode")) {
        view->enabled = caps.IsUsable(Feature::HeadTrackedCamera, developer);
        view->note = view->enabled ? "Menus and loading screens stay on the screen." : Roadmap(caps, Feature::HeadTrackedCamera, developer);
    }
    if (MenuItem* game = menu.Find("game_menu")) {
        game->enabled = caps.IsUsable(Feature::VirtualGamepad, developer); // opened with a pad Start press
        game->note = game->enabled ? "" : "Needs controller input; use the game's Escape key.";
    }
}

ModMenuCommand InterpretChange(const MenuItem& item) {
    if (item.id == "reload_mode") return {ModMenuAction::SetReloadMode, item.value};
    if (item.id == "snap_turn") return {ModMenuAction::SetSnapTurn, item.value};
    if (item.id == "theater_distance") return {ModMenuAction::SetTheaterDistance, item.value};
    if (item.id == "recenter") return {ModMenuAction::Recenter, 0};
    if (item.id == "mark_feedback") return {ModMenuAction::MarkFeedback, 0};
    if (item.id == "game_menu") return {ModMenuAction::GameMenu, 0};
    if (item.id == "view_mode") return {ModMenuAction::SetHeadTracking, item.value};
    if (item.id == "world_settle") return {ModMenuAction::SetWorldSettle, item.value + 1};
    if (item.id == "world_stereo") return {ModMenuAction::SetWorldStereo, item.value};
    if (item.id == "controller_aim") return {ModMenuAction::SetControllerAim, item.value};
    if (item.id == "holsters") return {ModMenuAction::SetHolsters, item.value};
    return {};
}

} // namespace fvr::bf4
