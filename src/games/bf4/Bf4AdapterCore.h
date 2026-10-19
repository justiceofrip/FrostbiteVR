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
#include "stereo/SideBySide.h"
#include "stereo/StereoProbe.h"
#include "../../engines/frostbite/Fb3CameraMath.h"
#include "fvr/game/Capability.h"
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
struct CameraRequest {
    int eye = -1; // -1 mono, 0 left, 1 right
    frostbite::LinearTransform local;
    frostbite::FovAngles fov;
    std::uint64_t poseId = 0; // world lane lease the pose belongs to (0: none)
    float parentYaw = std::numeric_limits<float>::quiet_NaN(); // body heading (controller aiming)
    float renderFovY = 0;     // vertical FOV to render (radians) for the full frame's aspect
    math::FovTangents eyeFov[2]; // the request's eye tangents (lane requests only)
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
    virtual void SendMouse(int /*dx*/, int /*dy*/) {}
};

struct PlatformFrame {
    std::uint64_t tickMs = 0;
    std::uint32_t backbufferWidth = 0, backbufferHeight = 0;
    int clientWidth = 0, clientHeight = 0;
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
    std::uint64_t reloadRequests = 0;
    std::uint64_t cameraFrames = 0;
    std::uint64_t recenters = 0;
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
    // Session default for "3D depth" (off after a session that ended with 3D on).
    void SetWorldStereo(bool on);

    // FrostbiteVR's own in-headset menu (menu button toggles it; the laser drives it).
    bool ModMenuOpen() const { return menuOpen_; }
    const ui::VrPanel& ModMenuPanel() const { return menuPanel_; }
    const ModMenuState& MenuSettings() const { return menuState_; }
    const interaction::PadState& Pad() const { return pad_; }

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
    void UpdatePad(const ControllerSnapshot& input);
    void UpdateMotionAim(const ControllerSnapshot& input, const PlatformFrame& frame);
    MotionAim motionAim_;
    MotionAimOutput aim_{};
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
