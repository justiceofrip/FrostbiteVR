#pragma once
#include <cstdint>
namespace fvr::bc2 {
enum class ReloadKeepAliveResult:unsigned {Accepted,Deferred,Rejected};
// Deferred never renews authority. The last accepted control keeps its original expiry.
inline bool ReloadKeepAliveDeferredWithinOriginalDeadline(ReloadKeepAliveResult result,
 std::int64_t now,std::int64_t originalDeadline)noexcept {
 return result==ReloadKeepAliveResult::Deferred&&now>0&&now<originalDeadline;
}
}
