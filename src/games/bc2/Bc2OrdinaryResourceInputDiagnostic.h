#pragma once
#include "Bc2ResourceMagazineSession.h"
namespace fvr::bc2 {
constexpr bool ValidOrdinaryResourceInputSession(std::uint32_t enabled,std::uint32_t flags,std::uint32_t duration,
    std::uint32_t mode,std::uint32_t pump,std::uint32_t boat,std::uint32_t body,
    std::uint32_t stockShot,std::uint32_t finiteBolt,std::uint32_t ordinaryBolt)noexcept {
    return !enabled||(enabled==1&&flags==0x12197809u&&duration==60000&&
        ValidResourceMagazineSession(mode,flags,duration)&&!pump&&!boat&&!body&&!stockShot&&!finiteBolt&&!ordinaryBolt);
}
}
