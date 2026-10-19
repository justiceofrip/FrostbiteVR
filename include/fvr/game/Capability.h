#pragma once
// Feature capability registry. Every native feature starts Disabled with an explicit
// reason and is only promoted on specific evidence (AGENTS.md rule). The launcher, the
// mod menu and feedback reports all read this one source of truth.
#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace fvr::game {

enum class Feature : std::uint8_t {
    TheaterView,        // game frame on a virtual screen (M0, no camera RE needed)
    ModMenuPointer,     // FrostbiteVR's own overlay menu, laser pointer
    GameMenuPointer,    // BF4 front-end / pause menus, laser pointer -> cursor injection
    HeadTrackedCamera,  // 6DoF head drives the render camera (M1)
    StereoNative,       // engine's built-in per-eye stereo path, retargeted (M2 preferred)
    StereoAlternateEye, // fallback: alternate-eye camera override (M2 fallback)
    StereoDriver,       // engine's own 3D Vision path, driven by the emulated nvapi64.dll
    ControllerAim,      // weapon/fire direction from the weapon-hand controller (M3)
    ButtonReload,       // reload on a controller button press via a native input binding (M3)
    VirtualGamepad,     // VR controllers drive a virtual XInput pad the game reads natively (M3)
    ManualReload,       // physical magazine reload (roadmap M5)
    Holsters,           // body-relative weapon/equipment holsters (roadmap M4)
    ArmIk,              // two-bone arm IK onto first-person arms (roadmap M6)
    FullBodyIk,         // visible body (roadmap M6+)
    Vehicles,           // vehicle seats/aim (roadmap M7)
    Count
};

enum class FeatureState : std::uint8_t {
    Disabled,  // not usable; reason says why
    Candidate, // implemented, evidence pending; usable only in developer sessions
    Enabled    // evidence recorded; usable in player sessions
};

// Strongest evidence recorded for a feature, independent of its state: one passing
// feature never promotes another, and lockdown disables without erasing history.
// (Same vocabulary as CURRENT.md: deterministic/emulated, actual-game check, headset.)
enum class Evidence : std::uint8_t {
    None,       // nothing beyond unit tests of its parts
    Emulated,   // end to end on the emulated game / fake XR runtime
    ActualGame, // observed working on the real bf4.exe (no headset)
    Headset     // accepted by a player in the headset
};

struct FeatureStatus {
    FeatureState state = FeatureState::Disabled;
    std::string reason = "not evaluated";
    Evidence evidence = Evidence::None;
};

std::string_view FeatureName(Feature feature) noexcept;
std::string_view FeatureStateName(FeatureState state) noexcept;
std::string_view EvidenceName(Evidence evidence) noexcept;

class CapabilityRegistry {
public:
    CapabilityRegistry();

    void Set(Feature feature, FeatureState state, std::string reason);
    // Raises the recorded evidence (never lowers it).
    void RecordEvidence(Feature feature, Evidence evidence);
    const FeatureStatus& Get(Feature feature) const;

    // Candidates are usable only when the session explicitly allows developer features.
    bool IsUsable(Feature feature, bool allowCandidates) const;

    // Disable everything at once (multiplayer lockdown, fatal binding failure).
    void DisableAll(std::string_view reason);

    // Plain-text status table for logs and feedback reports.
    std::string Describe() const;

private:
    std::array<FeatureStatus, static_cast<std::size_t>(Feature::Count)> status_{};
};

} // namespace fvr::game
