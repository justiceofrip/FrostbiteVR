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
        r.note = "B drops it; refill from belt. Hold B: instant.";
    }
    return r;
}

ui::ModMenuModel BuildModMenu(const game::CapabilityRegistry& caps, bool developer, const ModMenuState& state) {
    ui::ModMenuModel menu("FrostbiteVR");
    menu.Add({"reload_mode", "Reload", MenuItemKind::Choice, {"Button", "Manual (magazine)"},
              static_cast<int>(state.reloadMode), true, ""});
    // Shown but disabled until the XR host consumes them (never offer a dead switch).
    menu.Add({"snap_turn", "Snap turning", MenuItemKind::Toggle, {}, state.snapTurn ? 1 : 0, true,
              "Right stick: 30 degree steps."});
    menu.Add({"theater_distance", "Screen distance", MenuItemKind::Choice, {"Near", "Medium", "Far"},
              state.theaterDistance, false, "Coming in a later update."});
    menu.Add({"view_mode", "In campaign levels", MenuItemKind::Choice, {"Screen", "Head-tracked view"},
              state.headTracking ? 1 : 0, true, ""});
    menu.Add({"controller_aim", "Aim with", MenuItemKind::Choice, {"Right stick", "Right controller"},
              state.controllerAim ? 1 : 0, true, "Right stick turns your body."});
    menu.Add({"walk_direction", "Walk toward", MenuItemKind::Choice, {"Gun", "Body"}, state.walkWithBody ? 1 : 0, true,
              "Use Gun in vehicles (stick = throttle)."});
    menu.Add({"two_handed", "Two-handed aim", MenuItemKind::Choice, {"Off", "On"}, state.twoHandedAim ? 1 : 0, true,
              "Left grip in front of the gun steadies it."});
    menu.Add({"vignette", "Comfort vignette", MenuItemKind::Choice, {"Off", "On"}, state.vignette ? 1 : 0, true,
              "Darkens the edges while walking."});
    menu.Add({"aim_dot", "Aim dot", MenuItemKind::Choice, {"Off", "On"}, state.aimDot ? 1 : 0, true,
              "Green ring where the gun aims."});
    if (developer)
        menu.Add({"world_stereo", "3D depth (experimental)", MenuItemKind::Choice, {"Off", "On"},
                  state.worldStereo ? 1 : 0, true, "Freezes the game so far."});
    if (developer)
        menu.Add({"world_settle", "Frame sync (developer)", MenuItemKind::Choice,
                  {"1 frame", "2 frames", "3 frames", "4 frames"}, std::clamp(state.worldSettle, 1, 4) - 1, true,
                  "Raise it if the world swims."});
    menu.Add({"game_menu", "Game menu (pause)", MenuItemKind::Action, {}, 0, true, ""});
    menu.Add({"recenter", "Recenter view", MenuItemKind::Action, {}, 0, true,
              "Or hold both sticks down 1 s."});
    menu.Add({"holsters", "Body holsters", MenuItemKind::Choice, {"Off", "On"}, state.holsters ? 1 : 0, true, ""});
    menu.Add({"mark_feedback", "Mark this moment for feedback", MenuItemKind::Action, {}, 0, true,
              "Adds a marker to the log."});
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
        holsters->note = holsters->enabled ? "R hip pistol, back rifle, L hip grenade."
                                           : Roadmap(caps, Feature::Holsters, developer);
    }
    if (MenuItem* view = menu.Find("view_mode")) {
        view->enabled = caps.IsUsable(Feature::HeadTrackedCamera, developer);
        view->note = view->enabled ? "Menus stay on the screen." : Roadmap(caps, Feature::HeadTrackedCamera, developer);
    }
    if (MenuItem* game = menu.Find("game_menu")) {
        game->enabled = caps.IsUsable(Feature::VirtualGamepad, developer); // opened with a pad Start press
        game->note = game->enabled ? "" : "Needs controller input; use Escape.";
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
    if (item.id == "aim_dot") return {ModMenuAction::SetAimDot, item.value};
    if (item.id == "vignette") return {ModMenuAction::SetVignette, item.value};
    if (item.id == "two_handed") return {ModMenuAction::SetTwoHandedAim, item.value};
    if (item.id == "walk_direction") return {ModMenuAction::SetWalkDirection, item.value};
    return {};
}

} // namespace fvr::bf4
