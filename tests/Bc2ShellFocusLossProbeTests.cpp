#include "Bc2ShellFocusLossProbe.h"
#include "Test.h"
#include <sstream>
#include <string_view>
using namespace fvr::bc2;
namespace {
constexpr std::int64_t T=1000000000ll;
ShellFocusWitness Witness(){return {5,9,100,T+1000000,T+101000000,T,7,22,8};}
fvr::interaction::InputFrame ReceiverInput(){
    fvr::interaction::InputFrame in;in.generation=101;in.spaceGeneration=5;in.predictedNs=T;
    in.focused=in.headValid=true;in.head.position={.1f,.2f,.3f};in.referenceHead.position={1,2,3};
    for(auto& hand:in.hands){hand.active=fvr::interaction::Components;hand.gripTracked=hand.aimTracked=true;}
    return in;
}
std::string Report(const ShellFocusReceiver& receiver){std::ostringstream out;receiver.Report(out);return out.str();}
int ActualWireFocusShape(){
    auto in=ReceiverInput();fvr::ipc::InputPacket wire;
    CHECK(fvr::ipc::EncodeInput(in,T+100000000,wire));
    auto former=in;former.focused=false;
    CHECK(!fvr::ipc::EncodeInput(former,T+100000000,wire)); // Exact failed401 shape.
    ShellFocusReceiver::ApplyFocus(in,false);
    CHECK(fvr::ipc::EncodeInput(in,T+100000000,wire));
    fvr::interaction::InputFrame decoded;CHECK(fvr::ipc::DecodeInput(wire,decoded));
    CHECK(!decoded.focused&&decoded.headValid&&decoded.generation==101&&decoded.spaceGeneration==5);
    CHECK(decoded.predictedNs==T&&wire.deadlineQpc==T+100000000);
    CHECK(decoded.head.position.x==.1f&&decoded.referenceHead.position.x==1);
    for(const auto& hand:decoded.hands)CHECK(!hand.active&&!hand.held&&!hand.gripTracked&&!hand.aimTracked&&!hand.touchActive&&!hand.touched);
    // Regaining focus never resurrects a cached controller pose on its own.
    ShellFocusReceiver::ApplyFocus(in,true);CHECK(!in.hands[0].gripTracked&&!in.hands[1].active);
    auto fresh=ReceiverInput();++fresh.generation;fresh.predictedNs+=16000000;
    fresh.hands[0].grip.position.x=.25f;ShellFocusReceiver::ApplyFocus(fresh,true);
    CHECK(fvr::ipc::EncodeInput(fresh,T+116000000,wire)&&fvr::ipc::DecodeInput(wire,decoded));
    CHECK(decoded.focused&&decoded.generation==102&&decoded.hands[0].gripTracked&&decoded.hands[0].grip.position.x==.25f);
    return 0;
}
int PublicationFailureAndBusyDoNotClaimLoss(){
    auto in=ReceiverInput();ShellFocusReceiver invalid;CHECK(invalid.Begin(Witness(),T+2000000));
    in.focused=false;fvr::ipc::InputPacket wire;const auto encoded=fvr::ipc::EncodeInput(in,T+100000000,wire);
    CHECK(!encoded&&!invalid.Publication(in,true,encoded,unsigned(fvr::ipc::ChannelResult::Invalid),true,T+3000000));
    auto report=Report(invalid);CHECK(!invalid.Completed()&&report.find("\"first_lost_ns\":0")!=std::string::npos);
    CHECK(report.find("\"failed\":true")!=std::string::npos&&report.find("\"code\":2")!=std::string::npos);
    CHECK(report.find("\"sequence\":101")!=std::string::npos&&report.find("\"controllers_inactive\":false")!=std::string::npos);
    ShellFocusReceiver busy;CHECK(busy.Begin(Witness(),T+2000000));ShellFocusReceiver::ApplyFocus(in,false);
    CHECK(busy.Publication(in,true,false,unsigned(fvr::ipc::ChannelResult::Busy),true,T+3000000));
    CHECK(Report(busy).find("\"first_lost_ns\":0")!=std::string::npos&&!busy.Completed());
    ++in.generation;CHECK(busy.Publication(in,true,true,unsigned(fvr::ipc::ChannelResult::Ok),true,T+4000000));
    ++in.generation;CHECK(busy.Publication(in,true,true,unsigned(fvr::ipc::ChannelResult::Ok),true,T+504000000));
    in=ReceiverInput();in.generation=104;ShellFocusReceiver::ApplyFocus(in,true);
    CHECK(busy.Publication(in,true,true,unsigned(fvr::ipc::ChannelResult::Ok),true,T+505000000));CHECK(busy.Completed());
    for(unsigned kind=0;kind<3;++kind){ShellFocusReceiver bad;CHECK(bad.Begin(Witness(),T+2000000));
        const bool known=kind!=0,success=kind==2;
        CHECK(!bad.Publication(in,known,success,unsigned(fvr::ipc::ChannelResult::Closed),true,T+3000000));
        CHECK(!bad.Completed()&&Report(bad).find("\"last_publication\":")!=std::string::npos);
    }return 0;
}
int RealLossThenFreshNeutral(){
    ShellFocusReceiver receiver;ShellFocusEpisode episode;
    CHECK(episode.Arm(9,5,7,22,8,T));
    CHECK(episode.Source(100,5,T+1000000,T+101000000,T+2000000,true,true));
    const auto w=episode.Ready(T+3000000);CHECK(w&&receiver.Begin(*w,T+3000000));
    CHECK(!receiver.Focused(T+4000000));
    CHECK(receiver.Published(101,5,false,true,T+5000000));
    CHECK(episode.Source(101,5,T+4000000,T+104000000,T+6000000,false,true));
    CHECK(!episode.Ready(T+6000000));
    CHECK(episode.Counts(7,22,false,T+7000000));CHECK(!episode.Completed());
    CHECK(!receiver.Focused(T+504000000));
    CHECK(receiver.Published(102,5,false,true,T+504100000));
    CHECK(episode.Source(102,5,T+504000000,T+604000000,T+504200000,false,true));
    CHECK(receiver.Focused(T+507000000));
    CHECK(receiver.Published(103,5,true,true,T+508000000));CHECK(receiver.Completed());
    CHECK(episode.Source(103,5,T+507000000,T+607000000,T+509000000,true,true));
    CHECK(episode.Counts(7,22,true,T+608000000));CHECK(!episode.Completed());
    for(unsigned n=0;n<=60;++n){const auto now=T+620000000+n*50000000ll;
        CHECK(episode.Source(104+n,5,now,now+100000000,now,true,true));CHECK(episode.Counts(7,22,true,now));}
    CHECK(episode.Completed());return 0;
}
int NoInventedRenewalOrCoverage(){
    for(unsigned fault=0;fault<7;++fault){ShellFocusEpisode p;CHECK(p.Arm(9,5,7,22,8,T));
        CHECK(p.Source(100,5,T+1000000,T+101000000,T+2000000,true,true));
        auto sequence=101ull,space=5ull;auto observed=T+4000000,deadline=T+104000000,now=T+6000000;
        if(fault==0)space=6;if(fault==1)deadline=now;if(fault==2)observed=now+1;
        if(fault==3)deadline=observed+100000001;
        if(fault<4)CHECK(!p.Source(sequence,space,observed,deadline,now,false,true));
        else {CHECK(p.Source(sequence,space,observed,deadline,now,false,true));
            if(fault==4)CHECK(!p.Source(sequence,space,observed,deadline+1,now+1,false,true));
            if(fault==5)CHECK(!p.Source(sequence,space,observed,deadline,now+2000000,true,true));
            if(fault==6)CHECK(!p.Counts(8,21,false,now+1));}
        CHECK(!p.Completed());}
    ShellFocusEpisode neutral;CHECK(neutral.Arm(9,5,7,22,8,T));
    for(unsigned n=0;n<70;++n){const auto now=T+1000000+n*50000000ll;
        CHECK(neutral.Source(100+n,5,now,now+100000000,now,true,true));CHECK(neutral.Counts(7,22,true,now));}
    CHECK(!neutral.Completed()); // No focus loss cannot pass via ordinary counts.
    ShellFocusReceiver missing;CHECK(!missing.Completed());CHECK(!missing.Begin(Witness(),T+101000000));
    ShellFocusReceiver shortLoss;CHECK(shortLoss.Begin(Witness(),T+2000000));
    CHECK(shortLoss.Published(101,5,false,true,T+3000000));
    CHECK(!shortLoss.Published(102,5,true,true,T+4000000));CHECK(!shortLoss.Completed());
    return 0;
}
}
int main(int argc,char** argv){
    if(argc==2&&std::string_view(argv[1])=="--wire"){
        CHECK(ActualWireFocusShape()==0);CHECK(PublicationFailureAndBusyDoNotClaimLoss()==0);return 0;
    }
    CHECK(ActualWireFocusShape()==0);CHECK(PublicationFailureAndBusyDoNotClaimLoss()==0);
    CHECK(RealLossThenFreshNeutral()==0);CHECK(NoInventedRenewalOrCoverage()==0);
}
