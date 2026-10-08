#include "Bc2VehicleInput.h"
#include <bit>
#include <cmath>
#include <cstring>
namespace fvr::bc2 {
namespace {
bool ValidIdentity(const VehicleSeatIdentity& x)noexcept {
    return x.actor&&x.actorGeneration&&x.seatGeneration&&x.controlled&&x.entry&&x.router&&x.cache;
}
std::uint32_t Word(std::span<const std::byte> bytes,std::size_t at)noexcept {
    std::uint32_t value=0;std::memcpy(&value,bytes.data()+at,4);return value;
}
}
VehicleInputPlan BuildVehicleInputPlan(const VehicleSeatProfile& profile,const VehicleSeatSnapshot& seat,
    const VehicleInputCommand& command,std::span<const std::byte> cache)noexcept {
    VehicleInputPlan out;
    const auto reject=[&](VehicleInputPlanStatus status){VehicleInputPlan failed;failed.status=status;return failed;};
    if(profile.role==VehicleSeatRole::Unclassified||profile.role>VehicleSeatRole::DriverGunner)
        return reject(VehicleInputPlanStatus::UnsupportedSeat);
    if(!seat.bindingVerified||!profile.routeFingerprint||profile.routeFingerprint!=seat.routeFingerprint)
        return reject(VehicleInputPlanStatus::UnverifiedBinding);
    if(!ValidIdentity(seat.identity)||command.identity!=seat.identity)return reject(VehicleInputPlanStatus::WrongOwner);
    if(!seat.playing||!command.active)return reject(VehicleInputPlanStatus::Inactive);
    const auto mapped=[&](VehicleAxis axis){return profile.axes[unsigned(axis)].has_value();};
    const bool drives=profile.role==VehicleSeatRole::Driver||profile.role==VehicleSeatRole::DriverGunner;
    const bool shoots=profile.role==VehicleSeatRole::Gunner||profile.role==VehicleSeatRole::DriverGunner;
    if((drives&&(!mapped(VehicleAxis::Throttle)||!mapped(VehicleAxis::Steer)))||
       (!drives&&(mapped(VehicleAxis::Throttle)||mapped(VehicleAxis::Steer)))||
       (shoots&&(!mapped(VehicleAxis::LookYaw)||!mapped(VehicleAxis::LookPitch)||!mapped(VehicleAxis::Fire)))||
       (!shoots&&mapped(VehicleAxis::Fire)))return reject(VehicleInputPlanStatus::InvalidProfile);
    std::uint32_t ownedAxes=0;
    for(unsigned n=0;n<VehicleAxisCount;++n){
        const float value=command.axes[n];
        if(!std::isfinite(value)||value<((n==unsigned(VehicleAxis::Fire))?0.f:-1.f)||value>1.f)
            return reject(VehicleInputPlanStatus::InvalidInput);
        if(!profile.axes[n])continue;
        const auto action=unsigned(*profile.axes[n]);
        // Native scalar actions 0..10; signed movement/look mask is 0x6F3.
        // Weapon cycle7 is intentionally outside this minimal vehicle contract.
        if(action>10||(n==unsigned(VehicleAxis::Fire)?action!=8:((0x673u&(1u<<action))==0))||
           (ownedAxes&(1u<<action)))return reject(VehicleInputPlanStatus::InvalidProfile);
        ownedAxes|=1u<<action;
    }
    if(cache.size()<InputBytes)return reject(VehicleInputPlanStatus::InvalidCache);
    const auto allowed=Word(cache,0x98);
    for(unsigned n=0;n<VehicleAxisCount;++n)if(profile.axes[n]){
        const auto action=unsigned(*profile.axes[n]),offset=8+4*action;
        const auto before=Word(cache,offset);
        if(!std::isfinite(std::bit_cast<float>(before)))return reject(VehicleInputPlanStatus::InvalidCache);
        if(!(allowed&(1u<<action))){out.deniedAxes|=1u<<n;continue;}
        out.edits[out.count++]={offset,before,std::bit_cast<std::uint32_t>(command.axes[n])};
    }
    if(profile.exitVerified){
        constexpr auto mask=1u<<unsigned(EntryAction::ChangeVehicle);
        out.edits[out.count++]={0x98,allowed,(allowed&~mask)|(command.exitHeld?mask:0)};
    }
    out.status=VehicleInputPlanStatus::Ready;return out;
}
}
