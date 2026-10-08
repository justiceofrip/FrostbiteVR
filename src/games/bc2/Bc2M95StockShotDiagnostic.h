#pragma once
#include <cstdint>
namespace fvr::bc2 {
constexpr bool ValidM95StockShotSession(unsigned mode,std::uint32_t flags,std::uint32_t duration,
 std::uint32_t magazine,unsigned pump,unsigned boat,unsigned holster)noexcept {
 return mode==0||(mode==1&&flags==0x197809u&&duration==15000&&magazine==0&&pump==0&&boat==0&&holster==0);
}
}
