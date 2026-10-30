#pragma once
#define MuzzleEnrollments Opening426MuzzleEnrollments
#include "../opening426/NambuMuzzle.generated.h"
#undef MuzzleEnrollments
#define MuzzleEnrollments Firearm54MuzzleEnrollments
#include "Muzzle.generated.h"
#undef MuzzleEnrollments
namespace fvr::bc2::generated_muzzle {
inline constexpr auto MuzzleEnrollments=[] {
 std::array<MuzzleEnrollmentProfile,Opening426MuzzleEnrollments.size()+Firearm54MuzzleEnrollments.size()> out{};
 std::size_t n=0;
 for(const auto& p:Opening426MuzzleEnrollments)out[n++]=p;
 for(const auto& p:Firearm54MuzzleEnrollments)out[n++]=p;
 return out;
}();
}
