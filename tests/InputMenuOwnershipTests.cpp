#include "Test.h"
#include "fvr/interaction/MenuOwnershipPolicy.h"
#include "fvr/ipc/FrameChannel.h"
#include "fvr/ipc/InputProtocol.h"
#include <Windows.h>
#include <string>
using namespace fvr;
namespace {
struct Fixture {
    interaction::InputFrame input{};
    interaction::MenuPointer pointer;
    interaction::MenuOwnershipPolicy ownership;
    interaction::MenuPointerFrame current{};
    ipc::MenuState state{1,10,1,ipc::MenuMode::Gameplay,1280,720,1,0};
    std::int64_t now=1000000000,deadline=1100000000;
    Fixture(){input.generation=input.spaceGeneration=1;input.predictedNs=now;input.focused=input.headValid=true;
        for(auto& hand:input.hands){hand.active=interaction::Components;hand.gripTracked=hand.aimTracked=true;}}
    interaction::InputMenuOwnership Step(unsigned left=0,std::int64_t dt=20000000,bool known=true,bool renew=true){
        now+=dt;++input.generation;input.predictedNs=now;input.hands[0].held=left;
        if(renew){++state.sequence;deadline=now+100000000;}
        current=pointer.Update(input,known?&state:nullptr,0,0);
        return ownership.Update(input,current,known?&state:nullptr,deadline,now);
    }
};
int ChordAndAcknowledgement(){Fixture x;CHECK(x.Step().owner==interaction::InputMenuOwner::Gameplay);
    constexpr auto chord=interaction::Primary|interaction::Secondary;
    CHECK(x.Step(chord).owner==interaction::InputMenuOwner::Pending);
    auto filtered=x.input;x.current.FilterGameplayInput(filtered);CHECK(!filtered.hands[0].held);
    CHECK(x.current.control.toggle==0);CHECK(x.Step().owner==interaction::InputMenuOwner::Gameplay);
    x.Step(chord);for(unsigned i=0;i<25;++i)CHECK(x.Step(chord).owner==interaction::InputMenuOwner::Pending);
    CHECK(x.current.control.toggle==1);
    // Releasing the raw chord cannot acknowledge a dispatched menu request.
    CHECK(x.Step().owner==interaction::InputMenuOwner::Pending);
    CHECK(x.Step(0,20000000,false).owner==interaction::InputMenuOwner::Unknown);
    CHECK(x.Step().owner==interaction::InputMenuOwner::Pending);
    ++x.state.epoch;x.state.mode=ipc::MenuMode::Menu;CHECK(x.Step().owner==interaction::InputMenuOwner::Menu);
    CHECK(x.Step(interaction::MenuClick).owner==interaction::InputMenuOwner::Menu);CHECK(x.current.control.toggle==2);
    ++x.state.epoch;x.state.mode=ipc::MenuMode::Gameplay;
    CHECK(x.Step(interaction::MenuClick).owner==interaction::InputMenuOwner::Pending);
    CHECK(x.Step().owner==interaction::InputMenuOwner::Gameplay);return 0;}
int SourceExpiry(){Fixture x;auto menu=x.Step();CHECK(interaction::GameplayOwnsInput(menu,x.now));
    const auto original=menu.deadlineNs;
    for(unsigned i=0;i<4;++i){menu=x.Step(0,20000000,true,false);CHECK(menu.deadlineNs==original);}
    CHECK(interaction::GameplayOwnsInput(menu,original-1));CHECK(!interaction::GameplayOwnsInput(menu,original));
    CHECK(x.Step(0,20000000,true,false).owner==interaction::InputMenuOwner::Unknown);
    CHECK(x.Step().owner==interaction::InputMenuOwner::Gameplay);
    x.state.mode=ipc::MenuMode::Unknown;CHECK(x.Step().owner==interaction::InputMenuOwner::Unknown);
    x.state.mode=ipc::MenuMode::Gameplay;x.deadline=x.now+200000000;
    CHECK(x.Step(0,20000000,true,false).owner==interaction::InputMenuOwner::Unknown);return 0;}
int FocusCannotAcknowledge(){Fixture x;x.Step();CHECK(x.Step(interaction::MenuClick).owner==interaction::InputMenuOwner::Pending);
    x.input.focused=false;x.input.hands={};CHECK(x.Step().owner==interaction::InputMenuOwner::Unknown);
    x.input.focused=true;for(auto& h:x.input.hands){h.active=interaction::Components;h.gripTracked=h.aimTracked=true;}
    ++x.input.spaceGeneration;CHECK(x.Step().owner==interaction::InputMenuOwner::Pending);
    ++x.state.epoch;x.state.mode=ipc::MenuMode::Menu;CHECK(x.Step().owner==interaction::InputMenuOwner::Menu);
    ++x.state.epoch;x.state.mode=ipc::MenuMode::Gameplay;CHECK(x.Step().owner==interaction::InputMenuOwner::Gameplay);return 0;}
int Protocol(){Fixture x;x.input.menu=x.Step();ipc::InputPacket packet{};interaction::InputFrame decoded;
    CHECK(ipc::EncodeInput(x.input,987654321,packet));CHECK(packet.version==4&&packet.bytes==320);
    CHECK(ipc::DecodeInput(packet,decoded));CHECK(decoded.menu==x.input.menu);
    const auto before=packet;auto bad=packet;bad.menuReserved=1;CHECK(!ipc::DecodeInput(bad,decoded));
    bad=packet;bad.menuOwner=4;CHECK(!ipc::DecodeInput(bad,decoded));
    bad=packet;bad.menuEpoch=0;CHECK(!ipc::DecodeInput(bad,decoded));
    bad=packet;bad.menuOwner=0;CHECK(!ipc::DecodeInput(bad,decoded));
    for(unsigned version:{1u,2u}){bad=packet;bad.version=version;bad.bytes=288;CHECK(!ipc::DecodeInput(bad,decoded));}
    // New publication time changes the input lease, never the source menu lease.
    ++x.input.generation;CHECK(ipc::EncodeInput(x.input,987999999,packet));CHECK(packet.menuDeadlineNs==before.menuDeadlineNs);
    x.input.menu={};CHECK(ipc::EncodeInput(x.input,987999999,packet)&&ipc::DecodeInput(packet,decoded));
    CHECK(decoded.menu.owner==interaction::InputMenuOwner::Unknown&&!interaction::GameplayOwnsInput(decoded.menu,x.now));return 0;}
int SourceCallersKeepControls(){Fixture x;interaction::ControllerActions actions;
    for(unsigned i=0;i<5;++i){x.Step();actions.Update(x.input,{42,7,true,true},x.now);}
    CHECK(x.input.menu.owner==interaction::InputMenuOwner::Unknown);
    x.Step();x.input.hands[1].trigger=1;const auto action=actions.Update(x.input,{42,7,true,true},x.now);
    CHECK(action.held&interaction::Fire);CHECK(!interaction::GameplayOwnsInput(x.input.menu,x.now));return 0;}
int ChannelRoundTrip(){ipc::FrameChannel host,producer;CHECK(host.CreateHost());CHECK(producer.ConnectProducer(host.Token()));
    Fixture x;auto menu=x.Step();x.input.menu=menu;CHECK(host.PublishInput(x.input)==ipc::ChannelResult::Ok);
    interaction::InputFrame decoded;std::int64_t inputDeadline=0;CHECK(producer.ReadInput(decoded,&inputDeadline)==ipc::ChannelResult::Ok);
    CHECK(decoded.generation==x.input.generation&&decoded.spaceGeneration==x.input.spaceGeneration&&decoded.menu==menu&&inputDeadline>0);
    ++x.input.generation;x.input.menu={};x.input.hands[1].trigger=1;CHECK(host.PublishInput(x.input)==ipc::ChannelResult::Ok);
    CHECK(producer.ReadInput(decoded)==ipc::ChannelResult::Ok&&decoded.hands[1].trigger==1&&decoded.menu.owner==interaction::InputMenuOwner::Unknown);
    const auto token=host.Token();const auto oldName=L"Local\\FrostbiteVR.Control.v6."+token+L".map";
    const auto legacy=OpenFileMappingW(FILE_MAP_READ,FALSE,oldName.c_str());CHECK(!legacy);
    host.Close();CHECK(producer.ReadInput(decoded)==ipc::ChannelResult::Closed);CHECK(decoded.menu.owner==interaction::InputMenuOwner::Unknown);return 0;}
int LegacyNamespaceRejected(){const std::wstring token=L"11112222333344445555666677778888";
    const auto name=L"Local\\FrostbiteVR.Control.v6."+token;
    const auto mutex=CreateMutexW(nullptr,FALSE,(name+L".mutex").c_str());CHECK(mutex&&GetLastError()!=ERROR_ALREADY_EXISTS);
    const auto map=CreateFileMappingW(INVALID_HANDLE_VALUE,nullptr,PAGE_READWRITE,0,3472,(name+L".map").c_str());CHECK(map&&GetLastError()!=ERROR_ALREADY_EXISTS);
    ipc::FrameChannel producer;const bool connected=producer.ConnectProducer(token);CloseHandle(map);CloseHandle(mutex);
    CHECK(!connected);return 0;}
}
int main(){if(ChordAndAcknowledgement()||SourceExpiry()||FocusCannotAcknowledge()||Protocol()||SourceCallersKeepControls()||ChannelRoundTrip()||LegacyNamespaceRejected())return 1;
    std::puts("Seven focused menu/transport groups PASS: raw pending chord and native epoch acknowledgement; original expiry; focus/space loss; v3 metadata validation; Unknown preserves other controls; atomic v7 round trip/disconnect; legacy namespace rejection. No game, XR or graphics.");return 0;}
