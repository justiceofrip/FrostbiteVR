#include "NativeProbeConfig.h"
#include "Test.h"
#include <cstdio>
using fvr::bc2::ValidOrdinaryGrenadeInputSession;
int main(){
    constexpr unsigned flags=9u|0x800u|0x1000u|0x2000u|0x4000u|0x10000u|0x80000u|0x100000u|0x2000000u|0x10000000u;
    CHECK(sizeof(fvr::bc2::NativeProbeConfig)==1220);
    for(const auto optional:{0u,0x100u,0x200u,0x300u})CHECK(ValidOrdinaryGrenadeInputSession(3,flags|optional,20000,3,false,false,true));
    CHECK(!ValidOrdinaryGrenadeInputSession(3,flags,20000,3,false,false));
    CHECK(!ValidOrdinaryGrenadeInputSession(3,flags,20000,3,false,true)); // Fade availability grants no player mode.
    CHECK(!ValidOrdinaryGrenadeInputSession(3,flags,20000,3,true,false,true));
    for(unsigned bit=0;bit<32;++bit){const bool okay=ValidOrdinaryGrenadeInputSession(3,flags^(1u<<bit),20000,3,false,false,true);
        CHECK(okay==(bit==8||bit==9));}
    for(const auto bad:{0u,12000u,19999u,20001u,30000u,60000u})CHECK(!ValidOrdinaryGrenadeInputSession(3,flags,bad,3,false,false,true));
    for(unsigned magazine=0;magazine<9;++magazine)if(magazine!=3)CHECK(!ValidOrdinaryGrenadeInputSession(3,flags,20000,magazine,false,false,true));
    CHECK(!ValidOrdinaryGrenadeInputSession(4,flags,20000,3,false,true,true));
    CHECK(ValidOrdinaryGrenadeInputSession(4,flags,30000,3,false,false,true,true));
    CHECK(!ValidOrdinaryGrenadeInputSession(4,flags,30000,3,false,false,true,false));
    CHECK(!ValidOrdinaryGrenadeInputSession(4,flags,30000,3,false,true,false,true));
    CHECK(!ValidOrdinaryGrenadeInputSession(4,flags,30000,3,true,false,true,true));
    for(unsigned bit=0;bit<32;++bit)CHECK(ValidOrdinaryGrenadeInputSession(4,flags^(1u<<bit),30000,3,false,false,true,true)==(bit==8||bit==9));
    for(const auto bad:{0u,12000u,20000u,29999u,30001u,60000u})CHECK(!ValidOrdinaryGrenadeInputSession(4,flags,bad,3,false,false,true,true));
    CHECK(ValidOrdinaryGrenadeInputSession(0,0,0,0,false,false));
    CHECK(ValidOrdinaryGrenadeInputSession(1,flags,20000,3,false,false));
    CHECK(!ValidOrdinaryGrenadeInputSession(2,flags,20000,3,false,false));
    CHECK(ValidOrdinaryGrenadeInputSession(2,flags,20000,3,false,true));
    std::puts("Player mode3 config PASS: explicit compile admission, exact20s flags/session, no other diagnostics, matched1220-byte ABI and modes0/1/2.");return 0;
}
