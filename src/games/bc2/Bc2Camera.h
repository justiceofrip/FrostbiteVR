#pragma once
#include "fvr/engine/FrostbiteCamera.h"
#include "fvr/math/StereoCulling.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
namespace fvr::bc2 {
// BC2's verified 0x460-byte RenderView is a value object. Never use this
// layout as an assertion about BF3/BF4. All operations here own their bytes.
struct alignas(16) RenderViewCopy { std::array<std::byte,0x460> bytes{}; };
static_assert(sizeof(RenderViewCopy)==0x460);
// Native lazy getters must not mutate our installed value after its exact
// write snapshot. Run all dirty cache families on the owned copy, including
// the scalar cache at +0x210..+0x21c (dirty bit4). Native bindings are verified
// separately; this helper neither touches a live view nor masks restore bytes.
template<class CacheFn>
void PrecomputeCameraCaches(RenderViewCopy& copy,CacheFn view,CacheFn projection,
    CacheFn frustum,CacheFn scale,bool projectionChanged=true) noexcept {
    view(copy.bytes.data());
    if(projectionChanged)projection(copy.bytes.data());
    frustum(copy.bytes.data());
    std::uint32_t dirty=0;std::memcpy(&dirty,copy.bytes.data(),sizeof(dirty));
    if(projectionChanged||(dirty&4))scale(copy.bytes.data());
}
// Keeps projection/LOD parameters unchanged and invalidates only pose caches.
[[nodiscard]] std::optional<RenderViewCopy> BuildTransformCopy(
    const RenderViewCopy& prototype,const math::Matrix4& canonicalWorld) noexcept;
// Preserves unknown fields and native LOD reference parameters. Marks all
// dependent caches dirty; the bound native cache methods must rebuild them
// before use. This does not write a game object or grant a camera capability.
// Native frustum ignores crop and clamps vertical half-angle. This builder
// expands asymmetry to a centered cone and rejects unsupported wide cones.
[[nodiscard]] std::optional<RenderViewCopy> BuildCullingViewCopy(
    const RenderViewCopy& prototype,const math::StereoCullEnvelope&) noexcept;
[[nodiscard]] std::optional<RenderViewCopy> BuildRenderViewCopy(
    const RenderViewCopy& prototype,const math::Matrix4& canonicalWorld,
    const math::FovTangents& fov,float nearPlane,float farPlane) noexcept;
}
