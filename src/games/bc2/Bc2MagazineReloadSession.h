#pragma once
#include <cstdint>
namespace fvr::bc2 {
enum class MagazineReloadSession:std::uint32_t {None=0,GateInsertProbe=1,GateCancelProbe=2,Physical=3,PhysicalConsumerProbe=4,OriginalReturnProbe=5,FullReturnProbe=6};
// Construction policy only; admission still requires current native idle counts,
// exact owner/hand evidence and committed suppression. No motion script in mode3.
constexpr bool MagazineDetachedSessionEnabled(std::uint32_t mode)noexcept {
    return mode==static_cast<std::uint32_t>(MagazineReloadSession::Physical)||
        mode==static_cast<std::uint32_t>(MagazineReloadSession::FullReturnProbe);
}
constexpr bool ValidMagazineReloadSession(std::uint32_t mode,std::uint32_t flags,std::uint32_t duration)noexcept {
    if(!mode)return true;
    // Independent explicit 30 s diagnostic. Exact existing tracked hand path,
    // with no physical/SPAS/holster/visibility/optic/death/equip/sight fixtures.
    constexpr std::uint32_t required=9u|0x197800u;
    constexpr std::uint32_t allowed=required|0x200u;
    if(mode<=2)return duration==30000&&(flags&required)==required&&(flags&0xffu)==9&&!(flags&~allowed);
    constexpr auto physical=required|0x2000000u;
    if(mode==4||mode==5||mode==6)return duration==30000&&(flags&physical)==physical&&(flags&0xffu)==9&&!(flags&~(physical|0x200u));
    // Normal physical mode can coexist with sight/body interactions. Continuous
    // sessions carry the host PID in the duration union; no synthetic probes.
    constexpr auto normal=physical|0x200u|0x400u|0x200000u|0x10000000u;
    return mode==3&&(flags&physical)==physical&&(flags&0xffu)==9&&!(flags&~normal)&&
        ((flags&0x400u)?duration!=0:(duration>=1000&&duration<=60000));
}
}
