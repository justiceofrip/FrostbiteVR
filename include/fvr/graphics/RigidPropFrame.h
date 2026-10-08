#pragma once
#include "fvr/interaction/TrackingMath.h"
#include <cmath>
#include <cstdint>
#include <optional>
namespace fvr::graphics {
struct RigidPropGeometryKey {
    std::uint64_t asset=0,part=0,revision=0;
    bool operator==(const RigidPropGeometryKey&)const=default;
};
// Copied by the native adapter only at the current owned eye draw boundary.
// None of these numbers independently proves an engine pointer is still alive.
struct RigidPropEye {
    std::uint64_t world=0,request=0,view=0,frame=0,actor=0,actorGeneration=0,
        weapon=0,equipmentGeneration=0,space=0,input=0;
    unsigned eye=2;
    std::int64_t observedNs=0,deadlineNs=0;
    bool operator==(const RigidPropEye&)const=default;
};
inline bool ValidRigidPropKey(const RigidPropGeometryKey& k)noexcept{return k.asset&&k.part&&k.revision;}
inline bool RigidPropEyeFresh(const RigidPropEye& f,std::int64_t now)noexcept {
    return f.world&&f.request&&f.view&&f.frame&&f.actor&&f.actorGeneration&&f.weapon&&f.equipmentGeneration&&
        f.space&&f.input&&f.eye<2&&f.observedNs>0&&f.observedNs<=now&&f.deadlineNs>now&&
        f.deadlineNs-f.observedNs<=100000000;
}
// One independent instance per eye/frame. It must be freshly authorized by the
// adapter for EACH draw; no cached old-owner pose or copied deadline is renewed.
class RigidPropFrameGate {
public:
    explicit RigidPropFrameGate(bool enabled=false)noexcept:enabled_(enabled){}
    bool Admit(const RigidPropEye& draw,const RigidPropEye& current,std::int64_t now)noexcept {
        if(!enabled_||draw!=current||!RigidPropEyeFresh(draw,now))return false;
        const auto& previous=last_[draw.eye];
        if(previous){
            if(draw.observedNs<previous->observedNs)return false;
            // Same frame cannot be redrawn even if a producer changes its input
            // or renews the timestamps. New scenes may restart frame numbering.
            if(draw.world==previous->world&&draw.request==previous->request&&draw.frame<=previous->frame)return false;
        }
        last_[draw.eye]=draw;return true;
    }
private:
    bool enabled_=false;std::array<std::optional<RigidPropEye>,2> last_{};
};
inline bool RigidPropWorldValid(const math::Matrix4& m)noexcept {
    double length=0;for(unsigned c=0;c<3;++c)length+=double(m.values[0][c])*m.values[0][c];
    const double scale=std::sqrt(length);if(!std::isfinite(scale)||scale<.0099||scale>1000.01)return false;
    auto rigid=m;for(unsigned r=0;r<3;++r)for(unsigned c=0;c<3;++c)rigid.values[r][c]=float(rigid.values[r][c]/scale);
    return bool(interaction::InverseAnimatedTransform(rigid));
}
inline std::optional<math::Matrix4> RigidPropClipTransform(const math::Matrix4& partWorld,
    const math::Matrix4& currentEyeView,const math::Matrix4& currentEyeProjection)noexcept {
    if(!RigidPropWorldValid(partWorld)||!interaction::InverseRigid(currentEyeView))return {};
    for(const auto& row:currentEyeProjection.values)for(auto x:row)if(!std::isfinite(x))return {};
    if(currentEyeProjection.values[0][0]<=0||currentEyeProjection.values[1][1]<=0||
        std::abs(currentEyeProjection.values[2][3])<.5f)return {};
    const auto result=interaction::Multiply(interaction::Multiply(partWorld,currentEyeView),currentEyeProjection);
    for(const auto& row:result.values)for(auto x:row)if(!std::isfinite(x))return {};
    return result;
}
} // namespace fvr::graphics
