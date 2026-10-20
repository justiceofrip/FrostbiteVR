#include "Bf4AdapterCore.h"
#include "Bf4WorldView.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace fvr::bf4 {

using game::Feature;

std::string_view StereoModeName(StereoMode mode) noexcept {
    switch (mode) {
    case StereoMode::Off: return "off (theater)";
    case StereoMode::Theater3D: return "3D theater (engine stereo)";
    case StereoMode::EngineStereoTracked: return "engine stereo, head-tracked";
    case StereoMode::Mono: return "mono head-tracked";
    case StereoMode::AlternateEye: return "alternate-eye";
    case StereoMode::Native: return "native two-pass";
    }
    return "unknown";
}

AdapterCore::AdapterCore(SessionInfo session, BuildIdentity build, BindingTable bindings, IAdapterPlatform& platform)
    : session_(std::move(session)), build_(std::move(build)), bindings_(std::move(bindings)), platform_(platform) {
    ResolveReload();
    if (build_.verdict == BuildVerdict::WrongArch || build_.verdict == BuildVerdict::NotPe) {
        caps_.DisableAll("unsupported executable: " + build_.label);
        detached_ = true;
    }
    menuState_.reloadMode = session_.reloadMode;
    modMenu_ = BuildModMenu(caps_, session_.developerFeatures, menuState_);
    modMenu_.OnChange([this](const ui::MenuItem& item) {
        ApplyMenuCommand(InterpretChange(item));
        RefreshModMenu(modMenu_, caps_, session_.developerFeatures);
    });
}

void AdapterCore::UpdateModMenu(const ControllerSnapshot& in) {
    const bool usable = caps_.IsUsable(Feature::ModMenuPointer, session_.developerFeatures) && guard_.PresentationAllowed();
    if (!usable) {
        menuOpen_ = false;
        return;
    }
    if (in.menuButton) { // host MenuControl toggle edge (controller menu button)
        menuOpen_ = !menuOpen_;
        menuPanel_.Disarm(); // a trigger held while it opens never clicks
        if (menuOpen_) RefreshModMenu(modMenu_, caps_, session_.developerFeatures);
        platform_.Log(menuOpen_ ? "mod menu opened" : "mod menu closed");
    }
    if (!menuOpen_) return;
    ui::LayoutModMenu(modMenu_, menuPanel_);
    if (ui::PointModMenu(modMenu_, menuPanel_, {in.valid && in.pointer.active, in.pointer.down, in.pointer.u, in.pointer.v}))
        ui::LayoutModMenu(modMenu_, menuPanel_); // show the change in this frame's panel
}

void AdapterCore::UpdatePad(const ControllerSnapshot& in, const PlatformFrame& f) {
    // VR controllers -> virtual XInput pad. Presentation-level input like the laser (no
    // game memory), so it follows the presentation guard; neutral while the mod menu owns
    // input; reload only while button reload is effective and no native binding does it.
    const bool dev = session_.developerFeatures;
    if (!caps_.IsUsable(Feature::VirtualGamepad, dev) || !guard_.PresentationAllowed()) {
        if (padActive_) platform_.ApplyPad({}, false);
        padActive_ = false;
        pad_ = {};
        return;
    }
    interaction::PadOptions options;
    options.reloadEnabled = reloadMode_.effective == interaction::ReloadMode::ButtonPress &&
                            !caps_.IsUsable(Feature::ButtonReload, dev);
    interaction::InputFrame controls = in.controls;
    if (menuOpen_) controls.focused = false;
    if (padPulseFrames_ > 0 && !menuOpen_) {
        options.extraButtons = padPulse_;
        --padPulseFrames_;
    }
    pad_ = interaction::MapToPad(controls, padLayout_, options);
    if (!controls.focused && options.extraButtons) pad_.buttons |= options.extraButtons; // pulse without hands
    if (!menuOpen_) {
        // Body gestures: a grip press that grabbed from a holster is not the pad's button; with
        // holsters on, the gun hand's grip is never the pad's (it rests on the gun). A dropped
        // magazine holds fire until one is seated; seating presses the game's reload.
        for (const auto& b : padLayout_.buttons)
            if (b.control == interaction::VrControl::Squeeze && b.hand < 2 &&
                (gesture_.gripOwned[b.hand] || (b.hand == 1 && menuState_.holsters) || (b.hand == 0 && aim_.twoHanded)))
                pad_.buttons &= static_cast<std::uint16_t>(~b.padButton);
        pad_.buttons |= gesture_.padButtons;
        if (crouchPulse_) pad_.buttons |= interaction::pad::B;
        if (gesture_.blockFire) pad_.rightTrigger = 0;
        // Both stick clicks together are the recenter gesture: neither reaches the game.
        const auto stick = [&](unsigned hand) { return controls.focused && (controls.hands[hand].held & interaction::StickClick) != 0; };
        const unsigned sticks = stickChord_.Update(stick(0), stick(1), static_cast<double>(f.tickMs) / 1000.0);
        for (const auto& b : padLayout_.buttons)
            if (b.control == interaction::VrControl::StickClick && b.hand < 2) {
                if (sticks & (1u << b.hand)) pad_.buttons |= b.padButton;
                else pad_.buttons &= static_cast<std::uint16_t>(~b.padButton);
            }
    }
    if (aim_.engaged) {
        // The game aims by mouse now and turns with the body heading: no pad look, and
        // walking stays relative to the body although the game moves relative to its aim
        // (unless the player walks toward the gun, e.g. in a vehicle, where the left stick
        // is throttle and steering and must reach the game unchanged).
        pad_.thumbRX = pad_.thumbRY = 0;
    }
    if (aim_.engaged && menuState_.walkWithBody) {
        float forward = pad_.thumbLY / 32767.0f, strafe = pad_.thumbLX / 32767.0f;
        interaction::RotateMovement(-aim_.moveRotation, forward, strafe);
        auto toPad = [](float v) { return static_cast<std::int16_t>(std::lround(std::clamp(v, -1.0f, 1.0f) * 32767.0f)); };
        pad_.thumbLY = toPad(forward);
        pad_.thumbLX = toPad(strafe);
    }
    // Outside campaign play (and while BF4's own menu is up in a level) the trigger pointed
    // at the screen is a mouse click, not the pad's trigger as well (menus react to both).
    if (in.pointer.active && (!guard_.GameplayAllowed() || gameUi_)) pad_.leftTrigger = pad_.rightTrigger = 0;
    platform_.ApplyPad(pad_, true);
    padActive_ = true;
    ++stats_.padFrames;
}

