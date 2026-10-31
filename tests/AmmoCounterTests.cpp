#include "Bc2AmmoCounterHost.h"
#include "fvr/graphics/AmmoCounterPlacement.h"
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
    auto pair=AmmoCounterPair(a,newer,30,Now+1000000);CHECK(pair);CHECK(pair->observedNs==newer.observedNs&&pair->deadlineNs==a.deadlineNs);
    AmmoCounterPresentation p;CHECK(p.Receive(a,a,30,Now));CHECK(p.Select(30,Now));CHECK(!p.Select(31,Now));
    CHECK(!p.Select(30,a.deadlineNs));CHECK(!p.Select(30,Now-1));
    auto shorter=a;shorter.deadlineNs=Now+10000000;CHECK(p.Receive(shorter,shorter,30,Now));
    CHECK(p.Receive(a,a,30,Now+1));CHECK(!p.Select(30,shorter.deadlineNs)); // Duplicate cannot renew original expiry.
    p.Reset();CHECK(!p.Select(30,Now));CHECK(p.Receive(newer,newer,30,newer.observedNs));
    CHECK(!p.Receive(a,a,30,newer.observedNs));CHECK(p.Select(30,newer.observedNs)->sequence==newer.sequence);
    p.Reset();CHECK(p.Receive(a,a,30,Now));auto changed=a;--changed.loaded;
    CHECK(!p.Receive(changed,changed,30,Now));CHECK(p.Select(30,Now)->loaded==a.loaded);
    CHECK(p.Receive(a,a,30,Now));CHECK(!p.Receive({},{},30,Now));CHECK(!p.Select(30,Now));
    return 0;
}
int DisplaySurvivesAmmoGapsWithCurrentOwner(){
    const auto native=Reserve();const auto admitted=Sample();auto current=Owner();
    AmmoCounterPresentation hud;CHECK(hud.Receive(admitted,admitted,30,Now));
    for(const auto elapsed:{100000000LL,200000000LL,500000000LL,3000000000LL}){
        const auto now=Now+elapsed;
        current.observedNs=now-1000000;current.deadlineNs=now+90000000;
        const auto ownerOnly=AmmoCounterHostSample(native,current,now);
        CHECK(AmmoCounterOwnerOnlyFresh(ownerOnly,now));CHECK(!AmmoCounterFresh(ownerOnly,now));
        CHECK(!AmmoCounterPair(ownerOnly,ownerOnly,30,now));
        CHECK(ownerOnly.observedNs==current.observedNs&&ownerOnly.deadlineNs==current.deadlineNs);
        CHECK(hud.Receive(ownerOnly,ownerOnly,30,now));
        const auto* shown=hud.Select(30,now);CHECK(shown&&*shown==admitted);
        CHECK(!AmmoCounterFresh(*shown,now)); // Display retention does not fabricate native freshness.
        CHECK(!hud.Select(30,current.deadlineNs));
    }
    const auto now=current.observedNs+1000000;
    const auto missing=AmmoCounterHostSample({},current,now);
    CHECK(hud.Receive(missing,missing,30,now));CHECK(*hud.Select(30,now)==admitted);
    auto bad=native;bad.verified=false;
    CHECK(hud.Receive(AmmoCounterHostSample(bad,current,now),missing,30,now));
    CHECK(*hud.Select(30,now)==admitted);
    auto fresh=native;++fresh.sequence;fresh.observedNs=now;fresh.deadlineNs=now+100000000;--fresh.loaded;
    const auto updated=AmmoCounterHostSample(fresh,current,now);
    CHECK(hud.Receive(missing,updated,30,now));CHECK(*hud.Select(30,now)==updated);
    CHECK(hud.Receive(updated,missing,30,now));CHECK(*hud.Select(30,now)==updated);
    auto empty=fresh;++empty.sequence;++empty.observedNs;++empty.deadlineNs;empty.loaded=empty.reserve=0;
    const auto emptySample=AmmoCounterHostSample(empty,current,now+1);
    CHECK(hud.Receive(missing,emptySample,30,now+1));CHECK(hud.Select(30,now+1)->loaded==0);
    CHECK(hud.Select(30,now+1)->reserve==0); // A valid empty count still replaces retained digits.
    AmmoCounterPresentation unseen;CHECK(!unseen.Receive(missing,missing,30,now));CHECK(!unseen.Select(30,now));
    return 0;
}
int RejectedCountsCannotRenewOwnership(){
    auto admitted=Sample();admitted.sequence=10;
    for(unsigned fault=0;fault<4;++fault){
        AmmoCounterPresentation hud;CHECK(hud.Receive(admitted,admitted,30,Now));
        for(const auto elapsed:{50000000LL,100000000LL,200000000LL}){
            const auto now=Now+elapsed;auto left=admitted;
            left.observedNs=now;left.deadlineNs=now+AmmoCounterMaxAgeNs;
            if(fault==1||fault==2)++left.sequence;if(fault==3)--left.sequence;
            auto right=left;
            if(fault==1)--right.loaded; // Conflicting counts within a sequence.
            if(fault==2){++right.sequence;--right.observedNs;--right.deadlineNs;} // Sequence/time inversion, with individually valid leases.
            CHECK(AmmoCounterFresh(left,now)&&AmmoCounterFresh(right,now));
            CHECK(!hud.Receive(left,right,30,now));
            CHECK(hud.Select(30,admitted.deadlineNs-1));
            CHECK(!hud.Select(30,admitted.deadlineNs)); // Even repeated fresh-looking replays cannot extend the lease.
        }
    }
    for(unsigned reverse=0;reverse<2;++reverse){
        AmmoCounterPresentation hud;CHECK(hud.Receive(admitted,admitted,30,Now));
        const auto now=admitted.deadlineNs;auto current=Owner();
        current.observedNs=now;current.deadlineNs=now+AmmoCounterMaxAgeNs;
        const auto ownerOnly=AmmoCounterHostSample({},current,now);
        auto replay=admitted;replay.observedNs=now;replay.deadlineNs=current.deadlineNs;
        CHECK(!hud.Receive(reverse?ownerOnly:replay,reverse?replay:ownerOnly,30,now));
        const auto* shown=hud.Select(30,now);CHECK(shown&&*shown==admitted);
        CHECK(!AmmoCounterFresh(*shown,now)); // Only the independent owner marker renews display continuity.
        CHECK(!hud.Select(30,current.deadlineNs));
    }
    return 0;
}
int DisplayOwnerLifetimeAndDuplicatePackets(){
    const auto admitted=Sample();auto current=Owner();AmmoCounterPresentation hud;
    CHECK(hud.Receive(admitted,admitted,30,Now));
    current.observedNs=Now+1000000;current.deadlineNs=Now+110000000;
    const auto ownerOnly=AmmoCounterHostSample({},current,current.observedNs);
    CHECK(hud.Receive(ownerOnly,ownerOnly,30,current.observedNs));
    CHECK(hud.Select(30,admitted.deadlineNs));
    auto extended=ownerOnly;extended.deadlineNs+=10000000;
    CHECK(hud.Receive(extended,extended,30,current.observedNs+1));
    CHECK(!hud.Select(30,ownerOnly.deadlineNs)); // Duplicate owner packets cannot renew their source deadline.
    CHECK(!hud.Receive(ownerOnly,ownerOnly,30,ownerOnly.deadlineNs));
    CHECK(!hud.Select(30,ownerOnly.deadlineNs));
    for(unsigned fault=0;fault<10;++fault){
        hud.Reset();CHECK(hud.Receive(admitted,admitted,30,Now));auto owner=Owner();
        if(fault==0)owner.alive=false;if(fault==1)owner.onFoot=false;if(fault==2)owner.held=false;
        if(fault==3)owner.deadlineNs=Now;if(fault==4)owner.observedNs=Now+1;
        if(fault==5)owner.owner.weapon=0;if(fault==6)owner.owner.actorGeneration=0;
        if(fault==7)owner.owner.equipGeneration=0;if(fault==8)owner.owner.space=0;
        if(fault==9)owner.deadlineNs=Now+AmmoCounterOwnerMaxAgeNs+1;
        const auto lost=AmmoCounterHostSample({},owner,Now);CHECK(lost==AmmoCounterSample{});
        CHECK(!hud.Receive(lost,lost,30,Now));CHECK(!hud.Select(30,Now));
        const auto recovered=AmmoCounterHostSample({},Owner(),Now);
        CHECK(!hud.Receive(recovered,recovered,30,Now));CHECK(!hud.Select(30,Now));
    }
    for(unsigned changed=0;changed<3;++changed){
        auto owner=Owner();if(changed==0)++owner.owner.actorGeneration;
        if(changed==1)++owner.owner.equipGeneration;if(changed==2)++owner.owner.space;
        const auto switched=AmmoCounterHostSample({},owner,Now);
        hud.Reset();CHECK(hud.Receive(admitted,admitted,30,Now));
        CHECK(!hud.Receive(admitted,switched,owner.owner.space,Now));CHECK(!hud.Select(30,Now));
        hud.Reset();CHECK(hud.Receive(admitted,admitted,30,Now));
        CHECK(!hud.Receive(switched,switched,owner.owner.space,Now));CHECK(!hud.Select(owner.owner.space,Now));
        auto fresh=Reserve();fresh.identity.owner=owner.owner;
        const auto sample=AmmoCounterHostSample(fresh,owner,Now);
        CHECK(hud.Receive(switched,sample,owner.owner.space,Now));CHECK(*hud.Select(owner.owner.space,Now)==sample);
    }
    hud.Reset();CHECK(!hud.Select(30,Now));
    return 0;
}
int OwnerMarkersCannotAdmitCounts(){
    const auto ownerOnly=AmmoCounterHostSample({},Owner(),Now);
    CHECK(AmmoCounterOwnerOnlyFresh(ownerOnly,Now));
    for(unsigned fault=0;fault<10;++fault){
        auto bad=ownerOnly;
        if(fault==0)bad.sequence=1;if(fault==1)bad.reserved=2;if(fault==2)bad.loaded=1;
        if(fault==3)bad.reserve=1;if(fault==4)bad.capacity=30;if(fault==5)bad.observedNs=Now+1;
        if(fault==6)bad.deadlineNs=Now;if(fault==7)bad.actorGeneration=0;
        if(fault==8)bad.equipmentGeneration=0;if(fault==9)bad.spaceGeneration=0;
        CHECK(!AmmoCounterOwnerOnlyFresh(bad,Now));CHECK(!AmmoCounterFresh(bad,Now));
        AmmoCounterPresentation hud;CHECK(hud.Receive(Sample(),Sample(),30,Now));
        CHECK(!hud.Receive(bad,ownerOnly,30,Now));CHECK(!hud.Select(30,Now));
    }
    auto wrongAmmo=Reserve();++wrongAmmo.identity.owner.weapon;
    const auto currentOnly=AmmoCounterHostSample(wrongAmmo,Owner(),Now);
    CHECK(currentOnly==ownerOnly);CHECK(!AmmoCounterFresh(currentOnly,Now));
    return 0;
}
int CountsChangingBetweenEyes(){
    AmmoCounterPresentation hud;auto left=Sample();
    for(int loaded=left.loaded-1;loaded>=0;--loaded){
        auto right=left;++right.sequence;right.observedNs+=1000000;
        right.deadlineNs=right.observedNs+AmmoCounterMaxAgeNs;right.loaded=loaded;
        CHECK(hud.Receive(left,right,30,right.observedNs));
        CHECK(hud.Select(30,right.observedNs)->loaded==loaded);
        auto reverse=AmmoCounterPair(right,left,30,right.observedNs);
        CHECK(reverse&&reverse->sequence==right.sequence&&reverse->loaded==loaded);
        CHECK(reverse->deadlineNs==left.deadlineNs); // Never prolong the pair's lease.
        left=right;
    }
    auto reload=left;++reload.sequence;reload.observedNs+=1000000;
    reload.deadlineNs=reload.observedNs+AmmoCounterMaxAgeNs;
    reload.loaded=reload.capacity;reload.reserve-=reload.capacity;
    CHECK(hud.Receive(left,reload,30,reload.observedNs));
    CHECK(hud.Select(30,reload.observedNs)->loaded==reload.capacity);
    auto switched=reload;++switched.equipmentGeneration;
    CHECK(!hud.Receive(reload,switched,30,reload.observedNs));CHECK(!hud.Select(30,reload.observedNs));
    auto impossible=reload;impossible.sequence=left.sequence;impossible.observedNs=left.observedNs;
    CHECK(!AmmoCounterPair(left,impossible,30,reload.observedNs));
    auto reordered=reload;reordered.observedNs=left.observedNs-1;
    CHECK(!AmmoCounterPair(left,reordered,30,reload.observedNs));
    CHECK(!AmmoCounterPair(left,reload,30,left.deadlineNs));
    return 0;
}
int WeaponSidePlacement(){
    math::Pose head,grip,aim;head.position={0,1.7f,0};grip.position=aim.position={.2f,1.4f,-.45f};
    const auto initial=AmmoCounterBesideGun(head,grip,aim);CHECK(initial);
    CHECK(Near(initial->position.x,.05f)&&Near(initial->position.y,1.435f)&&Near(initial->position.z,-.57f));
    // Moving the gun changes label position; looking down does not drag it to a fixed visor location.
    grip.position.x+=.12f;const auto moved=AmmoCounterBesideGun(head,grip,aim);CHECK(moved);
    CHECK(Near(moved->position.x-initial->position.x,.12f));
    head.orientation={std::sin(.15f),0,0,std::cos(.15f)};
    const auto looked=AmmoCounterBesideGun(head,grip,aim);CHECK(looked);
    CHECK(Near(looked->position.x,moved->position.x)&&Near(looked->position.y,moved->position.y)&&Near(looked->position.z,moved->position.z));
    CHECK(Near(looked->orientation.x,head.orientation.x));
    head.orientation={};aim.orientation={0,std::sin(.785398163f),0,std::cos(.785398163f)};
    const auto turned=AmmoCounterBesideGun(head,grip,aim);CHECK(turned);
    CHECK(Near(turned->position.x-grip.position.x,-.12f)&&Near(turned->position.z-grip.position.z,.15f));
    // A common reference-space translation preserves the physical relation.
    const auto old=*turned;for(auto* p:{&head,&grip,&aim}){p->position.x+=12;p->position.y-=3;p->position.z+=8;}
    const auto rebased=AmmoCounterBesideGun(head,grip,aim);CHECK(rebased);
    CHECK(Near(rebased->position.x-old.position.x,12)&&Near(rebased->position.y-old.position.y,-3)&&Near(rebased->position.z-old.position.z,8));
    head={};grip={};aim={};CHECK(!AmmoCounterBesideGun(head,grip,aim)); // inside near-head exclusion
    grip.position.z=1;CHECK(!AmmoCounterBesideGun(head,grip,aim)); // behind the user
    grip.position.z=-3;CHECK(!AmmoCounterBesideGun(head,grip,aim));
    grip.position.z=-.5f;aim.orientation={0,0,0,0};CHECK(!AmmoCounterBesideGun(head,grip,aim));
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
int main(){if(NativeReadOnlyAdmission()||NativeReadClockOrdering()||PublishedReserveIsPassiveAndBounded()||PairAndFreshness()||
    DisplaySurvivesAmmoGapsWithCurrentOwner()||RejectedCountsCannotRenewOwnership()||DisplayOwnerLifetimeAndDuplicatePackets()||OwnerMarkersCannotAdmitCounts()||
    CountsChangingBetweenEyes()||WeaponSidePlacement()||BitmapContents())return 1;
    std::cout<<"AmmoCounter: strict native samples, retained same-owner display, paired telemetry and bitmap checks passed; no native acceptance\n";
}
