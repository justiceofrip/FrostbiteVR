#include "Test.h"
#include "fvr/ipc/MenuChannel.h"
#include <Windows.h>
using namespace fvr;
int main(){
    ipc::FrameChannel frames;CHECK(frames.CreateHost());ipc::MenuChannel host,producer,impostor;
    CHECK(!host.CreateHost(L"invalid"));CHECK(host.CreateHost(frames.Token()));CHECK(!impostor.CreateHost(frames.Token()));
    CHECK(producer.ConnectProducer(frames.Token()));CHECK(!impostor.ConnectProducer(frames.Token()));CHECK(host.Connected());
    ipc::MenuState state{1,1,0,ipc::MenuMode::Menu,1280,720,1};
    CHECK(producer.PublishState(state)==ipc::ChannelResult::Ok);CHECK(host.ReadState(state)==ipc::ChannelResult::Ok);
    const auto deadline=state.deadlineQpc;ipc::MenuState again;CHECK(host.ReadState(again)==ipc::ChannelResult::Ok&&again.deadlineQpc==deadline);
    ipc::MenuControl control;control.sequence=control.space=control.menuEpoch=1;control.u=.5f;control.v=.4f;control.flags=ipc::MenuFocused|ipc::MenuPoint|ipc::MenuDown;
    CHECK(host.PublishControl(control)==ipc::ChannelResult::Ok);CHECK(producer.ReadControl(control)==ipc::ChannelResult::Ok);
    CHECK(control.u==.5f&&(control.flags&ipc::MenuDown));CHECK(host.PublishControl(control)==ipc::ChannelResult::Invalid);
    ++control.sequence;control.flags=ipc::MenuDown;CHECK(host.PublishControl(control)==ipc::ChannelResult::Invalid);
    control.flags=0;control.toggle=1;CHECK(host.PublishControl(control)==ipc::ChannelResult::Ok);
    ++control.sequence;control.toggle=0;CHECK(host.PublishControl(control)==ipc::ChannelResult::Invalid);
    ipc::MenuSurface surface;surface.state=state;auto& d=surface.descriptor;d.width=1280;d.height=720;d.format=28;d.resourceEpoch=1;d.session[0]=1;
    auto& t=surface.ticket;t.resourceEpoch=t.sequence=t.frameId=t.spaceGeneration=t.trackingGeneration=1;t.predictedNs=1;t.session=d.session;
    CHECK(producer.PublishSurface(surface)==ipc::ChannelResult::Ok);ipc::MenuSurface taken;
    CHECK(host.TakeSurface(taken)==ipc::ChannelResult::Ok);CHECK(host.TakeSurface(taken)==ipc::ChannelResult::Busy);
    // A delivered texture is never reclaimed while its consumer may be reading,
    // including menu closure and expiry. Only exact GPU feedback ends ownership.
    state.sequence=2;state.epoch=2;state.mode=ipc::MenuMode::Gameplay;CHECK(producer.PublishState(state)==ipc::ChannelResult::Ok);
    Sleep(110);CHECK(producer.PollOutcome()==ipc::Outcome::Pending);
    auto wrong=t;++wrong.sequence;CHECK(host.SurfaceConsumed(wrong,true)==ipc::ChannelResult::Invalid);
    CHECK(host.SurfaceConsumed(t,true)==ipc::ChannelResult::Ok);CHECK(producer.PollOutcome()==ipc::Outcome::Consumed);
    CHECK(producer.ReadControl(control)==ipc::ChannelResult::Timeout&&control.sequence==0);
    CHECK(host.ReadState(state)==ipc::ChannelResult::Timeout&&state.sequence==0);
    state={3,3,0,ipc::MenuMode::Menu,1280,720,1};CHECK(producer.PublishState(state)==ipc::ChannelResult::Ok);CHECK(host.ReadState(state)==ipc::ChannelResult::Ok);
    surface.state=state;++t.sequence;++t.frameId;CHECK(producer.PublishSurface(surface)==ipc::ChannelResult::Ok);
    state.sequence=4;state.epoch=4;CHECK(producer.PublishState(state)==ipc::ChannelResult::Ok);
    CHECK(host.TakeSurface(taken)==ipc::ChannelResult::Timeout);CHECK(producer.PollOutcome()==ipc::Outcome::Discarded);
    CHECK(producer.PublishSurface(surface)==ipc::ChannelResult::Timeout);
    surface.state.reserved=1;CHECK(producer.PublishSurface(surface)==ipc::ChannelResult::Invalid);
    host.Close();CHECK(producer.ReadControl(control)==ipc::ChannelResult::Closed);producer.Close();return 0;
}
