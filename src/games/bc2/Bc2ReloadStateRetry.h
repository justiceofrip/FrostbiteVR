#pragma once
#include "Bc2ReloadState.h"
namespace fvr::bc2 {
struct ReloadStateRetryAttempt {
    bool attempted=false,recovered=false;
    ReloadStateStatus first=ReloadStateStatus::InvalidArguments,second=ReloadStateStatus::InvalidArguments;
};
// Re-read one torn full snapshot, using the same expected owner and the original
// observation deadline. No partial snapshot survives and no input is renewed.
template<class Read,class Clock>
ReloadStateResult ReadReloadStateCoherent(Read&& read,Clock&& clock,std::int64_t first,
    std::int64_t deadline,ReloadStateRetryAttempt& attempt)noexcept {
    attempt={};if(first<=0||deadline<=first)return {};
    auto result=read(first);attempt.first=result.status;
    if(result.snapshot||result.status!=ReloadStateStatus::ChangedDuringRead)return result;
    attempt.attempted=true;const auto next=clock();
    if(next<first||next>=deadline)return result;
    result=read(next);attempt.second=result.status;attempt.recovered=result.snapshot.has_value();return result;
}
}
