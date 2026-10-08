#pragma once
#define MagazineNativeRegistrations PreservedResource233Registrations
#include "../resource-enrollment233/ReviewedResourceRegistry.h"
#undef MagazineNativeRegistrations
#define generated scar235
#include "ScarRegistry.h"
#undef generated
namespace fvr::bc2::generated {
inline const auto MagazineNativeRegistrations=[] {
 std::array<MagazineNativeRegistration,PreservedResource233Registrations.size()+scar235::MagazineNativeRegistrations.size()> out{};
 std::size_t n=0;for(const auto& p:PreservedResource233Registrations)out[n++]=p;
 for(const auto& p:scar235::MagazineNativeRegistrations)out[n++]=p;
 return out;
}();
}
