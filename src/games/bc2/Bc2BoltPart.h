#pragma once
#include "Bc2PhysicalBolt.h"
namespace fvr::bc2 {
struct Bc2BoltPartBinding {std::uint64_t fingerprint=0;std::uint32_t weapon=0,part=0;};
// Topology only. Exact asset/mesh/rig admission belongs to the calibration join.
std::optional<Bc2BoltPartBinding> DeriveBoltPart(const RigSnapshot&,std::string_view partName);
Bc2BoltRawContact BuildBoltRawContact(const Bc2BoltTracking&,const RigSnapshot&,
    const math::Matrix4& rawMechanismWrist,const math::Matrix4& placedWeapon,float units,std::int64_t now)noexcept;
struct Bc2BoltPresentation {
    interaction::BoneWrite part{};
    math::Matrix4 mechanismWrist{};
};
// This proposes one independent leaf write; it never modifies the native rig.
// Guided hand/finger targets remain separate from original gesture evidence.
std::optional<Bc2BoltPresentation> BuildBoltPresentation(const Bc2BoltTracking&,const RigSnapshot&,
    const math::Matrix4& placedWeapon,float units,std::int64_t now)noexcept;
std::optional<RigPosePlan> BuildBoltPartPlan(const Bc2BoltTracking&,const RigSnapshot&,
    const math::Matrix4& placedWeapon,float units,std::int64_t now);
}
