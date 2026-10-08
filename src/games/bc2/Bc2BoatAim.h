#pragma once
#include "Bc2VehicleRoutes.h"
#include "fvr/interaction/VehicleHeadAim.h"
namespace fvr::bc2 {
struct BoatAimBinding {std::uint32_t image=0;bool verified=false;};
struct BoatAimSnapshot {
    VehicleSeatIdentity owner{};std::uint32_t player=0,collection=0,camera=0,cameraComponent=0,weaponComponent=0,nativeWeapon=0;
    unsigned cameraIndex=0,weaponIndex=0,yawIndex=0,pitchIndex=0;
    math::Matrix4 hull{},cameraWorld{},neutralCameraLocal{};
    float jointYaw=0,jointPitch=0;
};
inline interaction::VehicleAimLimits PblDriverAimLimits()noexcept {
    // Native 045259 response: +Roll increases component yaw; +Pitch reduces
    // component pitch. The renderer-facing camera reverses pitch; yaw remains a rotation about the same up axis.
    interaction::VehicleAimLimits limits;limits.yawSign=1;limits.pitchSign=1;
    limits.pitchMin=-.2617994f;limits.pitchMax=.6108652f;return limits;
}
// Read-only source-body verification, before enabling the explicit feature.
BoatAimBinding VerifyBoatAimCode(const VehicleRouteMemory&,std::uint32_t image)noexcept;
// Exact local PBLB driver only; caller first admits ReadPblDriverProfile. No
// native functions, state writes, new hooks, or inferred passenger authority.
std::optional<BoatAimSnapshot> ReadPblDriverAim(const VehicleRouteMemory&,const BoatAimBinding&,const VehicleRouteSnapshot&)noexcept;
}
