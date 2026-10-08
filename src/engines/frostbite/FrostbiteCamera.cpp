#include "fvr/engine/FrostbiteCamera.h"
#include "fvr/interaction/TrackingMath.h"
#include <cmath>
namespace fvr::engine {
namespace {
math::Matrix4 ReflectWorld(math::Matrix4 m){
    // C*M*C converts both the camera-local basis and the world basis.
    for(unsigned i=0;i<4;++i){m.values[2][i]=-m.values[2][i];m.values[i][2]=-m.values[i][2];}return m;
}
bool Finite(const math::Matrix4& m){for(const auto& row:m.values)for(float f:row)if(!std::isfinite(f))return false;return true;}
}
std::optional<runtime::WorldFrame> CanonicalCamera(const FrostbiteCameraInput& in) noexcept {
    if(!Finite(in.transform)||!std::isfinite(in.nearPlane)||!std::isfinite(in.farPlane)||!std::isfinite(in.worldUnitsPerMeter)||
       in.nearPlane<=0||in.farPlane<=in.nearPlane||in.worldUnitsPerMeter<=0)return {};
    auto camera=in.transform;for(unsigned row=0;row<4;++row)camera.values[row][3]=row==3?1.f:0.f;
    if(!interaction::InverseRigid(camera))return {};
    runtime::WorldFrame result{};result.camera=ReflectWorld(camera);result.nearPlane=in.nearPlane;result.farPlane=in.farPlane;result.worldUnitsPerMeter=in.worldUnitsPerMeter;
    return result;
}
std::optional<FrostbiteEye> NativeEye(const runtime::EyeView& eye) noexcept {
    if(!Finite(eye.projection)||!interaction::InverseRigid(eye.world))return {};
    auto transform=ReflectWorld(eye.world);const auto inverse=interaction::InverseRigid(transform);if(!inverse)return {};
    auto projection=eye.projection; // Clip space stays unchanged: P_RH = C * P_LH.
    for(float& f:projection.values[2])f=-f;
    for(unsigned row=0;row<4;++row)transform.values[row][3]=0;
    return FrostbiteEye{transform,*inverse,projection};
}
}