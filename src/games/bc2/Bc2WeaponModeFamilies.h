#pragma once
#include <array>
#include <string_view>
namespace fvr::bc2 {
// Exact primary/attachment configuration pair, never a shared launcher name.
// Ammo remains with each native item; this describes mode selection only.
struct WeaponModeFamilyProfile {
 std::string_view primaryAsset,primaryConfiguration,secondaryAsset,secondaryConfiguration,persistentId;
 unsigned secondaryAction=33,primaryAction=36;
 // Observed native acceptance remains distinct from measured variant eligibility.
 bool nativeAccepted=false;
 // Exact authored deployment/mesh pair using the same selector consumer;
 // sight geometry has its own independent measured enrollment. Requires the complete
 // live linked-persistence and reciprocal-route reader before every command.
 bool linkedVariantEligible=false;
};
#include "../../../profiles/weapons54-underbarrel/WeaponModeFamilies.generated.h"
inline constexpr bool WeaponModeFamilyEligible(const WeaponModeFamilyProfile& p)noexcept {
 return p.nativeAccepted||p.linkedVariantEligible;
}
}
