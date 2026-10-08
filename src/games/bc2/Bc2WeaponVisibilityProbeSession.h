#pragma once
#include <cstdint>
namespace fvr::bc2 {
constexpr std::uint32_t WeaponVisibilityProbeFlag=0x40000000u;
constexpr bool ValidWeaponVisibilityProbeConfig(std::uint32_t flags,std::uint32_t duration)noexcept {
    constexpr std::uint32_t required=WeaponVisibilityProbeFlag|0x197800u|9u;
    // Stream + controller aim/body/pose/hands/muzzle/support only. Mirror pacing
    // is harmless; reject every unrelated diagnostic, continuous mode and HUD.
    return !(flags&WeaponVisibilityProbeFlag)||((flags&required)==required&&
        (flags&0xffu)==9u&&!(flags&~(required|0x200u))&&duration==15000);
}
}
