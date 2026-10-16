#include "NativeProbeConfig.h"
#include "OrdinaryGrenadeFadeProbe.h"
#include "SceneRecoveryProbe.h"
#include "fvr/interaction/AimFrame.h"
#include "Test.h"
#include <cstdio>
using namespace fvr;
namespace {
int LeadCases(){
    CHECK(probe::GrenadeFadeLead(10000,6000));CHECK(probe::GrenadeFadeLead(16000,4000));
    CHECK(!probe::GrenadeFadeLead(10000,6001));CHECK(!probe::GrenadeFadeLead(16000,3999));
    CHECK(!probe::GrenadeFadeLead(10000,10001));
    // Actual death02 first native callback left only about3.02s; remain rejected.
    CHECK(!probe::GrenadeFadeLead(481569812,481566792));
    CHECK(probe::GrenadeFadeLead(481572812,481566792));
    std::puts("PASS: lead4..12s exact bounds, past window rejection, measured3.02s rejection and proposed6.02s admission");
    return 0;
}
}
int main(int argc,char** argv){
    if(argc>1){CHECK(argc==2&&std::string_view(argv[1])=="--lead-only");return LeadCases();}
    constexpr unsigned flags=9u|0x800u|0x1000u|0x2000u|0x4000u|0x10000u|0x80000u|0x100000u|0x2000000u|0x10000000u;
    CHECK(sizeof(bc2::NativeProbeConfig)==1220);
    CHECK(bc2::ValidOrdinaryGrenadeInputSession(1,flags,20000,3,false,false));
    CHECK(!bc2::ValidOrdinaryGrenadeInputSession(2,flags,20000,3,false,false));
    CHECK(bc2::ValidOrdinaryGrenadeInputSession(2,flags,20000,3,false,true));
    CHECK(!bc2::ValidOrdinaryGrenadeInputSession(3,flags,20000,3,false,true));
    CHECK(!bc2::ValidOrdinaryGrenadeInputSession(2,flags,60000,3,false,true));
    CHECK(!bc2::ValidOrdinaryGrenadeInputSession(2,flags,20000,3,true,true));
    CHECK(!bc2::ValidOrdinaryGrenadeInputSession(2,flags|0x20000,20000,3,false,true));
    CHECK(!bc2::ValidOrdinaryGrenadeInputSession(2,flags|0x400,20000,3,false,true));
    for(const auto bad:{L"--death-probe",L"--ordinary-grenade-probe",L"--scene-recovery-probe",L"--controls-fire",L"--body-props-cache"})CHECK(!probe::OrdinaryGrenadeFadeOption(bad));
    CHECK(probe::OrdinaryGrenadeFadeOption(L"--fade-window"));
    CHECK(probe::GrenadeFadeWindow(10000,8000,250));
    CHECK(!probe::GrenadeFadeWindow(UINT64_MAX-7999,8000,250));
    CHECK(!probe::GrenadeFadeWindow(10000,10000,250));
    CHECK(!probe::GrenadeFadeWindow(10000,8000,100));
    CHECK(LeadCases()==0);
    // Model an arbitrarily fast producer before the window: old unrestricted
    // scheduling spends240 immediately; this schedule retains exactly32 slots.
    unsigned pairs=0;
    for(unsigned now=0;now<10000;++now)if(probe::GrenadeFadeRequest(now,10000,pairs))++pairs;
    CHECK(pairs==208);
    for(unsigned slot=0;slot<32;++slot){CHECK(probe::GrenadeFadeRequest(10000+slot*250,10000,pairs));++pairs;}
    CHECK(pairs==240);CHECK(!probe::GrenadeFadeRequest(18000,10000,pairs));
    // Late/slow prefix still permits post-window drain rather than claiming all
    //240 pairs existed. Input remains independent of pair availability.
    CHECK(probe::GrenadeFadeRequest(18000,10000,220));
    runtime::TrackingFrame tracking{};tracking.focused=tracking.headValid=true;
    tracking.generation=tracking.spaceGeneration=1;tracking.predictedNs=1000000000;
    probe::GrenadeFadeGesture gesture;unsigned presses=0,releases=0,trigger=0;bool previous=false;
    for(unsigned ms=0;ms<19000;ms+=10){
        auto input=probe::NeutralSceneInput(tracking,ms+1);
        const bool held=gesture.Held(ms,8000,ms>=8250);
        probe::OrdinaryGrenadeFadeInput(input,ms,ms,8000,held);
        CHECK(probe::OrdinaryGrenadeFadeControls(input,ms,held));
        CHECK(input.head.orientation.w==1&&input.head.orientation.x==0);
        presses+=held&&!previous;releases+=!held&&previous;previous=held;
        trigger+=input.hands[0].trigger!=0||input.hands[1].trigger!=0;
        if(ms>=8000){const auto aim=interaction::ControllerAim(input.referenceHead,input.hands[1].aim);
            CHECK(aim);CHECK(Near(aim->pitch,-1.474803209f));CHECK(Near(aim->yaw,0));}
        if(ms==9000){input.hands[1].trigger=1;CHECK(!probe::OrdinaryGrenadeFadeControls(input,ms,held));}
    }
    CHECK(presses==1&&releases==1&&trigger==0&&gesture.Admitted());
    probe::GrenadeFadeGesture missing;
    CHECK(!missing.Held(9000,8000,false));CHECK(!missing.Held(9010,8000,true));
    CHECK(!missing.Admitted()&&missing.Decided());
    probe::GrenadeFadeGesture late;
    CHECK(!late.Held(9101,8000,true));CHECK(!late.Held(9200,8000,true));
    probe::GrenadeFadeGesture bounds;
    CHECK(!bounds.Held(8999,8000,true));CHECK(bounds.Held(9000,8000,true));
    CHECK(bounds.Held(9649,8000,true));CHECK(!bounds.Held(9650,8000,true));
    CHECK(!bounds.Held(14000,8000,true));
    std::puts("PASS: ABI/config isolation, option/window bounds, retained32-pair window, one gesture, missing/late capture suppression, native downward aim, no trigger");
}
