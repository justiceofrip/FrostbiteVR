#pragma once
#include "fvr/math/OpticAperture.h"
#include <array>
#include <cstddef>
#include <optional>
#include <cstdint>
#include <string_view>
namespace fvr::bc2 {
// Offline measured geometry, not GPU/native ownership. This profile must NOT
// authorize a draw skip by itself. Section palette35 is an asset ID, never an
// index into a captured GPU constant buffer or the full soldier rig.
struct Bc2ReticleApertureProfile {
    std::string_view mesh,material,section,meshDataSha256;
    std::uint32_t lod=0,indexCount=0,firstIndex=0,vertexOffset=0,stride=0,vertices=0;
    std::uint32_t assetPaletteId=0,boneNameHash=0;
    std::array<math::OpticAperture,2> apertures{};
    math::ReticleBounds reticle{};
};
const Bc2ReticleApertureProfile& AcogReticleApertureProfile()noexcept;
// Candidate draw metadata is a filter, NOT ownership proof. Native integration
// still needs selected Meshes1p -> exact geometry buffers/ranges -> submitted
// jntWpn_10 skin transform -> current rendered eye. See the evidence document.
// Exact column-packed (3 columns x 4 floats) NATIVE SKIN matrix from the
// submitted optic. It already includes inverse bind and current bone pose.
// Input eye is canonical LH engine world, not tracking LOCAL or a centre eye.
// The measured asset is authored in metres. Returned position uses the asset's
// original handedness (+Z through optic); arbitrary SIMD padding is absent.
std::optional<math::Vec3> AcogEyeInBindSpace(
    std::span<const std::byte,48> submittedNativeSkin,math::Vec3 eyeCanonicalLh)noexcept;
// firstIndex/vertexOffset below are resource-relative values AFTER native
// relocation normalization, not unverified raw DrawIndexed arguments.
bool MatchesAcogReticleSection(std::string_view mesh,std::string_view material,
    std::uint32_t lod,std::uint32_t indexCount,std::uint32_t firstIndex,
    std::uint32_t vertexOffset,std::uint32_t stride,std::uint32_t vertices)noexcept;
}
