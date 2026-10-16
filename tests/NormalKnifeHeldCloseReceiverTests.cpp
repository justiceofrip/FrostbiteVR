#include "NormalKnifeHeldCloseReceiver.h"
#include "Test.h"
#include <sstream>
using namespace fvr;
namespace {
using Receiver=probe::NormalKnifeHeldCloseReceiver;
constexpr std::int64_t Start=1000000000;
interaction::InputFrame Input(unsigned ms){interaction::InputFrame i;i.generation=ms/10+1;i.spaceGeneration=7;
    i.predictedNs=Start+std::int64_t(ms)*1000000;i.focused=i.headValid=true;
    for(auto& h:i.hands){h.active=interaction::Components;h.gripTracked=h.aimTracked=true;}return i;
}
void Step(Receiver& r,interaction::InputFrame& i,unsigned ms,bool process=true,unsigned pairs=1,
    ipc::MenuMode mode=ipc::MenuMode::Gameplay,bool busy=false,std::int64_t callDuration=0){
    const auto now=Start+std::int64_t(ms)*1000000;ipc::MenuState menu{i.generation,1,now+100000000,mode,1280,720,1,0};
    r.Update(i,ms,now,process,pairs,ipc::ChannelResult::Ok,menu,now,1000000000);
    r.Published(i,true,!busy,unsigned(busy?ipc::ChannelResult::Busy:ipc::ChannelResult::Ok),now,now+callDuration);
}
int FixedPhysicalHold(){Receiver r;
    for(unsigned ms=0;ms<12000;ms+=10){auto i=Input(ms);Step(r,i,ms,true,ms?1:0,ipc::MenuMode::Gameplay,ms==5000);
        CHECK(!r.Failed());CHECK(i.hands[1].squeeze==(ms>=2450?1.f:0.f));
        CHECK(i.hands[0].squeeze==0&&i.hands[0].trigger==0&&i.hands[1].trigger==0);
        CHECK(!i.hands[0].held&&!i.hands[1].held);
        if(ms==1500)CHECK(Near(i.hands[1].grip.position.x,.15f));
        if(ms==2200||ms==4250)CHECK(Near(i.hands[1].grip.position.x,.12f));
        if(ms==6050)CHECK(Near(i.hands[1].grip.position.x,.15f));
    }
    r.Finish(12000,Start+12000000000ll);CHECK(r.Complete());std::ostringstream out;r.Report(out);
    CHECK(out.str().find("\"grip_presses\":1,\"grip_releases\":0")!=std::string::npos);
    CHECK(out.str().find("\"native_acceptance\":false")!=std::string::npos);return 0;
}
int MissingSetup(){for(unsigned missing=0;missing<2;++missing){Receiver r;
    for(unsigned ms=0;ms<=1500;ms+=10){auto i=Input(ms);Step(r,i,ms,true,missing?1:0,missing?ipc::MenuMode::Unknown:ipc::MenuMode::Gameplay);CHECK(!i.hands[1].squeeze);}
    CHECK(r.Failed()&&!r.Complete());}return 0;
}
int CurrentStops(){for(unsigned cause=0;cause<4;++cause){Receiver r;
    for(unsigned ms=0;ms<3000;ms+=10){auto i=Input(ms);Step(r,i,ms);}
    auto i=Input(3000);if(cause==1)i.hands[1].gripTracked=false;if(cause==2)++i.spaceGeneration;
    Step(r,i,3000,cause!=3,1,cause==0?ipc::MenuMode::Unknown:ipc::MenuMode::Gameplay);
    CHECK(r.Failed()&&!i.hands[1].squeeze);
    auto recovered=Input(3010);Step(r,recovered,3010);CHECK(!recovered.hands[1].squeeze&&!r.Complete());}return 0;
}
int BusyDoesNotExtend(){Receiver r;for(unsigned ms=0;ms<=3000;ms+=10){auto i=Input(ms);Step(r,i,ms);}
    auto a=Input(3090);Step(r,a,3090,true,1,ipc::MenuMode::Gameplay,true,10000000);
    CHECK(r.Reason()==Receiver::Failure::Gap);
    auto b=Input(3100);Step(r,b,3100);CHECK(r.Reason()==Receiver::Failure::Gap&&!b.hands[1].squeeze);return 0;
}
int EndRequiresFreshHeld(){Receiver r;for(unsigned ms=0;ms<=11800;ms+=10){auto i=Input(ms);Step(r,i,ms);}
    r.Finish(12000,Start+12000000000ll);CHECK(!r.Complete());return 0;
}
int Bound(){unsigned due=0;for(unsigned ms=0;ms<12000;++ms)if(Receiver::PairRequestDue(ms,due))++due;
    CHECK(due==240&&!Receiver::PairRequestDue(12000,240));return 0;}
}
int main(){if(FixedPhysicalHold()||MissingSetup()||CurrentStops()||BusyDoesNotExtend()||EndRequiresFreshHeld()||Bound())return 1;
    std::puts("normal held-close receiver: six new focused groups passed");return 0;}
