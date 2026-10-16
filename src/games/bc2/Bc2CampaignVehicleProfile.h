#pragma once
#include "Bc2VehicleRoutes.h"
#include "fvr/interaction/VehicleTracking.h"
namespace fvr::bc2 {
// Read-only candidate. Admission is a current native consumer/entry join, never
// a vehicle name, entry order, mapping fingerprint alone, or requested role.
struct CampaignVehicleBinding {unsigned image=0;bool verified=false;};
struct CampaignVehicleWord {unsigned address=0,value=0;};
struct CampaignVehicleRow {unsigned component=0,entry=0,flags=0,packed=0;};
struct CampaignVehicleSnapshot {
    VehicleSeatProfile profile{};VehicleSeatIdentity owner{};
    unsigned entryClass=0,collection=0,count=0,layout=0,driver=0,camera=0,weapons=0;
    std::array<CampaignVehicleRow,256> rows{};
    std::array<CampaignVehicleWord,256> words{};unsigned wordCount=0;
};
CampaignVehicleBinding VerifyCampaignVehicleCode(const VehicleRouteMemory&,unsigned image)noexcept;
std::optional<CampaignVehicleSnapshot> ReadCampaignVehicleProfile(const VehicleRouteMemory&,const CampaignVehicleBinding&,const VehicleRouteSnapshot&)noexcept;
bool RevalidateCampaignVehicleProfile(const VehicleRouteMemory&,const CampaignVehicleSnapshot&,const VehicleRouteSnapshot&)noexcept;
// Native float permissions remain authoritative per axis in VehicleInputPlan.
// A fixed gunner does not require the unrelated hull throttle/steering bits.
bool CampaignVehiclePlaying(const VehicleSeatProfile&,unsigned permissions)noexcept;
VehicleInputCommand CampaignVehicleCommand(const VehicleSeatIdentity&,const interaction::VehicleControlOutput&)noexcept;
}
