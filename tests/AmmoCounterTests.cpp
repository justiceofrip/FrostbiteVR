#include "Bc2AmmoCounterHost.h"
#include "Bc2ReloadFlowRuntime.h"
#include "Test.h"
#include <iostream>
#include <string_view>
using namespace fvr;using namespace fvr::graphics;using namespace fvr::bc2;
namespace {
constexpr std::int64_t Now=1000000000;
Bc2AmmoReserveLease Reserve(){Bc2AmmoReserveLease a;
    a.identity.owner={0x10000,0x20000,0x30000,0x40000,10,20,30};
    a.identity.firing={0x50000,0x60000,0x70000};a.identity.serverPlayer=0x80000;a.identity.serverSoldier=0x90000;a.identity.serverItem=0xa0000;
    a.sequence=1;a.observedNs=Now;a.deadlineNs=Now+200000000;a.loaded=20;a.reserve=180;a.capacity=30;a.verified=true;return a;
}
AmmoCounterOwner Owner(){return {Reserve().identity.owner,Now,Now+200000000,true,true,true};}
AmmoCounterSample Sample(){return AmmoCounterHostSample(Reserve(),Owner(),Now);}
int NativeReadOnlyAdmission(){const auto a=Reserve();const auto o=Owner();auto sample=AmmoCounterHostSample(a,o,Now);
    CHECK(sample.loaded==20&&sample.reserve==180&&sample.capacity==30);CHECK(sample.deadlineNs==Now+100000000);
    auto owner=o;owner.deadlineNs=Now+3000000;CHECK(AmmoCounterHostSample(a,owner,Now).deadlineNs==owner.deadlineNs);
    CHECK(!AmmoCounterHostSample({},o,Now).sequence);
    for(unsigned fault=0;fault<17;++fault){auto bad=a;auto current=o;
        if(fault==0)bad.verified=false;if(fault==1)++bad.identity.owner.weapon;if(fault==2)++bad.identity.owner.equipGeneration;
        if(fault==3)bad.identity.serverItem=0;if(fault==4)bad.identity.firing[2]=0;if(fault==5)bad.observedNs=Now+1;
        if(fault==6)bad.deadlineNs=Now;if(fault==7)bad.loaded=-1;if(fault==8)bad.loaded=31;if(fault==9)bad.reserve=-1;
        if(fault==10)current.alive=false;if(fault==11)current.onFoot=false;if(fault==12)current.held=false;
        if(fault==13)current.deadlineNs=Now;if(fault==14)current.observedNs=Now+1;if(fault==15)bad.capacity=0;
        if(fault==16)bad.reserve=1000001;
        CHECK(!AmmoCounterHostSample(bad,current,Now).sequence);
    }
    auto empty=a;empty.loaded=empty.reserve=0;CHECK(AmmoCounterHostSample(empty,o,Now).sequence);
    CHECK(!AmmoCounterHostSample(a,o,Now+100000000).sequence);
    return 0;
}
int NativeReadClockOrdering(){
    auto current=Owner();auto native=Reserve();std::int64_t clock=Now;
    const auto read=[&]()noexcept->std::optional<Bc2AmmoReserveLease>{
        // Model ReadReserve: counts are observed after the caller enters, and
        // ownership/cohort validation finishes after those actual count reads.
        clock+=1000000;++native.sequence;native.observedNs=clock;native.deadlineNs=clock+200000000;
        clock+=1000000;return native;
    };
    const auto now=[&]()noexcept{return clock;};
    const auto entered=now();const auto readAfterEntry=read();
    CHECK(!AmmoCounterHostSample(readAfterEntry,current,entered).sequence); // Checkpoint207 composition rejected every fresh read.
    const auto first=ReadAmmoCounterHostSample(current,read,now);
    CHECK(first.sequence==native.sequence&&first.loaded==20&&first.reserve==180);
    CHECK(first.observedNs==native.observedNs&&first.observedNs<clock);
    CHECK(first.deadlineNs==native.observedNs+AmmoCounterMaxAgeNs);
    const auto second=ReadAmmoCounterHostSample(current,read,now);
    AmmoCounterPresentation presentation;CHECK(presentation.Receive(first,second,current.owner.space,clock));
    CHECK(presentation.Select(current.owner.space,clock));CHECK(!presentation.Select(current.owner.space,first.deadlineNs));
    // A slow read must not renew held ownership or old count data to obtain a HUD.
    current.deadlineNs=clock+1000000;
    CHECK(!ReadAmmoCounterHostSample(current,read,now).sequence);
    current=Owner();const auto old=native;clock=old.observedNs+AmmoCounterMaxAgeNs;
    CHECK(!ReadAmmoCounterHostSample(current,[&]()noexcept{return std::optional{old};},now).sequence);
    auto future=native;future.observedNs=clock+1;future.deadlineNs=future.observedNs+200000000;
    CHECK(!ReadAmmoCounterHostSample(current,[&]()noexcept{return std::optional{future};},now).sequence);
    CHECK(!ReadAmmoCounterHostSample(current,[]()noexcept{return std::optional<Bc2AmmoReserveLease>{};},now).sequence);
    return 0;
}
int PublishedReserveIsPassiveAndBounded(){
    ReloadReservePublication cache;CHECK(!cache.Read());const auto original=Reserve();
    const auto epoch=cache.Begin();cache.Observe(epoch,{ReloadObservationResult::Available,original});
    const auto copy=cache.Read();CHECK(copy&&copy->identity==original.identity&&copy->sequence==original.sequence);
    CHECK(copy->observedNs==original.observedNs&&copy->deadlineNs==original.deadlineNs);
    auto clock=Now+1000000;
    const auto render=[&]()noexcept{return ReadAmmoCounterHostSample(Owner(),[&]()noexcept{return cache.Read();},[&]()noexcept{return clock;});};
    const auto first=render();CHECK(first.sequence==original.sequence&&first.loaded==original.loaded&&first.reserve==original.reserve);
    clock+=1000000;CHECK(render()==first); // Repeated eye reads have no observation/sequence/deadline side effects.
    cache.Observe(epoch,{ReloadObservationResult::Deferred,{}});CHECK(render()==first);
    clock=Now+AmmoCounterMaxAgeNs;CHECK(!render().sequence);CHECK(cache.Read()->deadlineNs==original.deadlineNs);
    // A rejected observation/owner clear wins against an earlier in-flight read.
    auto newer=original;++newer.sequence;newer.observedNs+=1000000;newer.deadlineNs+=1000000;
    cache.Observe(epoch,{ReloadObservationResult::Rejected,{}});CHECK(!cache.Read());
    cache.Observe(epoch,{ReloadObservationResult::Available,newer});CHECK(!cache.Read());
    const auto nextEpoch=cache.Begin();cache.Observe(nextEpoch,{ReloadObservationResult::Available,newer});CHECK(cache.Read());
    cache.Observe(nextEpoch,{ReloadObservationResult::Available,original});CHECK(cache.Read()->sequence==newer.sequence);
    cache.Clear();CHECK(!cache.Read());cache.Observe(nextEpoch,{ReloadObservationResult::Available,newer});CHECK(!cache.Read());
    cache.Observe(cache.Begin(),{ReloadObservationResult::Available,original});CHECK(cache.Read());
    cache.Observe(cache.Begin(),{ReloadObservationResult::CohortGap,{}});CHECK(!cache.Read());
    cache.Observe(cache.Begin(),{ReloadObservationResult::Available,original});CHECK(cache.Read());
    cache.Observe(cache.Begin(),{ReloadObservationResult::Available,{}});CHECK(!cache.Read());
    return 0;
}
int PairAndFreshness(){const auto a=Sample();CHECK(AmmoCounterPair(a,a,30,Now));
    for(unsigned fault=0;fault<8;++fault){auto b=a;
        if(fault==0)++b.actorGeneration;if(fault==1)++b.equipmentGeneration;if(fault==2)++b.spaceGeneration;
        if(fault==3)--b.loaded;if(fault==4)--b.reserve;if(fault==5)++b.capacity;if(fault==6)++b.observedNs;if(fault==7)b.sequence=0;
        CHECK(!AmmoCounterPair(a,b,30,Now+1));}
    auto newer=a;++newer.sequence;newer.observedNs+=1000000;newer.deadlineNs+=1000000;
    auto pair=AmmoCounterPair(a,newer,30,Now+1000000);CHECK(pair);CHECK(pair->observedNs==a.observedNs&&pair->deadlineNs==a.deadlineNs);
    AmmoCounterPresentation p;CHECK(p.Receive(a,a,30,Now));CHECK(p.Select(30,Now));CHECK(!p.Select(31,Now));
    CHECK(!p.Select(30,a.deadlineNs));CHECK(!p.Select(30,Now-1));
    auto shorter=a;shorter.deadlineNs=Now+10000000;CHECK(p.Receive(shorter,shorter,30,Now));
    CHECK(p.Receive(a,a,30,Now+1));CHECK(!p.Select(30,shorter.deadlineNs)); // Duplicate cannot renew original expiry.
    p.Reset();CHECK(!p.Select(30,Now));CHECK(p.Receive(newer,newer,30,newer.observedNs));
    CHECK(!p.Receive(a,a,30,newer.observedNs));CHECK(!p.Select(30,newer.observedNs));
    p.Reset();CHECK(p.Receive(a,a,30,Now));auto changed=a;--changed.loaded;
    CHECK(!p.Receive(changed,changed,30,Now));CHECK(!p.Select(30,Now));
    CHECK(p.Receive(a,a,30,Now));CHECK(!p.Receive({},{},30,Now));CHECK(!p.Select(30,Now));
    return 0;
}
int BitmapContents(){
    CHECK(std::string_view(FormatAmmoCounter(30,180).data.data())=="30 / 180");
    CHECK(std::string_view(FormatAmmoCounter(0,0).data.data())=="0 / 0");
    CHECK(std::string_view(FormatAmmoCounter(1000000,1000000).data.data())=="1000000 / 1000000");
    CHECK(!FormatAmmoCounter(-1,30).size);
    AmmoCounterBitmap image;CHECK(image.Render(30,180));unsigned white=0,background=0;
    for(unsigned y=0;y<image.Height;++y)for(unsigned x=0;x<image.Width;++x){const auto i=(y*image.Width+x)*4;
        const auto alpha=image.pixels[i+3];CHECK(alpha==0||alpha==192||alpha==255);
        if(alpha==255){CHECK(image.pixels[i]==255&&image.pixels[i+1]==255&&image.pixels[i+2]==255);++white;}
        if(alpha==192){CHECK(!image.pixels[i]&&!image.pixels[i+1]&&!image.pixels[i+2]);++background;}
        if(!x||!y||x==image.Width-1||y==image.Height-1)CHECK(!alpha);
    }
    CHECK(white>100&&background>white);const auto before=image.pixels;
    CHECK(image.Render(29,180));CHECK(image.pixels!=before);CHECK(image.Render(1000000,1000000));
    CHECK(!image.Render(0,-1));for(auto p:image.pixels)CHECK(p==0);
    return 0;
}
}
int main(){if(NativeReadOnlyAdmission()||NativeReadClockOrdering()||PublishedReserveIsPassiveAndBounded()||PairAndFreshness()||BitmapContents())return 1;
    std::cout<<"AmmoCounter: native identity/freshness, paired telemetry and bitmap checks passed; no native acceptance\n";
}
