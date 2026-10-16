#include "PlayerGrenadeReceiver.h"
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
}
int main(){if(OriginalExpiry()||ClosedAndBadClock()||ExactlyOne()||MenuInterruptionNeverRetries()||PublicationAndArguments())return 1;
    std::printf("Player receiver PASS: five focused source/recipe groups; size=%zu. No graphics/native actions.\n",sizeof(probe::PlayerGrenadeReceiver));return 0;}
