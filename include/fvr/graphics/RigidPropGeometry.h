#pragma once
#include "fvr/interaction/TrackingMath.h"
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>
namespace fvr::graphics {
// CPU copies only. An asset fingerprint is not a current native draw/owner proof.
enum class RigidPropPosition : unsigned {Float3,Half4};
enum class RigidPropBasis : unsigned {NativeRightHanded,CanonicalLeftHanded};
struct RigidPropSection {
    unsigned indexCount=0,stride=0,paletteSize=0,partPaletteIndex=0;
    RigidPropPosition position=RigidPropPosition::Float3;
    std::uint64_t vertexSkinHash=0,positionHash=0;
    unsigned expectedPartTriangles=0;
};
struct RigidPropDrawBytes {
    std::span<const std::byte> vertices,selectedIndices;
    unsigned indexBytes=0;
    std::uint32_t vertexOffset=0;
    std::int32_t baseVertex=0;
};
struct RigidPropVertex {float x=0,y=0,z=0,nx=0,ny=0,nz=0;};
struct RigidPropMesh {
    std::vector<RigidPropVertex> vertices; // independent, unindexed triangle list
    std::uint64_t sourceVertexSkinHash=0,sourcePositionHash=0;
};
// Explicit source basis conversion followed by the named part's inverse bind.
// Rejects all mixed-weight/mixed-owner triangles, bad indices, hash mismatches,
// nonfinite coordinates, and a changed selected-part triangle count.
std::optional<RigidPropMesh> ExtractRigidProp(const RigidPropSection&,const RigidPropDrawBytes&,
    const math::Matrix4& canonicalInverseBind,RigidPropBasis sourceBasis);
struct RigidPropFingerprint {std::uint64_t skin=0,positions=0;};
std::optional<RigidPropFingerprint> FingerprintRigidPropDraw(const RigidPropSection&,const RigidPropDrawBytes&)noexcept;
} // namespace fvr::graphics
