#pragma once
#include "fvr/interaction/ControllerInput.h"
#include "fvr/interaction/TrackingMath.h"
#include <cmath>
namespace fvr::interaction {
struct VehicleTrackingOwner {
    std::uint64_t actor=0,generation=0,vehicle=0,seat=0;
    bool operator==(const VehicleTrackingOwner&)const=default;
};
struct VehicleCameraSample {
    VehicleTrackingOwner owner{};std::uint64_t space=0;
    bool supported=false,live=false;
    // Original input clock, not the camera preparation time. Zero/expired
    // source can suspend a known seat but must never admit an unanchored view.
    std::int64_t observedNs=0,deadlineNs=0;
};
enum class VehicleCameraAdmission:std::uint8_t {Unsupported,Wait,Ready};
inline bool PublishVehicleCameraSample(VehicleCameraSample& previous,VehicleCameraSample next)noexcept {
    // An absent XR packet does not identify a new reference space. Preserve
    // only the same seat's anchor identity; no source lifetime is renewed.
    if(next.supported&&!next.space&&next.owner==previous.owner)next.space=previous.space;
    const bool reset=!next.supported||!previous.supported||next.owner!=previous.owner||next.space!=previous.space;
    previous=next;return reset;
}
inline VehicleCameraAdmission AdmitVehicleCamera(const VehicleCameraSample& s,
    std::uint64_t actor,std::uint64_t vehicle,std::uint64_t seat,std::uint64_t space,std::int64_t now)noexcept {
    if(!s.supported)return VehicleCameraAdmission::Unsupported;
    if(!s.live||!actor||!vehicle||!seat||!s.owner.generation||!space||s.owner.actor!=actor||
        s.owner.vehicle!=vehicle||s.owner.seat!=seat||s.space!=space||s.observedNs<=0||now<s.observedNs||
        s.deadlineNs<=now||s.deadlineNs-s.observedNs>150000000)return VehicleCameraAdmission::Wait;
    return VehicleCameraAdmission::Ready;
}
// Per-seat local reference. Native seat/turret camera remains authoritative;
// accumulated physical displacement from on-foot travel is not reapplied.
// Caller serializes this policy in its single camera-preparation lane.
class VehicleCameraAnchor {
public:
    std::optional<math::Matrix4> Base(const VehicleTrackingOwner& owner,
        const math::Matrix4& nativeCamera,const math::Pose& reference,const math::Pose& head,
        std::uint64_t space,float units)noexcept {
        if(!owner.actor||!owner.generation||!owner.vehicle||!owner.seat||!space||!InverseRigid(nativeCamera))return {};
        if(!ready_||owner!=owner_||space!=space_||units!=units_){
            const auto upright=UprightReference(head);if(!upright)return {};
            math::Matrix4 identity{};for(unsigned n=0;n<4;++n)identity.values[n][n]=1;
            const auto local=math::ComposeRuntimeHeadWithLhCamera(identity,reference,*upright,units);
            const auto inverse=local?InverseRigid(*local):std::nullopt;if(!inverse)return {};
            inverse_=*inverse;owner_=owner;space_=space;units_=units;ready_=true;
        }
        // The ordinary tracked-view builder still composes the original eye
        // poses. This one inverse basis is shared by both eyes and culling.
        return Multiply(inverse_,nativeCamera);
    }
    void Reset()noexcept{ready_=false;owner_={};space_=0;}
    bool Ready()const noexcept{return ready_;}
private:
    VehicleTrackingOwner owner_{};std::uint64_t space_=0;float units_=0;
    math::Matrix4 inverse_{};bool ready_=false;
};
struct VehicleControlOwner {
    VehicleTrackingOwner seat{};std::uint64_t space=0;
    bool operator==(const VehicleControlOwner&)const=default;
};
struct VehicleControlAxes {float throttle=0,steer=0,lookYaw=0,lookPitch=0,fire=0;bool exit=false;};
struct VehicleControlOutput {VehicleControlAxes axes{};bool active=false,armedLeft=false,armedRight=false;};
// Separate from infantry ControllerActions: no head-relative travel, snap turn,
// weapon flick, ADS or body-follow commands. Native seat profile chooses routes.
class VehicleControllerActions {
public:
    VehicleControlOutput Update(const InputFrame& input,const VehicleControlOwner& owner,
        bool playing,bool originalDeadlineFresh)noexcept {
        VehicleControlOutput out;
        const bool valid=originalDeadlineFresh&&playing&&owner.seat.actor&&owner.seat.generation&&owner.seat.vehicle&&owner.seat.seat&&
            owner.space==input.spaceGeneration&&ValidInput(input)&&input.focused&&input.headValid;
        if(!valid){armed_={};last_={};generation_=0;return out;}
        if(owner!=owner_||input.generation<generation_){owner_=owner;armed_={};last_={};generation_=0;}
        // Tracking loss cancels immediately even if a duplicated transport
        // sample carries an independently updated validity bit.
        if(!input.hands[0].gripTracked){armed_[0]=false;last_.axes.throttle=last_.axes.steer=0;last_.axes.exit=false;last_.armedLeft=false;}
        if(!input.hands[1].gripTracked){armed_[1]=false;last_.axes.lookYaw=last_.axes.lookPitch=last_.axes.fire=0;last_.armedRight=false;}
        // Runtime action-active loss is also an immediate cancellation, even
        // when transport repeats the last sequence with different validity.
        if(!(input.hands[0].active&Stick))last_.axes.throttle=last_.axes.steer=0;
        if(!(input.hands[0].active&Primary)||!(input.hands[0].held&Primary))last_.axes.exit=false;
        if(!(input.hands[1].active&Stick))last_.axes.lookYaw=last_.axes.lookPitch=0;
        if(!(input.hands[1].active&Trigger))last_.axes.fire=0;
        last_.active=(last_.armedLeft&&input.hands[0].gripTracked)||(last_.armedRight&&input.hands[1].gripTracked);
        if(input.generation==generation_)return last_;generation_=input.generation;
        const auto axis=[](float value){constexpr float dead=.18f;return std::abs(value)<=dead?0.f:std::copysign((std::abs(value)-dead)/(1.f-dead),value);};
        for(unsigned side=0;side<2;++side){const auto& h=input.hands[side];
            if(!h.gripTracked){armed_[side]=false;continue;}
            const bool neutral=std::abs(h.stickX)<=.18f&&std::abs(h.stickY)<=.18f&&h.trigger<=.1f&&!(h.held&Primary);
            if(!armed_[side]){if(neutral)armed_[side]=true;continue;}
            out.active=true;
            if(side==0){if(h.active&Stick){out.axes.throttle=axis(h.stickY);out.axes.steer=axis(h.stickX);}out.axes.exit=(h.active&Primary)&&(h.held&Primary);}
            else{if(h.active&Stick){out.axes.lookYaw=axis(h.stickX);out.axes.lookPitch=axis(h.stickY);}if(h.active&Trigger)out.axes.fire=h.trigger;}
        }
        out.armedLeft=armed_[0];out.armedRight=armed_[1];last_=out;return out;
    }
    void Reset()noexcept{owner_={};armed_={};last_={};generation_=0;}
private:
    VehicleControlOwner owner_{};std::array<bool,2> armed_{};VehicleControlOutput last_{};std::uint64_t generation_=0;
};
}
