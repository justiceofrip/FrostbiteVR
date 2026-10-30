#pragma once
#define MagazineNativeRegistrations PreservedResource233Registrations
#include "../resource-enrollment233/ReviewedResourceRegistry.h"
#undef MagazineNativeRegistrations
#define generated scar235
#include "../resource-enrollment235-scar/ScarRegistry.h"
#undef generated
#define generated f2000240
#include "../resource-enrollment240-f2000/F2000Registry.h"
#undef generated
#define generated pistol295
#include "../resource-enrollment295-pistol/PistolRegistry.h"
#undef generated
#define generated opening426
#include "../opening426/NambuRegistry.generated.h"
#undef generated
#define generated pistol41
#include "PistolRegistry.generated.h"
#undef generated
namespace fvr::bc2::generated {
inline const auto MagazineNativeRegistrations=[] {
 std::array<MagazineNativeRegistration,PreservedResource233Registrations.size()+scar235::MagazineNativeRegistrations.size()+f2000240::MagazineNativeRegistrations.size()+pistol295::MagazineNativeRegistrations.size()+opening426::MagazineNativeRegistrations.size()+pistol41::MagazineNativeRegistrations.size()> out{};
 std::size_t n=0;for(const auto& p:PreservedResource233Registrations)out[n++]=p;
 for(const auto& p:scar235::MagazineNativeRegistrations)out[n++]=p;
 for(const auto& p:f2000240::MagazineNativeRegistrations)out[n++]=p;
 for(const auto& p:pistol295::MagazineNativeRegistrations)out[n++]=p;
 for(const auto& p:opening426::MagazineNativeRegistrations)out[n++]=p;
 for(const auto& p:pistol41::MagazineNativeRegistrations)out[n++]=p;
 return out;
}();
}