void AdapterCore::ApplyMenuCommand(const ModMenuCommand& command) {
    switch (command.action) {
    case ModMenuAction::SetReloadMode:
        menuState_.reloadMode = command.value == 1 ? interaction::ReloadMode::Manual : interaction::ReloadMode::ButtonPress;
        SetReloadMode(menuState_.reloadMode);
        break;
    case ModMenuAction::SetSnapTurn: { // Smooth, Snap 30, Snap 45, Snap 90
        static constexpr int kDegrees[] = {30, 30, 45, 90};
        menuState_.snapTurn = command.value != 0;
        menuState_.snapDegrees = kDegrees[std::clamp(command.value, 0, 3)];
        char line[48];
        std::snprintf(line, sizeof(line), "turning: %s (%d degrees)", menuState_.snapTurn ? "snap" : "smooth", menuState_.snapDegrees);
        platform_.Log(menuState_.snapTurn ? line : "turning: smooth");
        break;
    }
    case ModMenuAction::SetTurnSpeed:
        menuState_.turnSpeed = std::clamp(command.value, 0, 2);
        platform_.Log(menuState_.turnSpeed == 0 ? "smooth turn speed: slow" : menuState_.turnSpeed == 2 ? "smooth turn speed: fast"
                                                                                                        : "smooth turn speed: normal");
        break;
    case ModMenuAction::SetTheaterDistance: menuState_.theaterDistance = command.value; break;
    case ModMenuAction::Recenter:
        haveReference_ = false; // the next valid head pose becomes the reference
        platform_.Log("recenter requested from the mod menu");
        break;
    case ModMenuAction::MarkFeedback: platform_.Log("FEEDBACK MARK: the player marked this moment"); break;
    case ModMenuAction::SetHeadTracking:
        menuState_.headTracking = command.value != 0;
        platform_.Log(menuState_.headTracking ? "campaign view: head-tracked" : "campaign view: screen");
        break;
    case ModMenuAction::SetVignette:
        menuState_.vignette = command.value != 0;
        platform_.Log(menuState_.vignette ? "comfort vignette: on" : "comfort vignette: off");
        break;
    case ModMenuAction::SetHudInsets:
        menuState_.hudInsets = command.value != 0;
        platform_.Log(menuState_.hudInsets ? "HUD corners: on" : "HUD corners: off");
        break;
    case ModMenuAction::SetDepthStereo: // "3D": Off, From depth, Alternate eyes
        menuState_.depthStereo = command.value == 1;
        menuState_.alternateEyes = command.value == 2;
        platform_.Log(menuState_.alternateEyes ? "3D: alternate eyes" : menuState_.depthStereo ? "3D: from depth" : "3D: off");
        break;
    case ModMenuAction::SetPhysicalCrouch:
        menuState_.physicalCrouch = command.value != 0;
        platform_.Log(menuState_.physicalCrouch ? "physical crouch: on" : "physical crouch: off");
        break;
    case ModMenuAction::SetWalkDirection:
        menuState_.walkWithBody = command.value != 0;
        platform_.Log(menuState_.walkWithBody ? "walk toward: body" : "walk toward: gun");
        break;
    case ModMenuAction::SetTwoHandedAim:
        menuState_.twoHandedAim = command.value != 0;
        platform_.Log(menuState_.twoHandedAim ? "two-handed aim: on" : "two-handed aim: off");
        break;
    case ModMenuAction::SetAimDot:
        menuState_.aimDot = command.value != 0;
        platform_.Log(menuState_.aimDot ? "aim dot: on" : "aim dot: off");
        break;
    case ModMenuAction::SetGunFollowsAim:
        menuState_.gunInHand = command.value == 3; // BF4's rig drawn at the hand: no turned render needed
        menuState_.gunFollowsAim = command.value == 1 || command.value == 2;
        menuState_.gunReachWide = command.value == 2;
        platform_.Log(menuState_.gunInHand ? "gun follows aim: in the hand"
                      : !menuState_.gunFollowsAim ? "gun follows aim: off"
                      : menuState_.gunReachWide ? "gun follows aim: on, wide reach" : "gun follows aim: on");
        break;
    case ModMenuAction::SetArms:
        menuState_.armIk = command.value != 0;
        platform_.Log(menuState_.armIk ? "arms: from the body" : "arms: with the gun");
        break;
    case ModMenuAction::SetHolsters:
        menuState_.holsters = command.value != 0;
        platform_.Log(menuState_.holsters ? "body holsters: on" : "body holsters: off");
        break;
    case ModMenuAction::SetControllerAim:
        menuState_.controllerAim = command.value != 0;
        platform_.Log(menuState_.controllerAim ? "aim: right controller" : "aim: right stick");
        break;
    case ModMenuAction::SetWorldStereo:
        // Unsupported (BF4-STEREO.md): two views overrun BF4's one-view world renderer.
        menuState_.worldStereo = false;
        platform_.Log("BF4 split 3D: unsupported (the world renderer holds one view)");
        break;
    case ModMenuAction::SetWorldSettle:
        menuState_.worldSettle = std::clamp(command.value, 1, 4);
        platform_.Log("head-tracking frame sync: " + std::to_string(menuState_.worldSettle) + " frame(s)");
        break;
    case ModMenuAction::GameMenu: // BF4's own pause menu: a short Start press, menu closes
        padPulse_ = interaction::pad::Start;
        padPulseFrames_ = 10;
        menuOpen_ = false;
        platform_.Log("game menu requested from the mod menu (pad Start)");
        break;
    case ModMenuAction::None: return;
    }
    ++stats_.menuCommands;
}

void AdapterCore::OnSwapchainAdopted(bool ok) {
    if (detached_) return;
    adopted_ = ok;
    ApplyBindingsToCapabilities(bindings_, caps_, ok);
    ResolveReload();
    RefreshModMenu(modMenu_, caps_, session_.developerFeatures); // availability follows the new capabilities
    platform_.Log(ok ? "swapchain adopted; capabilities evaluated" : "no usable swapchain");
}

void AdapterCore::SetReloadMode(interaction::ReloadMode requested) {
    session_.reloadMode = requested;
    ResolveReload();
    reload_.Reset();
    platform_.Log("reload mode -> " + std::string(interaction::ReloadModeName(reloadMode_.effective)) +
                  (reloadMode_.note.empty() ? "" : " (" + reloadMode_.note + ")"));
}

