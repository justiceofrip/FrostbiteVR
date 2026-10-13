#include "Test.h"
#include "fvr/graphics/RenderBootstrap.h"
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
}
int main(){if(Publication()||CancelBeforeEntry()||CancelDuringCallback()||AcquireCompleteValue())return 1;
    std::cout<<"RenderBootstrap: 4 groups passed\n";return 0;}
