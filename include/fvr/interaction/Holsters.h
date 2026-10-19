#pragma once
// Roadmap M4 policy, implemented ahead of the game binding: body frame estimation and
// holster draw/stow with persistent ownership. No game data needed; the adapter maps
// HolsterSlot::nativeSlotIndex to BF4 inventory slots when M3/M4 bindings exist.
#include "BodyModel.h"

#include <string>
#include <tuple>
#include <vector>

namespace fvr::interaction {

class BodyFrameEstimator final : public IBodyFrameEstimator {
public:
    struct Config {
        float playerHeight = 1.75f;     // metres, from the calibration step
        float yawDeadzone = 0.65f;      // rad the head may turn before the torso follows
        float yawFollowRate = 4.0f;     // rad/s once outside the deadzone
    };
    BodyFrameEstimator() = default;
    explicit BodyFrameEstimator(Config c) : config_(c) {}

    BodyFrame Update(const math::Pose& head, const math::Pose& leftHand, const math::Pose& rightHand,
                     double timeSeconds) override;
    float BodyYaw() const { return yaw_; }
    void Reset() { initialised_ = false; }

private:
    Config config_{};
    bool initialised_ = false;
    float yaw_ = 0.0f;
    double lastTime_ = 0.0;
};

// Default BF4-style layout: primary over the right shoulder, secondary on the right hip,
// grenades on the left hip, knife on the chest, magazines on the left chest/belt.
std::vector<HolsterSlot> DefaultHolsterLayout();

class HolsterSystem final : public IHolsterSystem {
public:
    struct Config {
        float gripPress = 0.70f;
        float gripRelease = 0.40f;
        bool returnToOriginOnRelease = true; // let go away from any holster: item goes home
    };

    explicit HolsterSystem(std::vector<HolsterSlot> slots) : HolsterSystem(std::move(slots), Config{}) {}
    HolsterSystem(std::vector<HolsterSlot> slots, Config config);
    HolsterSystem(const HolsterSystem&) = delete; // slot ids view into owned storage
    HolsterSystem& operator=(const HolsterSystem&) = delete;

    std::span<const HolsterSlot> Slots() const override { return slots_; }
    HolsterResult Update(const BodyFrame& body, const math::Pose& leftHand, bool leftGrip,
                         const math::Pose& rightHand, bool rightGrip) override;
    // Analog grip variant (hysteresis applied here).
    HolsterResult UpdateAnalog(const BodyFrame& body, const math::Pose& leftHand, float leftGrip,
                               const math::Pose& rightHand, float rightGrip);
    void OnLifetimeTransition() override;

    // Persistent state queries.
    int HeldSlot(bool left) const { return hands_[left ? 0 : 1].heldSlot; }
    bool Occupied(int slot) const { return occupied_[static_cast<std::size_t>(slot)]; }
    int FindSlot(std::string_view id) const;
    math::Vec3 SlotWorldPosition(const BodyFrame& body, int slot) const;
    // Adapter sync: the game changed inventory (pickup, death, scripted loadout).
    void SetOccupied(int slot, bool occupied) { occupied_[static_cast<std::size_t>(slot)] = occupied; }

private:
    struct Hand {
        int heldSlot = -1;   // origin slot of the item in this hand, -1 = empty
        bool gripDown = false;
    };
    HolsterResult Step(const BodyFrame& body, int handIndex, const math::Pose& hand, bool grip);
    int Nearest(const BodyFrame& body, const math::Vec3& p, bool wantOccupied, EquipmentCategory category,
                bool anyCategory) const;

    std::vector<HolsterSlot> slots_;
    std::vector<std::string> ids_; // owns the string_view storage
    std::vector<bool> occupied_;
    Hand hands_[2];                    // [0] left, [1] right
    bool analog_[2] = {false, false};  // grip hysteresis state for UpdateAnalog
    std::vector<HolsterResult> queued_;
    Config config_;
};

} // namespace fvr::interaction