bool AdapterCore::SetMenuSetting(const std::string& id, int value) {
    ui::MenuItem* item = modMenu_.Find(id);
    if (!item || !item->enabled || value < 0) return false;
    if (item->kind == ui::MenuItemKind::Choice && value >= static_cast<int>(item->choices.size())) return false;
    if (item->kind == ui::MenuItemKind::Toggle && value > 1) return false;
    if (item->kind != ui::MenuItemKind::Choice && item->kind != ui::MenuItemKind::Toggle) return false;
    item->value = value;
    ApplyMenuCommand(InterpretChange(*item));
    RefreshModMenu(modMenu_, caps_, session_.developerFeatures);
    return true;
}

namespace {
bool PlayerSetting(const ui::MenuItem& item) {
    if (item.kind != ui::MenuItemKind::Choice && item.kind != ui::MenuItemKind::Toggle) return false;
    // The launcher's settings own the reload mode; developer items stay per session.
    return item.id != "reload_mode" && item.id != "world_settle" && item.id != "world_stereo";
}
} // namespace

std::string AdapterCore::MenuValuesText() const {
    std::string out;
    for (const auto& item : modMenu_.Items())
        if (item.enabled && PlayerSetting(item)) out += item.id + " " + std::to_string(item.value) + "\n";
    return out;
}

int AdapterCore::ApplyMenuValuesText(std::string_view text) {
    int applied = 0;
    std::size_t at = 0;
    while (at < text.size()) {
        std::size_t end = text.find('\n', at);
        if (end == std::string_view::npos) end = text.size();
        const std::string_view line = text.substr(at, end - at);
        at = end + 1;
        const std::size_t space = line.find(' ');
        if (space == std::string_view::npos || space == 0) continue;
        const std::string id(line.substr(0, space));
        int value = 0;
        bool digits = space + 1 < line.size();
        for (std::size_t k = space + 1; k < line.size() && digits; ++k) {
            const char c = line[k];
            if (c == '\r') break;
            if (c < '0' || c > '9' || value > 1000) digits = false;
            else value = value * 10 + (c - '0');
        }
        const ui::MenuItem* item = modMenu_.Find(id);
        if (!digits || !item || !PlayerSetting(*item) || item->value == value) continue;
        if (SetMenuSetting(id, value)) ++applied;
    }
    return applied;
}

void AdapterCore::ResolveReload() {
    reloadMode_ = ResolveBf4ReloadMode(session_.reloadMode, caps_, session_.developerFeatures);
}

void AdapterCore::Detach(std::string_view why) {
    if (detached_) return;
    platform_.SetGameKeys({}); // never leave a game key held
    platform_.SetStereoSuppressed(true); // the engine returns to a single pass
    events_.clear();
    pointer_.ForceRelease(events_); // never leave the game with a held button
    if (!events_.empty()) platform_.ApplyPointer(events_);
    caps_.DisableAll("locked: " + std::string(why));
    stereo_ = StereoMode::Off;
    if (padActive_) platform_.ApplyPad({}, false); // hand the pad back before hooks go
    padActive_ = false;
    pad_ = {};
    menuOpen_ = false;
    detached_ = true;
    platform_.DetachAll(why);
    platform_.Log("DETACH: " + std::string(why));
}

math::Pose AdapterCore::RecenterReference(const math::Pose& head) {
    const math::Vec3 f = math::Rotate(head.orientation, {0, 0, -1});
    const float yaw = std::atan2(-f.x, -f.z); // 0 = looking down -Z, + = turned left
    math::Pose ref;
    ref.orientation = math::FromAxisAngle({0, 1, 0}, yaw);
    ref.position = head.position;
    return ref;
}

math::Pose AdapterCore::ApplyReference(const math::Pose& reference, const math::Pose& pose) {
    const math::Quat inv = math::Conjugate(reference.orientation);
    math::Pose local;
    local.orientation = inv * pose.orientation;
    local.position = math::Rotate(inv, pose.position - reference.position);
    return local;
}

frostbite::LinearTransform AdapterCore::XrPoseToBf4Camera(const math::Pose& pose) {
    // BF4's view space is right-handed: the projection builder (+0xB7EAE0) puts w = -z when
    // the view's +0xD9 flag is set, and it is set in every live session. The camera
    // transform's rows are right, up, and the +Z axis (behind the camera), as in OpenXR, so
    // the headset pose goes in unmirrored. (The mirrored, left-handed conversion turned
    // the world the wrong way: looking left turned right, looking up looked down.)
    frostbite::LinearTransform t;
    t.right = math::Rotate(pose.orientation, {1, 0, 0});
    t.up = math::Rotate(pose.orientation, {0, 1, 0});
    t.forward = math::Rotate(pose.orientation, {0, 0, 1});
    t.trans = pose.position;
    return t;
}

frostbite::FovAngles AdapterCore::UnionFov(const frostbite::FovAngles& a, const frostbite::FovAngles& b) {
    return {std::min(a.left, b.left), std::max(a.right, b.right), std::max(a.up, b.up), std::min(a.down, b.down)};
}

bool AdapterCore::EngineStereoAllowed() const {
    const bool optedIn = session_.stereo != StereoSetting::Off;
    const bool usable = caps_.IsUsable(Feature::StereoDriver, session_.developerFeatures || session_.stereo == StereoSetting::Probe);
    return optedIn && usable && guard_.PresentationAllowed() && EnginePairsWorking();
}

bool AdapterCore::EnginePairsWorking() const {
    if (session_.stereoRoute == StereoRoute::Nvapi) return tracker_.StereoWorking();
    // Split: the most recent check saw two matching eye views, and the majority agreed.
    const StereoStats& s = stereoStats_;
    return s.lastSplit.verdict == stereo::SplitVerdict::Stereo && s.splitStereoChecks > s.splitMonoChecks;
}

bool AdapterCore::OnSplitFrame() {
    // The engine decides per frame (StereoEnable && StereoSplitScreenEnable), so the
    // backbuffer is re-checked periodically: first at frame 120, then every 300 frames.
    if (stats_.frames >= 120 && (stereoStats_.splitChecks == 0 || stats_.frames - lastSplitCheck_ >= 300)) {
        lastSplitCheck_ = stats_.frames;
        stereo::SplitAnalysis a;
        if (platform_.AnalyzeBackbufferSplit(a)) {
            ++stereoStats_.splitChecks;
            if (a.verdict == stereo::SplitVerdict::Stereo) ++stereoStats_.splitStereoChecks;
            if (a.verdict == stereo::SplitVerdict::Mono) ++stereoStats_.splitMonoChecks;
            stereoStats_.lastSplit = a;
        }
    }
    if (!EnginePairsWorking() && session_.stereo != StereoSetting::Probe) return false; // nothing to show yet
    if (!platform_.CaptureSplitPair()) return false;
    ++stereoStats_.captures;
    ++stereoStats_.splitPairs;
    return true;
}

