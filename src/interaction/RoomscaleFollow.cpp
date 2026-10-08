#include "fvr/interaction/RoomscaleFollow.h"
#include <algorithm>
#include <cmath>
namespace fvr::interaction {
RoomscaleOutput RoomscaleFollow::Update(const RoomscaleStep& s) noexcept {
    RoomscaleOutput out{};out.consumedLocalMeters=consumed_;
    const auto finite=[](const math::Vec3& p){return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z);};
    if(!s.owner||!s.space||!s.nowMs||!finite(s.headLocalMeters)||!finite(s.bodyWorldMeters)||!std::isfinite(s.bodyYaw)||
       std::hypot(s.headLocalMeters.x,s.headLocalMeters.z)>3){driving_=false;return out;}
    if(s.owner!=owner_||s.space!=space_){owner_=s.owner;space_=s.space;consumed_={};lastMs_=0;driving_=false;}
    // Snap turns/recenter/teleports must not turn native movement into spurious
    // physical tracking. Native updates may share a coarse millisecond stamp;
    // displacement from the last observed body position prevents double consumption.
    const auto dt=s.nowMs>lastMs_?s.nowMs-lastMs_:0;
    if(driving_&&lastMs_&&s.nowMs>=lastMs_&&dt<=150){
        const float dx=s.bodyWorldMeters.x-lastBody_.x,dz=s.bodyWorldMeters.z-lastBody_.z;
        if(std::hypot(dx,dz)<=.25f){const float c=std::cos(lastYaw_),sn=std::sin(lastYaw_);
            consumed_.x+=dx*c-dz*sn;consumed_.z-=dx*sn+dz*c;
        }
    }
    lastMs_=s.nowMs;lastBody_=s.bodyWorldMeters;lastYaw_=s.bodyYaw;driving_=false;
    out.consumedLocalMeters=consumed_;out.valid=true;
    if(s.manualMovement)return out; // Do not cancel the player's joystick motion.
    const float x=s.headLocalMeters.x-consumed_.x,z=s.headLocalMeters.z-consumed_.z,distance=std::hypot(x,z);
    constexpr float leanRadius=.12f;
    if(distance<=leanRadius||distance>3)return out;
    const float speed=std::min(.8f,2.4f*(distance-leanRadius));
    out.strafeMetersPerSecond=speed*x/distance;out.forwardMetersPerSecond=-speed*z/distance;
    out.driving=driving_=true;return out;
}
}
