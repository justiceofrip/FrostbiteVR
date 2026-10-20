#pragma once
// Holsters and magazine reload for BF4 as VR gestures, input-only: they press the game's
// own controls (weapon keys, the pad's reload) and write no game memory. The game keeps
// its inventory, ammunition and animations; the gestures decide when to ask for them.
//
// Holsters (body-relative, fvr::interaction::HolsterSystem; body heading follows the head
// with a dead zone):
//   over the right shoulder  -> primary weapon     (key 1)
//   right hip                -> sidearm            (key 2)
//   over the left shoulder   -> first gadget       (key 3)
//   left hip                 -> grenade: grab it, let go away from the hip to throw (G)
//   chest                    -> knife              (F, BF4's melee)
//   belt, front left         -> a magazine (manual reload)
// A grip press that starts inside a holster belongs to the gesture, not to the pad.
//
// Manual reload (magazine gesture): a tap on right B drops the magazine of the gun in hand
// (on release); fire is held off until a magazine is seated; take one from the belt with
// the left hand and bring it to the gun's magazine well (just below and in front of the
// right hand); seating it presses the game's reload and BF4 plays its own reload. Holding
// B holds the pad's X for as long as B is held: BF4's reload, or its interact/pick-up when
// something is in reach (the same button on the pad), so nothing is lost to the gesture.
// A magazine pushed into a gun that still has one is a tactical reload (same press).
//
// Persistent state (AGENTS.md): a dropped magazine stays out of that gun (primary and
// sidearm separately) and a magazine stays in the left hand until it is seated, dropped
// or a lifetime transition (level load) happens. Repeated packets, menus and tracking loss
// never reset them; while the input is not usable the state simply waits.
#include "fvr/interaction/ControllerInput.h"
#include "fvr/interaction/Holsters.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace fvr::bf4 {

// Set-1 scan codes of BF4's default PC bindings the gestures press.
namespace gamekey {
inline constexpr std::uint16_t Primary = 0x02;   // 1
inline constexpr std::uint16_t Secondary = 0x03; // 2
inline constexpr std::uint16_t Gadget = 0x04;    // 3 (first gadget)
inline constexpr std::uint16_t Grenade = 0x22;   // G (throw grenade)
inline constexpr std::uint16_t Melee = 0x21;     // F (knife)
} // namespace gamekey

std::vector<interaction::HolsterSlot> Bf4HolsterLayout();

enum class Bf4Gun : std::uint8_t { Primary = 0, Secondary = 1 };

struct BodyGestureInput {
    bool engage = false;       // campaign play, menu closed, feature on
    bool holsters = true;      // holster gestures on
    bool manualReload = false; // magazine gestures (else right B is the plain reload button)
    double timeSeconds = 0;
    interaction::InputFrame controls; // this packet (tracking space)
    bool reloadButton = false; // right B (the layout's reload control)
    bool switchButton = false; // left Y (pad Y: BF4 toggles primary/sidearm)
};

struct HapticCue {
    bool valid = false;
    bool strong = false; // seated / thrown (else: grabbed)
};

struct BodyGestureOutput {
    std::uint16_t padButtons = 0;      // pulses to add to the pad (reload)
    bool blockFire = false;            // the gun in hand has no magazine
    std::array<bool, 2> gripOwned{};   // this grip press is a gesture: not the pad's
    std::array<std::uint16_t, 2> keys{}; // keys held this frame (0: none); the platform diffs
    std::array<HapticCue, 2> haptic{};   // per hand
};

class BodyGestures {
public:
    static constexpr double kKeyHoldSeconds = 0.08;   // a key is held this long per press
    static constexpr double kReloadPulseSeconds = 0.12;
    static constexpr double kHoldSeconds = 0.4; // B held this long: the pad's X (reload / interact)
    static constexpr float kMagwellRadius = 0.12f;     // metres around the magazine well
    static constexpr float kThrowDistance = 0.25f;     // grenade released this far from the hip
    static constexpr float kGripPress = 0.70f, kGripRelease = 0.40f;

    BodyGestures();
    BodyGestureOutput Update(const BodyGestureInput& in);
    // Level load / new session: magazines seated, holsters restocked, hands empty.
    void OnLifetimeTransition();

    Bf4Gun Current() const { return current_; }
    bool GadgetInHand() const { return gadget_; }
    bool MagazineOut(Bf4Gun gun) const { return magazineOut_[static_cast<int>(gun)]; }
    bool MagazineInHand() const { return magazineInHand_; }
    // The torso's heading (the holsters' body frame: follows the head beyond a deadzone), in
    // the controllers' space, while the gestures are engaged.
    bool BodyValid() const { return usable_; }
    float BodyYaw() const { return body_.BodyYaw(); }
    std::string Describe() const;
    // Where the magazine well is for a right-hand grip pose (tracking space).
    static math::Vec3 Magwell(const math::Pose& rightGrip);
    // A holster's position in the latest body frame (tracking space; zero before one).
    math::Vec3 SlotPosition(std::string_view id) const;
    // What to show in the headset now (tracking space): the belt while the gun in hand has
    // no magazine and the hand none either (kind 0), the magazine well while one is in the
    // left hand (kind 1).
    struct Marker {
        bool show = false;
        math::Vec3 position{};
        std::uint32_t kind = 0;
    };
    std::array<Marker, 2> Markers() const;

    struct Counters {
        std::uint64_t draws = 0, throws = 0, knives = 0, gadgets = 0, ejects = 0, grabs = 0, seats = 0, drops = 0,
                      quickReloads = 0, keyPresses = 0, hovers = 0, tacticalReloads = 0;
    };
    const Counters& Stats() const { return stats_; }

private:
    void PressKey(std::uint16_t key, double now);
    void PulseReload(double now);
    void Select(Bf4Gun gun, double now);

    enum class Held : std::uint8_t { None, Grenade };
    interaction::BodyFrameEstimator body_;
    interaction::BodyFrame lastBody_{};
    math::Pose lastRightGrip_{};
    bool rightGripTracked_ = false, manualReload_ = false, usable_ = false;
    interaction::HolsterSystem holsters_;
    Bf4Gun current_ = Bf4Gun::Primary;
    bool gadget_ = false; // a gadget is in hand (selected from its holster)
    std::array<bool, 2> magazineOut_{};
    bool magazineInHand_ = false;
    std::array<bool, 2> grip_{}, gripOwned_{};
    std::array<Held, 2> held_{};
    std::array<int, 2> hover_{-1, -1}; // usable holster each hand is in (pulse on entry)
    bool reloadWas_ = false, switchWas_ = false, holdActive_ = false;
    double reloadDownAt_ = 0;
    std::uint16_t key_ = 0;
    double keyUntil_ = 0, reloadUntil_ = 0;
    Counters stats_;
};

} // namespace fvr::bf4
