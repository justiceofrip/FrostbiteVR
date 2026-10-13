#pragma once
#define AuthoredSupports PreservedSupport202
#include "../checkpoint202/Bc2AuthoredSupports.generated.h"
#undef AuthoredSupports
#define AuthoredSupports EnrolledSupport259
#include "../support259/StaticSupports.generated.h"
#undef AuthoredSupports
#define AuthoredSupports EnrolledSupport306
#include "PistolSupport.generated.h"
#undef AuthoredSupports
namespace fvr::bc2::generated {
inline const auto AuthoredSupports=[] {
 std::array<AuthoredSupportProfile,PreservedSupport202.size()+EnrolledSupport259.size()+EnrolledSupport306.size()> out{};
 std::size_t n=0;for(const auto& p:PreservedSupport202)out[n++]=p;
 for(const auto& p:EnrolledSupport259)out[n++]=p;
 for(const auto& p:EnrolledSupport306)out[n++]=p;
 return out;
}();
}
