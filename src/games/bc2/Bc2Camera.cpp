#include "Bc2Camera.h"
#include <cmath>
#include <algorithm>
#include <cstring>
#include <cstdint>
namespace fvr::bc2 {
std::optional<RenderViewCopy> BuildTransformCopy(const RenderViewCopy& prototype,const math::Matrix4& world) noexcept {
    math::Matrix4 identity{};for(unsigned i=0;i<4;++i)identity.values[i][i]=1;
    const auto eye=engine::NativeEye({world,identity});if(!eye)return {};
    RenderViewCopy result=prototype;std::uint32_t dirty=0;std::memcpy(&dirty,result.bytes.data(),4);dirty|=9;
    std::memcpy(result.bytes.data(),&dirty,4);std::memcpy(result.bytes.data()+0x50,&eye->transform,64);return result;
}
std::optional<RenderViewCopy> BuildCullingViewCopy(const RenderViewCopy& prototype,const math::StereoCullEnvelope& envelope) noexcept {
    const auto& f=envelope.fov;
    if(!math::MakeLhProjectionFromFovTangents(f,envelope.nearPlane,envelope.farPlane))return {};
    const float halfWidth=(std::max)(std::abs(f.left),std::abs(f.right));
    const float halfHeight=(std::max)(std::abs(f.down),std::abs(f.up));
    if(!std::isfinite(halfWidth)||!std::isfinite(halfHeight)||halfWidth<=0||halfHeight<=0)return {};
    // Native 0x00965bf0 ultimately clamps halfFov to this observed double.
    // Leave an angular margin so float conversion cannot cross the clamp.
    constexpr double nativeHalfFovLimit=0.9999989867210388;
    if(std::atan(double(halfHeight))>nativeHalfFovLimit-0.00001)return {};
    return BuildRenderViewCopy(prototype,envelope.world,{-halfWidth,halfWidth,halfHeight,-halfHeight},envelope.nearPlane,envelope.farPlane);
}
std::optional<RenderViewCopy> BuildRenderViewCopy(const RenderViewCopy& prototype,
    const math::Matrix4& world,const math::FovTangents& fov,float nearPlane,float farPlane) noexcept {
    const auto projection=math::MakeLhProjectionFromFovTangents(fov,nearPlane,farPlane);
    if(!projection)return {};
    const auto eye=engine::NativeEye({world,*projection});if(!eye)return {};
    // Crop origin uses normalized full-frame extents; BC2's vertical origin
    // has the same sign as native projection[2][1] when crop height is one.
    const double width=double(fov.right)-fov.left,height=double(fov.up)-fov.down;
    const float vertical=static_cast<float>(2*std::atan(height*.5));
    const float aspect=static_cast<float>(width/height);
    const float shiftX=static_cast<float>((double(fov.right)+fov.left)/(2*width));
    const float shiftY=static_cast<float>((double(fov.up)+fov.down)/(2*height));
    if(!std::isfinite(vertical)||vertical<=0||vertical>=3.1415926f||
       !std::isfinite(aspect)||aspect<=0||!std::isfinite(vertical*aspect)||
       !std::isfinite(shiftX)||!std::isfinite(shiftY))return {};
    RenderViewCopy result=prototype;
    const auto put=[&](unsigned offset,const auto& value){std::memcpy(result.bytes.data()+offset,&value,sizeof(value));};
    std::uint32_t dirty=0;std::memcpy(&dirty,result.bytes.data(),4);dirty|=0xf;
    put(0,dirty);put(4,std::uint32_t{0});result.bytes[8]=std::byte{1};
    put(0xc,1.f);put(0x10,vertical);put(0x14,vertical*aspect);
    put(0x1c,nearPlane);put(0x20,farPlane);put(0x24,aspect);
    put(0x3c,shiftX);put(0x40,shiftY);put(0x44,1.f);put(0x48,1.f);
    put(0x50,eye->transform);
    return result;
}
}
