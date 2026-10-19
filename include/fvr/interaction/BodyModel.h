#pragma once
// Roadmap seams for holsters (M4), manual reload (M5) and IK (M6). These are interfaces
// and plain data only: nothing here touches the game. They exist now so the BF4 adapter,
// the input layer and the menu are wired against stable shapes and the roadmap features
// land as new implementations instead of refactors.
#include "fvr/math/Vec.h"

#include <cstdint>
#include <span>
#include <string_view>

namespace fvr::interaction {

// ---- Body frame: estimated from head + hands, shared by holsters and IK -------------
enum class BodyAnchor : std::uint8_t {
    Head, Chest, Pelvis,
    HipLeft, HipRight, ThighLeft, ThighRight,
    ShoulderLeft, ShoulderRight, BackLeft, BackRight,
    Count
};

struct BodyFrame {
    math::Pose anchors[static_cast<std::size_t>(BodyAnchor::Count)]{};
    float playerHeightMetres = 1.75f;
    bool valid = false;
};

class IBodyFrameEstimator {
public:
    virtual ~IBodyFrameEstimator() = default;
    virtual BodyFrame Update(const math::Pose& head, const math::Pose& leftHand,
                             const math::Pose& rightHand, double timeSeconds) = 0;
};

// ---- Holsters (M4) ------------------------------------------------------------------
enum class EquipmentCategory : std::uint8_t { Primary, Secondary, Gadget, Grenade, Knife, Magazine };

struct HolsterSlot {
    std::string_view id;            // stable id used in settings ("hip_right", "back_left")
    BodyAnchor anchor;
    math::Vec3 offset;              // metres, in the anchor's frame
    float radius = 0.12f;           // grab sphere
    EquipmentCategory accepts;
    int nativeSlotIndex = -1;       // adapter-provided inventory slot; -1 = not mapped yet
};

enum class HolsterEvent : std::uint8_t { None, Draw, Stow };

struct HolsterResult {
    HolsterEvent event = HolsterEvent::None;
    std::string_view slotId;
    bool leftHand = false;
};

class IHolsterSystem {
public:
    virtual ~IHolsterSystem() = default;
    virtual std::span<const HolsterSlot> Slots() const = 0;
    // Persistent ownership: a stowed/drawn state survives frames until an explicit
    // lifetime transition (AGENTS.md: never reset on a repeated controller packet).
    virtual HolsterResult Update(const BodyFrame& body, const math::Pose& leftHand, bool leftGrip,
                                 const math::Pose& rightHand, bool rightGrip) = 0;
    virtual void OnLifetimeTransition() = 0; // death, level load
};

// ---- Arm / body IK (M6) ---------------------------------------------------------------
struct ArmChainTarget {
    math::Pose shoulder;
    math::Pose handTarget;
    math::Vec3 elbowHint; // pole vector in world space
};

struct ArmChainResult {
    math::Pose upperArm;
    math::Pose forearm;
    math::Pose hand;
    bool reached = false;
};

class IArmIkSolver {
public:
    virtual ~IArmIkSolver() = default;
    virtual ArmChainResult Solve(const ArmChainTarget& target, float upperLength, float foreLength) = 0;
};

// The adapter owns the mapping from solver output to BF4's first-person rig bones. It is
// a separate interface because rig indices are game-specific and must be verified per
// build; FvrCore's existing ArmIk math is reused as the IArmIkSolver implementation.
class IRigWriter {
public:
    virtual ~IRigWriter() = default;
    virtual bool Available() const = 0;
    virtual void WriteArms(const ArmChainResult& left, const ArmChainResult& right) = 0;
    virtual void RestoreAll() = 0; // exact-write restoration on every outcome
};

} // namespace fvr::interaction
