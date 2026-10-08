#pragma once
#include "Bc2MagazineReloadSession.h"
#include "Bc2BodyInventorySession.h"

namespace fvr::bc2 {
// Construction only. Exact native profile, inventory, current owner, request,
// geometry and actual completion checks stay in their ordinary consumers.
// Body inventory is explicit because it supplies the native item lifetime and
// chest cache. Mode3 already permits sight and host-lifetime tracked input.
constexpr bool ValidResourceMagazineSession(std::uint32_t mode,std::uint32_t flags,std::uint32_t durationOrHostPid)noexcept {
    return mode==static_cast<std::uint32_t>(MagazineReloadSession::Physical)&&
        (flags&0x10000000u)&&ValidBodyInventoryConfig(flags)&&
        ValidMagazineReloadSession(mode,flags,durationOrHostPid);
}
// Separate finite input fixture; never accepted by the ordinary mode3 gate.
constexpr bool ValidResourceInventorySession(std::uint32_t mode,std::uint32_t flags,std::uint32_t duration)noexcept {
    return mode==7&&ValidBodyInventoryConfig(flags)&&ValidMagazineReloadSession(mode,flags,duration);
}
}
