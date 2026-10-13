#pragma once
#define AuthoredSupports PreservedSupport202
#include "../checkpoint202/Bc2AuthoredSupports.generated.h"
#undef AuthoredSupports
#define AuthoredSupports EnrolledSupport308
#include "StaticSupports.generated.h"
#undef AuthoredSupports
namespace fvr::bc2::generated {
inline const auto AuthoredSupports=[] {
 std::array<AuthoredSupportProfile,PreservedSupport202.size()+EnrolledSupport308.size()> out{};
 std::size_t n=0;for(const auto& p:PreservedSupport202)out[n++]=p;
 for(const auto& p:EnrolledSupport308)out[n++]=p;
 return out;
}();
}
