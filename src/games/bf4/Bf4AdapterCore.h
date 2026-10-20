#pragma once
// Bf4AdapterCore: everything the adapter decides per frame, with no Windows, D3D or
// game-memory dependency. Bf4Module.cpp implements IAdapterPlatform (Win32 + D3D11 +
// MinHook) and forwards each Present into OnFrame(). The emulated tests implement the
// same interface with fakes, so the real frame logic runs headless in CI and here.
#include "Bf4Bindings.h"
#include "Bf4FrameworkGlue.h"
#include "Bf4ModMenu.h"
#include "Bf4PadLayout.h"
#include "Bf4PointerInjector.h"
#include "Bf4Profile.h"
#include "Bf4SessionGuard.h"
#include "Bf4SessionInfo.h"
#include "Bf4MotionAim.h"
#include "Bf4BodyGestures.h"
#include "Bf4PhysicalCrouch.h"
#include "Bf4StickChord.h"
#include "Bf4WorldView.h"
#include "stereo/SideBySide.h"
#include "stereo/StereoProbe.h"
#include "../../engines/frostbite/Fb3CameraMath.h"
#include "fvr/game/Capability.h"
#include "fvr/interaction/ControllerInput.h"
#include "fvr/interaction/ReloadPolicy.h"
#include "fvr/ui/ModMenuPanel.h"

#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fvr::bf4 {

// A camera change the platform applies through the camera binding, composing `local`
// (tracking space, Frostbite convention) onto the game's own camera anchor, with every
// write journaled for exact restoration.
// The platform builds the projection from `fov` with the game's own near/far and depth
// convention (learned in RE task R3), so the core never guesses those.
// Gun in the hand: where the shots cross the drawn barrel's line (MotionAimInput::converge).
inline constexpr float kGunConvergeMeters = 20.0f;
// Alternate eyes before the headset reported its eye distance: half a typical 64 mm.
inline constexpr float kDefaultHalfEye = 0.032f;

struct CameraRequest {
    int eye = -1; // -1 mono, 0 left, 1 right
    frostbite::LinearTransform local;
    frostbite::FovAngles fov;
    std::uint64_t poseId = 0; // world lane lease the pose belongs to (0: none)
    float parentYaw = std::numeric_limits<float>::quiet_NaN(); // body heading (controller aiming)
    float renderFovY = 0;     // vertical FOV to render (radians) for the full frame's aspect
    math::FovTangents eyeFov[2]; // the request's eye tangents (lane requests only)
    bool gunFollowsAim = false;  // render along the game's aim, eyes rotated back (Bf4WorldView.h)
    float gunFovBudget = kGunAimFovBudget; // how wide that render may get (the player's reach choice)
    // Gun in the hand (Bf4FirstPersonView.h): the right hand relative to this request's head
    // (grip position, aim orientation; OpenXR convention), for the first-person pass.
    bool gunInHand = false;
    bool handValid = false;
    math::Pose handInHead{};
    bool armIk = false;       // ...and the arms reach it from the body (Bf4ArmIk.h)
    bool bodyValid = false;   // ...the torso then (the holsters' body frame: level, its heading) relative to this head
    math::Pose bodyInHead{};
    bool leftValid = false;   // ...the left controller's grip relative to this head (tracked)
    bool leftFree = false;    // ...and whether the left hand is off the gun (not steadying it)
    math::Pose leftHandInHead{};
    // Alternate eyes (3D): the platform renders each frame from one eye, the eyes in turn,
    // half the eye distance (world units) from this head (Bf4FirstPersonView.h).
    bool alternateEyes = false;
    float halfEye = 0;
};

// Off: flat theater. Theater3D: engine stereo pair on the theater screen, no head tracking.
// Mono/AlternateEye/Native: head-tracked via bindings. EngineStereoTracked: head-tracked
// mono camera, engine splits the eyes itself (3D Vision path).
enum class StereoMode : std::uint8_t { Off, Theater3D, Mono, AlternateEye, Native, EngineStereoTracked };
std::string_view StereoModeName(StereoMode mode) noexcept;

