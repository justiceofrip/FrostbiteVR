#pragma once
#include "fvr/graphics/RigidPropGeometry.h"
#include <array>
namespace fvr::bc2 {
// Derived installed-resource fingerprints only. No mesh bytes or native draw admission.
struct BeltPropSectionProfile {const char* asset;const char* mesh;const char* part;const char* section;graphics::RigidPropSection geometry;};
inline constexpr std::array<BeltPropSectionProfile,6> BeltPropSections{{
{"XM8_sp_s","Objects/Weapons/Handheld/US_rgl_XM8/US_rgl_XM8_Mesh","jntWpn_6","jntWpn_7_Composite",{8181,64,5,4,graphics::RigidPropPosition::Float3,0xb983689a4a45d242ull,0x1186d1f53e719085ull,36}},
{"XM8_sp_s","Objects/Weapons/Handheld/US_rgl_XM8/US_rgl_XM8_Mesh","jntWpn_6","jntWpn_1_Rubber",{5205,64,3,2,graphics::RigidPropPosition::Float3,0xbb25b4229ad0d7d9ull,0x3496d9ffe80c8faeull,44}},
{"XM8_sp_s","Objects/Weapons/Handheld/US_rgl_XM8/US_rgl_XM8_Mesh","jntWpn_6","jntWpn_6_Plastic",{1152,64,1,0,graphics::RigidPropPosition::Float3,0x990776bf47b64fe5ull,0x3c0ec8d0d11cf77dull,384}},
{"XM8_sp_s","Objects/Weapons/Handheld/US_rgl_XM8/US_rgl_XM8_Mesh","jntWpn_6","jntWpn_6_Ammo",{834,64,3,0,graphics::RigidPropPosition::Float3,0x924d2fd5524463dbull,0x73c7e4eb48e7ddefull,38}},
{"SPAS12_sp","Objects/Weapons/Handheld/UL_shg_SPAS12/UL_shg_SPAS-12_Mesh","jntWpn_7","jntWpn_7_Ammo_Brass",{270,48,1,0,graphics::RigidPropPosition::Half4,0xe05c8e35ec306bd2ull,0x5fd96edbcded25baull,90}},
{"SPAS12_sp","Objects/Weapons/Handheld/UL_shg_SPAS12/UL_shg_SPAS-12_Mesh","jntWpn_7","jntWpn_7_ammo_plastic",{90,48,1,0,graphics::RigidPropPosition::Half4,0xce6e99d2c3cd267eull,0xa9888be789a5c902ull,30}},
}};
inline constexpr bool IndependentBeltPropNativeAdmitted=false;
}
