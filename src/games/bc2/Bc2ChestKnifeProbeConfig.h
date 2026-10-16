#pragma once
namespace fvr::bc2 {
constexpr bool ValidChestKnifeProbe(unsigned mode,unsigned flags,unsigned duration,
    unsigned magazineSession,bool other,bool compiled)noexcept {
    constexpr unsigned required=9u|0x197800u|0x2000000u|0x10000000u;
    return !mode||(compiled&&(mode==1||mode==2)&&duration==20000&&magazineSession==3&&
        (flags&~0x200u)==required&&!other);
}
}
