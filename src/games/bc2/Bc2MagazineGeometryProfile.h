#pragma once
#include "Bc2SelectedMeshes1p.h"
#include "fvr/interaction/DetachableMagazine.h"
#include <array>
#include <string_view>

namespace fvr::bc2 {
inline constexpr std::string_view Xm8MagazineAsset="XM8_sp_s";
inline constexpr std::string_view Xm8MagazineMesh="Objects/Weapons/Handheld/US_rgl_XM8/US_rgl_XM8_Mesh";
inline constexpr std::uint64_t Xm8MagazineRig=0xa7f219a1426216abull;

inline constexpr unsigned MagazineAssemblyLimit=8;
struct MagazineAssemblyMember {
 std::string_view bone,parent;
 math::Matrix4 itemFromBone; // Bone-local to root magazine-local, metres.
};
struct MagazineBoneRoles {
 std::string_view weapon,magazine,wrist;
 // Three authored joints for each of thumb/index/middle/ring/pinky, in order.
 std::array<std::string_view,15> fingers{};
};
// Geometry data only; registered origins distinguish measured calibration from
// explicit experimental authored/design estimates. A profile neither admits native reload callbacks nor
// proves ammunition, ownership, linked-weapon mapping, or runtime acceptance.
// ARs/SMGs share DetachableMagazine/ReloadInsertion; calibration is separate data.
struct MagazineGeometryProfile {
 std::string_view asset,mesh;
 SelectedMeshKind meshKind=SelectedMeshKind::Xm8;
 std::uint64_t rigFingerprint=0;
 interaction::DetachableMagazineConfig interaction;
 MagazineBoneRoles bones;
 math::Matrix4 attachedItem;
 std::array<math::Matrix4,15> wristFromFinger;
 // Exact authored weapon configuration path, independent from display name.
 // Empty is retained only for the two immutable legacy builtin descriptors.
 std::string_view configurationPath;
 // Optional complete rigid subtree, parent before child. Zero retains the
 // existing leaf contract and the exact legacy pose/hide write sequence.
 std::array<MagazineAssemblyMember,MagazineAssemblyLimit> assembly{};
 unsigned assemblyCount=0;
};
const MagazineGeometryProfile& Xm8MagazineGeometry()noexcept;
// Exact registered assets only; there is no category, prefix or rig-only fallback.
// Accepted scoped XM8 takes priority over any optional generated reference.
const MagazineGeometryProfile* FindMagazineGeometry(std::string_view asset)noexcept;
struct MagazineNativeProfile;
bool MagazineGeometryMatchesConfiguration(const MagazineGeometryProfile&,
 const MagazineNativeProfile&)noexcept;
const MagazineGeometryProfile* FindMagazineGeometry(const MagazineNativeProfile&)noexcept;
// Pure bounded selection used by the optional registry. A reviewed native family
// is necessary; caller compilation of estimates never bypasses native receipts.
const MagazineGeometryProfile* SelectExperimentalMagazineGeometry(
 std::span<const MagazineGeometryProfile>,std::string_view asset)noexcept;
const MagazineGeometryProfile* SelectExperimentalMagazineGeometry(
 std::span<const MagazineGeometryProfile>,const MagazineNativeProfile&)noexcept;
}
