#pragma once
#include <cstdint>
namespace fvr::bc2 {
constexpr bool ValidBodyInventoryConfig(std::uint32_t flags)noexcept {
    // Explicit real tracked session. Synthetic action/reload fixtures retain
    // their original waist geometry and cannot accidentally grab body slots.
    return !(flags&0x10000000u)||((flags&0xffu)==9&&(flags&0x2197800u)==0x2197800u&&!(flags&0x6dc68000u));
}
}
