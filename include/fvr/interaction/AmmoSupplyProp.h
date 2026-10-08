#pragma once
#include "fvr/interaction/AmmoSupplyVisual.h"
namespace fvr::interaction {
struct AmmoSupplyPropPose {AmmoSupplyVisualSample source;math::Matrix4 partWorld;};
// A render-only availability handle. Uses the SAME body contact and original
// source expiry as pickup, never spawns/reserves an item or implies mag count.
// partFromContact must come from the measured authored part/grasp calibration.
inline std::optional<AmmoSupplyPropPose> BuildAmmoSupplyPropPose(const AmmoSupplyVisualSample& source,
    const InputFrame& input,const math::Matrix4& eyeBase,const math::Matrix4& partFromContact,std::int64_t now)noexcept {
    if(!AmmoSupplyVisualFresh(source,now)||!InverseRigid(partFromContact))return {};
    const auto anchor=AmmoSupplyAnchorWorld(input,eyeBase,source);if(!anchor)return {};
    auto scaled=partFromContact;
    for(unsigned n=0;n<3;++n)scaled.values[3][n]*=input.worldUnitsPerMeter;
    // Mesh vertices are in metres; scale both local geometry and translation.
    for(unsigned row=0;row<3;++row)for(unsigned col=0;col<3;++col)scaled.values[row][col]*=input.worldUnitsPerMeter;
    const auto world=Multiply(scaled,*anchor);
    for(const auto& row:world.values)for(auto x:row)if(!std::isfinite(x))return {};
    return AmmoSupplyPropPose{source,world};
}
inline bool AmmoSupplyPropRetained(const AmmoSupplyPropPose& old,const AmmoSupplyVisualSample& current,std::int64_t now)noexcept {
    return AmmoSupplyVisualRetained(old.source,current,now);
}
} // namespace fvr::interaction
