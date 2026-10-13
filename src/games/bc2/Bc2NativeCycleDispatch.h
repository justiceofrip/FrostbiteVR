#pragma once
#include <cstdint>
namespace fvr::bc2 {
enum class NativeCycleStepRoute {OrdinaryEmptyControl,IssuedHold,GuardedRecovery};
// A normal cycle frame also wraps Watching/Complete/Releasing Updates. Those
// callbacks must still reach the reviewed state-2 auto-reload inhibitor. Only
// an actually issued hold, or the separate continuous guard, bypasses it.
inline NativeCycleStepRoute SelectNativeCycleStepRoute(bool frame,bool normalDecision,bool issuedHold)noexcept {
    return !frame?NativeCycleStepRoute::OrdinaryEmptyControl:
        !normalDecision?NativeCycleStepRoute::GuardedRecovery:
        issuedHold?NativeCycleStepRoute::IssuedHold:NativeCycleStepRoute::OrdinaryEmptyControl;
}
// The read scope exits once, then the selecting scope enters once. Any other
// callback registration/exit invalidates the original native pre-read, even
// when that callback has already drained before the exclusive barrier begins.
inline bool NativeCycleSelectionReadCurrent(std::uint64_t observed,std::uint64_t current)noexcept {
    return observed&&observed<=UINT64_MAX-2&&current==observed+2;
}
}