// What the platform knows about the emulated nvapi64.dll.
struct StereoShimState {
    bool bound = false;          // our nvapi64.dll is loaded and the eye callback registered
    bool handleCreated = false;
    bool directMode = false;
    std::uint64_t eyeSwitches = 0;
    unsigned gpuVendorId = 0;    // the real adapter's vendor
    bool vendorSpoofed = false;
};

class IAdapterPlatform {
public:
    virtual ~IAdapterPlatform() = default;
    virtual bool PunkBusterLoaded() = 0;              // module list scan (called every ~2 s)
    virtual SessionObservation ObserveGame() = 0;     // binding-backed level/server signals
    virtual void ApplyPointer(std::span<const PointerEvent> events) = 0;
    virtual bool PublishFrame(const FrameMeta& meta) = 0;
    virtual bool ApplyCamera(std::span<const CameraRequest> requests) = 0;
    virtual void RequestNativeReload() = 0;
    virtual void DetachAll(std::string_view reason) = 0;
    virtual void Log(std::string_view line) = 0;

    // Engine stereo (3D Vision path). CaptureEye copies the current backbuffer into that
    // eye's texture; called on the render thread at eye boundaries.
    virtual StereoShimState StereoShim() = 0;
    virtual bool CaptureEye(int eye) = 0;
    virtual bool WriteStereoSnapshot() = 0;             // side-by-side BMP in the log folder
    virtual void SetStereoSuppressed(bool suppressed) = 0; // shim reports "stereo off"
    virtual std::string NvapiReport() = 0;
    virtual void WriteReport(std::string_view name, std::string_view text) = 0;

    // Engine split-screen stereo (RenderDevice.StereoSplitScreenEnable): copy the left and
    // right halves of the backbuffer into the two eye textures, on the render thread at
    // Present. False if the backbuffer cannot be copied (e.g. MSAA, odd size).
    virtual bool CaptureSplitPair() = 0;
    // CPU readback of the current backbuffer for the side-by-side check (probe; costly).
    virtual bool AnalyzeBackbufferSplit(stereo::SplitAnalysis& out) = 0;

    // FrostbiteVR's own menu while open: rasterise the panel and send it on the theater
    // lane instead of the game frame (the host's laser then points at the panel).
    virtual bool PublishPanel(const ui::VrPanel& panel) = 0;

    // Virtual gamepad (XInput user 0) the game polls; active=false hands the pad back
    // to real hardware / disconnected. Called every frame while the feature is usable.
    virtual void ApplyPad(const interaction::PadState& pad, bool active) = 0;

    // Controller aiming (Bf4MotionAim.h): the game's current aim as its last view submit
    // carried it (sample increments per submit), and relative mouse look to steer it.
    virtual bool GameAim(AimAngle& /*aim*/, std::uint64_t& /*sample*/) { return false; }
    // The game's own vertical FOV as its last view submit carried it (0: unknown).
    virtual float GameFovY() { return 0; }
    virtual void SendMouse(int /*dx*/, int /*dy*/) {}

    // Holster gestures (Bf4BodyGestures.h): the BF4 keys to hold this frame (set-1 scan
    // codes, 0 = none). The platform presses new ones and releases the rest.
    virtual void SetGameKeys(const std::array<std::uint16_t, 2>& /*held*/) {}
    // A short controller pulse on the XR host (hand 0 left, 1 right) answering the input
    // packet `inputGeneration` of reference space `space`.
    virtual void Haptic(unsigned /*hand*/, bool /*strong*/, std::uint64_t /*inputGeneration*/, std::uint64_t /*space*/) {}
    // The world lane's current eye-pair request (the oldest queued lease) as it stands now,
    // e.g. after this Present captured the previous one; false when none is held.
    virtual bool CurrentWorld(WorldRequest& /*out*/) { return false; }
};

struct PlatformFrame {
    std::uint64_t tickMs = 0;
    std::uint32_t backbufferWidth = 0, backbufferHeight = 0;
    int clientWidth = 0, clientHeight = 0;
    // The game's window is in front and shows the system cursor: BF4's own menus (pause,
    // death, mission end) own the screen. Hidden in play (live, 2026-10-19: pause menu
    // CURSOR_SHOWING with a cursor handle; playing, neither).
    bool gameCursor = false;
};

