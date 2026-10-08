#include "ResourceMagazineProbeInput.h"
#include "Test.h"
#include <cstdio>
using namespace fvr;
int main(){
    CHECK(probe::ResourceMagazineScheduleVersion==2);
    CHECK(probe::ResourceMagazineHeldScheduleVersion==3);
    unsigned accepted=0;
    for(unsigned bits=0;bits<16;++bits){
        const bool held=bits&1,inventory=bits&2,exit=bits&4,direction=bits&8;
        const bool valid=probe::ValidResourceMagazineStartHeld(held,inventory,exit,direction);
        CHECK(valid==(!held||(inventory&&!exit&&!direction)));accepted+=valid;
    }
    CHECK(accepted==9);
    // Examine every millisecond, including exact pulse edges and post-run drain.
    for(std::uint64_t now=0;now<=75000;++now){
        for(float direction:{-1.f,1.f})for(bool vehicle:{false,true}){
            const auto ordinary=probe::ResourceMagazineSchedule(now,vehicle,direction);
            CHECK(ordinary.use==(vehicle&&now>=400&&now<500));
            CHECK(ordinary.select==(((now>=1000&&now<1800)||(vehicle&&now>=3000&&now<3300))?direction:0.f));
            const auto neutral=probe::ResourceMagazineSchedule(now,vehicle,direction,true);
            CHECK(!neutral.use&&neutral.select==0);
        }
        interaction::InputFrame input;input.generation=93;input.spaceGeneration=7;
        input.hands[0].held=interaction::Primary;input.hands[1].stickY=-1;
        input.hands[0].squeeze=.4f;input.hands[1].trigger=.3f;input.hands[1].held=interaction::Secondary;
        probe::ResourceMagazineInput(input,now,false,1,true);
        CHECK(input.hands[0].held==0&&input.hands[1].stickY==0);
        CHECK(input.generation==93&&input.spaceGeneration==7);
        CHECK(input.hands[0].squeeze==.4f&&input.hands[1].trigger==.3f&&input.hands[1].held==interaction::Secondary);
    }
    std::puts("Resource receiver:16 admission combinations;75s exact setup/neutral boundaries; unrelated fields preserved.");
}
