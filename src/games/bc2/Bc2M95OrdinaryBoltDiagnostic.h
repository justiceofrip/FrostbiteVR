#pragma once
#include "Bc2ResourceMagazineSession.h"
namespace fvr::bc2 {
// Input generation only. Ordinary consumers retain all claim/native authority.
constexpr bool ValidM95OrdinaryBoltInputSession(std::uint32_t cycles,std::uint32_t flags,std::uint32_t duration,
    std::uint32_t magazineSession,std::uint32_t pumpMode,std::uint32_t boatMode,std::uint32_t bodyDiagnostic,
    std::uint32_t stockShot,std::uint32_t finiteBolt)noexcept {
    return !cycles||(cycles<=2&&flags==0x12197809u&&duration==30000&&
        ValidResourceMagazineSession(magazineSession,flags,duration)&&
        !pumpMode&&!boatMode&&!bodyDiagnostic&&!stockShot&&!finiteBolt);
}
}