std::string AdapterCore::AimTraceCsv() const {
    std::string csv = "tick_ms,sample,game_yaw,game_pitch,target_yaw,target_pitch,mouse_x,mouse_y,gain_x,gain_y,guards,mutes,"
                      "foreign,flips,engaged\n";
    const std::size_t count = std::min(aimTraceNext_, aimTrace_.size());
    for (std::size_t i = aimTraceNext_ - count; i < aimTraceNext_; ++i) {
        const AimTraceRow& r = aimTrace_[i % kAimTraceSize];
        char line[256];
        std::snprintf(line, sizeof(line), "%llu,%llu,%.6f,%.6f,%.6f,%.6f,%d,%d,%.7f,%.7f,%u,%u,%u,%u,%d\n",
                      static_cast<unsigned long long>(r.tickMs), static_cast<unsigned long long>(r.sample), r.gameYaw, r.gamePitch,
                      r.targetYaw, r.targetPitch, r.mouseX, r.mouseY, r.gainX, r.gainY, r.guards, r.mutes, r.foreign, r.flips,
                      r.engaged ? 1 : 0);
        csv += line;
    }
    return csv;
}

void AdapterCore::UpdateGameUi(const PlatformFrame& f) {
    // Only during a level: the front end's menus are on the screen anyway.
    const bool shown = f.gameCursor && guard_.GameplayAllowed();
    if (shown == gameUi_) {
        gameUiRun_ = 0;
        return;
    }
    if (++gameUiRun_ < kGameUiFrames) return;
    gameUiRun_ = 0;
    gameUi_ = shown;
    if (gameUi_) ++stats_.gameMenus;
    platform_.Log(gameUi_ ? "game menu on screen: theater view" : "game menu closed");
}

StereoMode AdapterCore::ChooseStereo() const {
    const bool dev = session_.developerFeatures;
    const bool engine = EngineStereoAllowed();
    // BF4's own menu in a level (pause, death, mission end) goes on the screen like the
    // front end's: the head-tracked view would show a magnified piece of it, out of the laser's reach.
    const bool headTracked = menuState_.headTracking && guard_.GameplayAllowed() && !gameUi_ &&
                             caps_.IsUsable(Feature::HeadTrackedCamera, dev);
    if (!headTracked) return engine ? StereoMode::Theater3D : StereoMode::Off;
    if (caps_.IsUsable(Feature::StereoNative, dev)) return StereoMode::Native;
    if (engine) return StereoMode::EngineStereoTracked;
    if (caps_.IsUsable(Feature::StereoAlternateEye, dev)) return StereoMode::AlternateEye;
    return StereoMode::Mono;
}

void AdapterCore::UpdateStereoDriverCapability() {
    if (session_.stereo == StereoSetting::Off) return; // stays "stereo off in launcher settings"
    if (session_.stereoRoute == StereoRoute::Split) {
        const auto& s = stereoStats_;
        if (s.splitChecks == 0)
            caps_.Set(Feature::StereoDriver, game::FeatureState::Disabled, "split-screen: no frame checked yet");
        else if (!EnginePairsWorking())
            caps_.Set(Feature::StereoDriver, game::FeatureState::Disabled,
                      std::string("split-screen: backbuffer is ") + stereo::SplitVerdictName(s.lastSplit.verdict));
        else
            caps_.Set(Feature::StereoDriver, game::FeatureState::Candidate,
                      "engine renders side-by-side eye pairs; headset check pending");
        return;
    }
    const StereoShimState shim = platform_.StereoShim();
    if (!shim.bound)
        caps_.Set(Feature::StereoDriver, game::FeatureState::Disabled, "nvapi64.dll shim not loaded by the game (yet)");
    else if (!tracker_.StereoWorking())
        caps_.Set(Feature::StereoDriver, game::FeatureState::Disabled, "shim loaded; no sequential eye pairs yet");
    else
        caps_.Set(Feature::StereoDriver, game::FeatureState::Candidate, "engine renders L/R pairs; headset check pending");
}

void AdapterCore::SetWorldStereo(bool /*on*/) {
    // BF4's split two-view render is unsupported (see SetWorldStereo in ApplyMenuCommand).
    menuState_.worldStereo = false;
    if (ui::MenuItem* item = modMenu_.Find("world_stereo")) item->value = 0;
}

void AdapterCore::OnStereoEye(stereo::Eye eye) {
    if (detached_ || session_.stereo == StereoSetting::Off) return;
    const auto action = tracker_.OnActiveEye(eye);
    if (action == stereo::CaptureAction::None) return;
    if (platform_.CaptureEye(action == stereo::CaptureAction::CaptureLeft ? 0 : 1)) ++stereoStats_.captures;
}

std::string AdapterCore::StereoReport() {
    const StereoShimState shim = platform_.StereoShim();
    stereo::ProbeFacts facts;
    facts.shimLoaded = shim.bound;
    facts.stereoHandleCreated = shim.handleCreated;
    facts.directMode = shim.directMode;
    facts.eyeSwitches = shim.eyeSwitches;
    facts.gpuVendorId = shim.gpuVendorId;
    facts.vendorSpoofed = shim.vendorSpoofed;
    std::string r;
    if (session_.stereoRoute == StereoRoute::Split) {
        const auto& s = stereoStats_;
        const bool working = EnginePairsWorking();
        r = "FrostbiteVR stereo probe\nroute: split-screen (RenderDevice.StereoSplitScreenEnable)\n";
        r += "verdict: " + std::string(working ? "working" : stereo::SplitVerdictName(s.lastSplit.verdict)) + "\n";
        r += std::string("next: ") +
             (working ? "BF4 renders both eyes side by side. Check the snapshot for depth; next step is per-eye display in the headset."
              : s.lastSplit.verdict == stereo::SplitVerdict::Mono
                  ? "The halves show different parts of the screen: BF4 did not enter split-screen stereo (user.cfg not applied?)."
                  : "No textured frame checked yet (loading/black screen); stay in the menu or a level longer.") + "\n";
        char line[200];
        std::snprintf(line, sizeof(line), "checks=%llu stereo=%llu mono=%llu last_correlation=%.3f last_textured=%.2f pairs=%llu\n",
                      static_cast<unsigned long long>(s.splitChecks), static_cast<unsigned long long>(s.splitStereoChecks),
                      static_cast<unsigned long long>(s.splitMonoChecks), s.lastSplit.correlation, s.lastSplit.texture,
                      static_cast<unsigned long long>(s.splitPairs));
        r += line;
    } else {
        r = stereo::BuildProbeReport(facts, tracker_, platform_.NvapiReport());
    }
    r += "snapshot=" + std::string(stereoStats_.snapshotWritten ? "stereo-probe-sbs.bmp" : "not written") + "\n";
    return r;
}

