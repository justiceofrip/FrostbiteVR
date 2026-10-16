#include "Test.h"
#include "Bc2VehicleMountedObserver.h"
#include <cstring>
#include <cstdlib>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>
using namespace fvr::bc2;
namespace fvr::bc2 {
struct VehicleMountedObserverTestAccess {
    static void Budget(VehicleMountedObserver& o,unsigned bucket,std::uint64_t count){o.sourceCalls_[bucket].store(count);}
    static void Lock(VehicleMountedObserver& o){o.journalLock_.test_and_set();}
    static void Unlock(VehicleMountedObserver& o){o.journalLock_.clear();}
};
}
namespace {
struct Fixture {
    std::vector<unsigned char> bytes=std::vector<unsigned char>(0x1600000);
    std::map<unsigned,std::string> types;
    VehicleShotSource source{};VehicleRouteSnapshot seat{};VehicleMountedBinding binding{0x400000,true};
    OriginalFiringUpdate call{0x32004,0x39000,0x7d4dff,0};
    unsigned component=0x30000,mounted=0x32000,effects=0x35000,data=0x36000,primary=0x37000;
    unsigned failAt=0;std::int64_t now=150;bool current=true,sourceAvailable=true,seatAvailable=true,stack=true;
    std::unique_ptr<VehicleMountedObserver> observer=std::make_unique<VehicleMountedObserver>();
    void Put(unsigned at,unsigned value){std::memcpy(bytes.data()+at,&value,4);}
    unsigned Get(unsigned at)const {unsigned value=0;std::memcpy(&value,bytes.data()+at,4);return value;}
    Fixture(){
        source.input=7;source.space=8;source.observationEpoch=9;source.observedNs=100;source.deadlineNs=200;
        source.owner={0x21000,0x22000,0x23000,0x24000,2,3};
        seat.identity={1,2,3,0x25000,0x26000,0x27000,0x28000};seat.player=source.owner.player;seat.weak=source.owner.weak;seat.slot=1;seat.fingerprint=44;
        auto& p=source.profile;p.owner=seat.identity;p.collection=0x20000;p.count=p.layout=3;p.weapons=1;
        p.profile.role=VehicleSeatRole::Gunner;p.profile.routeFingerprint=44;
        p.rows[0]={0x29000,0,0,255<<8};p.rows[1]={seat.identity.entry,seat.identity.entry,0,0};p.rows[2]={component,0,0,1<<8};
        Put(p.collection,seat.identity.controlled);bytes[p.collection+13]=bytes[p.collection+14]=3;
        std::memcpy(bytes.data()+p.collection+16,p.rows.data(),p.count*16);
        Put(component,0x1430d38);Put(component+12,0x31000);Put(component+0xd0,mounted);Put(component+0xcc,effects);
        Put(0x31000+216,data);Put(mounted,0x33000);Put(0x33000+8,0x7d4c10);Put(0x33000+12,0x7bf840);
        Put(mounted+0x13c,0x31000);Put(mounted+12,data);Put(mounted+0x134,effects);Put(data+64,primary);Put(primary+0x4c,8);
        Put(effects,0x34000);Put(0x34000+0x4c,0x81c170);Put(effects+16,data);
        Put(effects+0xd0,0x34100);Put(0x34100+12,0x808220);
        Put(call.firing+0x18,0x34200);Put(call.firing+0x1c,0x34204);Put(0x34200,effects+0xd0);
        Put(call.firing+0x3c,2);Put(call.firing+0x44,2);Put(call.firing+0x7c,0xffffffff);
        Put(call.context+0x18,0x3c888889);Put(call.context+0x1c,0x12345678);Put(call.context+0x20,0x3f800000);
        types[component]="ClientWeaponComponent";types[effects]="ClientWeaponFiringEffects";types[data]="WeaponFiringData";types[primary]="FiringFunctionData";
        for(auto at:{component,component+12,component+0xd0,0x31000u+216,mounted+12,data+64,primary+0x4c})p.words[p.wordCount++]={at,Get(at)};
        const auto configured=observer->Configure(binding,Api());if(!configured)std::abort();observer->Registration(true);
    }
    VehicleRouteMemory Memory(){return {this,[](void* p,unsigned at,void* dst,std::size_t count){auto& f=*static_cast<Fixture*>(p);
        if(at==f.failAt||std::uint64_t(at)+count>f.bytes.size())return false;std::memcpy(dst,f.bytes.data()+at,count);return true;},
        [](void* p,unsigned at,const char* name){const auto& f=*static_cast<Fixture*>(p);const auto i=f.types.find(at);return i!=f.types.end()&&i->second==name;}};}
    VehicleMountedApi Api(){return {this,Memory(),
        [](void* p,VehicleShotSource& out)noexcept{auto& f=*static_cast<Fixture*>(p);if(!f.sourceAvailable)return false;out=f.source;return true;},
        [](void* p,const VehicleShotSource& s)noexcept{const auto& f=*static_cast<Fixture*>(p);return f.current&&s.observationEpoch==f.source.observationEpoch;},
        [](void* p,const VehicleShotSource&,VehicleRouteSnapshot& out)noexcept{const auto& f=*static_cast<Fixture*>(p);out=f.seat;return f.seatAvailable;},
        [](void* p)noexcept{return static_cast<Fixture*>(p)->now;},
        [](void* p,unsigned,std::size_t)noexcept{return static_cast<Fixture*>(p)->stack;}};}
    VehicleMountedSample Read(){return ReadVehicleMounted(Memory(),binding,source,seat,call,now,stack);}
    void Update(){OriginalFiringScope scope(observer->Callbacks(),call);}
    std::string Report(){std::ostringstream out;observer->Report(out);return out.str();}
};
int OriginalContextAndReturn(){auto f=std::make_unique<Fixture>();const auto callbacks=f->observer->Callbacks();CHECK(!callbacks.copyInput);
    auto before=f->Read();CHECK(before.reason==VehicleMountedReason::Okay&&before.dtBits==0x3c888889&&before.rawToken==0x12345678);
    CHECK(before.flags==0&&before.contextBytes[0]==0);unsigned originalCalls=0;
    {
        OriginalFiringScope scope(callbacks,f->call);CHECK(!scope.InputCopy());CHECK(!scope.InputCopy());
        ++originalCalls;f->Put(f->call.context+0x14,0x98765432);f->Put(f->call.firing+0x3c,5);f->Put(f->call.firing+0x44,6);
    }
    CHECK(originalCalls==1);CHECK(f->Get(f->call.context+0x14)==0x98765432);
    const auto report=f->Report();CHECK(report.find("\"matched_update_scopes\":1")!=std::string::npos);
    CHECK(report.find("\"state\":5,\"next\":6")!=std::string::npos);CHECK(report.find("\"input_copy\":false")!=std::string::npos);return 0;
}
int FireDoesNotInventByte24(){auto f=std::make_unique<Fixture>();f->source.fire=1;f->Put(f->call.context+0x2c,1);f->Update();
    f->bytes[f->call.context+0x24]=1;f->Update();const auto report=f->Report();
    CHECK(report.find("\"context_fire_counts\":[0,2]")!=std::string::npos);CHECK(report.find("\"byte24_counts\":[1,1]")!=std::string::npos);
    CHECK(report.find("\"native_fire_by_source\":[[0,0],[0,2]]")!=std::string::npos);
    CHECK(report.find("\"discharge_verified\":false")!=std::string::npos);return 0;
}
int NativeJoinAndRawReadFailure(){auto f=std::make_unique<Fixture>();f->call.firing+=4;CHECK(f->Read().reason==VehicleMountedReason::CandidateCount);
    f->call.firing-=4;f->Put(f->effects+16,f->data+4);CHECK(f->Read().reason==VehicleMountedReason::Join);
    f->Put(f->effects+16,f->data);f->Put(0x34200,f->effects+0xd4);CHECK(f->Read().reason==VehicleMountedReason::Listener);
    f->Put(0x34200,f->effects+0xd0);f->stack=false;CHECK(f->Read().reason==VehicleMountedReason::ContextStack);
    f->stack=true;f->failAt=f->call.context;CHECK(f->Read().reason==VehicleMountedReason::ContextRead);
    f->failAt=0;f->Put(f->call.context+0x18,0x7fc00000);const auto row=f->Read();
    CHECK(row.reason==VehicleMountedReason::ContextShape&&row.contextRead&&row.stateRead&&row.dtBits==0x7fc00000);
    ++f->call.caller;CHECK(f->Read().reason==VehicleMountedReason::Caller);return 0;
}
int RetainedSourceDoesNotRenew(){auto f=std::make_unique<Fixture>();const auto callbacks=f->observer->Callbacks();
    const auto token=callbacks.enter(callbacks.context,f->call);++f->source.input;f->source.deadlineNs=900;f->now=200;
    callbacks.leave(callbacks.context,f->call,token);auto report=f->Report();
    CHECK(report.find("\"matched_update_scopes\":0")!=std::string::npos);CHECK(report.find("\"source_ns\":100,\"deadline_ns\":200")!=std::string::npos);
    f=std::make_unique<Fixture>();const auto next=f->observer->Callbacks();const auto old=next.enter(next.context,f->call);
    ++f->source.observationEpoch;next.leave(next.context,f->call,old);report=f->Report();CHECK(report.find("\"matched_update_scopes\":0")!=std::string::npos);return 0;
}
int BoundedSlotsAndExactReturn(){auto f=std::make_unique<Fixture>();const auto callbacks=f->observer->Callbacks();std::array<OriginalFiringToken,8> tokens;
    for(auto& token:tokens){token=callbacks.enter(callbacks.context,f->call);CHECK(token.words[0]!=0);}
    CHECK(callbacks.enter(callbacks.context,f->call).words[0]==0);
    auto changed=f->call;++changed.context;callbacks.leave(callbacks.context,changed,tokens[0]);
    for(const auto& token:tokens)callbacks.leave(callbacks.context,f->call,token);
    const auto report=f->Report();CHECK(report.find("\"slot_drops\":1")!=std::string::npos);CHECK(report.find("\"bad_tokens\":1")!=std::string::npos);
    CHECK(report.find("\"scope_leaves\":8")!=std::string::npos);CHECK(report.find("\"in_flight\":0")!=std::string::npos);return 0;
}
int NeutralDoesNotFillHeldBudget(){auto f=std::make_unique<Fixture>();for(unsigned n=0;n<160;++n){++f->source.input;f->Update();}
    auto report=f->Report();CHECK(report.find("\"kept_by_cohort\":[1,0,0]")!=std::string::npos);CHECK(report.find("\"collapsed_by_cohort\":[159,0,0]")!=std::string::npos);
    VehicleMountedObserverTestAccess::Budget(*f->observer,0,4096);f->Update();f->source.fire=1;f->Put(f->call.context+0x2c,1);f->Update();
    report=f->Report();CHECK(report.find("\"read_budget_drops\":[1,0]")!=std::string::npos);CHECK(report.find("\"kept_by_cohort\":[1,1,0]")!=std::string::npos);return 0;
}
int NonblockingDropAndNoSource(){auto f=std::make_unique<Fixture>();VehicleMountedObserverTestAccess::Lock(*f->observer);f->Update();
    VehicleMountedObserverTestAccess::Unlock(*f->observer);f->sourceAvailable=false;f->Update();const auto report=f->Report();
    CHECK(report.find("\"journal_contention\":1")!=std::string::npos);CHECK(report.find("\"no_current_source\":1")!=std::string::npos);
    CHECK(report.find("\"in_flight\":0")!=std::string::npos);return 0;
}
int UnwindIsNotReturnAndOccupiedRejected(){auto f=std::make_unique<Fixture>();
    try{OriginalFiringScope scope(f->observer->Callbacks(),f->call);throw 1;}catch(int){}
    auto report=f->Report();CHECK(report.find("\"unwound_scopes\":1")!=std::string::npos);CHECK(report.find("\"matched_update_scopes\":0")!=std::string::npos);
    // Real occupied-slot admission is the unchanged dispatcher Register guard;
    // rejection must not masquerade as a working observer in its own report.
    f=std::make_unique<Fixture>();f->observer->Registration(false);f->Update();report=f->Report();
    CHECK(report.find("\"registration_rejected\":true")!=std::string::npos);CHECK(report.find("\"scope_leaves\":0")!=std::string::npos);return 0;
}
}
int main(){return OriginalContextAndReturn()||FireDoesNotInventByte24()||NativeJoinAndRawReadFailure()||
    RetainedSourceDoesNotRenew()||BoundedSlotsAndExactReturn()||NeutralDoesNotFillHeldBudget()||
    NonblockingDropAndNoSource()||UnwindIsNotReturnAndOccupiedRejected();}
