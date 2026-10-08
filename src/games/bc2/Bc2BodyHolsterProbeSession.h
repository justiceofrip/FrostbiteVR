#pragma once
#include <cstdint>
namespace fvr::bc2 {
constexpr std::uint32_t BodyHolsterProbeFlag=0x80000000u;
constexpr bool ValidBodyHolsterProbeConfig(std::uint32_t flags,std::uint32_t duration)noexcept {
    // The body adapter requires the ordinary physical-reload consumer, which
    // remains inert with neutral left grip. No physical/native reload fixture.
    constexpr std::uint32_t required=BodyHolsterProbeFlag|0x12197800u|9u;
    return !(flags&BodyHolsterProbeFlag)||((flags&required)==required&&
        (flags&0xffu)==9u&&!(flags&~(required|0x200u))&&duration==15000);
}
}