void AdapterCore::UpdateMotionAim(const ControllerSnapshot& in, const PlatformFrame& f) {
    MotionAimInput m;
    // The world lane holds a lease on most frames but not all (between a capture and the
    // next request): aiming stays engaged across such gaps, or every gap would re-anchor the
    // body heading to the game's aim and reset the learned mouse gain.
    if (in.world.valid) lastLeaseMs_ = f.tickMs;
    const bool worldLane = lastLeaseMs_ && f.tickMs - lastLeaseMs_ <= kWorldLaneHoldMs;
    m.engage = menuState_.controllerAim && !menuOpen_ && !gameUi_ && stereo_ == StereoMode::Mono && worldLane && haveReference_ &&
               guard_.GameplayAllowed();
    // Gun follows aim: the aim stays where the render can turn all the way to it, judged with
    // the head pose the camera will carry (else the latest tracked one) and the lane's eyes.
    if (in.world.valid) {
        lastEyeFov_[0] = in.world.fov[0];
        lastEyeFov_[1] = in.world.fov[1];
        haveEyeFov_ = true;
    }
    const math::Pose* head = in.world.valid ? &in.world.head : (in.controls.focused && in.controls.headValid ? &in.controls.head : nullptr);
    m.limitToRender = menuState_.gunFollowsAim && haveEyeFov_ && head && haveReference_;
    if (m.limitToRender) {
        const auto camera = XrPoseToBf4Camera(ApplyReference(reference_, *head));
        const math::Vec3 rows[3] = {camera.right, camera.up, camera.forward};
        for (int i = 0; i < 3; ++i) {
            m.head.row[i][0] = rows[i].x;
            m.head.row[i][1] = rows[i].y;
            m.head.row[i][2] = rows[i].z;
        }
        m.eyes[0] = lastEyeFov_[0];
        m.eyes[1] = lastEyeFov_[1];
        m.aspect = f.backbufferHeight ? float(f.backbufferWidth) / float(f.backbufferHeight) : 16.0f / 9.0f;
        m.renderBudget = menuState_.gunReachWide ? kGunAimFovBudgetWide : kGunAimFovBudget;
    }
    const auto& hand = in.controls.hands[1];
    m.aimTracked = in.controls.focused && hand.aimTracked;
    m.reference = reference_;
    m.aim = hand.aim;
    m.gameValid = platform_.GameAim(m.game, m.gameSample);
    m.gameFov = platform_.GameFovY();
    m.turn = std::fabs(hand.stickX) > 0.2f ? hand.stickX : 0.0f;
    m.snapTurn = menuState_.snapTurn;
    m.snapAngle = static_cast<float>(menuState_.snapDegrees) * 0.017453293f;
    static constexpr float kTurnRates[] = {1.5708f, MotionAim::kTurnRate, 4.1888f}; // 90, ~150, 240 deg/s
    m.turnRate = kTurnRates[std::clamp(menuState_.turnSpeed, 0, 2)];
    // Two-handed aim: the left grip held in front of the gun, unless that grip carries
    // something (a magazine, a grenade) for the gestures.
    const auto& left = in.controls.hands[0];
    m.twoHanded = menuState_.twoHandedAim;
    m.supportGrip = in.controls.focused && left.gripTracked && hand.gripTracked && (left.active & interaction::Squeeze) &&
                    left.squeeze >= 0.7f && !gesture_.gripOwned[0] && !gestures_.MagazineInHand();
    m.rearGrip = hand.grip.position;
    m.frontGrip = left.grip.position;
    // Gun in the hand: the shots cross the drawn barrel's line kGunConvergeMeters out.
    if (menuState_.gunInHand && head && hand.gripTracked) {
        m.converge = kGunConvergeMeters;
        m.eye = head->position;
        m.muzzle = hand.grip.position;
    }
    m.dt = lastAimTickMs_ && f.tickMs > lastAimTickMs_ ? std::min(0.1f, (f.tickMs - lastAimTickMs_) / 1000.0f) : 0.0f;
    lastAimTickMs_ = f.tickMs;
    const bool was = aim_.engaged;
    aim_ = motionAim_.Update(m);
    if (session_.developerFeatures && (aim_.engaged || was)) {
        if (aimTrace_.size() != kAimTraceSize) aimTrace_.assign(kAimTraceSize, {});
        AimTraceRow& r = aimTrace_[aimTraceNext_ % kAimTraceSize];
        ++aimTraceNext_;
        const AimAngle target = motionAim_.Desired();
        r = {f.tickMs, m.gameSample, m.game.yaw, m.game.pitch, target.yaw, target.pitch, motionAim_.GainX(), motionAim_.GainY(),
             aim_.mouseX, aim_.mouseY, static_cast<std::uint32_t>(motionAim_.OscillationGuards()),
             static_cast<std::uint32_t>(motionAim_.Mutes()), static_cast<std::uint32_t>(motionAim_.ForeignTurns()),
             static_cast<std::uint32_t>(motionAim_.SignFlips()), aim_.engaged};
    }
    if (aim_.engaged != was) platform_.Log(aim_.engaged ? "controller aim engaged" : "controller aim released");
    if (aim_.twoHanded && !twoHandedWas_) ++twoHandedEngages_;
    twoHandedWas_ = aim_.twoHanded;
    if (aim_.engaged && (aim_.mouseX || aim_.mouseY)) platform_.SendMouse(aim_.mouseX, aim_.mouseY);
}

namespace {
bool ControlDown(const interaction::InputFrame& c, const interaction::PadBinding& b) {
    if (!c.focused || b.hand > 1) return false;
    const auto& h = c.hands[b.hand];
    switch (b.control) {
    case interaction::VrControl::Primary: return (h.held & interaction::Primary) != 0;
    case interaction::VrControl::Secondary: return (h.held & interaction::Secondary) != 0;
    case interaction::VrControl::StickClick: return (h.held & interaction::StickClick) != 0;
    case interaction::VrControl::Squeeze: return (h.active & interaction::Squeeze) && h.squeeze >= 0.7f;
    }
    return false;
}
} // namespace

