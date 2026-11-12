#pragma once
#include "fvr/graphics/BodyPropFrame.h"

namespace fvr::graphics {
// Synthetic mod presentation, never an installed mesh or a native case asset.
inline constexpr RigidPropGeometryKey SpentCaseGeometry{0x4656524341534531ull,0x484f4c4c4f574341ull,1};
inline constexpr std::int64_t SpentCaseFallDurationNs=600000000;
// The presentation uses +Z as its authored outward direction in the measured
// supply-root frame. This is a mod choice, not proof of a native extraction axis.
inline std::optional<math::Matrix4> SpentCaseFallWorld(const math::Matrix4& releasedWorldMeters,
    std::int64_t releasedNs,std::int64_t observedNs)noexcept {
    if(!interaction::InverseRigid(releasedWorldMeters)||releasedNs<=0||observedNs<releasedNs||
       observedNs-releasedNs>=SpentCaseFallDurationNs)return {};
    const double seconds=double(observedNs-releasedNs)*1e-9;
    auto world=releasedWorldMeters;
    for(unsigned c=0;c<3;++c)world.values[3][c]+=float(.25*seconds)*releasedWorldMeters.values[2][c];
    world.values[3][1]-=float(4.905*seconds*seconds);
    return interaction::InverseRigid(world)?std::optional(world):std::nullopt;
}
}