struct StereoStats {
    std::uint64_t captures = 0;
    std::uint64_t pairsPublished = 0;
    bool snapshotWritten = false;
    // Split-screen route.
    std::uint64_t splitPairs = 0;
    std::uint64_t splitChecks = 0, splitStereoChecks = 0, splitMonoChecks = 0;
    stereo::SplitAnalysis lastSplit;
};

struct CoreStats {
    std::uint64_t frames = 0;
    std::uint64_t framesPublished = 0;
    std::uint64_t earlyPoses = 0; // next lease's pose applied in the Present that captured the last
    std::uint64_t reloadRequests = 0;
    std::uint64_t cameraFrames = 0;
    std::uint64_t recenters = 0;
    std::uint64_t recenterGestures = 0; // both stick clicks held a second
    std::uint64_t gameMenus = 0;        // times BF4's own menu took the screen during a level
    std::uint64_t menuPanels = 0;
    std::uint64_t menuCommands = 0;
    std::uint64_t padFrames = 0;
};

class AdapterCore {
public:
    AdapterCore(SessionInfo session, BuildIdentity build, BindingTable bindings, IAdapterPlatform& platform);

    void OnSwapchainAdopted(bool ok);
    void OnFrame(const PlatformFrame& frame, const ControllerSnapshot& input);

    // Runtime setting change from the in-headset mod menu.
    void SetReloadMode(interaction::ReloadMode requested);
    // A mod menu item set to `value` as if chosen in the headset (developer research);
    // false for an unknown or disabled item or a value out of range.
    bool SetMenuSetting(const std::string& id, int value);
    // The player's menu choices kept between sessions: "id value" lines of the enabled choice and
    // toggle items, except what the launcher owns (reload mode) and developer items. Apply sets
    // the known ones back (after adoption, when the items are enabled); returns how many.
    std::string MenuValuesText() const;
    int ApplyMenuValuesText(std::string_view text);
    // BF4's split two-view 3D: unsupported (its world renderer holds one view), always off.
    void SetWorldStereo(bool on);

    // FrostbiteVR's own in-headset menu (menu button toggles it; the laser drives it).
    bool ModMenuOpen() const { return menuOpen_; }
    const ui::VrPanel& ModMenuPanel() const { return menuPanel_; }
    const ModMenuState& MenuSettings() const { return menuState_; }
    const interaction::PadState& Pad() const { return pad_; }
    const BodyGestures& Gestures() const { return gestures_; }

    // Engine stereo: SetActiveEye seen by the shim (render thread).
    void OnStereoEye(stereo::Eye eye);
    const stereo::EyeSequenceTracker& StereoTracker() const { return tracker_; }
    const StereoStats& Stereo3D() const { return stereoStats_; }
    std::string StereoReport();

    bool Detached() const { return detached_; }
    const SessionGuard& Guard() const { return guard_; }
    const game::CapabilityRegistry& Caps() const { return caps_; }
    const BindingTable& Bindings() const { return bindings_; }
    const interaction::ReloadModeResolution& ReloadMode() const { return reloadMode_; }
    StereoMode Stereo() const { return stereo_; }
    bool GameMenuOnScreen() const { return gameUi_; } // BF4's own menu during a level (theater view)
    // Developer research: the last kAimTraceSize controller-aim updates as CSV (one row each:
    // tick, game sample, the game's aim, the target, counts sent, gains and counters), oldest
    // first, so a live event (e.g. a death camera) can be replayed in a test.
    std::string AimTraceCsv() const;
    const CoreStats& Stats() const { return stats_; }
    std::uint64_t Clicks() const { return pointer_.Clicks(); }
    std::string Status() const;

    // Yaw-only recenter reference (pitch/roll stay with the player's head).
    static math::Pose RecenterReference(const math::Pose& head);
    static math::Pose ApplyReference(const math::Pose& reference, const math::Pose& pose);
    static frostbite::FovAngles UnionFov(const frostbite::FovAngles& a, const frostbite::FovAngles& b);
    // OpenXR pose (relative to the recentre reference) as a BF4 camera-space transform.
    static frostbite::LinearTransform XrPoseToBf4Camera(const math::Pose& pose);

private:
    void Detach(std::string_view why);
    StereoMode ChooseStereo() const;
    void UpdateCamera(const ControllerSnapshot& input, const PlatformFrame& frame);
    void UpdateModMenu(const ControllerSnapshot& input);
    void ApplyMenuCommand(const ModMenuCommand& command);

