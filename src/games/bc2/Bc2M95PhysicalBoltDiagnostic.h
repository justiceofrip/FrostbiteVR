#pragma once
#include <cstdint>
namespace fvr::bc2 {
inline bool ValidM95PhysicalBoltSession(std::uint32_t cycles,std::uint32_t flags,std::uint32_t duration,
    std::uint32_t magazineSession,std::uint32_t pumpMode,std::uint32_t boatMode,std::uint32_t bodyDiagnostic,
    std::uint32_t stockShot)noexcept {
    return cycles==0||(cycles<=2&&flags==0x197809u&&duration==30000u&&!magazineSession&&!pumpMode&&!boatMode&&!bodyDiagnostic&&!stockShot);
}
}
