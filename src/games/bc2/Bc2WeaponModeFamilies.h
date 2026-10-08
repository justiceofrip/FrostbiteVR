#pragma once
#include <array>
#include <string_view>
namespace fvr::bc2 {
// Exact primary/attachment configuration pair, never a shared launcher name.
// Ammo remains with each native item; this describes mode selection only.
struct WeaponModeFamilyProfile {
 std::string_view primaryAsset,primaryConfiguration,secondaryAsset,secondaryConfiguration,persistentId;
 unsigned secondaryAction=33,primaryAction=36;
 bool nativeAccepted=false;
};
inline constexpr std::array<WeaponModeFamilyProfile,2> WeaponModeFamilies{{
 {"XM8_sp_s","Objects/Weapons/Handheld/US_rgl_XM8/SP_rgl_XM8_Scoped","40mmgl","Objects/Weapons/Handheld/US_rgl_XM8/SP_rgl_XM320_Scoped","sp_xm8_s",33,36,true},
 {"AEK971_sp","Objects/Weapons/Handheld/RU_rgl_AEK971/SP_rgl_AEK971","40mmgl","Objects/Weapons/Handheld/RU_rgl_AEK971/SP_rgl_GP30","",33,36,false},
}};
}
