#include "Test.h"
#include "Bc2VehicleShotObserver.h"
#include <chrono>
#include <cstring>
#include <future>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <thread>
#include <vector>
using namespace fvr::bc2;
namespace fvr::bc2 {
struct VehicleShotObserverTestAccess {
    static constexpr std::size_t SnapshotBytes(){return sizeof(VehicleShotObserver::Snapshot);}
    template<class F>static void Contended(VehicleShotObserver& observer,F action){
        std::promise<void> held,release;auto released=release.get_future();
        std::thread owner([&]{std::lock_guard lock(observer.mutex_);held.set_value();released.wait_for(std::chrono::seconds(2));});
        held.get_future().wait();action();release.set_value();owner.join();
    }
};
}
namespace {
struct Fixture {
    std::vector<unsigned char> bytes=std::vector<unsigned char>(0x1600000);
    std::map<unsigned,std::string> types;
    VehicleShotBinding binding{0x400000,true};VehicleShotSource source{};VehicleRouteSnapshot seat{};
    VehicleShotCall call{0x35000,0x370a0,0x808258,0x39000};
    unsigned collection=0x20000,component=0x30000,mounted=0x32000,effects=0x35000,firing=0x36000,primary=0x37000,queue=0x40000;
    unsigned failReadAt=0;
    void Put(unsigned at,unsigned x){std::memcpy(bytes.data()+at,&x,4);}
    unsigned Get(unsigned at){unsigned x=0;std::memcpy(&x,bytes.data()+at,4);return x;}
    Fixture(){
        source.input=7;source.space=8;source.observedNs=100;source.deadlineNs=200;source.fire=1;
        source.owner={0x21000,0x22000,0x23000,0x24000,2,3};
        seat.identity={1,2,3,0x25000,0x26000,0x27000,0x28000};seat.player=source.owner.player;seat.weak=source.owner.weak;seat.fingerprint=44;
        auto& p=source.profile;p.owner=seat.identity;p.collection=collection;p.count=p.layout=3;p.weapons=1;p.profile.routeFingerprint=44;
        p.profile.role=VehicleSeatRole::Gunner;p.rows[0]={0x29000,0,0,255<<8};p.rows[1]={seat.identity.entry,seat.identity.entry,0,0};p.rows[2]={component,0,0,1<<8};
        Put(collection,seat.identity.controlled);bytes[collection+13]=bytes[collection+14]=3;
        std::memcpy(bytes.data()+collection+16,p.rows.data(),p.count*16);
        Put(component,binding.image+0x1030d38);Put(component+12,0x31000);Put(component+0xd0,mounted);
        Put(0x31000+216,firing);Put(mounted,0x33000);Put(0x33000+8,0x7d4c10);Put(0x33000+12,0x7bf840);
        Put(mounted+12,firing);Put(mounted+0x134,effects);Put(firing+64,primary);Put(primary+0x4c,8);
        Put(effects,0x34000);Put(0x34000+0x4c,0x81c170);Put(effects+16,firing);
        Put(effects+0xd0,0x34100);Put(0x34100+12,0x808220);
        Put(mounted+4+0x18,0x34200);Put(mounted+4+0x1c,0x34204);Put(0x34200,effects+0xd0);
        Put(effects+0x650,queue);Put(effects+0x654,queue);Put(effects+0x658,queue+16*0xb0);Put(call.config+0x44,0x38000);
        types[component]="ClientWeaponComponent";types[effects]="ClientWeaponFiringEffects";types[firing]="WeaponFiringData";types[primary]="FiringFunctionData";
        for(auto at:{component,component+12,component+0xd0,0x31000u+216,mounted+12,firing+64,primary+0x4c})p.words[p.wordCount++]={at,Get(at)};
    }
    VehicleRouteMemory Memory(){return {this,[](void* c,unsigned at,void* out,std::size_t n){auto& f=*static_cast<Fixture*>(c);if(at==f.failReadAt||std::uint64_t(at)+n>f.bytes.size())return false;std::memcpy(out,f.bytes.data()+at,n);return true;},
        [](void* c,unsigned at,const char* type){auto& f=*static_cast<Fixture*>(c);return f.types.contains(at)&&f.types[at]==type;}};}
    VehicleShotSample Read(std::int64_t now=150){return ReadVehicleShot(Memory(),binding,source,seat,call,now);}
};
int MatchedCallbackAndQueue(){Fixture f;const auto before=f.Read();CHECK(before.reason==VehicleShotReason::Okay);
    f.Put(f.effects+0x654,f.queue+0xb0);f.Put(f.queue,0x38000);const auto after=f.Read(151);
    CHECK(VehicleShotSame(before,after));CHECK(VehicleShotQueueGrowth(f.Memory(),before,after));
    f.Put(f.queue,0x38004);CHECK(!VehicleShotQueueGrowth(f.Memory(),before,after));
    f.Put(f.queue,0x38000);f.Put(f.effects+0x650,f.queue+0x1000);CHECK(!VehicleShotQueueGrowth(f.Memory(),before,after));return 0;}
int ForeignOwnership(){Fixture f;f.call.effects+=4;CHECK(f.Read().reason==VehicleShotReason::Foreign);
    f.call.effects-=4;f.seat.identity.entry+=4;CHECK(f.Read().reason==VehicleShotReason::Owner);
    f.seat.identity.entry-=4;f.Put(f.mounted+12,f.firing+4);CHECK(f.Read().reason==VehicleShotReason::Changed);
    f.Put(f.mounted+12,f.firing);f.Put(f.effects+16,f.firing+4);CHECK(f.Read().reason==VehicleShotReason::Foreign);return 0;}
int SourceDeadlineAndCaller(){Fixture f;CHECK(f.Read(200).reason==VehicleShotReason::Stale);CHECK(f.Read(99).reason==VehicleShotReason::Stale);
    CHECK(f.Read(199).reason==VehicleShotReason::Okay);++f.call.caller;CHECK(f.Read().reason==VehicleShotReason::Caller);
    --f.call.caller;++f.call.config;CHECK(f.Read().reason==VehicleShotReason::Foreign);return 0;}
int ReturnedIdentityAndQueueBounds(){Fixture f;const auto before=f.Read();f.Put(f.effects+0x654,f.queue+1);CHECK(f.Read().reason==VehicleShotReason::Queue);
    f.Put(f.effects+0x654,f.queue);auto after=f.Read();++after.owner.actorGeneration;CHECK(!VehicleShotSame(before,after));
    f.Put(0x34200,f.effects+0xd4);CHECK(f.Read().reason==VehicleShotReason::Foreign);return 0;}
int BoundedReport(){Fixture f;VehicleShotObserver observer;CHECK(!observer.Source());observer.Publish(f.source);CHECK(observer.Source()->deadlineNs==200);
    const auto source=*observer.Source();const auto a=f.Read();for(unsigned i=0;i<35;++i)observer.Record(source,f.call,a,a,false);
    std::ostringstream out;observer.Report(out);const auto s=out.str();CHECK(s.find("\"callbacks\":35")!=std::string::npos);
    CHECK(s.find("\"matched_callbacks\":35")!=std::string::npos);CHECK(s.find("\"dropped\":3")!=std::string::npos);
    CHECK(s.find("\"projectile_spawn_verified\":false")!=std::string::npos);return 0;}
int NonblockingContention(){Fixture f;VehicleShotObserver observer;observer.Publish(f.source);
    const auto original=*observer.Source();const auto a=f.Read();std::string busyReport;bool absent=false,retired=false;
    VehicleShotObserverTestAccess::Contended(observer,[&]{
        auto newer=f.source;++newer.input;observer.Publish(newer);
        absent=!observer.Source();observer.Record(original,f.call,a,a,true);
        std::ostringstream out;observer.Report(out);busyReport=out.str();
        observer.Invalidate();retired=!observer.Current(original);
    });
    CHECK(absent&&retired&&!observer.Source());
    CHECK(busyReport.find("\"snapshot_available\":false")!=std::string::npos);
    CHECK(busyReport.find("\"matched_callbacks\"")==std::string::npos);
    observer.Record(original,f.call,a,a,true);std::ostringstream out;observer.Report(out);const auto text=out.str();
    for(const char* key:{"publish_contention","source_contention","record_contention","report_contention","invalidated_records"})
        CHECK(text.find(std::string("\"")+key+"\":1")!=std::string::npos);
    CHECK(text.find("\"callbacks\":0")!=std::string::npos);
    CHECK(text.find("\"observed_queue_growth\":0")!=std::string::npos);return 0;
}
int TransitionFenceAndUnlockedReport(){Fixture f;VehicleShotObserver observer;observer.Publish(f.source);
    const auto old=*observer.Source();const auto a=f.Read();CHECK(observer.Current(old));
    // Same pointers and unexpired source after an observed exit/reentry must
    // not inherit the old actor/seat/space labels, even if Publish was busy.
    observer.Invalidate();CHECK(!observer.Current(old)&&!observer.Source());
    observer.Record(old,f.call,a,a,true);observer.Publish(f.source);const auto renewed=*observer.Source();
    CHECK(renewed.observationEpoch!=old.observationEpoch&&observer.Current(renewed));
    CHECK(renewed.observedNs==old.observedNs&&renewed.deadlineNs==old.deadlineNs);
    CHECK(ReadVehicleShot(f.Memory(),f.binding,renewed,f.seat,f.call,200).reason==VehicleShotReason::Stale);
    observer.Record(renewed,f.call,a,a,true);
    struct ReentrantBuffer : std::stringbuf {
        VehicleShotObserver& observer;bool checked=false,unlocked=false;
        explicit ReentrantBuffer(VehicleShotObserver& value):observer(value){}
        std::streamsize xsputn(const char* s,std::streamsize n)override {
            if(!checked){checked=true;unlocked=bool(observer.Source());}
            return std::stringbuf::xsputn(s,n);
        }
    } buffer(observer);
    std::ostream out(&buffer);observer.Report(out);CHECK(buffer.checked&&buffer.unlocked);
    CHECK(buffer.str().find("\"matched_callbacks\":1")!=std::string::npos);
    CHECK(buffer.str().find("\"invalidated_records\":1")!=std::string::npos);
    CHECK(buffer.str().find("\"queue_attribution_verified\":false")!=std::string::npos);
    CHECK(buffer.str().find("queue_acceptances")==std::string::npos);return 0;
}
int ExactForeignLeaves(){
    Fixture f;f.call.effects+=4;auto row=f.Read();CHECK(row.reason==VehicleShotReason::Foreign);
    CHECK(row.diagnostic.leaf==VehicleShotLeaf::CandidateCount&&row.diagnostic.expected==1&&row.diagnostic.observed==0);
    CHECK(row.diagnostic.candidateCount==1&&row.diagnostic.weaponCandidates==1);
    CHECK(row.diagnostic.candidates[0].component==f.component&&row.diagnostic.candidates[0].mounted==f.mounted);
    CHECK(row.diagnostic.candidates[0].effects==f.effects&&row.diagnostic.candidates[0].effects!=f.call.effects);
    f.call.effects-=4;f.Put(f.effects+16,f.firing+4);row=f.Read();CHECK(row.reason==VehicleShotReason::Foreign);
    CHECK(row.diagnostic.leaf==VehicleShotLeaf::EffectsFiring&&row.diagnostic.address==f.effects+16);
    CHECK(row.diagnostic.expected==f.firing&&row.diagnostic.observed==f.firing+4);
    f.Put(f.effects+16,f.firing);f.call.config+=4;row=f.Read();CHECK(row.reason==VehicleShotReason::Foreign);
    CHECK(row.diagnostic.leaf==VehicleShotLeaf::ShotConfig&&row.diagnostic.expected==f.primary+0xa0&&row.diagnostic.observed==f.call.config);
    f.call.config-=4;
    const std::pair<unsigned,VehicleShotLeaf> methods[]{{0x33008,VehicleShotLeaf::MountedUpdate},{0x3300c,VehicleShotLeaf::MountedInput},
        {0x3404c,VehicleShotLeaf::EffectsShoot},{0x3410c,VehicleShotLeaf::ListenerCallback}};
    for(const auto& [at,leaf]:methods){const auto original=f.Get(at);f.Put(at,original+4);row=f.Read();
        CHECK(row.reason==VehicleShotReason::Foreign&&row.diagnostic.leaf==leaf&&row.diagnostic.address==at);
        CHECK(row.diagnostic.expected==original&&row.diagnostic.observed==original+4);f.Put(at,original);}
    const std::pair<unsigned,VehicleShotLeaf> objects[]{{f.component,VehicleShotLeaf::ComponentType},{f.effects,VehicleShotLeaf::EffectsType},
        {f.firing,VehicleShotLeaf::FiringType},{f.primary,VehicleShotLeaf::PrimaryType}};
    for(const auto& [at,leaf]:objects){const auto original=f.types[at];f.types[at]="Foreign";row=f.Read();
        CHECK(row.reason==VehicleShotReason::Foreign&&row.diagnostic.leaf==leaf&&row.diagnostic.address==at);f.types[at]=original;}
    f.Put(f.mounted+4+0x1c,0x34202);row=f.Read();CHECK(row.reason==VehicleShotReason::Foreign&&row.diagnostic.leaf==VehicleShotLeaf::ListenerShape);
    f.Put(f.mounted+4+0x1c,0x34204);f.Put(0x34200,f.effects+0xd4);row=f.Read();
    CHECK(row.reason==VehicleShotReason::Foreign&&row.diagnostic.leaf==VehicleShotLeaf::ListenerMembership);
    CHECK(row.diagnostic.expected==1&&row.diagnostic.observed==0);f.Put(0x34200,f.effects+0xd0);
    f.failReadAt=f.mounted+0x134;row=f.Read();CHECK(row.reason==VehicleShotReason::Read&&row.diagnostic.leaf==VehicleShotLeaf::CandidateRead);
    CHECK(row.diagnostic.firstReadFailure==f.failReadAt);return 0;
}
int ReservedHeldRejectionEvidence(){
    static_assert(VehicleShotObserverTestAccess::SnapshotBytes()<65536);
    Fixture f;VehicleShotObserver observer;f.call.effects+=4;const auto rejected=f.Read();
    for(unsigned bucket=0;bucket<2;++bucket){auto publication=f.source;publication.fire=float(bucket);observer.Publish(publication);
        const auto source=*observer.Source();for(unsigned n=0;n<6;++n)observer.Record(source,f.call,rejected,rejected,false);}
    std::ostringstream stream;observer.Report(stream,true);const auto text=stream.str();
    CHECK(text.find("\"callbacks\":12")!=std::string::npos&&text.find("\"matched_callbacks\":0")!=std::string::npos);
    CHECK(text.find("\"records\":[]")!=std::string::npos);
    CHECK(text.find("\"rejection_seen_by_source_fire\":[6,6]")!=std::string::npos);
    CHECK(text.find("\"rejection_kept_by_source_fire\":[4,4]")!=std::string::npos);
    std::ofstream("diagnostic-report.json")<<text;return 0;
}
int AfterFailureRetainsBeforeIdentity(){Fixture f;VehicleShotObserver observer;observer.Publish(f.source);const auto source=*observer.Source();
    const auto before=f.Read();CHECK(before.reason==VehicleShotReason::Okay);f.Put(0x34200,f.effects+0xd4);const auto after=f.Read(151);
    observer.Record(source,f.call,before,after,false);std::ostringstream stream;observer.Report(stream,true);const auto text=stream.str();
    CHECK(text.find("\"matched_callbacks\":0")!=std::string::npos);
    CHECK(text.find("\"rejection_seen_by_source_fire\":[0,1]")!=std::string::npos);
    CHECK(text.find("\"before\":{\"reason\":0,\"leaf\":0")!=std::string::npos);
    CHECK(text.find("\"after\":{\"reason\":6,\"leaf\":17")!=std::string::npos);return 0;}
}
int main(int argc,char** argv){
    const bool diagnosticsOnly=argc==2&&std::string(argv[1])=="--diagnostics-only";
    if(diagnosticsOnly){if(ExactForeignLeaves()||ReservedHeldRejectionEvidence()||AfterFailureRetainsBeforeIdentity())return 1;
        std::puts("Bc2VehicleShotObserverTests:3 new diagnostic groups PASS");return 0;}
    const bool revisionOnly=argc==2&&std::string(argv[1])=="--revision-only";
    if(!revisionOnly&&(MatchedCallbackAndQueue()||ForeignOwnership()||SourceDeadlineAndCaller()||ReturnedIdentityAndQueueBounds()||BoundedReport()))return 1;
    if(NonblockingContention()||TransitionFenceAndUnlockedReport())return 1;
    if(!revisionOnly&&(ExactForeignLeaves()||ReservedHeldRejectionEvidence()||AfterFailureRetainsBeforeIdentity()))return 1;
    std::puts(revisionOnly?"Bc2VehicleShotObserverTests:2 revision groups PASS":"Bc2VehicleShotObserverTests:10 groups PASS");return 0;
}
