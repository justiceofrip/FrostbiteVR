#pragma once
#include "ControllerInput.h"
namespace fvr::interaction {
struct SupportGripOwner {std::uint64_t actor=0,generation=0,equipped=0;bool operator==(const SupportGripOwner&)const=default;};
struct SupportGripContact {
 bool valid=false;float distanceMeters=0;
 // Calibrated minus raw wrist separation, in upright reference-space metres.
 math::Vec3 wristOffsetMeters{};
};
enum class SupportRelease : std::uint8_t {None,Tracking,Contact,Action,Identity,Button,Distance,Separation,Crossed};
struct SupportGripResult {InputFrame input{};bool holding=false,engaged=false,released=false;float correctionRadians=0;std::uint64_t token=0;SupportRelease reason=SupportRelease::None;};
// Only the gun-hand orientation changes. Positions and off-hand pose stay free.
class SupportGrip {
public:
 // A transient hand owner prevents acquisition but may observe a fresh release.
 // Hard cancellation/tracking or identity loss still requires neutral rearming.
 SupportGripResult Update(const SupportGripOwner&,const InputFrame&,const SupportGripContact&,bool cancel=false,bool handBusy=false) noexcept;
 void Reset()noexcept {const auto next=nextToken_;*this={};nextToken_=next;}
private:
 SupportGripOwner owner_{};std::uint64_t space_=0,generation_=0;std::int64_t time_=0;
 std::uint64_t nextToken_=0,token_=0;
 float units_=0;bool armed_=false,holding_=false;math::Vec3 localDirection_{};
};
}