    SessionInfo session_;
    BuildIdentity build_;
    BindingTable bindings_;
    IAdapterPlatform& platform_;
    game::CapabilityRegistry caps_;
    SessionGuard guard_;
    PointerInjector pointer_;
    interaction::ButtonReloadRouter reload_;
    interaction::ReloadModeResolution reloadMode_;
    std::vector<PointerEvent> events_;
    std::vector<CameraRequest> camera_;
    math::Pose reference_{};
    float halfEyeDistance_ = 0; // the headset's, from the latest world request
    bool haveReference_ = false;
    bool recenterWasDown_ = false;
    // BF4's host runs without --roomscale, where the host itself would take the both-sticks
    // hold: the adapter recognises it from the controller sample instead.
    interaction::RecenterGesture recenterGesture_;
    StickChord stickChord_;
    // BF4's own menu on screen (PlatformFrame::gameCursor) for kGameUiFrames frames in a row
    // switches to the theater view; as many frames without it switch back.
    static constexpr int kGameUiFrames = 4;
    bool gameUi_ = false;
    int gameUiRun_ = 0;
    void UpdateGameUi(const PlatformFrame& frame);
    bool detached_ = false;
    bool adopted_ = false;
    std::uint64_t lastModuleScan_ = 0;
    bool scannedOnce_ = false;
    StereoMode stereo_ = StereoMode::Off;
    int nextEye_ = 0;
    CoreStats stats_;
    ModMenuState menuState_;
    ui::ModMenuModel modMenu_{"FrostbiteVR"};
    ui::VrPanel menuPanel_{1280, 720}; // 16:9, the host's verified pointer surface
    bool menuOpen_ = false;
    interaction::PadLayout padLayout_ = DefaultPadLayout();
    interaction::PadState pad_;
    bool padActive_ = false;
    std::uint16_t padPulse_ = 0;
    int padPulseFrames_ = 0;
    void UpdatePad(const ControllerSnapshot& input, const PlatformFrame& frame);
    void UpdateGestures(const ControllerSnapshot& input, const PlatformFrame& frame);
    void ResolveReload();
    BodyGestures gestures_;
    BodyGestureOutput gesture_{};
    PhysicalCrouch crouch_;
    bool crouchPulse_ = false;
    bool gameplayWas_ = false;
    std::string lastGestures_;
    void UpdateMotionAim(const ControllerSnapshot& input, const PlatformFrame& frame);
    MotionAim motionAim_;
    MotionAimOutput aim_{};
    struct AimTraceRow {
        std::uint64_t tickMs = 0, sample = 0;
        float gameYaw = 0, gamePitch = 0, targetYaw = 0, targetPitch = 0, gainX = 0, gainY = 0;
        int mouseX = 0, mouseY = 0;
        std::uint32_t guards = 0, mutes = 0, foreign = 0, flips = 0;
        bool engaged = false;
    };
    static constexpr std::size_t kAimTraceSize = 4096;
    std::vector<AimTraceRow> aimTrace_;
    std::size_t aimTraceNext_ = 0;
    static constexpr std::uint64_t kWorldLaneHoldMs = 500; // lease gaps shorter than this keep aiming engaged
    std::uint64_t lastLeaseMs_ = 0;
    math::FovTangents lastEyeFov_[2]; // the latest world request's eyes (gun follows aim's reach)
    bool haveEyeFov_ = false;
    ComfortVignette vignette_;
    bool twoHandedWas_ = false;
    std::uint64_t twoHandedEngages_ = 0;
    std::uint64_t lastVignetteMs_ = 0;
    std::uint64_t lastAimTickMs_ = 0;

    bool EngineStereoAllowed() const;
    bool EnginePairsWorking() const; // route-dependent: verified split frames or sequential eyes
    void UpdateStereoDriverCapability();
    bool OnSplitFrame(); // split route, per Present; returns true when a pair was captured
    std::uint64_t lastSplitCheck_ = 0;
    stereo::EyeSequenceTracker tracker_;
    StereoStats stereoStats_;
    std::uint64_t lastReportFrame_ = 0;
};

} // namespace fvr::bf4