void AdapterCore::UpdateGestures(const ControllerSnapshot& in, const PlatformFrame& f) {
    const bool dev = session_.developerFeatures;
    const bool gameplay = guard_.GameplayAllowed();
    if (gameplay && !gameplayWas_) gestures_.OnLifetimeTransition(); // a level starts: full loadout
    gameplayWas_ = gameplay;
    BodyGestureInput g;
    g.engage = gameplay && !menuOpen_ && !gameUi_ && caps_.IsUsable(Feature::VirtualGamepad, dev) && guard_.PresentationAllowed();
    g.holsters = menuState_.holsters;
    g.manualReload = reloadMode_.effective == interaction::ReloadMode::Manual;
    g.timeSeconds = static_cast<double>(f.tickMs) / 1000.0;
    g.controls = in.controls;
    g.reloadButton = ControlDown(in.controls, padLayout_.reload);
    for (const auto& b : padLayout_.buttons)
        if (b.padButton == interaction::pad::Y) g.switchButton = ControlDown(in.controls, b);
    gesture_ = gestures_.Update(g);
    // Physical crouch: pulses the pad's crouch (a toggle in BF4) when the body's posture changes.
    CrouchInput k;
    k.enabled = menuState_.physicalCrouch;
    k.usable = g.engage && in.controls.focused && in.controls.headValid;
    k.timeSeconds = g.timeSeconds;
    k.headHeight = in.controls.head.position.y;
    for (const auto& b : padLayout_.buttons) {
        if (b.padButton == interaction::pad::B) k.crouchButton = ControlDown(in.controls, b);
        if (b.padButton == interaction::pad::LeftThumb) k.sprintButton = ControlDown(in.controls, b);
        if (b.padButton == interaction::pad::A) k.jumpButton = ControlDown(in.controls, b);
    }
    crouchPulse_ = crouch_.Update(k);
    platform_.SetGameKeys(gesture_.keys);
    for (unsigned h = 0; h < 2; ++h)
        if (gesture_.haptic[h].valid)
            platform_.Haptic(h, gesture_.haptic[h].strong, in.controls.generation, in.controls.spaceGeneration);
    // Gestures happen at human pace: log each change of their state.
    std::string d = gestures_.Describe();
    if (d != lastGestures_) {
        platform_.Log(d);
        lastGestures_ = std::move(d);
    }
}

void AdapterCore::UpdateCamera(const ControllerSnapshot& in, const PlatformFrame& f) {
    camera_.clear();
    // Mono (world lane): the camera follows the head only for a pose the XR host asked to
    // see (a lease); that frame then goes back as the eye pair labelled with exactly it.
    const bool lane = stereo_ == StereoMode::Mono || stereo_ == StereoMode::EngineStereoTracked;
    if (stereo_ == StereoMode::Off || stereo_ == StereoMode::Theater3D || !haveReference_ ||
        (lane ? !in.world.valid : !in.valid)) {
        nextEye_ = 0; // an alternate-eye run always starts on the left eye
        return;
    }
    const float aspect = f.backbufferHeight ? float(f.backbufferWidth) / float(f.backbufferHeight) : 16.0f / 9.0f;
    // Alternate eyes: the platform moves each frame's camera to one eye (render thread).
    const bool alternate = lane && stereo_ == StereoMode::Mono && menuState_.alternateEyes;
    auto request = [&](int eye) {
        CameraRequest r;
        r.eye = eye;
        const math::Pose& p = eye < 0 ? (lane ? in.world.head : in.head) : in.eyes[eye].pose;
        if (alternate && eye < 0) {
            r.alternateEyes = true;
            r.halfEye = in.world.halfEyeDistance > 0 ? in.world.halfEyeDistance
                        : halfEyeDistance_ > 0      ? halfEyeDistance_
                                                    : kDefaultHalfEye;
        }
        r.local = XrPoseToBf4Camera(ApplyReference(reference_, p));
        r.fov = eye < 0 ? UnionFov(in.eyes[0].fov, in.eyes[1].fov) : in.eyes[eye].fov;
        if (lane) {
            if (aim_.engaged) r.parentYaw = aim_.bodyYaw;
            r.poseId = in.world.poseId;
            r.renderFovY = RequiredVerticalFov(in.world.fov[0], in.world.fov[1], aspect);
            r.eyeFov[0] = in.world.fov[0];
            r.eyeFov[1] = in.world.fov[1];
            r.gunFollowsAim = menuState_.gunFollowsAim && stereo_ == StereoMode::Mono && !alternate;
            r.gunFovBudget = menuState_.gunReachWide ? kGunAimFovBudgetWide : kGunAimFovBudget;
            r.gunInHand = menuState_.gunInHand && stereo_ == StereoMode::Mono;
            r.armIk = r.gunInHand && menuState_.armIk;
            const auto& hand = in.controls.hands[1];
            if (r.gunInHand && in.controls.focused && hand.gripTracked && hand.aimTracked) {
                math::Pose held;
                held.position = hand.grip.position; // where the hand holds the grip...
                held.orientation = hand.aim.orientation; // ...the barrel along the aim ray (the bullets' direction)
                math::Pose heldRef = ApplyReference(reference_, held);
                // Two-handed: the barrel along the line between the hands, as the aim is.
                if (aim_.engaged && aim_.twoHanded && aim_.barrelValid)
                    heldRef.orientation = math::FromAxisAngle({0, 1, 0}, -aim_.barrel.yaw) *
                                          math::FromAxisAngle({1, 0, 0}, aim_.barrel.pitch);
                if (const auto rel = math::MakeRelativePose(ApplyReference(reference_, p), heldRef)) {
                    r.handInHead = *rel;
                    r.handValid = true;
                }
                // Arms from the body: the torso at the eye, level, facing the body frame's heading
                // (it follows the head only past a deadzone, so looking around keeps the shoulders).
                if (r.armIk && gestures_.BodyValid()) {
                    math::Pose torso;
                    torso.position = p.position;
                    torso.orientation = math::FromAxisAngle({0, 1, 0}, gestures_.BodyYaw());
                    if (const auto rel = math::MakeRelativePose(p, torso)) {
                        r.bodyInHead = *rel;
                        r.bodyValid = true;
                    }
                }
                // The left hand leaves the gun unless its grip steadies it (two-handed aim).
                const auto& left = in.controls.hands[0];
                if (r.armIk && left.gripTracked)
                    if (const auto rel = math::MakeRelativePose(p, left.grip)) {
                        r.leftHandInHead = *rel;
                        r.leftValid = true;
                        r.leftFree = !(aim_.engaged && aim_.twoHanded);
                    }
            }
        }
        camera_.push_back(r);
    };
    switch (stereo_) {
    case StereoMode::Mono:
    case StereoMode::EngineStereoTracked: request(-1); break;
    case StereoMode::Theater3D: break;
    case StereoMode::AlternateEye: request(nextEye_); nextEye_ ^= 1; break;
    case StereoMode::Native: request(0); request(1); break;
    case StereoMode::Off: break;
    }
    if (platform_.ApplyCamera(camera_)) ++stats_.cameraFrames;
}

