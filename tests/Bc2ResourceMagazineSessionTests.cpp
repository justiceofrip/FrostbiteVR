#include "Bc2ResourceMagazineSession.h"
#include "Test.h"
#include <cstdio>
#include <initializer_list>
using namespace fvr::bc2;
int main(){
    constexpr unsigned normal=9u|0x197800u|0x2000000u|0x10000000u;
    for(unsigned extras:{0u,0x200u,0x200000u,0x200200u}){
        CHECK(ValidResourceMagazineSession(3,normal|extras,30000));
        CHECK(ValidResourceMagazineSession(3,normal|extras|0x400u,44824));
        CHECK(!ValidResourceMagazineSession(3,normal|extras|0x400u,0));
    }
    for(unsigned mode:{0u,1u,2u,4u,5u,6u,7u,8u,9u})
        CHECK(!ValidResourceMagazineSession(mode,normal,30000));
    for(unsigned required:{0x800u,0x1000u,0x2000u,0x4000u,0x10000u,0x80000u,0x100000u,0x2000000u,0x10000000u})
        CHECK(!ValidResourceMagazineSession(3,normal&~required,30000));
    for(unsigned forbidden:{0x100u,0x8000u,0x20000u,0x40000u,0x400000u,0x800000u,0x1000000u,0x4000000u,0x8000000u,0x20000000u,0x40000000u,0x80000000u})
        CHECK(!ValidResourceMagazineSession(3,normal|forbidden,30000));
    for(unsigned duration:{0u,999u,60001u})CHECK(!ValidResourceMagazineSession(3,normal,duration));
    CHECK(ValidResourceMagazineSession(3,normal,1000));CHECK(ValidResourceMagazineSession(3,normal,60000));
    // Existing bounded private fixtures keep their own contract; enabling the
    // ordinary backend does not reinterpret those modes as user hand input.
    CHECK(ValidMagazineReloadSession(4,normal,30000));CHECK(ValidMagazineReloadSession(5,normal,30000));
    CHECK(ValidResourceInventorySession(7,normal,60000));CHECK(ValidResourceInventorySession(7,normal|0x200u,60000));
    for(unsigned mode:{0u,1u,2u,3u,4u,5u,6u,8u,9u})CHECK(!ValidResourceInventorySession(mode,normal,60000));
    for(unsigned duration:{0u,1000u,30000u,59999u,60001u})CHECK(!ValidResourceInventorySession(7,normal,duration));
    for(unsigned bit:{0x400u,0x100u,0x200000u,0x4000000u,0x80000000u})CHECK(!ValidResourceInventorySession(7,normal|bit,60000));
    CHECK(!ValidResourceInventorySession(7,normal&~0x10000000u,60000));
    std::puts("Ordinary resource mode: explicit body/shell/sight/host coexistence and diagnostic exclusions passed.");return 0;
}
