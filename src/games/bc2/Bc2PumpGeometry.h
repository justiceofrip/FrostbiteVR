#pragma once
#include "Bc2Rig.h"
#include "fvr/interaction/ReloadInsertion.h"
#include <algorithm>
#include <cmath>
namespace fvr::bc2 {
// Measured adapter data, never native cycle admission. Axis is in weapon space;
// rearDirection on the held boundary selects its signed stroke direction.
struct Bc2PumpGeometry {
    std::string_view root,part;
    std::array<float,3> axis{};
    float stroke=0;
};
struct Bc2PumpFixedPart {std::string_view name,parent;math::Matrix4 inWeapon;};
inline bool PumpGeometryValid(const Bc2PumpGeometry& g)noexcept {
    const auto name=[](std::string_view s){return !s.empty()&&s.size()<128&&
        std::all_of(s.begin(),s.end(),[](unsigned char c){return c>=33&&c<=126;});};
    return name(g.root)&&name(g.part)&&g.root!=g.part&&
        std::isfinite(g.stroke)&&g.stroke>=.01f&&g.stroke<=.5f&&
        std::all_of(g.axis.begin(),g.axis.end(),[](float x){return std::isfinite(x);})&&
        std::abs(std::hypot(g.axis[0],g.axis[1],g.axis[2])-1.f)<1e-5f;
}
}
