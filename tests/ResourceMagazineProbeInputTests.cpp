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
    for(unsigned which=0;which<7;++which){
        probe::ResourceSceneResume resume;std::uint64_t id=1,issued=1000,deadline=1500,now=1100;
        if(which==0)id=2;if(which==1)issued=0;if(which==2)issued=1200;
        if(which==3)deadline=now;if(which==4)deadline=999;if(which==5)deadline=2001;
        if(which==6)now=1501;
        CHECK(!resume.Accept(id,issued,deadline,now));CHECK(!resume.Accepted());
        CHECK(!resume.Step(now).use&&resume.Step(now).select==0);
    }
    probe::ResourceSceneResume resume;
    CHECK(resume.Accept(1,1000,1500,1100));CHECK(!resume.Accept(1,1101,1500,1101));
    unsigned useMs=0,selectionMs=0;
    for(std::uint64_t elapsed=0;elapsed<=75000;++elapsed){
        const auto step=resume.Step(1100+elapsed);
        CHECK(step.use==(elapsed>=400&&elapsed<500));
        CHECK(step.select==0); // Scene replacement uses physical shoulder draw.
        if(step.use)++useMs;if(step.select)++selectionMs;
    }
    CHECK(useMs==100&&selectionMs==0);
    CHECK(!resume.Step(1500).use&&resume.Failed());
    CHECK(!resume.Step(76000).select&&!resume.Accept(1,76000,76100,76000));
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
