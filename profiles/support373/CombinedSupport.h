#pragma once
#define AuthoredSupports PreservedSupport202
#include "../checkpoint202/Bc2AuthoredSupports.generated.h"
#undef AuthoredSupports
#define AuthoredSupports EnrolledSupport312
#include "../support312/StaticSupports.generated.h"
#undef AuthoredSupports
#define AuthoredSupports BoundedSupport372
#include "../bounded-support372/CandidateSupports.generated.h"
#undef AuthoredSupports
namespace fvr::bc2::generated {
inline const auto AuthoredSupports=[] {
 std::array<AuthoredSupportProfile,PreservedSupport202.size()+EnrolledSupport312.size()+BoundedSupport372.size()> out{};
 std::size_t n=0;for(const auto& p:PreservedSupport202)out[n++]=p;
 for(const auto& p:EnrolledSupport312)out[n++]=p;
 for(const auto& p:BoundedSupport372)out[n++]=p;
 return out;
}();
}
