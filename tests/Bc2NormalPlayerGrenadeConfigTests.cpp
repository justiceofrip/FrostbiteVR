#include "NativeProbeConfig.h"
#include "Test.h"
#include <cstdio>
using namespace fvr::bc2;
int main(){
    constexpr unsigned required=9u|0x197800u|0x2000000u|0x10000000u|0x400u;
    NativeProbeConfig c;CHECK(sizeof(c)==1220&&c.bytes==1220&&c.normalPlayerGrenades==0&&c.chestKnifeProbe==0);
    CHECK(c.ordinaryGrenadeInput==0&&c.bytes!=1204&&c.bytes!=1208); // Old peers fail their exact size admission.
    CHECK(ValidNormalPlayerGrenades(0,0,0,0,true,false)); // Existing callers do not enable this path.
    for(const auto optional:{0u,0x200u,0x200000u,0x200200u})
        CHECK(ValidNormalPlayerGrenades(1,required|optional,123,3,false,true));
    for(unsigned bit=0;bit<32;++bit)
        CHECK(ValidNormalPlayerGrenades(1,required^(1u<<bit),123,3,false,true)==(bit==9||bit==21));
    CHECK(!ValidNormalPlayerGrenades(1,required,123,3,false,false));
    CHECK(!ValidNormalPlayerGrenades(1,required,0,3,false,true));
    CHECK(!ValidNormalPlayerGrenades(1,required,123,3,true,true));
    CHECK(!ValidNormalPlayerGrenades(2,required,123,3,false,true));
    for(unsigned session=0;session<9;++session)if(session!=3)
        CHECK(!ValidNormalPlayerGrenades(1,required,123,session,false,true));
    // A finite mode3 recipe is neither normal startup nor normal compile authority.
    CHECK(!ValidNormalPlayerGrenades(1,required&~0x400u,20000,3,false,true));
    CHECK(!ValidOrdinaryGrenadeInputSession(3,required,20000,3,false,false,true));
    CHECK(!ValidOrdinaryGrenadeInputSession(3,required&~0x400u,20000,3,false,false,false));
    std::puts("PASS: normal player grenade configuration; explicit compile/startup, continuous host, no fixture, matched1220 ABI.");
}
