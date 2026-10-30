#pragma once
#include "fvr/graphics/RigidPropGeometry.h"
#include <array>
namespace fvr::bc2 {
// Derived installed-resource fingerprints only. No mesh bytes or native draw admission.
struct BeltPropSectionProfile {const char* asset;const char* mesh;const char* part;const char* section;graphics::RigidPropSection geometry;};
inline constexpr std::array<BeltPropSectionProfile,32> BeltPropSections{{
{"XM8_sp_s","Objects/Weapons/Handheld/US_rgl_XM8/US_rgl_XM8_Mesh","jntWpn_6","jntWpn_7_Composite",{8181,64,5,4,graphics::RigidPropPosition::Float3,0xb983689a4a45d242ull,0x1186d1f53e719085ull,36}},
{"XM8_sp_s","Objects/Weapons/Handheld/US_rgl_XM8/US_rgl_XM8_Mesh","jntWpn_6","jntWpn_1_Rubber",{5205,64,3,2,graphics::RigidPropPosition::Float3,0xbb25b4229ad0d7d9ull,0x3496d9ffe80c8faeull,44}},
{"XM8_sp_s","Objects/Weapons/Handheld/US_rgl_XM8/US_rgl_XM8_Mesh","jntWpn_6","jntWpn_6_Plastic",{1152,64,1,0,graphics::RigidPropPosition::Float3,0x990776bf47b64fe5ull,0x3c0ec8d0d11cf77dull,384}},
{"XM8_sp_s","Objects/Weapons/Handheld/US_rgl_XM8/US_rgl_XM8_Mesh","jntWpn_6","jntWpn_6_Ammo",{834,64,3,0,graphics::RigidPropPosition::Float3,0x924d2fd5524463dbull,0x73c7e4eb48e7ddefull,38}},
{"SPAS12_sp","Objects/Weapons/Handheld/UL_shg_SPAS12/UL_shg_SPAS-12_Mesh","jntWpn_7","jntWpn_7_Ammo_Brass",{270,48,1,0,graphics::RigidPropPosition::Half4,0xe05c8e35ec306bd2ull,0x5fd96edbcded25baull,90}},
{"SPAS12_sp","Objects/Weapons/Handheld/UL_shg_SPAS12/UL_shg_SPAS-12_Mesh","jntWpn_7","jntWpn_7_ammo_plastic",{90,48,1,0,graphics::RigidPropPosition::Half4,0xce6e99d2c3cd267eull,0xa9888be789a5c902ull,30}},
{"MG3k","Objects/Weapons/Handheld/BU_lmg_MG3/BU_lmg_MG3_Mesh","jntWpn_6","jntWpn_1_BrushedMetal",{14565,64,8,7,graphics::RigidPropPosition::Float3,0x089780c94e323949ull,0x8c43f3e7ef842142ull,426}},
{"MG3k","Objects/Weapons/Handheld/BU_lmg_MG3/BU_lmg_MG3_Mesh","jntWpn_6","jntWpn_19_Ammo",{5400,64,5,4,graphics::RigidPropPosition::Float3,0x931ee41a205bb126ull,0x2aca37efeca40596ull,240}},
{"MG3","Objects/Weapons/Handheld/BU_lmg_MG3/BU_lmg_MG3_Mesh","jntWpn_6","jntWpn_1_BrushedMetal",{14565,64,8,7,graphics::RigidPropPosition::Float3,0x089780c94e323949ull,0x8c43f3e7ef842142ull,426}},
{"MG3","Objects/Weapons/Handheld/BU_lmg_MG3/BU_lmg_MG3_Mesh","jntWpn_6","jntWpn_19_Ammo",{5400,64,5,4,graphics::RigidPropPosition::Float3,0x931ee41a205bb126ull,0x2aca37efeca40596ull,240}},
{"QJU88","Objects/Weapons/Handheld/MEC_lmg_QJY88/MEC_lmg_QJY88_Mesh","jntWpn_6","export1p|lod0|MEC_lmg_QJY88_Base|MEC_lmg_QJY88_BaseShape_BrushedMetal",{15105,64,10,3,graphics::RigidPropPosition::Float3,0x07d2a2ae969f5d2dull,0x4d0d886081dd9beeull,716}},
{"QJU88","Objects/Weapons/Handheld/MEC_lmg_QJY88/MEC_lmg_QJY88_Mesh","jntWpn_6","export1p|lod0|MEC_lmg_QJY88_Base|MEC_lmg_QJY88_BaseShape_Plastic",{6450,64,4,2,graphics::RigidPropPosition::Float3,0x12de5e035117903bull,0x22f33c3d7f059037ull,180}},
{"QJU88","Objects/Weapons/Handheld/MEC_lmg_QJY88/MEC_lmg_QJY88_Mesh","jntWpn_6","jntWpn_6_Fabrics",{276,64,1,0,graphics::RigidPropPosition::Float3,0xca71915d65e87bfeull,0x0fe86d4af00535daull,92}},
{"M60","Objects/Weapons/Handheld/BU_lmg_M60/BU_lmg_M60_Mesh","jntWpn_6","jntWpn_9_BrushedMetal",{17442,48,10,3,graphics::RigidPropPosition::Half4,0x7842c9fe0fd022d9ull,0xce7ed20cbaa864b5ull,13}},
{"M60","Objects/Weapons/Handheld/BU_lmg_M60/BU_lmg_M60_Mesh","jntWpn_6","jntWpn_6_Fabrics",{1080,48,1,0,graphics::RigidPropPosition::Half4,0xfcd2800d0eb4d58full,0xb6bfac8d100c16e7ull,360}},
{"PKM","Objects/Weapons/Handheld/RU_lmg_PKM/RU_lmg_PKM_Mesh","jntWpn_6","jntWpn_1_BrushedMetal",{20988,64,12,8,graphics::RigidPropPosition::Float3,0x2726da019badf5c3ull,0xc8eb61229c90ea27ull,100}},
{"PKM","Objects/Weapons/Handheld/RU_lmg_PKM/RU_lmg_PKM_Mesh","jntWpn_6","jntWpn_6_Fabrics",{2148,64,1,0,graphics::RigidPropPosition::Float3,0x865482c945003b68ull,0x1d0d3b4511981ef0ull,716}},
{"M249","Objects/Weapons/Handheld/UL_lmg_FNMinimi/UL_lmg_FNMinimi_Mesh","jntWpn_6","jntWpn_6_Plastic",{3498,64,2,0,graphics::RigidPropPosition::Float3,0x03e1c98a30c6f293ull,0xaad771ba6586f7c7ull,166}},
{"MG3_sp","Objects/Weapons/Handheld/BU_lmg_MG3/BU_lmg_MG3_Mesh","jntWpn_6","jntWpn_1_BrushedMetal",{14565,64,8,7,graphics::RigidPropPosition::Float3,0x089780c94e323949ull,0x8c43f3e7ef842142ull,426}},
{"MG3_sp","Objects/Weapons/Handheld/BU_lmg_MG3/BU_lmg_MG3_Mesh","jntWpn_6","jntWpn_19_Ammo",{5400,64,5,4,graphics::RigidPropPosition::Float3,0x931ee41a205bb126ull,0x2aca37efeca40596ull,240}},
{"QJU88_sp_k","Objects/Weapons/Handheld/MEC_lmg_QJY88/MEC_lmg_QJY88_Mesh","jntWpn_6","export1p|lod0|MEC_lmg_QJY88_Base|MEC_lmg_QJY88_BaseShape_BrushedMetal",{15105,64,10,3,graphics::RigidPropPosition::Float3,0x07d2a2ae969f5d2dull,0x4d0d886081dd9beeull,716}},
{"QJU88_sp_k","Objects/Weapons/Handheld/MEC_lmg_QJY88/MEC_lmg_QJY88_Mesh","jntWpn_6","export1p|lod0|MEC_lmg_QJY88_Base|MEC_lmg_QJY88_BaseShape_Plastic",{6450,64,4,2,graphics::RigidPropPosition::Float3,0x12de5e035117903bull,0x22f33c3d7f059037ull,180}},
{"QJU88_sp_k","Objects/Weapons/Handheld/MEC_lmg_QJY88/MEC_lmg_QJY88_Mesh","jntWpn_6","jntWpn_6_Fabrics",{276,64,1,0,graphics::RigidPropPosition::Float3,0xca71915d65e87bfeull,0x0fe86d4af00535daull,92}},
{"M60_sp","Objects/Weapons/Handheld/BU_lmg_M60/BU_lmg_M60_Mesh","jntWpn_6","jntWpn_9_BrushedMetal",{17442,48,10,3,graphics::RigidPropPosition::Half4,0x7842c9fe0fd022d9ull,0xce7ed20cbaa864b5ull,13}},
{"M60_sp","Objects/Weapons/Handheld/BU_lmg_M60/BU_lmg_M60_Mesh","jntWpn_6","jntWpn_6_Fabrics",{1080,48,1,0,graphics::RigidPropPosition::Half4,0xfcd2800d0eb4d58full,0xb6bfac8d100c16e7ull,360}},
{"M60_sp_s","Objects/Weapons/Handheld/BU_lmg_M60/BU_lmg_M60_Mesh","jntWpn_6","jntWpn_9_BrushedMetal",{17442,48,10,3,graphics::RigidPropPosition::Half4,0x7842c9fe0fd022d9ull,0xce7ed20cbaa864b5ull,13}},
{"M60_sp_s","Objects/Weapons/Handheld/BU_lmg_M60/BU_lmg_M60_Mesh","jntWpn_6","jntWpn_6_Fabrics",{1080,48,1,0,graphics::RigidPropPosition::Half4,0xfcd2800d0eb4d58full,0xb6bfac8d100c16e7ull,360}},
{"QJU88_sp","Objects/Weapons/Handheld/MEC_lmg_QJY88/MEC_lmg_QJY88_Mesh","jntWpn_6","export1p|lod0|MEC_lmg_QJY88_Base|MEC_lmg_QJY88_BaseShape_BrushedMetal",{15105,64,10,3,graphics::RigidPropPosition::Float3,0x07d2a2ae969f5d2dull,0x4d0d886081dd9beeull,716}},
{"QJU88_sp","Objects/Weapons/Handheld/MEC_lmg_QJY88/MEC_lmg_QJY88_Mesh","jntWpn_6","export1p|lod0|MEC_lmg_QJY88_Base|MEC_lmg_QJY88_BaseShape_Plastic",{6450,64,4,2,graphics::RigidPropPosition::Float3,0x12de5e035117903bull,0x22f33c3d7f059037ull,180}},
{"QJU88_sp","Objects/Weapons/Handheld/MEC_lmg_QJY88/MEC_lmg_QJY88_Mesh","jntWpn_6","jntWpn_6_Fabrics",{276,64,1,0,graphics::RigidPropPosition::Float3,0xca71915d65e87bfeull,0x0fe86d4af00535daull,92}},
{"MG3_sp_k","Objects/Weapons/Handheld/BU_lmg_MG3/BU_lmg_MG3_Mesh","jntWpn_6","jntWpn_1_BrushedMetal",{14565,64,8,7,graphics::RigidPropPosition::Float3,0x089780c94e323949ull,0x8c43f3e7ef842142ull,426}},
{"MG3_sp_k","Objects/Weapons/Handheld/BU_lmg_MG3/BU_lmg_MG3_Mesh","jntWpn_6","jntWpn_19_Ammo",{5400,64,5,4,graphics::RigidPropPosition::Float3,0x931ee41a205bb126ull,0x2aca37efeca40596ull,240}},
}};
inline constexpr bool IndependentBeltPropNativeAdmitted=false;
}
