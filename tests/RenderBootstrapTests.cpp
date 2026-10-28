#include "Test.h"
#include "fvr/graphics/RenderBootstrap.h"
#include "fvr/graphics/GraphicsCallProvenance.h"
#include <atomic>
#include <latch>
#include <thread>
#include <iostream>
namespace {
struct Snapshot {unsigned device=0,context=0,released=0;};
using Bootstrap=fvr::graphics::RenderBootstrap<Snapshot>;
int Publication(){
    Bootstrap b;unsigned calls=0,released=0;
    CHECK(!b.Read());
    b.Observe([&](Snapshot& s)noexcept {++calls;s={12,34,1};++released;return true;});
    CHECK(b.State()==Bootstrap::Phase::Ready&&b.Read()->context==34&&released==1);
    b.Observe([&](Snapshot&)noexcept {++calls;return false;});
    CHECK(calls==1&&b.Read()->device==12);
    b.Cancel();CHECK(!b.Read());
    b.Observe([&](Snapshot&)noexcept {++calls;return true;});CHECK(calls==1);
    Bootstrap invalid;invalid.Observe([](Snapshot& s)noexcept {s={12,0,0};return false;});
    CHECK(invalid.State()==Bootstrap::Phase::Failed&&!invalid.Read());
    return 0;
}
int CancelBeforeEntry(){
    Bootstrap b;b.Cancel();unsigned calls=0;
    b.Observe([&](Snapshot&)noexcept {++calls;return true;});
    CHECK(!calls&&!b.Read()&&b.State()==Bootstrap::Phase::Cancelled);return 0;
}
int CancelDuringCallback(){
    Bootstrap b;std::latch entered(1),finish(1);std::atomic<unsigned> calls=0,released=0;
    std::thread callback([&]{b.Observe([&](Snapshot& s)noexcept {
        ++calls;entered.count_down();finish.wait();s={42,73,1};++released;return true;
    });});
    entered.wait();const bool withheld=!b.Read();
    // A concurrent or reentrant Present cannot capture a second snapshot.
    b.Observe([&](Snapshot&)noexcept {++calls;return true;});
    b.Cancel();finish.count_down();callback.join();
    CHECK(withheld&&calls==1&&released==1&&!b.Read());
    CHECK(b.State()==Bootstrap::Phase::Cancelled);return 0;
}
int AcquireCompleteValue(){
    Bootstrap b;std::latch entered(1),finish(1);std::atomic<bool> released=false;
    std::thread callback([&]{b.Observe([&](Snapshot& s)noexcept {
        entered.count_down();finish.wait();s={0x1234,0x9876,1};released.store(true);return true;
    });});
    entered.wait();const bool withheld=!b.Read();finish.count_down();
    while(b.State()==Bootstrap::Phase::Capturing)std::this_thread::yield();
    const auto result=b.Read();callback.join();
    CHECK(withheld&&result&&result->device==0x1234&&result->context==0x9876&&result->released==1&&released);
    return 0;
}
int ProvenanceIdentityAndDrain(){
    using P=fvr::graphics::GraphicsCallProvenance;using K=fvr::graphics::GraphicsCallKind;
    P p;CHECK(!p.Configure({}));CHECK(!p.Begin(K::Present,0,0,3,50,10,100).admitted);
    CHECK(p.Configure({1,2,3,10}));CHECK(!p.Configure({4,5,6,20}));
    CHECK(!p.Begin(K::CreateTexture2D,9,0,0,50,10,100).admitted);
    CHECK(!p.Begin(K::Indexed,0,8,0,50,10,100).admitted);
    CHECK(!p.Begin(K::Present,0,0,9,50,10,100).admitted);
    auto t=p.Begin(K::CreateTexture2D,1,0,0,123,20,100);CHECK(t.admitted&&p.Active()==1);
    const auto slot=t.rows[0];const auto entry=p.Entry(slot);CHECK(entry&&entry->thread==20&&entry->caller==123&&entry->device==1);
    CHECK(!p.Exit(slot));CHECK(!p.Stop());CHECK(!p.Begin(K::Present,0,0,3,0,10,102).admitted);
    p.End(t,{110,-7,true});CHECK(p.Stop()&&p.Active()==0);const auto end=p.Exit(slot);
    CHECK(end&&end->resultKnown&&end->result==-7&&end->endQpc==110);
    p.End(t,{120,0,false});CHECK(p.Active()==0);CHECK(p.CountsFor(unsigned(K::CreateTexture2D)).foreign==1);
    CHECK(p.RejectedIdentity()==3);return 0;
}
int ProvenanceQuotasAndOverlap(){
    using P=fvr::graphics::GraphicsCallProvenance;using K=fvr::graphics::GraphicsCallKind;
    P p;CHECK(p.Configure({1,2,3,10}));
    for(unsigned i=0;i<40;++i){auto t=p.Begin(K::CreateTexture2D,1,0,0,100,10,i+1);p.End(t,{i+2,0,true});}
    auto outer=p.Begin(K::Present,0,0,3,200,10,100);
    auto foreign=p.Begin(K::CreateTexture2D,1,0,0,300,20,101);
    CHECK(foreign.admitted&&foreign.count==2); // First ordinary quota remains full; foreign/overlap retain later call.
    CHECK(p.Entry(foreign.rows[0])->bucket==1&&p.Entry(foreign.rows[1])->bucket==2);
    CHECK(p.Entry(foreign.rows[1])->overlapThread==10);
    auto nested=p.Begin(K::MenuPump,1,2,3,400,10,102);CHECK(nested.admitted);
    CHECK(p.CountsFor(unsigned(K::MenuPump)).overlap==1); // Concurrent foreign scope, not same-thread nesting.
    p.End(nested,{103,0,false});p.End(foreign,{104,0,true});p.End(outer,{105,0,false});
    for(unsigned i=0;i<40;++i){auto t=p.Begin(K::CreateTexture2D,1,0,0,500,20,200+i);p.End(t,{201+i,0,true});}
    const auto c=p.CountsFor(unsigned(K::CreateTexture2D));CHECK(c.calls==81&&c.foreign==41&&c.overlap==1&&c.omitted>0);
    unsigned entries=0;for(unsigned i=0;i<P::Rows;++i)if(p.Entry(i)){++entries;CHECK(p.Exit(i));}
    CHECK(entries<=P::Rows&&p.Stop());return 0;
}

}
int main(){if(Publication()||CancelBeforeEntry()||CancelDuringCallback()||AcquireCompleteValue()||ProvenanceIdentityAndDrain()||ProvenanceQuotasAndOverlap())return 1;
    std::cout<<"RenderBootstrap: 6 groups passed\n";return 0;}
