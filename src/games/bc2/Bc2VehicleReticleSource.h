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
struct VehicleReticleSourceDiagnostic {
    // 0 not sampled, 1 binding/owner, 2 source read/change, 3 unsupported
    // transform, 4 shot branch/direction, 5 transform shape, 6 velocity, 7 ready.
    unsigned stage=0,additionalTransform=0,adjustment=0,cameraFlags=0,count=0,cameraOwner=0;
    float inheritSpeed=0;
};
inline constexpr bool VehicleReticleNativeShotSourceAdmitted=false;
VehicleReticleSourceBinding VerifyVehicleReticleSourceCode(const VehicleRouteMemory&,unsigned image)noexcept;
// Disk proof locates the reader; the caller supplies an already verified local
// PBLB-driver BoatAim snapshot. This observer never issues NativeSightLine.
// Authored direction is restricted to canonical forward/zero. The observed
//0/1 inherited-speed coefficients use the coherent native velocity vector.
// Camera-relative targeting and internal mounted transforms remain unsupported;
// the authored camera-spawn flag is inert only with its verified null owner.
std::optional<VehicleGunRayObservation> ReadPblDriverGunRayObservation(const VehicleRouteMemory&,
    const VehicleReticleSourceBinding&,const BoatAimSnapshot&,VehicleReticleSourceDiagnostic* diagnostic=nullptr)noexcept;
}