void AdapterCore::OnFrame(const PlatformFrame& f, const ControllerSnapshot& in) {
    if (detached_ || !adopted_) return;
    ++stats_.frames;
    const bool dev = session_.developerFeatures;

    // 1. Session guard. Module scan every 2 s; binding-backed signals every frame.
    SessionObservation obs = platform_.ObserveGame();
    if (!scannedOnce_ || f.tickMs - lastModuleScan_ >= 2000) {
        scannedOnce_ = true;
        lastModuleScan_ = f.tickMs;
        obs.punkBusterModuleLoaded = platform_.PunkBusterLoaded();
    }
    guard_.Observe(obs);
    if (guard_.Locked()) {
        Detach(guard_.Reason());
        return;
    }

    // 2. Recenter: first valid pose sets the reference; afterwards on request edges only.
    //    The world lane's head pose wins (it is the pose the camera will carry).
    //    Both stick clicks held a second (the host recenters by itself only in roomscale).
    const bool gesture = !menuOpen_ && recenterGesture_.Update(in.controls);
    if (gesture) {
        ++stats_.recenterGestures;
        platform_.Log("recenter: both sticks held");
    }
    const bool headKnown = in.world.valid || in.valid;
    if (headKnown && (!haveReference_ || (in.recenterRequested && !recenterWasDown_) || gesture)) {
        if (haveReference_) ++stats_.recenters;
        reference_ = RecenterReference(in.world.valid ? in.world.head : in.head);
        haveReference_ = true;
    }
    recenterWasDown_ = in.valid && in.recenterRequested;

    // 3. Menus. FrostbiteVR's own menu takes the laser while open; otherwise the laser
    //    drives BF4's menus through the Win32 cursor path, BF4's in-level ones on the screen.
    UpdateGameUi(f);
    UpdateModMenu(in);
    UpdateMotionAim(in, f);
    UpdateGestures(in, f);
    UpdatePad(in, f);
    if (!menuOpen_ && caps_.IsUsable(Feature::GameMenuPointer, dev) && guard_.PresentationAllowed()) {
        pointer_.Update(in.pointer, f.clientWidth, f.clientHeight, events_);
    } else {
        events_.clear();
        pointer_.ForceRelease(events_);
    }
    if (!events_.empty()) platform_.ApplyPointer(events_);

    // 4. Button reload: campaign only, binding-gated, never while the pointer owns input.
    if (guard_.GameplayAllowed() && reloadMode_.effective == interaction::ReloadMode::ButtonPress &&
        caps_.IsUsable(Feature::ButtonReload, dev)) {
        interaction::ReloadFrameInput r;
        r.timeSeconds = in.timeSeconds;
        r.reloadButtonDown = in.valid && in.reloadButton;
        r.inMenu = in.pointer.active || menuOpen_;
        if (reload_.Update(r) == interaction::ReloadCommand::RequestNativeReload) {
            platform_.RequestNativeReload();
            ++stats_.reloadRequests;
        }
    }

    // 5. Engine stereo: the frame boundary closes the last eye pass.
    bool pair = false;
    if (session_.stereo != StereoSetting::Off && session_.stereoRoute == StereoRoute::Split) {
        pair = OnSplitFrame();
        UpdateStereoDriverCapability();
    } else if (session_.stereo != StereoSetting::Off) {
        const auto present = tracker_.OnPresent();
        if (present.capture != stereo::CaptureAction::None &&
            platform_.CaptureEye(present.capture == stereo::CaptureAction::CaptureLeft ? 0 : 1))
            ++stereoStats_.captures;
        pair = present.pairComplete;
        UpdateStereoDriverCapability();
    }

    // 6. Camera / stereo mode.
    stereo_ = ChooseStereo();
    UpdateCamera(in, f);

    // 7. Frame out: the mod menu panel while it is open; else a stereo pair as two eye
    //    frames, otherwise one mono frame.
    if (menuOpen_) {
        if (platform_.PublishPanel(menuPanel_)) ++stats_.menuPanels;
    } else if (caps_.IsUsable(Feature::TheaterView, dev) || stereo_ != StereoMode::Off) {
        FrameMeta meta;
        meta.frameIndex = stats_.frames;
        meta.width = f.backbufferWidth;
        meta.height = f.backbufferHeight;
        meta.menuVisible = in.pointer.active;
        meta.world = stereo_ == StereoMode::Mono; // head-tracked view goes to the headset as eye pairs
        meta.worldSettle = static_cast<std::uint32_t>(menuState_.worldSettle);
        meta.worldStereo = session_.developerFeatures && menuState_.worldStereo;
        // The barrel shows where the shots go while the gun follows the aim (the player wants
        // no crosshair then); with that off nothing else would, so the dot comes back.
        meta.reticle = (menuState_.aimDot || (!menuState_.gunFollowsAim && !menuState_.gunInHand)) && aim_.engaged;
        meta.depthStereo = menuState_.depthStereo && meta.world;
        meta.hudInsets = menuState_.hudInsets && meta.world;
        {
            // Comfort vignette: stick walking and smooth turning (snap turns are instant).
            const float walk = std::hypot(pad_.thumbLX / 32767.0f, pad_.thumbLY / 32767.0f);
            const auto& r = in.controls.hands[1];
            const bool smoothTurn = aim_.engaged && !menuState_.snapTurn && std::fabs(r.stickX) > 0.2f;
            const float dt = lastVignetteMs_ && f.tickMs > lastVignetteMs_ ? (f.tickMs - lastVignetteMs_) / 1000.0f : 0.0f;
            lastVignetteMs_ = f.tickMs;
            meta.vignette = vignette_.Update(menuState_.vignette && !menuOpen_, std::max(walk, smoothTurn ? 1.0f : 0.0f), dt);
        }
        const auto marks = gestures_.Markers();
        for (std::size_t i = 0; i < marks.size() && i < 2; ++i)
            meta.markers[i] = {marks[i].show, marks[i].position, marks[i].kind};
        if (in.world.valid && in.world.halfEyeDistance > 0) halfEyeDistance_ = in.world.halfEyeDistance;
        meta.halfEyeDistance = halfEyeDistance_;
        const bool enginePair = pair && (stereo_ == StereoMode::Theater3D || stereo_ == StereoMode::EngineStereoTracked);
        if (enginePair) {
            if (session_.stereoRoute == StereoRoute::Split) meta.width = f.backbufferWidth / 2; // one half per eye
            meta.eye = 0;
            const bool l = platform_.PublishFrame(meta);
            meta.eye = 1;
            const bool r = platform_.PublishFrame(meta);
            if (l && r) { ++stats_.framesPublished; ++stereoStats_.pairsPublished; }
        } else {
            meta.eye = stereo_ == StereoMode::AlternateEye && !camera_.empty() ? camera_.front().eye : -1;
            if (platform_.PublishFrame(meta)) ++stats_.framesPublished;
        }
        // World lane: when this Present captured the pose it was carrying, the next queued
        // request's pose goes to the camera now rather than at the next Present, so the very
        // next submit already carries it (one frame less per pair; the labelling rule is
        // unchanged: a capture still needs `settle` submits of the new pose).
        WorldRequest next;
        if (meta.world && in.world.valid && platform_.CurrentWorld(next) && next.valid && next.poseId != in.world.poseId) {
            ControllerSnapshot early = in;
            early.world = next;
            UpdateCamera(early, f);
            ++stats_.earlyPoses;
        }
    }

    // 8. Probe artefacts: one snapshot once pairs are flowing; report every ~5 s (overwritten).
    if (session_.stereo != StereoSetting::Off) {
        const std::uint64_t pairsSoFar = session_.stereoRoute == StereoRoute::Split ? stereoStats_.splitPairs : tracker_.Pairs();
        const bool textured = session_.stereoRoute != StereoRoute::Split ||
                              stereoStats_.lastSplit.verdict != stereo::SplitVerdict::Undetermined;
        if (pair && !stereoStats_.snapshotWritten && pairsSoFar >= 120 && textured)
            stereoStats_.snapshotWritten = platform_.WriteStereoSnapshot();
        if (stats_.frames - lastReportFrame_ >= 450) {
            lastReportFrame_ = stats_.frames;
            platform_.WriteReport("stereo-probe.txt", StereoReport());
        }
    }
}

