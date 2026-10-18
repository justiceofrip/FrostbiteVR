#include "PlayerGrenadeReceiver.h"
#include "TriggerInputSmoke.h"
#include "Test.h"
#include <sstream>
using namespace fvr;
namespace {
constexpr std::int64_t Frequency=1000000000,Start=1000000000;
interaction::InputFrame Input(std::uint64_t ms){interaction::InputFrame i;i.generation=ms+1;i.spaceGeneration=1;i.predictedNs=Start+std::int64_t(ms)*1000000;
    i.focused=i.headValid=true;for(auto& h:i.hands){h.active=interaction::Components;h.gripTracked=h.aimTracked=true;}
    probe::OrdinaryGrenadeInput(i,ms);return i;
}
ipc::MenuState State(std::uint64_t ms){return {ms+1,1,Start+std::int64_t(ms)*1000000+100000000,ipc::MenuMode::Gameplay,1280,720,1,0};}
int OriginalExpiry(){probe::PlayerGrenadeReceiver p;auto i=Input(0);const auto s=State(0);p.Decorate(i,0,ipc::ChannelResult::Ok,s,Start,Frequency);
    CHECK(i.menu.owner==interaction::InputMenuOwner::Gameplay);const auto deadline=i.menu.deadlineNs;
    i=Input(50);p.Decorate(i,50,ipc::ChannelResult::Busy,{},Start+50000000,Frequency);CHECK(i.menu.deadlineNs==deadline);
    i=Input(100);p.Decorate(i,100,ipc::ChannelResult::Busy,{},Start+100000000,Frequency);CHECK(i.menu.owner==interaction::InputMenuOwner::Unknown);return 0;
}
int ClosedAndBadClock(){for(unsigned failure=0;failure<3;++failure){probe::PlayerGrenadeReceiver p;auto i=Input(0);p.Decorate(i,0,ipc::ChannelResult::Ok,State(0),Start,Frequency);
    i=Input(20);p.Decorate(i,20,failure==0?ipc::ChannelResult::Closed:ipc::ChannelResult::Ok,State(20),Start+20000000,failure==1?0:failure==2?1000000001:Frequency);
    CHECK(i.menu.owner==interaction::InputMenuOwner::Unknown);CHECK(i.hands[1].held==0);
}return 0;}
int ExactlyOne(){probe::PlayerGrenadeReceiver p;for(unsigned ms=0;ms<12000;ms+=10){auto i=Input(ms);CHECK(probe::OrdinaryGrenadeControls(i,ms));
    const auto now=Start+std::int64_t(ms)*1000000;p.Decorate(i,ms,ipc::ChannelResult::Ok,State(ms),now,Frequency);
    CHECK(i.menu.owner==interaction::InputMenuOwner::Gameplay);CHECK(i.menu.deadlineNs==State(ms).deadlineQpc);
    CHECK(i.hands[0].trigger==0&&i.hands[1].trigger==0);p.Published(i,true,true,0,now);
}CHECK(p.OneGesture());std::ostringstream out;p.Report(out);CHECK(out.str().find("\"click_presses\":1,\"click_releases\":1")!=std::string::npos);
    CHECK(out.str().find("\"events_dropped\":0")!=std::string::npos);return 0;
}
int MenuInterruptionNeverRetries(){for(unsigned missing=0;missing<3;++missing){probe::PlayerGrenadeReceiver p;for(unsigned ms=0;ms<12000;ms+=10){auto i=Input(ms);auto state=State(ms);
    auto read=ipc::ChannelResult::Ok;if(ms==5300){if(missing==0)read=ipc::ChannelResult::Timeout;if(missing==1)state.mode=ipc::MenuMode::Menu;if(missing==2)read=ipc::ChannelResult::Closed;}
    const auto now=Start+std::int64_t(ms)*1000000;p.Decorate(i,ms,read,state,now,Frequency);if(ms>=5300)CHECK(i.hands[1].held==0);p.Published(i,true,true,0,now);
}CHECK(!p.OneGesture());std::ostringstream out;p.Report(out);CHECK(out.str().find("\"click_presses\":1,\"click_releases\":1")!=std::string::npos);
    CHECK(out.str().find("\"menu_cancelled\":true")!=std::string::npos);
}return 0;}
int PublicationAndArguments(){probe::PlayerGrenadeReceiver p;auto i=Input(5000);p.Decorate(i,5000,ipc::ChannelResult::Ok,State(5000),Start+5000000000,Frequency);
    p.Published(i,true,false,1,Start+5000000000);CHECK(!p.OneGesture());std::ostringstream out;p.Report(out);CHECK(out.str().find("\"publication_failures\":1")!=std::string::npos);
    CHECK(probe::PlayerGrenadeOption(L"--player-grenade-probe"));CHECK(!probe::PlayerGrenadeOption(L"--ordinary-grenade-probe"));CHECK(!probe::PlayerGrenadeOption(L"--controls-death-probe"));return 0;
}
// Selected-knife fixtures reuse the same real-menu decorator with recipe time0.
// A packet carries the original native lease; no synthetic "playing" boolean
// or newer controller generation can renew it or acknowledge pending ownership.
int SelectedKnifeOwnership(){
    const auto input=[](unsigned ms){runtime::TrackingFrame t{};t.spaceGeneration=9;t.predictedNs=Start+std::int64_t(ms)*1000000;
        t.focused=t.headValid=true;auto i=probe::NeutralSceneInput(t,ms+1);i.trackingBasisGeneration=1;return i;};
    for(unsigned failure=0;failure<5;++failure){probe::PlayerGrenadeReceiver menu;auto i=input(0);i.hands[1].trigger=1;
        auto s=State(0);auto read=ipc::ChannelResult::Ok;
        if(failure==0)read=ipc::ChannelResult::Closed;
        if(failure==1)read=ipc::ChannelResult::Busy;
        if(failure==2)s.mode=ipc::MenuMode::Unknown;
        if(failure==3)s.mode=ipc::MenuMode::Menu;
        if(failure==4)++s.deadlineQpc;
        menu.Decorate(i,0,read,s,Start,Frequency);
        CHECK(!interaction::GameplayOwnsInput(i.menu,Start)&&i.hands[1].trigger==0);
        CHECK(i.hands[1].gripTracked&&i.hands[1].grip.position.z==-.20f&&i.trackingBasisGeneration==1);
    }
    probe::PlayerGrenadeReceiver menu;auto i=input(0);const auto s=State(0);
    menu.Decorate(i,0,ipc::ChannelResult::Ok,s,Start,Frequency);
    CHECK(interaction::GameplayOwnsInput(i.menu,Start)&&i.menu.sequence==s.sequence&&i.menu.epoch==s.epoch&&i.menu.deadlineNs==s.deadlineQpc);
    i=input(50);i.hands[1].trigger=1;menu.Decorate(i,0,ipc::ChannelResult::Busy,{},Start+50000000,Frequency);
    CHECK(i.hands[1].trigger==1&&i.menu.sequence==s.sequence&&i.menu.deadlineNs==s.deadlineQpc);
    i=input(100);i.hands[1].trigger=1;menu.Decorate(i,0,ipc::ChannelResult::Busy,{},Start+100000000,Frequency);
    CHECK(!interaction::GameplayOwnsInput(i.menu,Start+100000000)&&i.hands[1].trigger==0);
    i=input(110);menu.Decorate(i,0,ipc::ChannelResult::Ok,State(110),Start+110000000,Frequency);
    i=input(120);i.hands[0].held=interaction::MenuClick;i.hands[1].trigger=1;
    menu.Decorate(i,0,ipc::ChannelResult::Ok,State(120),Start+120000000,Frequency);
    CHECK(i.menu.owner==interaction::InputMenuOwner::Pending&&i.hands[1].trigger==0);
    i=input(130);i.hands[1].trigger=1;menu.Decorate(i,0,ipc::ChannelResult::Ok,State(130),Start+130000000,Frequency);
    CHECK(i.menu.owner==interaction::InputMenuOwner::Pending&&i.hands[1].trigger==0);
    auto changed=State(140);changed.epoch=2;i=input(140);
    menu.Decorate(i,0,ipc::ChannelResult::Ok,changed,Start+140000000,Frequency);
    CHECK(interaction::GameplayOwnsInput(i.menu,Start+140000000)&&i.menu.epoch==2);
    return 0;
}
int SelectedKnifeSuppressedPulseCannotPass(){
    runtime::TrackingFrame t{};t.spaceGeneration=1;t.predictedNs=Start;t.focused=t.headValid=true;
    probe::TriggerInputSmoke smoke;probe::PlayerGrenadeReceiver menu;auto i=smoke.Input(t,1,0);
    menu.Decorate(i,0,ipc::ChannelResult::Ok,State(0),Start,Frequency);
    smoke.Published(i,0,Start,Start+1000,true,false);
    t.predictedNs=Start+400000000;i=smoke.Input(t,2,400);CHECK(i.hands[1].trigger==1);
    auto s=State(400);s.mode=ipc::MenuMode::Menu;
    menu.Decorate(i,0,ipc::ChannelResult::Ok,s,t.predictedNs,Frequency);
    CHECK(i.hands[1].trigger==0&&!interaction::GameplayOwnsInput(i.menu,t.predictedNs));
    smoke.Published(i,400,t.predictedNs,t.predictedNs+1000,true,false);
    CHECK(!smoke.Failed()&&!smoke.Complete(5000,true));
    std::ostringstream report;smoke.Report(report,5000,true);
    CHECK(report.str().find("\"press_input\":0")!=std::string::npos);
    return 0;
}
}
int main(){if(OriginalExpiry()||ClosedAndBadClock()||ExactlyOne()||MenuInterruptionNeverRetries()||PublicationAndArguments()||
    SelectedKnifeOwnership()||SelectedKnifeSuppressedPulseCannotPass())return 1;
    std::printf("Player receiver PASS: seven focused source/recipe groups; size=%zu. No graphics/native actions.\n",sizeof(probe::PlayerGrenadeReceiver));return 0;}
