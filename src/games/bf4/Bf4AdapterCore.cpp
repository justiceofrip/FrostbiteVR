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
    reloadMode_ = interaction::ResolveReloadMode(session_.reloadMode, caps_, session_.developerFeatures);
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

void AdapterCore::UpdatePad(const ControllerSnapshot& in) {
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
    // Outside campaign play the trigger pointed at the screen is a mouse click, not the
    // pad's trigger as well (the front end reacts to both).
    if (in.pointer.active && !guard_.GameplayAllowed()) pad_.leftTrigger = pad_.rightTrigger = 0;
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
    case ModMenuAction::SetSnapTurn: menuState_.snapTurn = command.value != 0; break;
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
    case ModMenuAction::SetWorldStereo:
        menuState_.worldStereo = command.value != 0;
        platform_.Log(menuState_.worldStereo ? "3D depth: on" : "3D depth: off");
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
    reloadMode_ = interaction::ResolveReloadMode(session_.reloadMode, caps_, session_.developerFeatures);
    RefreshModMenu(modMenu_, caps_, session_.developerFeatures); // availability follows the new capabilities
    platform_.Log(ok ? "swapchain adopted; capabilities evaluated" : "no usable swapchain");
}

void AdapterCore::SetReloadMode(interaction::ReloadMode requested) {
    session_.reloadMode = requested;
    reloadMode_ = interaction::ResolveReloadMode(requested, caps_, session_.developerFeatures);
    reload_.Reset();
    platform_.Log("reload mode -> " + std::string(interaction::ReloadModeName(reloadMode_.effective)) +
                  (reloadMode_.note.empty() ? "" : " (" + reloadMode_.note + ")"));
}

void AdapterCore::Detach(std::string_view why) {
    if (detached_) return;
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

StereoMode AdapterCore::ChooseStereo() const {
    const bool dev = session_.developerFeatures;
    const bool engine = EngineStereoAllowed();
    const bool headTracked =
        menuState_.headTracking && guard_.GameplayAllowed() && caps_.IsUsable(Feature::HeadTrackedCamera, dev);
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

void AdapterCore::SetWorldStereo(bool on) {
    menuState_.worldStereo = on;
    if (ui::MenuItem* item = modMenu_.Find("world_stereo")) item->value = on ? 1 : 0;
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
    auto request = [&](int eye) {
        CameraRequest r;
        r.eye = eye;
        const math::Pose& p = eye < 0 ? (lane ? in.world.head : in.head) : in.eyes[eye].pose;
        r.local = frostbite::XrPoseToFrostbite(ApplyReference(reference_, p));
        r.fov = eye < 0 ? UnionFov(in.eyes[0].fov, in.eyes[1].fov) : in.eyes[eye].fov;
        if (lane) {
            r.poseId = in.world.poseId;
            r.renderFovY = RequiredVerticalFov(in.world.fov[0], in.world.fov[1], aspect);
            r.eyeFov[0] = in.world.fov[0];
            r.eyeFov[1] = in.world.fov[1];
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
    const bool headKnown = in.world.valid || in.valid;
    if (headKnown && (!haveReference_ || (in.recenterRequested && !recenterWasDown_))) {
        if (haveReference_) ++stats_.recenters;
        reference_ = RecenterReference(in.world.valid ? in.world.head : in.head);
        haveReference_ = true;
    }
    recenterWasDown_ = in.valid && in.recenterRequested;

    // 3. Menus. FrostbiteVR's own menu takes the laser while open; otherwise the laser
    //    drives BF4's menus through the Win32 cursor path.
    UpdateModMenu(in);
    UpdatePad(in);
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
