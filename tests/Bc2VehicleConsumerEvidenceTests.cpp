#include "Test.h"
#include "Bc2VehicleConsumerEvidence.h"
#include <utility>
using namespace fvr::bc2;
namespace v=vehicleConsumerEvidence;
namespace {
VehicleFinalInputCommit Commit(unsigned throttle=0x3ec7ce0c){
    VehicleFinalInputCommit c;c.manager=1;c.player=2;c.actor=3;c.weak=4;c.vehicle=5;c.entry=6;c.router=7;c.cache=0x10000;c.driver=0x20000;
    c.actorGeneration=1;c.seatGeneration=2;c.inputEpoch=3;c.movementEpoch=4;c.space=5;c.routeFingerprint=6;
    c.update=7;c.gather=8;c.input=9;c.throttle=throttle;c.permissions=1909;c.stamp={100,1};return c;
}
v::View View(const VehicleFinalInputCommit& c){
    v::View value;value.owner=v::Copy(c);value.pair=0x30000;value.current=c.cache;value.previous=0x11000;
    value.words={c.throttle,0,c.permissions,8};value.physics=0x40000;value.physicsVtable=0x50000;
    value.observationSerial=1;value.generationsCurrent=true;return value;
}
v::Publication Publication(unsigned throttle=0x3ec7ce0c,std::uint64_t revision=1){
    auto c=Commit(throttle);c.update+=revision;c.gather+=revision;c.input+=revision;
    return v::MakePublication(c,View(c),{revision,1},{revision,1},revision,110);
}
v::Record Record(const v::Publication& p){
    v::Record r;r.publication=p;r.before=r.after=p.view;r.entry=r.returned={p.last.revision,0};
    r.invocation=1;r.returnPublication=p.generation;r.beginQpc=120;r.endQpc=130;r.thread=2;
    r.receiver=p.view.owner.driver+0xc0;r.argument=p.view.pair;r.caller=v::CallerRva;
    r.beforeReadable=r.afterReadable=r.normal=true;return r;
}
int PublicationIdentity(){
    unsigned calls=0;const auto torn=v::SampleEpoch([&](){return std::uint64_t(++calls);},[](){return 0u;});
    CHECK(torn.revision==2&&torn.active==UINT32_MAX&&calls==2);
    const auto stable=v::SampleEpoch([](){return std::uint64_t(4);},[](){return 0u;});
    CHECK((stable==v::Epoch{4,0}));
    v::ProducerClock clock;CHECK(!clock.Begin().active);clock.Start();auto first=clock.Begin();
    CHECK((first.active&&clock.Sample()==v::Epoch{1,1}));
    v::Journal<> journal;auto c=Commit();auto view=View(c);
    journal.Publish(v::MakePublication(c,view,clock.Sample(),clock.Sample(),first.revision,110));
    CHECK(journal.Latest().complete&&journal.Latest().generation==1);
    auto during=Record(journal.Latest());during.entry=during.returned=clock.Sample();v::Complete(during);
    CHECK(!during.joined&&(during.reasons&v::Overlap));
    clock.End(first);CHECK((!first.active&&clock.Sample()==v::Epoch{1,0}));
    auto r=Record(journal.Latest());v::Complete(r);CHECK(r.joined);
    const auto old=r;
    for(unsigned i=0;i<32;++i){
        auto next=clock.Begin();++c.update;++c.gather;++c.input;
        // Two alternating buffers eventually reuse both pointer and scalar.
        std::swap(view.current,view.previous);c.cache=view.current;
        journal.Publish(v::MakePublication(c,view,clock.Sample(),clock.Sample(),next.revision,111+i));clock.End(next);
        CHECK(journal.Latest().complete&&journal.Latest().generation==i+2);
        auto stale=old;stale.entry=stale.returned=clock.Sample();stale.returnPublication=journal.Latest().generation;v::Complete(stale);
        CHECK(!stale.joined&&(stale.reasons&v::Overlap)&&(stale.reasons&v::PublicationChanged));
    }
    CHECK(journal.Latest().view.current==old.before.current&&journal.Latest().view.words==old.before.words);
    clock.Close();CHECK(!clock.Begin().active);clock.Start();CHECK(!clock.Begin().active);return 0;
}
int PublicationRefusals(){
    for(unsigned fault=0;fault<12;++fault){auto c=Commit();auto value=View(c);v::Epoch first{1,1},last=first;unsigned check=0;std::int64_t now=110;
        switch(fault){case 0:++value.owner.seatGeneration;break;case 1:++value.owner.route;break;
        case 2:std::swap(value.current,value.previous);break;case 3:value.previous=value.current;break;
        case 4:value.words[0]=0;break;case 5:value.words[2]^=1;break;
        case 6:c.check=VehicleFinalInputCheck::Owner;break;case 7:first.active=last.active=2;break;
        case 8:++last.revision;break;case 9:now=99;break;case 10:check=v::Read;break;case 11:value.physics=0;break;}
        const auto p=v::MakePublication(c,value,first,last,1,now,check);CHECK(!p.complete&&p.reasons);
        v::Journal<> journal;journal.Publish(Publication());CHECK(journal.Latest().complete);
        journal.Publish(p);CHECK(!journal.Latest().complete&&journal.Latest().generation==2);
        auto r=Record(journal.Latest());v::Complete(r);CHECK(!r.joined&&(r.reasons&v::NoPublication));
    }
    v::ProducerClock clock;clock.Start();auto outer=clock.Begin(),inner=clock.Begin();const auto c=Commit();
    CHECK(!v::MakePublication(c,View(c),clock.Sample(),clock.Sample(),inner.revision,110).complete);
    clock.End(inner);CHECK(!v::MakePublication(c,View(c),clock.Sample(),clock.Sample(),outer.revision,110).complete);
    clock.End(outer);CHECK(clock.Sample().active==0);return 0;
}
int ConsumerRefusals(){
    v::Journal<> journal;journal.Publish(Publication());
    for(unsigned fault=0;fault<23;++fault){auto r=Record(journal.Latest());unsigned expected=0;
        switch(fault){case 0:++r.before.owner.actorGeneration;expected=v::Owner;break;
        case 1:++r.after.owner.seatGeneration;expected=v::Owner;break;
        case 2:++r.after.owner.space;expected=v::Owner;break;
        case 3:++r.before.pair;expected=v::Pair;break;
        case 4:std::swap(r.before.current,r.before.previous);expected=v::Pair;break;
        case 5:++r.after.current;expected=v::Pair;break;
        case 6:r.before.words[0]=0;expected=v::Words;break;
        case 7:r.after.words[1]=0x3f800000;expected=v::Words;break;
        case 8:r.after.words[2]^=1;expected=v::Words;break;
        case 9:r.before.words[3]^=1;expected=v::Words;break;
        case 10:++r.caller;expected=v::Caller;break;case 11:++r.receiver;expected=v::Receiver;break;
        case 12:r.normal=false;expected=v::Unwind;break;
        case 13:r.beforeReadable=false;expected=v::Read;break;case 14:r.afterReadable=false;expected=v::Read;break;
        case 15:++r.returned.revision;expected=v::Overlap;break;
        case 16:++r.returnPublication;expected=v::PublicationChanged;break;
        case 17:r.reasons=v::Nested;expected=v::Nested;break;
        case 18:r.endQpc=119;expected=v::Clock;break;
        case 19:++r.after.physics;expected=v::Physics;break;
        case 20:r.after.zeroControls=1;expected=v::Words;break;
        case 21:r.before.generationsCurrent=false;expected=v::Owner;break;
        case 22:++r.after.observationSerial;expected=v::Owner;break;}
        v::Complete(r);CHECK(!r.joined&&(r.reasons&expected));
        journal.Observe(r);CHECK(journal.Samples()==fault+1);
    }
    // Stable boundary inhibition/braking is data, not a failed native call or
    // evidence that a particular native branch/setter actually executed.
    for(unsigned gate=0;gate<2;++gate){auto p=Publication(0x3ec7ce0c,gate+2);p.view.words[1]=0x3f000000;p.view.zeroControls=gate;
        journal.Publish(p);auto r=Record(journal.Latest());v::Complete(r);CHECK(r.joined&&r.before.words[1]);}
    return 0;
}
int Retention(){
    v::Journal<8> journal;journal.Publish(Publication());
    auto add=[&](unsigned throttle,unsigned brake,unsigned gate,unsigned reason){auto r=Record(journal.Latest());
        r.before.words[0]=r.after.words[0]=throttle;r.before.words[1]=r.after.words[1]=brake;
        r.before.zeroControls=r.after.zeroControls=gate;r.reasons=reason;journal.Observe(r);};
    add(0x3ec7ce0c,0,0,0);add(0,0,0,0);add(0,0,1,v::Caller);add(0,1,0,v::Unwind);
    for(unsigned n=0;n<100;++n){journal.Publish(Publication(0,n+2));add(0,0,0,n%2?v::Read:0);}
    CHECK(journal.Size()==8&&journal.Samples()==104&&journal.Omitted()==96);
    CHECK(journal.PublicationSize()==8&&journal.Publications()==101&&journal.PublicationOmitted()==93);
    unsigned masks=0;bool latest=false;for(unsigned n=0;n<journal.Size();++n){const auto& row=journal.At(n);masks|=row.firstMask;latest|=row.sequence==104;}
    CHECK(masks==63&&latest);return 0;
}
}
int main(){
    if(PublicationIdentity()||PublicationRefusals()||ConsumerRefusals()||Retention())return 1;
    std::puts("Vehicle consumer evidence: publication identity, slot reuse, boundary refusals and bounded retention passed");
}
