#pragma once
#include "Bc2VehicleInput.h"
#include <array>
#include <cstdint>
namespace fvr::bc2 {
struct VehicleRouteMemory {
    void* context=nullptr;
    bool (*read)(void*,std::uint32_t,void*,std::size_t)=nullptr;
    bool (*type)(void*,std::uint32_t,const char*)=nullptr;
};
struct VehicleRouteOwner {
    std::uint32_t manager=0,player=0,soldier=0,weak=0;
    std::uint64_t actorGeneration=0,seatGeneration=0;
};
struct VehicleRouteBinding {
    std::uint32_t managerVtable=0,routerVtable=0,cacheVtable=0;
    bool gatherCodeVerified=false; // Root's exact live DiscoverInputBinding proof.
};
struct VehicleRoute {std::uint32_t action=0,conceptId=0;bool operator==(const VehicleRoute&)const=default;};
struct VehicleRouteSnapshot {
    VehicleSeatIdentity identity{};std::uint32_t player=0,weak=0,slot=0;
    std::array<VehicleRoute,50> routes{};unsigned count=0;
    std::uint64_t fingerprint=0;std::uint32_t permissionMask=0;
};
enum class VehicleRouteStatus : std::uint8_t {Okay,Unverified,NoOwner,NotVehicle,ReadFailed,Malformed,Changed,NotOnFoot};
struct VehicleRouteResult {VehicleRouteStatus status=VehicleRouteStatus::Unverified;VehicleRouteSnapshot snapshot{};};
// Read-only local entry/router snapshot. No native calls, assets or writes.
// Hash syntax does NOT classify the seat or establish its action consumers.
VehicleRouteResult ReadVehicleRoutes(const VehicleRouteMemory&,const VehicleRouteBinding&,const VehicleRouteOwner&)noexcept;
// The same verified EntryComponent/router layout also exists on the current
// soldier. This reader refuses attached seats and preserves the vehicle API.
VehicleRouteResult ReadOnFootRoutes(const VehicleRouteMemory&,const VehicleRouteBinding&,const VehicleRouteOwner&)noexcept;
bool HasOnFootContextUseVehicleAlias(const VehicleRouteSnapshot&)noexcept;
}
