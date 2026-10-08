#include "BoatFireProbe.h"
#include <cassert>
#include <iostream>
int main(){
    using fvr::probe::BoatFireProbe;
    assert(!BoatFireProbe::Trigger(0));assert(!BoatFireProbe::Trigger(1999));
    assert(BoatFireProbe::Trigger(2000));assert(BoatFireProbe::Trigger(2079));
    assert(!BoatFireProbe::Trigger(2080));assert(!BoatFireProbe::Trigger(6500));
    assert(!BoatFireProbe::Trigger(UINT64_MAX));
    unsigned active=0,rises=0;bool previous=false;
    for(std::uint64_t ms=0;ms<25000;++ms){const bool now=BoatFireProbe::Trigger(ms);active+=now;rises+=now&&!previous;previous=now;}
    assert(active==80&&rises==1);
    BoatFireProbe captures;unsigned count=0;
    for(std::uint64_t ms=0;ms<25000;++ms){count+=captures.Capture(ms);assert(!captures.Capture(ms));}
    assert(count==8);
    BoatFireProbe skipped;assert(skipped.Capture(2600));assert(!skipped.Capture(2600));
    assert(!skipped.Capture(2000));assert(skipped.Capture(6000));assert(!skipped.Capture(UINT64_MAX));
    std::cout<<"4 boat fire schedule groups PASS (boundaries, single window, bounded captures, missed targets)\n";
}
