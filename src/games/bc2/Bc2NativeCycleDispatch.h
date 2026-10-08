#pragma once
#include <cstdint>
namespace fvr::bc2 {
// The read scope exits once, then the selecting scope enters once. Any other
// callback registration/exit invalidates the original native pre-read, even
// when that callback has already drained before the exclusive barrier begins.
inline bool NativeCycleSelectionReadCurrent(std::uint64_t observed,std::uint64_t current)noexcept {
    return observed&&observed<=UINT64_MAX-2&&current==observed+2;
}
}
