#pragma once
#include <cstdint>
namespace fvr::bc2 {
// Explicit physical diagnostic only: exactly two ordinary 100ms trigger pulses.
// Saved native run224357 reached SPAS state7 with 0.119s remaining at the old
// 4.5s pulse; that pulse ended before the native pump cycle completed. Space the
// second pulse at5.0s. This schedule does not assert that either shot fired:
// the physical probe must still observe enough real native empty slots.
inline float PhysicalReloadPreparationTrigger(std::uint64_t elapsedMs,bool enabled)noexcept {
    return enabled&&((elapsedMs>=3300&&elapsedMs<3400)||(elapsedMs>=5000&&elapsedMs<5100))?1.f:0.f;
}
}
