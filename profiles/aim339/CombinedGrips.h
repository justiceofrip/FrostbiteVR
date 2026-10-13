#pragma once
#define AuthoredGrips Preserved202Grips
#include "../checkpoint202/Bc2AuthoredGrips.generated.h"
#undef AuthoredGrips
#define AuthoredGrips Scar339Grips
#include "ScarAuthoredGrip.h"
#undef AuthoredGrips
namespace fvr::bc2::generated {
inline const auto AuthoredGrips=[] {
 std::array<AuthoredGripProfile,Preserved202Grips.size()+Scar339Grips.size()> out{};
 std::size_t n=0;for(const auto& p:Preserved202Grips)out[n++]=p;
 for(const auto& p:Scar339Grips)out[n++]=p;return out;
}();
}
