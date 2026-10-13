#pragma once
#include <cstdint>
namespace fvr::bc2 {
// One bounded plan shared by wire validation, recording and player input.
// Five is the explicit full-magazine/last-round experiment, not a wider
// permission for arbitrary pulse counts or renewed diagnostic deadlines.
constexpr std::uint32_t OrdinaryBoltProbeDurationMs(unsigned cycles)noexcept {
    return cycles==1||cycles==2?30000u:cycles==5?60000u:0u;
}
}
