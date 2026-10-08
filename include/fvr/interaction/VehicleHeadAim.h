#pragma once
#include "fvr/interaction/VehicleTracking.h"
#include <algorithm>
namespace fvr::interaction {
// Engine-neutral policy. Adapter supplies a verified seat and a fresh native
// hull/camera pair. Nothing here reads native memory or changes a game object.
struct VehicleAimObservation {
    VehicleTrackingOwner owner{};std::uint64_t space=0;
    math::Matrix4 hull{},camera{},neutralCameraLocal{};
    std::int64_t observedNs=0,deadlineNs=0;bool verified=false;
};
struct VehicleAimLimits {
    float yawMin=-1.5707963f,yawMax=1.5707963f,pitchMin=-.6108652f,pitchMax=.2617994f;
    // Adapter owns the native action sign. These are bounded error-controller
    // tuning values, not a claim about native degrees per input unit.
    float yawSign=1,pitchSign=1,gain=.12f,maxInput=.08f,deadband=.006f;
};
struct VehicleAimOutput {
    bool ready=false;float yaw=0,pitch=0,yawError=0,pitchError=0;
    math::Matrix4 stableCamera{},desiredCamera{};
};
inline std::optional<math::Matrix4> VehicleAimViewBase(const math::Matrix4& stable,
    const math::Pose& seatReference,const math::Pose& hostReference,float units)noexcept {
    if(!InverseRigid(stable))return {};
    math::Matrix4 identity{};for(unsigned n=0;n<4;++n)identity.values[n][n]=1;
    const auto relative=math::ComposeRuntimeHeadWithLhCamera(identity,hostReference,seatReference,units);
    const auto inverse=relative?InverseRigid(*relative):std::nullopt;
    return inverse?std::optional<math::Matrix4>(Multiply(*inverse,stable)):std::nullopt;
}
class VehicleHeadAim {
public:
    VehicleAimOutput Update(const VehicleAimObservation& sample,const InputFrame& input,
        std::int64_t now,const VehicleAimLimits& limits={})noexcept {
        VehicleAimOutput out;
        const auto finite=[](float x){return std::isfinite(x);};
        const bool valid=sample.verified&&sample.owner.actor&&sample.owner.generation&&sample.owner.vehicle&&sample.owner.seat&&
            sample.space&&sample.space==input.spaceGeneration&&sample.observedNs>0&&now>=sample.observedNs&&now<sample.deadlineNs&&
            sample.deadlineNs-sample.observedNs<=150000000&&input.focused&&input.headValid&&ValidInput(input);
        if(!valid){return out;} // Keep same-seat origin; never renew its input lease.
        if(!finite(limits.yawMin)||!finite(limits.yawMax)||!finite(limits.pitchMin)||!finite(limits.pitchMax)||
            limits.yawMin>=limits.yawMax||limits.pitchMin>=limits.pitchMax||limits.yawMin< -3.141593f||limits.yawMax>3.141593f||
            limits.pitchMin< -1.55f||limits.pitchMax>1.55f||!finite(limits.gain)||limits.gain<=0||limits.gain>1||
            !finite(limits.maxInput)||limits.maxInput<=0||limits.maxInput>.25f||!finite(limits.deadband)||limits.deadband<0||limits.deadband>.1f||
            std::abs(limits.yawSign)!=1||std::abs(limits.pitchSign)!=1)return out;
        const auto hullInverse=InverseRigid(sample.hull);if(!hullInverse||!InverseRigid(sample.camera)||!InverseRigid(sample.neutralCameraLocal))return out;
        if(!ready_||owner_!=sample.owner||space_!=sample.space||input.generation<generation_){
            const auto head=UprightReference(input.head);if(!head)return out;
            reference_=*head;local_=sample.neutralCameraLocal;
            for(unsigned n=0;n<3;++n)local_.values[3][n]=0;
            owner_=sample.owner;space_=sample.space;ready_=true;
        }
        generation_=input.generation;
        // Native camera origin follows the seat, but turret yaw/pitch must not
        // rotate the HMD camera again. Hull roll/pitch/yaw still follows normally.
        local_=sample.neutralCameraLocal;
        auto stable=Multiply(local_,sample.hull);
        stable.values[3]=sample.camera.values[3];
        const auto desired=math::ComposeRuntimeHeadWithLhCamera(stable,reference_,input.head,input.worldUnitsPerMeter);
        const auto stableInverse=InverseRigid(stable);if(!desired||!stableInverse)return out;
        const auto target=Multiply(*desired,*stableInverse),current=Multiply(sample.camera,*stableInverse);
        const auto yaw=[](const math::Matrix4& m){return std::atan2(m.values[2][0],m.values[2][2]);};
        const auto pitch=[](const math::Matrix4& m){return std::atan2(m.values[2][1],std::hypot(m.values[2][0],m.values[2][2]));};
        const auto wrap=[](float a){return std::remainder(a,6.283185307f);};
        out.yawError=wrap(std::clamp(yaw(target),limits.yawMin,limits.yawMax)-yaw(current));
        out.pitchError=std::clamp(pitch(target),limits.pitchMin,limits.pitchMax)-pitch(current);
        const auto command=[&](float error,float sign){return std::abs(error)<=limits.deadband?0.f:sign*std::clamp(error*limits.gain,-limits.maxInput,limits.maxInput);};
        out.yaw=command(out.yawError,limits.yawSign);out.pitch=command(out.pitchError,limits.pitchSign);
        out.ready=true;out.stableCamera=stable;out.desiredCamera=*desired;return out;
    }
    void Reset()noexcept{ready_=false;owner_={};space_=generation_=0;}
    const math::Pose& Reference()const noexcept{return reference_;}
private:
    VehicleTrackingOwner owner_{};std::uint64_t space_=0,generation_=0;bool ready_=false;
    math::Pose reference_{};math::Matrix4 local_{};
};
}
