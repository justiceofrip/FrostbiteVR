#include "Test.h"
#include "Bc2MenuControl.h"
#include <iostream>
using namespace fvr;
using namespace fvr::bc2;
namespace {
ipc::MenuControl Control(std::uint64_t sequence,std::uint64_t toggle=0,std::uint64_t cancel=0){
    ipc::MenuControl c;c.sequence=sequence;c.space=c.menuEpoch=1;c.deadlineQpc=1000;
    c.toggle=toggle;c.cancel=cancel;c.flags=ipc::MenuFocused;return c;
}
int BusyKeepsUnseenEdge(){
    MenuControlSequence p;auto c=Control(1);CHECK(p.Observe(ipc::ChannelResult::Ok,c).read==MenuControlRead::Rearmed);
    // Mutex contention can coincide with publication of a new chord. A Busy
    // read returns no command bytes; the next valid publication still counts.
    for(unsigned n=0;n<100;++n){const auto busy=p.Observe(ipc::ChannelResult::Busy,{});
        CHECK(busy.read==MenuControlRead::Busy&&!busy.toggle&&!busy.cancel);}
    c=Control(2,1,1);auto edge=p.Observe(ipc::ChannelResult::Ok,c);
    CHECK(edge.read==MenuControlRead::Ready&&edge.toggle&&edge.cancel);
    for(unsigned n=0;n<10;++n){CHECK(p.Observe(ipc::ChannelResult::Busy,c).read==MenuControlRead::Busy);
        edge=p.Observe(ipc::ChannelResult::Ok,c);CHECK(edge.read==MenuControlRead::Ready&&!edge.toggle&&!edge.cancel);}
    c=Control(3,2,1);edge=p.Observe(ipc::ChannelResult::Ok,c);CHECK(edge.toggle&&!edge.cancel);return 0;
}
int ExpiryAndFocusRequireNeutralBaseline(){
    for(const auto result:{ipc::ChannelResult::Timeout,ipc::ChannelResult::Closed,ipc::ChannelResult::Invalid}){
        MenuControlSequence p;auto c=Control(1);p.Observe(ipc::ChannelResult::Ok,c);
        auto lost=p.Observe(result,Control(2,1,1));CHECK(lost.read==MenuControlRead::Inactive&&!lost.toggle&&!lost.cancel);
        p.Observe(ipc::ChannelResult::Busy,{});c=Control(3,2,2);
        auto recovered=p.Observe(ipc::ChannelResult::Ok,c);CHECK(recovered.read==MenuControlRead::Rearmed&&!recovered.toggle&&!recovered.cancel);
        c=Control(4,3,2);recovered=p.Observe(ipc::ChannelResult::Ok,c);CHECK(recovered.toggle&&!recovered.cancel);
    }
    MenuControlSequence p;p.Observe(ipc::ChannelResult::Ok,Control(1));
    auto c=Control(2,1);c.flags=0;CHECK(p.Observe(ipc::ChannelResult::Ok,c).read==MenuControlRead::Inactive);
    c=Control(3,2);auto recovered=p.Observe(ipc::ChannelResult::Ok,c);CHECK(recovered.read==MenuControlRead::Rearmed&&!recovered.toggle);
    c=Control(4,3);CHECK(p.Observe(ipc::ChannelResult::Ok,c).toggle);return 0;
}
int InitialSpaceAndMalformedControlsDoNotReplay(){
    MenuControlSequence p;CHECK(p.Observe(ipc::ChannelResult::Busy,{}).read==MenuControlRead::Busy);
    auto c=Control(1,4,2);auto edge=p.Observe(ipc::ChannelResult::Ok,c);
    CHECK(edge.read==MenuControlRead::Rearmed&&!edge.toggle&&!edge.cancel);
    c=Control(2,5,3);c.space=2;edge=p.Observe(ipc::ChannelResult::Ok,c);
    CHECK(edge.read==MenuControlRead::Rearmed&&!edge.toggle&&!edge.cancel);
    c=Control(3,6,3);c.space=2;edge=p.Observe(ipc::ChannelResult::Ok,c);CHECK(edge.toggle&&!edge.cancel);
    c=Control(4,7,4);c.flags=ipc::MenuDown;edge=p.Observe(ipc::ChannelResult::Ok,c);CHECK(edge.read==MenuControlRead::Inactive&&!edge.toggle&&!edge.cancel);
    c=Control(5,8,5);edge=p.Observe(ipc::ChannelResult::Ok,c);CHECK(edge.read==MenuControlRead::Rearmed&&!edge.toggle&&!edge.cancel);return 0;
}
}
int main(){CHECK(!BusyKeepsUnseenEdge());CHECK(!ExpiryAndFocusRequireNeutralBaseline());CHECK(!InitialSpaceAndMalformedControlsDoNotReplay());
    std::cout<<"Three menu-control edge groups passed; Busy emits no input and preserves the next fresh edge.\n";return 0;}
