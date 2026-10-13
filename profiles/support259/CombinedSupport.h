#pragma once
// Preserve the previously accepted SPAS anchor verbatim.
#define AuthoredSupports PreservedSupport202
#include "../checkpoint202/Bc2AuthoredSupports.generated.h"
#undef AuthoredSupports
#define AuthoredSupports EnrolledSupport259
#include "StaticSupports.generated.h"
#undef AuthoredSupports
namespace fvr::bc2::generated {
inline const auto AuthoredSupports=[] {
 std::array<AuthoredSupportProfile,PreservedSupport202.size()+EnrolledSupport259.size()> out{};
 std::size_t n=0;for(const auto& p:PreservedSupport202)out[n++]=p;
 for(const auto& p:EnrolledSupport259)out[n++]=p;
 return out;
}();
}
