#pragma once
#include "Bc2VehicleRoutes.h"
#include "Bc2EquipmentIdentity.h"
#include "Bc2ActiveEntryCamera.h"
namespace fvr::bc2 {
// Read-only attachment/equipment join. This is not a driver profile and grants
// no input, camera override, locomotion, roomscale, or weapon callback writes.
struct AttachedSoldierBinding {unsigned image=0;bool verified=false;};
struct AttachedSoldierWord {unsigned address=0,value=0,mask=~0u;};
struct AttachedSoldierSnapshot {
    VehicleRouteSnapshot seat{};
    unsigned soldier=0,entryData=0,constraints=0,inventory=0,selected=0,weapon=0,aim=0,aimer=0;
    WeaponEquipmentIdentity equipment{};
    std::array<float,4> authoredDegrees{},nativeLimits{};
    ActiveEntryCamera camera{};
    unsigned collection=0,cameraIndex=0,entryIndex=0;
    std::array<AttachedSoldierWord,1024> words{};unsigned wordCount=0;
};
AttachedSoldierBinding VerifyAttachedSoldierCode(const VehicleRouteMemory&,unsigned image)noexcept;
std::optional<AttachedSoldierSnapshot> ReadAttachedSoldierOwnership(const VehicleRouteMemory&,const AttachedSoldierBinding&,const VehicleRouteSnapshot&)noexcept;
bool RevalidateAttachedSoldierOwnership(const VehicleRouteMemory&,const AttachedSoldierSnapshot&,const VehicleRouteSnapshot&)noexcept;
}
