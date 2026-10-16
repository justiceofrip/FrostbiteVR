#include "ContextualToolProbeReceiver.h"
#include "NativeProbeConfig.h"
#include "Test.h"
#include <algorithm>
#include <sstream>
using namespace fvr;
namespace {
constexpr std::int64_t Start=1000000000,Frequency=1000000000;
interaction::InputFrame Input(unsigned ms){interaction::InputFrame i;i.generation=ms/10+2;i.spaceGeneration=1;
    i.predictedNs=Start+std::int64_t(ms)*1000000;i.focused=i.headValid=true;
    for(auto& h:i.hands){h.active=interaction::Components;h.gripTracked=h.aimTracked=true;}return i;
}
bc2::ContextualToolProbeWitness Witness(const interaction::InputFrame& i,probe::ToolRecipePhase phase){
    bc2::ContextualToolProbeWitness w;w.pid=1;w.creation=2;w.input=i.generation-1;w.space=1;
    w.actor=3;w.actorGeneration=4;w.weak=5;w.inventory=6;w.originalWeapon=7;w.originalData=8;w.toolWeapon=9;w.toolData=10;
    w.observedNs=std::uint64_t(i.predictedNs-10000000);w.deadlineNs=std::uint64_t(i.predictedNs+90000000);
    constexpr std::string_view name="Objects/Weapons/Handheld/LaserDesignator/SP_LaserDesignator_Dummy";
    std::copy(name.begin(),name.end(),w.toolAsset.begin());
    if(phase==probe::ToolRecipePhase::AwaitEmpty||phase==probe::ToolRecipePhase::GripNeutral){w.empty=1;w.selectedSlot=1;}
    else if(phase==probe::ToolRecipePhase::AwaitHide||phase==probe::ToolRecipePhase::Complete){
        w.empty=1;w.selectedSlot=1;w.equipCommits=w.selectedObservations=w.returnCommits=w.hideCompletions=1;w.lastBodyReturn=11;
    }else {
        w.selectedSlot=4;w.bodyOperation=11;w.equipCommits=w.selectedObservations=1;
        if(phase!=probe::ToolRecipePhase::ReleaseNeutral&&phase!=probe::ToolRecipePhase::ReturnClick){w.held=1;w.holdToken=12;}
    }
    return w;
}
void Step(probe::ContextualToolProbeReceiver& r,interaction::InputFrame& i,unsigned ms,const bc2::ContextualToolProbeWitness* w,
    ipc::MenuMode menu=ipc::MenuMode::Gameplay,bool delivered=true){
    ipc::MenuState state{i.generation,1,i.predictedNs+100000000,menu,1280,720,1,0};
    r.Update(i,ms,i.predictedNs,1,2,w,ipc::ChannelResult::Ok,state,i.predictedNs,Frequency);
    r.Published(i,true,delivered,unsigned(delivered?ipc::ChannelResult::Ok:ipc::ChannelResult::Busy),i.predictedNs);
}
int ConfigAndLease(){
    constexpr unsigned flags=9u|0x197800u|0x2000000u|0x10000000u;
    CHECK(bc2::ValidContextualToolFixture(flags,20000,3,false));CHECK(sizeof(bc2::NativeProbeConfig)==1220);
    CHECK(!bc2::ValidContextualToolFixture(flags,19000,3,false));CHECK(!bc2::ValidContextualToolFixture(flags,20000,0,false));
    CHECK(!bc2::ValidContextualToolFixture(flags,20000,3,true));CHECK(!bc2::ValidContextualToolFixture(flags|0x400u,20000,3,false));
    CHECK(!bc2::ValidContextualToolFixture(flags&~0x10000000u,20000,3,false));
    bc2::ContextualToolProbePublication publication;auto i=Input(0);auto w=Witness(i,probe::ToolRecipePhase::AwaitEmpty);
    publication.Publish(w);auto current=publication.Read(i.predictedNs);CHECK(current&&current->input==w.input);
    CHECK(!publication.Read(std::int64_t(w.deadlineNs)));CHECK(!publication.Read(std::int64_t(w.observedNs-1)));
    // Worker read time never substitutes for the packet's original source time.
    current=publication.Read(i.predictedNs+50000000);CHECK(current&&current->observedNs==w.observedNs&&current->deadlineNs==w.deadlineNs);
    publication.Publish({});CHECK(!publication.Read(i.predictedNs));return 0;
}
int ActualHoldAndNewHide(){probe::ContextualToolProbeReceiver r;
    for(unsigned ms=0;ms<18000;ms+=10){auto i=Input(ms);auto w=Witness(i,r.Phase());Step(r,i,ms,&w);
        CHECK(i.hands[0].held==0&&i.hands[0].squeeze==0);
        for(const auto& h:i.hands)CHECK(h.trigger==0&&h.stickX==0&&h.stickY==0);}
    CHECK(r.Completed());std::ostringstream report;r.Report(report);
    CHECK(report.str().find("\"click_presses\":2,\"click_releases\":2")!=std::string::npos);
    CHECK(report.str().find("\"hold_token\":12")!=std::string::npos);return 0;
}
int MissingForeignAndStaleNeverDraw(){for(unsigned kind=0;kind<6;++kind){probe::ContextualToolProbeReceiver r;
    for(unsigned ms=0;ms<18000;ms+=10){auto i=Input(ms);auto w=Witness(i,r.Phase());
        if(kind==5)w.toolAsset[std::string_view("Objects/Weapons/Handheld/LaserDesignator/SP_LaserDesignator").size()]='\0'; // Different authored effects family.
        if(kind==1)w.toolAsset[0]='X';if(kind==2)w.deadlineNs=std::uint64_t(i.predictedNs);
        if(kind==3)w.input=i.generation;if(kind==4)w.creation++;
        Step(r,i,ms,kind?&w:nullptr);CHECK(i.hands[1].held==0&&i.hands[1].squeeze==0);}
    CHECK(!r.Completed());}return 0;
}
int UnresolvedNeverRetriedOrAcknowledged(){probe::ContextualToolProbeReceiver r;
    for(unsigned ms=0;ms<18000;ms+=10){auto i=Input(ms);auto w=Witness(i,r.Phase());
        if(r.Phase()>=probe::ToolRecipePhase::DrawClick){w.held=w.holdToken=w.empty=0;w.bodyOperation=11;w.selectedSlot=1;w.equipCommits=1;w.selectedObservations=0;}
        Step(r,i,ms,&w);if(r.Phase()==probe::ToolRecipePhase::Failed)CHECK(i.hands[1].held==0&&i.hands[1].squeeze==0);}
    CHECK(!r.Completed());std::ostringstream report;r.Report(report);
    CHECK(report.str().find("\"click_presses\":1,\"click_releases\":1")!=std::string::npos);
    CHECK(report.str().find("\"return_press_input\":0")!=std::string::npos);return 0;
}
int LossAndWrongHideStayFailed(){for(unsigned kind=0;kind<5;++kind){probe::ContextualToolProbeReceiver r;bool changed=false;
    for(unsigned ms=0;ms<18000;ms+=10){auto i=Input(ms);auto w=Witness(i,r.Phase());auto menu=ipc::MenuMode::Gameplay;
        if(!changed&&r.Phase()==probe::ToolRecipePhase::Hold&&kind<3){changed=true;
            if(kind==0)w.actorGeneration++;if(kind==1)menu=ipc::MenuMode::Menu;if(kind==2)w.holdToken++;}
        if(r.Phase()==probe::ToolRecipePhase::AwaitHide){changed=true;if(kind==3)w.lastBodyReturn=10;if(kind==4){w.empty=0;w.bodyOperation=11;}}
        Step(r,i,ms,&w,menu);}
    CHECK(changed&&!r.Completed());}return 0;
}
int BusyAndPairPacing(){probe::ContextualToolProbeReceiver r;bool missed=false;
    for(unsigned ms=0;ms<18000;ms+=10){auto i=Input(ms);auto w=Witness(i,r.Phase());
        const bool miss=!missed&&r.Phase()==probe::ToolRecipePhase::DrawClick;Step(r,i,ms,&w,ipc::MenuMode::Gameplay,!miss);missed|=miss;}
    CHECK(missed&&r.Completed());std::ostringstream report;r.Report(report);CHECK(report.str().find("\"publication_busy\":1")!=std::string::npos);
    unsigned consumed=0;std::uint64_t last=0,pending=0,maxGap=0;bool waiting=false;
    for(std::uint64_t ms=0;ms<18000;++ms){
        if(waiting&&ms>=pending){if(consumed)maxGap=(std::max)(maxGap,ms-last);last=ms;++consumed;waiting=false;}
        if(!waiting&&probe::ContextualToolProbeReceiver::PairRequestDue(ms,consumed)){pending=ms+20;waiting=true;}
        if(ms==13000)CHECK(consumed<240&&last>=12900);}
    CHECK(consumed==240&&maxGap<100);CHECK(!probe::ContextualToolProbeReceiver::PairRequestDue(18000,240));return 0;
}
}
int main(){if(ConfigAndLease()||ActualHoldAndNewHide()||MissingForeignAndStaleNeverDraw()||UnresolvedNeverRetriedOrAcknowledged()||LossAndWrongHideStayFailed()||BusyAndPairPacing())return 1;
    std::puts("Contextual finite fixture PASS: six focused config/source/recipe/unknown/ownership/publication groups.");return 0;}
