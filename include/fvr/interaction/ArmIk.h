#pragma once
#include "RigPose.h"
#include <array>
namespace fvr::interaction {
struct ArmAnchor {
    math::Vec3 shoulder{},poleDirection{}; // Body-relative anatomy, in world space.
};
// Extension is a presentation policy for an already admitted current wrist.
// It grants no tracking, contact, attachment or native gameplay authority.
enum class ArmReachPolicy { ClampToAnatomy, ExtendToAdmittedWrist };
struct ArmTarget {
    math::Matrix4 wrist{}; // canonical world transform, including controller roll
    math::Vec3 poleDirection{}; // elbow intent in the same world basis
    std::optional<math::Vec3> shoulder; // Omit to retain the native shoulder origin.
    bool enabled=true; // False preserves this entire native arm branch.
    ArmReachPolicy reach=ArmReachPolicy::ClampToAnatomy;
};
struct ArmPose {
    std::vector<BoneWrite> writes;
    std::array<bool,2> reachClamped{};
    std::array<float,2> targetError{};
    // Explicit segment translation extension; never a hand or basis scale.
    std::array<float,2> segmentScale{1.f,1.f};
};
// Pure pose generation. Native matrices/topology are inputs; no engine memory,
// writes, timing or bone indices are owned here. The adapter decides which
// evaluated pose and publication phase are safe. Finger and twist descendants
// retain their native transform relative to the segment they follow.
std::optional<ArmPose> SolveTrackedArms(std::span<const std::int32_t> parents,
    std::span<const math::Matrix4> native,ArmJoints left,ArmJoints right,
    const ArmTarget& leftTarget,const ArmTarget& rightTarget);
}
