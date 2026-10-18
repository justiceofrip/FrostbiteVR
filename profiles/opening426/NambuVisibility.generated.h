#pragma once
#include "Bc2VisibilityProfiles.h"
#include <array>
namespace fvr::bc2 {
inline constexpr std::array<std::string_view,1> VisibilityMeshes0{"Objects/Weapons/Handheld/Beach/Nambu100/Nambu100_Mesh"};
inline constexpr std::array<std::string_view,5> VisibilityBones0{"jntWpn_12","jntWpn_3","jntWpn_2","jntWpn_1","jntWpn_4"};
inline constexpr std::array<VisibilityDescriptor,1> VisibilityDescriptors{{
{"Nambu100",VisibilityMeshes0,VisibilityBones0,0xa7f219a1426216abull,false,"Objects/Weapons/Handheld/Beach/Nambu100/Nambu100",false,false,true},
}};
}
