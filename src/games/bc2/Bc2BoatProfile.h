#pragma once
#include "Bc2VehicleRoutes.h"
namespace fvr::bc2 {
// Exact PBLB driver profile. Checks reflected layout, asset/entry identity,
// authored mapping and current native router; unsupported seats stay native.
std::optional<VehicleSeatProfile> ReadPblDriverProfile(const VehicleRouteMemory&,const VehicleRouteSnapshot&)noexcept;
}
