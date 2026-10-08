#pragma once
#include "fvr/math/StereoMath.h"
#include <cstdint>
namespace fvr::interaction {
// Horizontal body-follow policy. The adapter submits ordinary collision-tested
// locomotion and reports the actual body position on the following update.
// Only that measured displacement is consumed from the tracked camera offset.
struct RoomscaleStep {
    std::uint64_t owner=0,space=0,nowMs=0;
    math::Vec3 headLocalMeters{},bodyWorldMeters{};
    float bodyYaw=0; // canonical LH body heading, radians
    bool manualMovement=false;
};
struct RoomscaleOutput {
    float forwardMetersPerSecond=0,strafeMetersPerSecond=0;
    math::Vec3 consumedLocalMeters{};
    bool valid=false,driving=false;
};
class RoomscaleFollow {
public:
    RoomscaleOutput Update(const RoomscaleStep&) noexcept;
    void Suspend() noexcept {driving_=false;}
private:
    std::uint64_t owner_=0,space_=0,lastMs_=0;
    math::Vec3 consumed_{},lastBody_{};
    float lastYaw_=0;bool driving_=false;
};
}
