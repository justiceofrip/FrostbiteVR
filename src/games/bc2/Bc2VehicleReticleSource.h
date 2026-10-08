#pragma once
#include "Bc2BoatAim.h"
#include "fvr/interaction/TrackingMath.h"
namespace fvr::bc2 {
// Full inspected code/data bytes must match the actual read-only memory image.
// This is code provenance, not a mounted-instance/shot-source acceptance grant.
struct VehicleReticleSourceBinding {unsigned image=0;bool verified=false;};
struct VehicleGunRayObservation {
    math::Vec3 origin{},direction{};
    unsigned effects=0,firingData=0,primaryFire=0;
};
inline constexpr bool VehicleReticleNativeShotSourceAdmitted=false;
VehicleReticleSourceBinding VerifyVehicleReticleSourceCode(const VehicleRouteMemory&,unsigned image)noexcept;
// Disk proof locates the reader; the caller supplies an already verified local
// PBLB-driver BoatAim snapshot. This observer never issues NativeSightLine.
// Authored direction is restricted to canonical forward/zero; camera-relative,
// inherited-speed and internal mounted-transform branch remain unsupported.
std::optional<VehicleGunRayObservation> ReadPblDriverGunRayObservation(const VehicleRouteMemory&,
    const VehicleReticleSourceBinding&,const BoatAimSnapshot&)noexcept;
}
