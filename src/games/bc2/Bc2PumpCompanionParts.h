#pragma once
#include "Bc2PumpPart.h"

namespace fvr::bc2 {
// A measured mechanical companion path. Both legs use uniform physical rail
// progress [0,1]; array0 is closed and the last pose is the shared rear stop.
// These immutable adapter data never schedule a clip or grant native action.
struct Bc2PumpCompanionPart {
    std::string_view name,parent;
    math::Matrix4 closed{};
    std::span<const math::Matrix4> rear,forward;
};
inline bool PumpCompanionsValid(const Bc2PumpGeometry& geometry,
    std::span<const Bc2PumpCompanionPart> parts,std::span<const Bc2PumpFixedPart> fixed={})noexcept {
    if(parts.size()>8)return false;
    for(const auto& p:parts){
        if(p.name.empty()||p.name.size()>=128||p.parent!=geometry.root||p.name==geometry.root||p.name==geometry.part||
           p.rear.size()<2||p.rear.size()>65||p.forward.size()!=p.rear.size()||
           !interaction::reload_insertion_detail::Rigid(p.closed)||
           p.rear.front().values!=p.closed.values||p.forward.front().values!=p.closed.values||
           p.rear.back().values!=p.forward.back().values||
           std::count_if(parts.begin(),parts.end(),[&](const auto& other){return other.name==p.name;})!=1||
           std::any_of(fixed.begin(),fixed.end(),[&](const auto& other){return other.name==p.name;}))return false;
        for(const auto path:{p.rear,p.forward})for(const auto& pose:path)
            if(!interaction::reload_insertion_detail::Rigid(pose)||
               std::hypot(pose.values[3][0],pose.values[3][1],pose.values[3][2])>2.f)return false;
    }
    return true;
}
inline std::optional<std::vector<interaction::BoneWrite>> BuildPumpCompanions(
    const RigSnapshot& rig,const math::Matrix4& placed,float units,const Bc2PumpGeometry& geometry,
    std::span<const Bc2PumpCompanionPart> parts,float travel,bool returning,float rearTolerance=0) {
    if(!PumpGeometryValid(geometry)||!PumpCompanionsValid(geometry,parts)||!std::isfinite(travel)||travel<0||travel>geometry.stroke||
       !std::isfinite(rearTolerance)||rearTolerance<0||rearTolerance>=geometry.stroke*.5f)return {};
    std::array<Bc2PumpFixedPart,8> sample{};unsigned count=0;
    for(const auto& p:parts){
        const auto at=std::find(rig.names.begin(),rig.names.end(),p.name);
        if(at==rig.names.end())return {};
        const auto index=std::int32_t(at-rig.names.begin());
        // Only separately skinned direct leaves are enrolled; no descendant
        // motion or shell/trigger ownership is inferred from a moving parent.
        if(std::find(rig.parents.begin(),rig.parents.end(),index)!=rig.parents.end())return {};
        const double progress=double(travel)/geometry.stroke*(p.rear.size()-1);
        const auto low=std::min<std::size_t>(std::size_t(progress),p.rear.size()-2);
        auto pose=interaction::reload_insertion_detail::BlendRigid(p.rear[low],p.rear[low+1],progress-low);
        if(returning){
            const auto forward=interaction::reload_insertion_detail::BlendRigid(p.forward[low],p.forward[low+1],progress-low);
            // The physical recognizer turns inside its rear tolerance. Join
            // the two authored legs spatially at that SAME threshold, so a
            // valid short-of-stop reversal cannot jump to another clip pose.
            const double blend=rearTolerance>0?std::clamp(double(geometry.stroke-rearTolerance-travel)/rearTolerance,0.,1.):1.;
            pose=interaction::reload_insertion_detail::BlendRigid(pose,forward,blend);
        }
        sample[count++]={p.name,p.parent,pose};
    }
    // Reuse exact named-parent/hidden/current-rig/unit checks for this frame's
    // rigid writes. The enrolled companion is moving data, not a fixed part.
    return bc2_pump_detail::FixedAssembly(rig,placed,units,geometry,std::span(sample.data(),count));
}
}
