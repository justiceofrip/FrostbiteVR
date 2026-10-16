#pragma once
#include "fvr/interaction/BodyAnchors.h"
#include <string_view>
namespace fvr::bc2::private_knife {
// Authored comfort placement. This is NOT an inferred native socket. Slot5 is
// separate from ammunition chest3, shoulders1/2 and hip4; no shared config is
// modified, nor is the knife enrolled in existing weapon shoulder assignment.
inline constexpr interaction::BodyAnchor KnifeChest{5,{.12f,-.25f,.15f},.07f};
// Saved316/323 measured static authored RIGHT wrist relative to jntWpn_1,
// canonical reflected-Z row-vector metres. No left-hand mirroring capability.
inline constexpr math::Matrix4 RightHandInKnife=[] {
    math::Matrix4 result;result.values={{
    {-.556891076f,-.048992343f,.829139361f,0},
    {.829301295f,.022693201f,.558340739f,0},
    {-.046170247f,.998541321f,.027991772f,0},
    {.019256875f,.072308343f,-.036899791f,1}
    }};return result;
}();
inline constexpr math::Vec3 BladeTipInKnife{0,.004729986f,-.250915766f};
inline constexpr math::Vec3 BladeAxisInKnife{0,0,-1};
inline constexpr std::string_view KnifeMesh="Objects/Weapons/Handheld/Knife/Knife_Mesh";
inline constexpr std::string_view ReferenceBindingDigest="899d7f96a0087c67552f2eda057bc65a253769638ee6f0a4e8f4a7eaec7c9e3e";
inline constexpr std::string_view RigHash="fnv1a64:a7f219a1426216ab";
inline constexpr std::string_view SkeletonSha256="d1d713cc7301bc5ae2b011a0991ca03c2fcbb26c1187900f6fd129533d173c09";
inline constexpr bool LeftHandGraspSupported=false,RenderEnrolled=false;
// Contact and motion must use original grip poses in one tracking epoch. For
// row vectors: inverse(RightHandInKnife) * actualRightGrip gives knife placement.
// Compute motion in a FIXED tracking reference, not the current head view or
// chest-anchor frame that follows current HMD translation. Flash is in handle.
}
