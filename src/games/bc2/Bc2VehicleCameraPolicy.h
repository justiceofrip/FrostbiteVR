#pragma once
#include "fvr/interaction/VehicleTracking.h"
namespace fvr::bc2 {
// Published with the exact seat sample under the adapter's vehicle-view lock.
// A globally enabled PBL feature does not make another vehicle a PBL seat.
enum class VehicleCameraMode {Unavailable,NativeSeat,PblHeadAim};
enum class VehicleCameraPath {Unsupported,Wait,NativeSeat,PblHeadAim};
struct VehicleCameraContext {
    std::uint64_t actor=0,vehicle=0,seat=0,space=0;
    std::int64_t nowNs=0;
    bool focused=false,headValid=false;
};
struct PblCameraAimProof {
    interaction::VehicleTrackingOwner owner{};
    std::uint64_t space=0;
    bool ready=false;
};
inline bool PublishVehicleCameraMode(interaction::VehicleCameraSample& previous,VehicleCameraMode& previousMode,
    interaction::VehicleCameraSample next,VehicleCameraMode mode)noexcept {
    if(!next.supported)mode=VehicleCameraMode::Unavailable;
    const bool reset=interaction::PublishVehicleCameraSample(previous,next);
    const bool modeChanged=previousMode!=mode;previousMode=mode;
    return reset||modeChanged;
}
inline VehicleCameraPath SelectVehicleCameraPath(const interaction::VehicleCameraSample& sample,
    VehicleCameraMode mode,const VehicleCameraContext& context,const PblCameraAimProof& aim)noexcept {
    const auto admission=interaction::AdmitVehicleCamera(sample,context.actor,context.vehicle,context.seat,context.space,context.nowNs);
    if(admission==interaction::VehicleCameraAdmission::Unsupported)return VehicleCameraPath::Unsupported;
    if(admission!=interaction::VehicleCameraAdmission::Ready||!context.focused||!context.headValid)return VehicleCameraPath::Wait;
    if(mode==VehicleCameraMode::NativeSeat)return VehicleCameraPath::NativeSeat;
    if(mode==VehicleCameraMode::PblHeadAim&&aim.ready&&aim.owner==sample.owner&&aim.space==sample.space)
        return VehicleCameraPath::PblHeadAim;
    return VehicleCameraPath::Wait;
}
}
