#pragma once
#include "Bc2InputBinding.h"
#include <array>
#include <optional>

namespace fvr::bc2 {
enum class VehicleSeatRole : std::uint8_t {Unclassified,Driver,Gunner,DriverGunner};
enum class VehicleAxis : std::uint8_t {Throttle,Steer,LookYaw,LookPitch,Fire,Count};
inline constexpr unsigned VehicleAxisCount=unsigned(VehicleAxis::Count);
struct VehicleSeatIdentity {
    std::uint64_t actor=0,actorGeneration=0,seatGeneration=0;
    std::uint32_t controlled=0,entry=0,router=0,cache=0;
    bool operator==(const VehicleSeatIdentity&)const=default;
};
// Populated only after the adapter verifies this actual entry's action mapping
// and native consumers. These are explicit per-seat bindings, not assumptions
// that every vehicle steers with EiaYaw or aims with EiaCameraYaw.
struct VehicleSeatProfile {
    VehicleSeatRole role=VehicleSeatRole::Unclassified;
    std::uint64_t routeFingerprint=0;
    std::array<std::optional<EntryAction>,VehicleAxisCount> axes{};
    bool exitVerified=false;
};
struct VehicleSeatSnapshot {
    VehicleSeatIdentity identity{};
    std::uint64_t routeFingerprint=0;
    bool bindingVerified=false,playing=false;
};
struct VehicleInputCommand {
    VehicleSeatIdentity identity{};
    bool active=false; // Caller owns original input deadline, focus and neutral recovery.
    std::array<float,VehicleAxisCount> axes{}; // Vehicle-relative axes, Fire in [0,1].
    bool exitHeld=false;
};
enum class VehicleInputPlanStatus : std::uint8_t {
    Ready,UnsupportedSeat,UnverifiedBinding,WrongOwner,Inactive,InvalidProfile,InvalidInput,InvalidCache
};
struct VehicleInputEdit {std::size_t offset=0;std::uint32_t before=0,after=0;};
struct VehicleInputPlan {
    VehicleInputPlanStatus status=VehicleInputPlanStatus::UnverifiedBinding;
    std::array<VehicleInputEdit,VehicleAxisCount+1> edits{};
    unsigned count=0;
    std::uint32_t deniedAxes=0; // Semantic axes suppressed by THIS gather's allowed bits.
};
// Pure byte-plan builder: no hooks, process calls, input dispatch or memory
// writes. Native float eligibility is preserved per action; unsupported fields
// and native driving/aim/fire restrictions are never overwritten. Runtime must
// revalidate identity and each before value immediately before a scoped commit.
VehicleInputPlan BuildVehicleInputPlan(const VehicleSeatProfile&,const VehicleSeatSnapshot&,
    const VehicleInputCommand&,std::span<const std::byte> cache)noexcept;
}
