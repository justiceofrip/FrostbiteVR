#pragma once
// Native side of BF4 manual reload, input-only (docs/BF4-MANUAL-RELOAD.md section 4,
// option 1). It never writes game memory: it holds the virtual pad's trigger, presses the
// game's own reload, and acknowledges the framework's ManualReload requests only from
// ammunition the game was seen to move on the player's firing object. Because the game
// performs the reload from input, this works whichever copy (client or server) turns out
// to be authoritative.
//
// Persistent state (AGENTS.md): a magazine out of the gun, an open breech and an owed
// charging/slide/pump/bolt step last across frames until they complete or the weapon's
// lifetime ends (switch, death). Repeated observations never clear them.
#include "Bf4ReloadClassifier.h"
#include "fvr/interaction/ManualReload.h"
#include "fvr/interaction/VirtualPad.h"

#include <cstdint>
#include <optional>
#include <string_view>

namespace fvr::bf4 {

// The player's firing object as observed this frame (the census/ownership chain supplies it).
struct NativeFiringSample {
    bool valid = false;         // the player holds an observed weapon
    std::uint64_t identity = 0; // changes on weapon switch or respawn
    std::int32_t state = 0, loaded = 0, reserve = 0, capacity = 0;
};

inline constexpr std::int32_t kNativeReadyState = 3; // firing state machine "ready" (BF4-MANUAL-RELOAD.md 2.2)

struct ReloadDriverInput {
    std::int64_t nowNs = 0;
    NativeFiringSample native{};
    std::optional<interaction::ManualReloadRequest> request{}; // a NEW coordinator request this frame
    bool transactionCancelled = false;  // the coordinator cancelled (its pending request is void)
    bool cycleGestureCompleted = false; // the VR charging handle / slide / pump / bolt stroke finished
};

enum class ReloadGate : std::uint8_t { None, MagazineOut, BreechOpen, NativeReload, OwedCycle };
std::string_view ReloadGateName(ReloadGate gate) noexcept;

struct ReloadDriverOutput {
    bool blockFire = false;
    bool pressReload = false;
    std::optional<interaction::ManualReloadAck> ack{};
    ReloadGate gate = ReloadGate::None;
    interaction::WeaponAction owedCycle = interaction::WeaponAction::Automatic;
};

class Bf4ReloadDriver {
public:
    // Unsupported archetypes (ButtonOnly, and Tube until per-round gating has native
    // evidence) leave everything to button reload: no gates, no presses, no acks.
    explicit Bf4ReloadDriver(const Bf4ReloadClassification& classification);
    bool Supported() const { return supported_; }
    ReloadDriverOutput Update(const ReloadDriverInput& in);

    static constexpr std::int64_t kPressNs = 60'000'000;  // reload held this long
    static constexpr std::int64_t kStartNs = 700'000'000; // the game must leave "ready" within this

private:
    void Handle(const interaction::ManualReloadRequest& r, const NativeFiringSample& n, std::int64_t now,
                ReloadDriverOutput& out);
    void StartNative(const interaction::ManualReloadRequest& r, const NativeFiringSample& n, std::int64_t now);
    void EndWeapon();
    static interaction::ManualReloadAck Ack(const interaction::ManualReloadRequest& r,
                                            interaction::ReloadAcknowledgement status);

    interaction::WeaponMechanismDescriptor descriptor_{};
    Bf4ReloadArchetype archetype_ = Bf4ReloadArchetype::ButtonOnly;
    bool supported_ = false;
    std::uint64_t identity_ = 0;
    bool magazineOut_ = false, breechOpen_ = false, roundStaged_ = false;
    interaction::WeaponAction owed_ = interaction::WeaponAction::Automatic;
    struct Pending {
        interaction::ManualReloadRequest request;
        std::int64_t pressedAt = 0;
        std::int32_t loaded = 0, reserve = 0;
        bool started = false;
    };
    std::optional<Pending> pending_;
    bool haveLast_ = false;
    std::int32_t lastLoaded_ = 0;
};

// Pad side: hold the trigger at zero while gated, assert the reload button while pressing.
inline void ApplyReloadDriverToPad(interaction::PadState& pad, const ReloadDriverOutput& out, std::uint16_t reloadButton) {
    if (out.blockFire) pad.rightTrigger = 0;
    if (out.pressReload) pad.buttons |= reloadButton;
}

} // namespace fvr::bf4