std::string AdapterCore::Status() const {
    std::string s;
    s += "build=" + std::string(BuildVerdictName(build_.verdict)) + " (" + build_.label + ")\n";
    s += "session=" + guard_.Reason() + "\n";
    s += "frames=" + std::to_string(stats_.frames) + "\n";
    s += "frames_published=" + std::to_string(stats_.framesPublished) + "\n";
    s += "mod_menu=" + std::string(menuOpen_ ? "open" : "closed") + " panels=" + std::to_string(stats_.menuPanels) +
         " commands=" + std::to_string(stats_.menuCommands) + "\n";
    char padLine[120];
    std::snprintf(padLine, sizeof(padLine), "pad=%s frames=%llu buttons=0x%04x lt=%u rt=%u lx=%d ly=%d rx=%d ry=%d\n",
                  padActive_ ? "active" : "off", static_cast<unsigned long long>(stats_.padFrames), pad_.buttons,
                  pad_.leftTrigger, pad_.rightTrigger, pad_.thumbLX, pad_.thumbLY, pad_.thumbRX, pad_.thumbRY);
    s += padLine;
    s += "clicks_posted=" + std::to_string(pointer_.Clicks()) + "\n";
    char aimLine[384];
    std::snprintf(aimLine, sizeof(aimLine),
                  "controller_aim=%s body_yaw=%.3f gain=%.6f,%.6f mutes=%llu snaps=%llu two_handed=%llu guards=%llu "
                  "foreign_turns=%llu reach_limited=%llu recenters=%llu recenter_gestures=%llu stick_chords=%llu game_menus=%llu "
                  "game_menu=%s\n",
                  aim_.engaged ? "engaged" : "off", aim_.bodyYaw, motionAim_.GainX(), motionAim_.GainY(),
                  static_cast<unsigned long long>(motionAim_.Mutes()), static_cast<unsigned long long>(motionAim_.Snaps()),
                  static_cast<unsigned long long>(twoHandedEngages_),
                  static_cast<unsigned long long>(motionAim_.OscillationGuards()),
                  static_cast<unsigned long long>(motionAim_.ForeignTurns()),
                  static_cast<unsigned long long>(motionAim_.ReachLimited()), static_cast<unsigned long long>(stats_.recenters),
                  static_cast<unsigned long long>(stats_.recenterGestures), static_cast<unsigned long long>(stickChord_.Chords()),
                  static_cast<unsigned long long>(stats_.gameMenus), gameUi_ ? "on" : "off");
    s += aimLine;
    s += gestures_.Describe() + " holsters=" + (menuState_.holsters ? "on" : "off") +
         " crouch=" + (crouch_.Crouched() ? "crouched" : "standing") + " crouch_toggles=" + std::to_string(crouch_.Toggles()) + "\n";
    s += "reload_requests=" + std::to_string(stats_.reloadRequests) + "\n";
    s += "stereo=" + std::string(StereoModeName(stereo_)) + "\n";
    s += "stereo_pairs=" + std::to_string(tracker_.Pairs()) + " stereo_pattern=" +
         std::string(stereo::FramePatternName(tracker_.Dominant())) + " stereo_captures=" +
         std::to_string(stereoStats_.captures) + "\n";
    s += "detached=" + std::string(detached_ ? "1" : "0") + "\n";
    s += "reload_mode=" + std::string(interaction::ReloadModeName(reloadMode_.effective));
    if (!reloadMode_.note.empty()) s += " (" + reloadMode_.note + ")";
    s += "\n";
    for (const auto& b : bindings_.All())
        s += "binding " + std::string(b.name) + "=" + std::string(frostbite::ScanStatusName(b.status)) + "\n";
    s += caps_.Describe();
    return s;
}

} // namespace fvr::bf4
