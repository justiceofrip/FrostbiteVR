#pragma once
#include "fvr/engine/FrostbiteCamera.h"
#include "fvr/math/StereoCulling.h"
#include <array>
#include <cstddef>
#include <optional>
namespace fvr::bc2 {
// BC2's verified 0x460-byte RenderView is a value object. Never use this
// layout as an assertion about BF3/BF4. All operations here own their bytes.
struct alignas(16) RenderViewCopy { std::array<std::byte,0x460> bytes{}; };
static_assert(sizeof(RenderViewCopy)==0x460);
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
