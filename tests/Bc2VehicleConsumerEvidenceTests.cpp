#include "Test.h"
#include "Bc2VehicleConsumerEvidence.h"
#include <cstring>
#include <map>
#include <utility>
#include <vector>
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
namespace d=v::destination;
struct DestinationMemory {
    static constexpr unsigned Image=0x400000,ImageSize=0x1200000,Driver=0x20000,Physics=0x40000,Control=0x60000;
    std::map<unsigned,unsigned> words{{Driver+0xd4,Physics},{Physics,Image+d::VtableRva},
        {Image+d::VtableRva+0x50,Image+d::ThrottleRva},{Image+d::VtableRva+0x58,Image+d::BrakeRva},
        {Physics+0x0c,Control},{Control+0x20,0x3ec7ce0c},{Control+0x28,0}};
    std::array<unsigned char,17> throttle=d::ThrottleBody,brake=d::BrakeBody;
    unsigned calls=0,failAt=0,changeAt=0,changeAddress=0,changeWord=0;
    bool changeCode=false;
    bool Read(unsigned address,void* destination,std::size_t size){
        ++calls;
        if(calls==changeAt){if(changeCode)throttle[0]^=1;else words[changeAddress]=changeWord;}
        if(calls==failAt)return false;
        if(size==17){
            const auto* bytes=address==Image+d::ThrottleRva?&throttle:address==Image+d::BrakeRva?&brake:nullptr;
            if(!bytes)return false;std::memcpy(destination,bytes->data(),size);return true;
        }
        if(size!=sizeof(unsigned))return false;
        const auto found=words.find(address);if(found==words.end())return false;
        std::memcpy(destination,&found->second,size);return true;
    }
    d::Snapshot Capture(){return d::Read(Image,ImageSize,Driver,Physics,Image+d::VtableRva,
        [&](unsigned at,void* destination,std::size_t size){return Read(at,destination,size);});}
};
int DestinationReads(){
    DestinationMemory memory;const auto positive=memory.Capture();
    CHECK(!positive.reasons&&positive.attempted==d::AllFields&&positive.read==d::AllFields&&memory.calls==18);
    CHECK(positive.physics==DestinationMemory::Physics&&positive.control==DestinationMemory::Control);
    CHECK(positive.words[0]==0x3ec7ce0c&&positive.words[1]==0&&positive.wordsAgain==positive.words);
    CHECK(d::SameObject(positive,positive));
    memory.words[DestinationMemory::Control+0x20]=0;
    const auto neutral=memory.Capture();
    CHECK(!neutral.reasons&&neutral.words[0]==0&&neutral.words[1]==0&&d::SameObject(positive,neutral));
    // Every memory/code read has its own failed-read bit. Missing evidence is
    // retained as a refusal and cannot qualify as a stable destination view.
    for(unsigned step=1;step<=18;++step){DestinationMemory failed;failed.failAt=step;const auto sample=failed.Capture();
        CHECK((sample.reasons&d::ReadFailed)&&(sample.attempted&(1u<<(step-1)))&&!(sample.read&(1u<<(step-1))));
        CHECK(!d::SameObject(sample,positive));
    }
    for(unsigned fault=0;fault<5;++fault){DestinationMemory invalid;
        if(fault==0)invalid.words[DestinationMemory::Physics]+=4;
        if(fault==1)invalid.words[DestinationMemory::Image+d::VtableRva+0x50]+=4;
        if(fault==2)invalid.words[DestinationMemory::Image+d::VtableRva+0x58]+=4;
        if(fault==3)invalid.throttle[0]^=1;
        if(fault==4)invalid.brake[0]^=1;
        const auto sample=invalid.Capture();CHECK(sample.reasons&(fault<3?d::Type:d::Code));
        CHECK(!(sample.attempted&d::Control));
    }
    DestinationMemory nullControl;nullControl.words[DestinationMemory::Physics+0xc]=0;
    CHECK(nullControl.Capture().reasons&d::ReadFailed);
    unsigned reads=0;
    const auto wrapped=d::Read(0xff000000,0x1200000,DestinationMemory::Driver,DestinationMemory::Physics,0,
        [&](unsigned,void*,std::size_t){++reads;return true;});
    CHECK((wrapped.reasons&d::Type)&&reads==0);
    return 0;
}
int DestinationBookends(){
    for(unsigned fault=0;fault<7;++fault){DestinationMemory memory;
        switch(fault){
        case 0:memory.changeAt=10;memory.changeAddress=DestinationMemory::Control+0x20;memory.changeWord=0;break;
        case 1:memory.changeAt=11;memory.changeAddress=DestinationMemory::Control+0x28;memory.changeWord=0x3f800000;break;
        case 2:memory.changeAt=12;memory.changeAddress=DestinationMemory::Physics+0xc;memory.changeWord=0x70000;break;
        case 3:memory.changeAt=13;memory.changeAddress=DestinationMemory::Driver+0xd4;memory.changeWord=0x50000;break;
        case 4:memory.changeAt=14;memory.changeAddress=DestinationMemory::Physics;memory.changeWord=DestinationMemory::Image+d::VtableRva+4;break;
        case 5:memory.changeAt=15;memory.changeAddress=DestinationMemory::Image+d::VtableRva+0x50;memory.changeWord=DestinationMemory::Image+d::BrakeRva;break;
        case 6:memory.changeAt=17;memory.changeCode=true;break;
        }
        const auto sample=memory.Capture();
        CHECK(sample.read==d::AllFields&&(sample.reasons&(fault<2?d::Values:fault<6?d::Identity:d::Code)));
        if(fault<2)CHECK(sample.words!=sample.wordsAgain);
        if(fault==2)CHECK(sample.control==DestinationMemory::Control&&sample.controlAgain==0x70000);
    }
    return 0;
}
int DestinationBoundaries(){
    DestinationMemory memory;const auto positive=memory.Capture();memory.words[DestinationMemory::Control+0x20]=0;
    const auto neutral=memory.Capture();
    auto p=Publication();p.view.physicsVtable=DestinationMemory::Image+d::VtableRva;
    v::Journal<> journal;journal.Publish(p);auto row=Record(journal.Latest());
    row.before.destination=positive;row.after.destination=neutral;v::Complete(row);
    CHECK(row.joined&&row.destinationComparable&&!row.destinationReasons);
    CHECK(row.before.destination.words[0]&&!row.after.destination.words[0]);
    // The destination may change across the original call. Equality of its
    // values is neither required nor evidence that a setter was invoked.
    for(unsigned fault=0;fault<7;++fault){auto broken=row;
        switch(fault){
        case 0:broken.after.destination.control=0x70000;break;
        case 1:broken.after.destination.reasons=d::ReadFailed;break;
        case 2:broken.after.destination.read^=d::Throttle;break;
        case 3:broken.after.destination.physics+=4;break;
        case 4:broken.after.destination.vtable+=4;break;
        case 5:broken.after.destination.reasons=d::Context;break;
        case 6:broken.after.destination={};break;
        }
        v::Complete(broken);CHECK(broken.joined&&!broken.destinationComparable&&broken.destinationReasons);
    }
    // An independently readable destination cannot erase strict publication,
    // owner, producer overlap or unwind refusals from the original observer.
    for(unsigned fault=0;fault<5;++fault){auto broken=row;
        switch(fault){case 0:broken.publication.complete=false;break;case 1:++broken.after.current;break;
        case 2:++broken.after.owner.seatGeneration;break;case 3:++broken.returned.revision;break;case 4:broken.normal=false;break;}
        v::Complete(broken);CHECK(!broken.joined&&!broken.destinationComparable&&(broken.destinationReasons&d::Context));
        CHECK(broken.before.destination.words[0]==0x3ec7ce0c&&broken.after.destination.words[0]==0);
    }
    return 0;
}
namespace s=v::state;
struct StateMemory {
    static constexpr unsigned Image=DestinationMemory::Image,ImageSize=DestinationMemory::ImageSize,
        Driver=DestinationMemory::Driver,Physics=DestinationMemory::Physics,Config=0x70000;
    std::map<unsigned,unsigned> words{{Driver+0xd4,Physics},{Physics,Image+d::VtableRva},
        {Image+d::VtableRva+0x1c,Image+s::UpdateRva},{Image+d::VtableRva+0x34,Image+s::SetterRva},
        {Physics+0x08,Config},{Physics+0x80,3},{Config+0x84,1}};
    std::map<unsigned,std::vector<unsigned char>> bodies{
        {Image+s::ConstructorRva,{s::ConstructorBody.begin(),s::ConstructorBody.end()}},
        {Image+s::OriginRva,{s::OriginBody.begin(),s::OriginBody.end()}},
        {Image+s::SetterRva,{s::SetterBody.begin(),s::SetterBody.end()}},
        {Image+s::TransitionRva,{s::TransitionBody.begin(),s::TransitionBody.end()}},
        {Image+s::UpdateRva,{s::UpdateBody.begin(),s::UpdateBody.end()}}};
    unsigned calls=0,failAt=0,changeAt=0,changeAddress=0,changeWord=0,changeCode=0;
    bool Read(unsigned address,void* destination,std::size_t size){
        ++calls;
        if(calls==changeAt){if(changeCode)bodies.at(changeCode).back()^=1;else words[changeAddress]=changeWord;}
        if(calls==failAt)return false;
        const auto body=bodies.find(address);
        if(body!=bodies.end()){
            if(size!=body->second.size())return false;std::memcpy(destination,body->second.data(),size);return true;
        }
        const auto word=words.find(address);if(size!=sizeof(unsigned)||word==words.end())return false;
        std::memcpy(destination,&word->second,size);return true;
    }
    s::Snapshot Capture(unsigned physics=Physics){return s::Read(Image,ImageSize,Driver,physics,Image+d::VtableRva,
        [&](unsigned at,void* destination,std::size_t size){return Read(at,destination,size);});}
};
int StateReads(){
    StateMemory memory;const auto initial=memory.Capture();
    CHECK(!initial.reasons&&initial.attempted==s::AllFields&&initial.read==s::AllFields&&memory.calls==24);
    CHECK(initial.live==3&&initial.initial==1&&initial.liveAgain==3&&initial.initialAgain==1);
    CHECK(initial.config==StateMemory::Config&&s::SameObject(initial,initial));
    // Initial-state provenance is not a live-state invariant. Even unknown raw
    // states remain observable without inventing an enum or a gameplay result.
    memory.words[StateMemory::Physics+0x80]=UINT32_MAX;const auto unknown=memory.Capture();
    CHECK(!unknown.reasons&&unknown.live==UINT32_MAX&&s::SameObject(initial,unknown));
    for(unsigned step=1;step<=24;++step){StateMemory failed;failed.failAt=step;const auto sample=failed.Capture();
        CHECK((sample.reasons&s::ReadFailed)&&(sample.attempted&(1u<<(step-1)))&&!(sample.read&(1u<<(step-1))));
        CHECK(!s::SameObject(sample,initial));
        if(step==10)CHECK(!(sample.reasons&s::Null)&&!(sample.attempted&s::Initial)&&sample.live==3);
    }
    for(unsigned fault=0;fault<4;++fault){StateMemory wrong;
        if(fault==0)wrong.words[StateMemory::Driver+0xd4]+=4;
        if(fault==1)wrong.words[StateMemory::Physics]+=4;
        if(fault==2)wrong.words[StateMemory::Image+d::VtableRva+0x1c]+=4;
        if(fault==3)wrong.words[StateMemory::Image+d::VtableRva+0x34]+=4;
        // Give a replacement physics pointer a readable but incorrect vtable.
        wrong.words[StateMemory::Physics+4]=StateMemory::Image+d::VtableRva;
        const auto sample=wrong.Capture();CHECK((sample.reasons&s::Type)&&!(sample.attempted&s::Config));
    }
    for(const auto rva:{s::ConstructorRva,s::OriginRva,s::SetterRva,s::TransitionRva,s::UpdateRva}){
        StateMemory wrong;wrong.bodies.at(StateMemory::Image+rva).back()^=1;const auto sample=wrong.Capture();
        CHECK((sample.reasons&s::Code)&&!(sample.attempted&(s::Config|s::Live|s::Initial)));
    }
    StateMemory nullConfig;nullConfig.words[StateMemory::Physics+8]=0;const auto null=nullConfig.Capture();
    CHECK((null.reasons&s::Null)&&null.live==3&&!(null.read&s::Initial));
    StateMemory nullPhysics;nullPhysics.words[StateMemory::Driver+0xd4]=0;
    CHECK(nullPhysics.Capture().reasons&s::Null);
    StateMemory overflowConfig;overflowConfig.words[StateMemory::Physics+8]=UINT32_MAX-0x80;
    CHECK(overflowConfig.Capture().reasons&s::Overflow);
    StateMemory overflowPhysics;constexpr unsigned highPhysics=UINT32_MAX-0x40;
    overflowPhysics.words[StateMemory::Driver+0xd4]=highPhysics;
    overflowPhysics.words[highPhysics]=StateMemory::Image+d::VtableRva;
    overflowPhysics.words[highPhysics+8]=StateMemory::Config;
    CHECK(overflowPhysics.Capture(highPhysics).reasons&s::Overflow);
    unsigned reads=0;const auto reader=[&](unsigned,void*,std::size_t){++reads;return true;};
    CHECK(s::Read(0xff000000,StateMemory::ImageSize,StateMemory::Driver,StateMemory::Physics,0,reader).reasons&s::Overflow);
    CHECK(s::Read(StateMemory::Image,StateMemory::ImageSize,UINT32_MAX-0x80,StateMemory::Physics,0,reader).reasons&s::Overflow);
    CHECK(s::Read(StateMemory::Image,StateMemory::ImageSize,0,StateMemory::Physics,0,reader).reasons&s::Null);
    CHECK(s::Read(StateMemory::Image,StateMemory::ImageSize,1,StateMemory::Physics,0,reader).reasons&s::Type);
    CHECK(reads==0);return 0;
}
int StateBookends(){
    for(unsigned fault=0;fault<7;++fault){StateMemory memory;
        switch(fault){
        case 0:memory.changeAt=13;memory.changeAddress=StateMemory::Physics+0x80;memory.changeWord=4;break;
        case 1:memory.changeAt=14;memory.changeAddress=StateMemory::Config+0x84;memory.changeWord=2;break;
        case 2:memory.changeAt=15;memory.changeAddress=StateMemory::Physics+8;memory.changeWord=0x71000;break;
        case 3:memory.changeAt=16;memory.changeAddress=StateMemory::Driver+0xd4;memory.changeWord=0x41000;break;
        case 4:memory.changeAt=17;memory.changeAddress=StateMemory::Physics;memory.changeWord=StateMemory::Image+d::VtableRva+4;break;
        case 5:memory.changeAt=18;memory.changeAddress=StateMemory::Image+d::VtableRva+0x1c;memory.changeWord=0;break;
        case 6:memory.changeAt=19;memory.changeAddress=StateMemory::Image+d::VtableRva+0x34;memory.changeWord=0;break;
        }
        const auto sample=memory.Capture();CHECK(sample.read==s::AllFields&&(sample.reasons&(fault<2?s::Values:s::Identity)));
        if(fault==0)CHECK(sample.live==3&&sample.liveAgain==4);
        if(fault==1)CHECK(sample.initial==1&&sample.initialAgain==2);
        if(fault==2)CHECK(sample.config==StateMemory::Config&&sample.configAgain==0x71000);
    }
    unsigned step=20;
    for(const auto rva:{s::ConstructorRva,s::OriginRva,s::SetterRva,s::TransitionRva,s::UpdateRva}){
        StateMemory memory;memory.changeAt=step++;memory.changeCode=StateMemory::Image+rva;const auto sample=memory.Capture();
        CHECK(sample.read==s::AllFields&&(sample.reasons&s::Code)&&sample.live==sample.liveAgain);
    }
    return 0;
}
int StateBoundaries(){
    StateMemory memory;const auto before=memory.Capture();memory.words[StateMemory::Physics+0x80]=4;
    const auto after=memory.Capture();DestinationMemory destination;const auto delivered=destination.Capture();
    auto p=Publication();p.view.physicsVtable=StateMemory::Image+d::VtableRva;
    v::Journal<> journal;journal.Publish(p);auto row=Record(journal.Latest());
    row.before.destination=row.after.destination=delivered;row.before.state=before;row.after.state=after;v::Complete(row);
    CHECK(row.joined&&!row.reasons&&row.destinationComparable&&!row.destinationReasons&&row.stateComparable&&!row.stateReasons);
    CHECK(row.before.state.live==3&&row.after.state.live==4&&row.after.state.initial==1);
    // Every new read failure leaves the already-valid18-read destination and
    // original publication join unchanged. Preserve refusal and raw state.
    for(unsigned step=1;step<=24;++step){StateMemory failed;failed.failAt=step;auto broken=row;
        broken.after.state=failed.Capture();v::Complete(broken);
        CHECK(broken.joined&&broken.reasons==row.reasons&&broken.destinationComparable&&broken.destinationReasons==row.destinationReasons);
        CHECK(!broken.stateComparable&&(broken.stateReasons&s::ReadFailed)&&broken.before.state==before);
    }
    for(unsigned fault=0;fault<9;++fault){auto broken=row;
        switch(fault){case 0:broken.after.state.config+=4;break;case 1:broken.after.state.reasons=s::Code;break;
        case 2:broken.after.state.reasons=s::Type;break;case 3:broken.after.state.reasons=s::Values;break;
        case 4:broken.after.state.read^=s::Live;break;case 5:broken.after.state.physics+=4;break;
        case 6:broken.after.state.vtable+=4;break;case 7:broken.after.state.reasons=s::Context;break;
        case 8:broken.after.state={};break;}
        v::Complete(broken);CHECK(broken.joined&&!broken.reasons&&broken.destinationComparable&&!broken.destinationReasons);
        CHECK(!broken.stateComparable&&broken.stateReasons);
    }
    for(unsigned fault=0;fault<5;++fault){auto broken=row;
        switch(fault){case 0:broken.publication.complete=false;break;case 1:++broken.after.current;break;
        case 2:++broken.after.owner.seatGeneration;break;case 3:++broken.returned.revision;break;case 4:broken.normal=false;break;}
        v::Complete(broken);CHECK(!broken.joined&&!broken.stateComparable&&(broken.stateReasons&s::Context));
        CHECK(broken.before.state==before&&broken.after.state==after);
    }
    return 0;
}
}
int main(){
    if(PublicationIdentity()||PublicationRefusals()||ConsumerRefusals()||Retention()||
       DestinationReads()||DestinationBookends()||DestinationBoundaries()||StateReads()||StateBookends()||StateBoundaries())return 1;
    std::puts("Vehicle consumer evidence: publication identity, bounded destination/state views, boundary refusals and retention passed");
}
