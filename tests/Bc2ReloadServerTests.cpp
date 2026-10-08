#include "Test.h"
#include "Bc2ReloadServer.h"
#include "Bc2ReloadPhaseRetry.h"
#include <cstring>
#include <fstream>
#include <iterator>
#include <limits>
#include <vector>
using namespace fvr::bc2;
namespace {
struct Fixture {
    static constexpr unsigned base=0x400000,cp=0x10000,cs=0x12000,weak=0x13000,ci=0x14000,ca=0x15000,cw=0x16000,
        data=0x17000,fd=0x18000,primary=0x19000,cf0=0x1a000,cf1=0x1b000,manager=0x20000,players=0x21000,
        sp=0x22000,ss=0x24000,si=0x26000,sa=0x27000,sw=0x28000,effects=0x29000,firing=0x2b000,callback=0x2c000,
        soldierData=0x2d000,inventoryData=0x2e000;
    std::vector<std::byte> bytes=std::vector<std::byte>(0x30000),image=std::vector<std::byte>(0x10000);
    ReloadServerBinding binding{};ReloadStateBinding state{};ReloadStateSnapshot client{};
    unsigned failType=0,failRead=0,raceAt=0,raceCount=0,raceTarget=0,raceValue=0;
    std::byte* At(unsigned at){return at>=base?image.data()+at-base:bytes.data()+at-0x10000;}
    template<class T>void Put(unsigned at,T value){std::memcpy(At(at),&value,sizeof(value));}
    Fixture(){
        binding.preferredBase=base;binding.imageSize=0x10000;binding.contextRva=0x100;binding.managerVtableRva=0x200;binding.controlledGetterRva=0x300;
        state.preferredBase=base;state.imageSize=0x10000;state.firingVtableRva=0x500;
        client.owner={cp,cs,weak,cw,1,2,3};client.sequence=4;client.observedNs=100;client.inventory=ci;client.selectedSlot=0;client.soldierFlags=1;
        client.config.weaponData=data;client.config.firingData=fd;client.config.primaryFire=primary;client.config.ammoAddress=primary+0x170;
        client.config.fireLogicType=1;client.config.reloadType=0;client.branches[0].address=cf0;client.branches[1].address=cf1;
        Put<std::uint8_t>(cp+0xccd,8);Put(cp+0xc54,weak);Put(weak,cs+4);Put(cs+0x220,cp);Put(cp+0xc68,cs);Put<std::uint8_t>(cs+0x114,1);
        Put(cs+0x24c,ci);Put(cs+0x260,ca);Put(cs+0x264,ca+8);Put(ca,cw);Put(ci+0x14c,0u);Put(cw+4,data);Put(data+0x98,fd);Put(fd+0x40,primary);Put(primary+0x24,1u);
        Put(base+0x108,manager);Put(manager,base+0x200);Put(manager+4,64u);Put(manager+0x6c,players);Put(players,sp);Put(cp+0x154,0u);Put(sp+0x154,0u);
        Put(sp,base+0x400);Put(base+0x430,base+0x300);Put(sp+0xc3c,ss);Put(sp+0xc6c,ss);Put(ss,base+0x600);Put(ss+0xc,soldierData);Put(cs+0xc,soldierData);Put(ss+0x220,sp);
        Put(ss+0x2b4,si);Put(si+4,inventoryData);Put(ss+0x2b8,sa);Put(ss+0x2bc,sa+8);Put(sa,sw);Put(si+0x14c,0u);
        Put(sw+4,data);Put(sw+0xc,effects);Put(sw+0x10,firing);Put(effects,base+0x700);Put(effects+0x10,fd);Put(effects+0x128,sp);Put(effects+0xd4,callback);Put(callback+4,fd);
        Put(firing,base+0x500);Put(firing+8,fd);Put(firing+12,primary+0x170);Put(firing+0x3c,11u);Put(firing+0x40,10u);Put(firing+0x44,12u);Put(firing+0x50,.72f);Put(firing+0x7c,3);Put(firing+0x80,24);
    }
    ReloadStateMemory Memory(){return {this,[](void* context,unsigned at,void* out,std::size_t size){auto& f=*static_cast<Fixture*>(context);
        if(at==f.failRead||!size||!((at>=0x10000&&std::uint64_t(at)+size<=0x40000)||(at>=base&&std::uint64_t(at)+size<=base+0x10000)))return false;
        if(at==f.raceAt&&++f.raceCount==2)f.Put(f.raceTarget,f.raceValue);
        std::memcpy(out,f.At(at),size);return true;
    },[](void* context,unsigned at,const char* name){const auto& f=*static_cast<Fixture*>(context);if(at==f.failType)return false;
        return (at==ss&&!std::strcmp(name,"ServerSoldierEntity"))||(at==inventoryData&&!std::strcmp(name,"WeaponSwitchingData"))||
            (at==data&&!std::strcmp(name,"SoldierWeaponData"))||(at==effects&&!std::strcmp(name,"ServerWeaponFiringEffects"));}};}
    std::optional<ReloadServerSnapshot> Read(){return ReadReloadServerState(Memory(),binding,state,base,client);}
};
int ValidSnapshotAndBoundary(){
    Fixture f;const auto before=f.bytes;const auto s=f.Read();CHECK(s&&s->links.player==f.sp&&s->links.soldier==f.ss&&s->links.item==f.sw&&s->links.firing==f.firing);
    CHECK(s->state.loaded==3&&s->state.reserve==24&&s->state.wrapperOffset==0x10&&!s->authorityProven&&f.bytes==before);
    f.Put(f.firing+0x7c,4);f.Put(f.firing+0x80,23);
    const auto b=ReadReloadServerBoundary(f.Memory(),f.binding,f.state,f.base,*s,200000100,200);CHECK(b&&b->loaded==4&&b->reserve==23);
    return 0;
}
int RelationRejections(){
    const std::array<std::pair<unsigned,unsigned>,19> changes{{{Fixture::manager,0},{Fixture::manager+4,257},{Fixture::cp+0x154,64},
        {Fixture::sp+0x154,1},{Fixture::sp+0xc6c,Fixture::ss+4},{Fixture::ss+0x220,Fixture::sp+4},{Fixture::ss+0xc,0},
        {Fixture::ss+0x2bc,Fixture::sa+3},{Fixture::si+0x14c,2},{Fixture::sa+4,Fixture::sw},{Fixture::sw+4,Fixture::data+4},
        {Fixture::effects+0x128,Fixture::sp+4},{Fixture::effects+0x10,Fixture::fd+4},{Fixture::callback+4,Fixture::fd+4},
        {Fixture::firing+8,Fixture::fd+4},{Fixture::firing+12,Fixture::primary+0x174},{Fixture::firing,Fixture::base+0x504},
        {Fixture::ca+4,Fixture::cw},{Fixture::primary+0x24,4}}};
    for(auto [at,value]:changes){Fixture f;const auto good=f.Read();CHECK(good);f.Put(at,value);CHECK(!f.Read());
        CHECK(!ReadReloadServerBoundary(f.Memory(),f.binding,f.state,f.base,*good,200000100,200));
        CHECK(!ReadReloadServerOwner(f.Memory(),f.binding,f.state,f.base,*good,200000100,200));}
    for(auto at:{Fixture::ss,Fixture::inventoryData,Fixture::data,Fixture::effects}){Fixture f;f.failType=at;CHECK(!f.Read());}
    return 0;
}
int LeasesAndPointerRaces(){
    Fixture f;const auto s=f.Read();CHECK(s);
    for(auto [deadline,now]:{std::pair<std::int64_t,std::int64_t>{100,100},{200,200},{200000101,200},{200,99}})
        {CHECK(!ReadReloadServerBoundary(f.Memory(),f.binding,f.state,f.base,*s,deadline,now));
         CHECK(!ReadReloadServerOwner(f.Memory(),f.binding,f.state,f.base,*s,deadline,now));}
    {Fixture g;g.raceAt=g.firing;g.raceTarget=g.firing+0x7c;g.raceValue=4;CHECK(!g.Read());}
    {Fixture g;g.raceAt=g.base+0x108;g.raceTarget=g.sa+4;g.raceValue=g.sw;CHECK(!g.Read());}
    auto bad=*s;bad.client.owner.space=0;CHECK(!ReadReloadServerBoundary(f.Memory(),f.binding,f.state,f.base,bad,200000100,200));
    f.Put(f.effects,f.base+0x704);CHECK(!ReadReloadServerBoundary(f.Memory(),f.binding,f.state,f.base,*s,200000100,200));
    return 0;
}
int MalformedState(){
    for(unsigned offset:{0x3cu,0x40u,0x44u}){Fixture f;f.Put(f.firing+offset,16u);CHECK(!f.Read());}
    for(unsigned offset:{0x7cu,0x80u}){Fixture f;f.Put(f.firing+offset,-2);CHECK(!f.Read());}
    Fixture f;f.Put(f.firing+0x50,std::numeric_limits<float>::quiet_NaN());CHECK(!f.Read());return 0;
}
int PublishedFlagDiagnostic(){
    Fixture f;const auto s=f.Read();CHECK(s);ReloadServerBoundaryDiagnostic d;
    f.Put<std::uint8_t>(f.cs+0x114,17);
    CHECK(ReadReloadServerBoundary(f.Memory(),f.binding,f.state,f.base,*s,200000100,200,&d));
    CHECK(d.failure==ReloadServerBoundaryFailure::None&&d.expectedSoldierFlags==1&&d.observedSoldierFlags==17&&d.differsOnlySoldierFlags);
    // Symmetric lease publication in the +40 phase retains identical ownership.
    const auto other=f.Read();CHECK(other);f.Put<std::uint8_t>(f.cs+0x114,1);
    CHECK(ReadReloadServerBoundary(f.Memory(),f.binding,f.state,f.base,*other,200000100,200,&d));
    // No other soldier or player flag may differ, even with valid pointers.
    f.Put(f.cs+0x248,f.ci);
    for(unsigned bit:{1u,2u,4u,8u,32u,64u,128u}){
        f.Put<std::uint8_t>(f.cs+0x114,std::uint8_t(1^bit));
        CHECK(!ReadReloadServerBoundary(f.Memory(),f.binding,f.state,f.base,*s,200000100,200,&d));
        CHECK(d.failure==ReloadServerBoundaryFailure::PublishedLinks);
    }
    f.Put<std::uint8_t>(f.cs+0x114,17);f.Put<std::uint8_t>(f.cp+0xccd,24);
    CHECK(!ReadReloadServerBoundary(f.Memory(),f.binding,f.state,f.base,*s,200000100,200,&d));CHECK(!d.differsOnlySoldierFlags);
    f.Put<std::uint8_t>(f.cp+0xccd,8);f.Put(f.effects,f.base+0x704);
    CHECK(!ReadReloadServerBoundary(f.Memory(),f.binding,f.state,f.base,*s,200000100,200,&d));CHECK(!d.differsOnlySoldierFlags);
    f.Put(f.effects,f.base+0x700);f.Put(f.cs+0x24c,f.ci+4);
    CHECK(!ReadReloadServerBoundary(f.Memory(),f.binding,f.state,f.base,*s,200000100,200,&d));CHECK(d.failure==ReloadServerBoundaryFailure::ScopeBefore);
    // A 0x10 change DURING a read is still a race, not an accepted lease phase.
    Fixture g;const auto stable=g.Read();CHECK(stable);
    g.raceAt=g.cs+0x114;g.raceTarget=g.cs+0x114;g.raceValue=17;
    CHECK(!ReadReloadServerBoundary(g.Memory(),g.binding,g.state,g.base,*stable,200000100,200,&d));CHECK(d.failure==ReloadServerBoundaryFailure::ChangedLinks);
    CHECK(d.observedSoldierFlags==1&&d.afterSoldierFlags==17&&d.changedOnlySoldierFlags);
    Fixture ammo;const auto original=ammo.Read();CHECK(original);
    ammo.raceAt=ammo.firing;ammo.raceTarget=ammo.firing+0x7c;ammo.raceValue=4;
    CHECK(!ReadReloadServerBoundary(ammo.Memory(),ammo.binding,ammo.state,ammo.base,*original,200000100,200,&d));
    CHECK(d.failure==ReloadServerBoundaryFailure::State&&d.stateFailure==3&&d.changedOffset==0x7c&&d.changedBefore==3&&d.changedAfter==4);
    Fixture clean;const auto coherent=clean.Read();CHECK(coherent);
    CHECK(ReadReloadServerBoundary(clean.Memory(),clean.binding,clean.state,clean.base,*coherent,200000100,200,&d));
    CHECK(!d.stateFailure&&d.changedOffset==UINT32_MAX&&!d.changedOnlySoldierFlags);
    return 0;
}
int StructuralOwnerRetainsExactLinks(){
    for(unsigned offset:{0x3cu,0x40u,0x44u,0x48u,0x50u,0x60u,0x74u,0x7cu,0x80u,0x94u,0x9cu,0xa4u,0xa8u}){
        Fixture f;const auto s=f.Read();CHECK(s);f.raceAt=f.firing;f.raceTarget=f.firing+offset;f.raceValue=0x55667788;
        CHECK(ReadReloadServerOwner(f.Memory(),f.binding,f.state,f.base,*s,200000100,200)&&f.raceCount==2);
        Fixture boundary;const auto b=boundary.Read();CHECK(b);boundary.raceAt=boundary.firing;boundary.raceTarget=boundary.firing+offset;boundary.raceValue=0x55667788;
        ReloadServerBoundaryDiagnostic d;
        CHECK(!ReadReloadServerBoundary(boundary.Memory(),boundary.binding,boundary.state,boundary.base,*b,200000100,200,&d));
        CHECK(d.failure==ReloadServerBoundaryFailure::State&&d.stateFailure==3&&d.changedOffset==offset);
    }
    for(unsigned offset:{0u,8u,12u}){Fixture f;const auto s=f.Read();CHECK(s);f.raceAt=f.firing;f.raceTarget=f.firing+offset;f.raceValue=0;
        CHECK(!ReadReloadServerOwner(f.Memory(),f.binding,f.state,f.base,*s,200000100,200));}
    for(unsigned address:{Fixture::sw+0x10,Fixture::effects,Fixture::si+0x14c,Fixture::sp+0xc6c}){
        Fixture f;const auto s=f.Read();CHECK(s);f.raceAt=f.base+0x108;f.raceTarget=address;f.raceValue=address==Fixture::si+0x14c?1:0;
        CHECK(!ReadReloadServerOwner(f.Memory(),f.binding,f.state,f.base,*s,200000100,200));}
    Fixture f;const auto s=f.Read();CHECK(s);f.Put<std::uint8_t>(f.cs+0x114,17);
    CHECK(ReadReloadServerOwner(f.Memory(),f.binding,f.state,f.base,*s,200000100,200));
    f.Put<std::uint8_t>(f.cs+0x114,3);CHECK(!ReadReloadServerOwner(f.Memory(),f.binding,f.state,f.base,*s,200000100,200));
    f.Put<std::uint8_t>(f.cs+0x114,1);f.raceAt=f.cs+0x114;f.raceTarget=f.cs+0x114;f.raceValue=17;
    ReloadServerBoundaryDiagnostic d;CHECK(!ReadReloadServerOwner(f.Memory(),f.binding,f.state,f.base,*s,200000100,200,&d));
    CHECK(d.failure==ReloadServerBoundaryFailure::ChangedLinks&&d.changedOnlySoldierFlags);
    return 0;
}
int ServerPhaseRetryUsesOnlyFreshCompleteReads(){
    ReloadServerBoundaryDiagnostic policy;policy.changedOnlySoldierFlags=true;policy.observedSoldierFlags=19;policy.afterSoldierFlags=3;
    for(unsigned n=0;n<=unsigned(ReloadServerBoundaryFailure::ChangedLinks);++n){policy.failure=ReloadServerBoundaryFailure(n);
        CHECK(ReloadPhaseOnlyRace(policy)==(policy.failure==ReloadServerBoundaryFailure::ChangedLinks));}
    policy.changedOnlySoldierFlags=false;CHECK(!ReloadPhaseOnlyRace(policy));
    for(bool owner:{false,true})for(unsigned scenario=0;scenario<8;++scenario){
        Fixture f;const auto s=f.Read();CHECK(s);f.raceAt=f.cs+0x114;f.raceTarget=f.cs+0x114;f.raceValue=scenario==4?3:17;
        if(scenario==5){f.raceAt=f.cp+0xccd;f.raceTarget=f.weak;f.raceValue=f.cs+8;}
        if(scenario==6)f.raceAt=0;
        unsigned reads=0,clocks=0;ReloadServerBoundaryDiagnostic d;ReloadPhaseRetryAttempt retry;
        const auto result=ReadReloadPhaseCoherent([&](std::int64_t now,ReloadServerBoundaryDiagnostic& diagnostic){++reads;
            return owner?ReadReloadServerOwner(f.Memory(),f.binding,f.state,f.base,*s,200000100,now,&diagnostic):
                bool(ReadReloadServerBoundary(f.Memory(),f.binding,f.state,f.base,*s,200000100,now,&diagnostic));
        },[&]{++clocks;
            if(scenario==1){f.raceCount=0;f.raceValue=1;}
            if(scenario==2)f.Put(f.sp+0xc6c,f.ss+4);
            return scenario==3?200000100ll:scenario==7?199ll:201ll;
        },200,d,retry);
        CHECK(result==(scenario==0||scenario==6));
        CHECK(reads==(scenario<4?2u:1u)&&clocks==(scenario<4||scenario==7?1u:0u));
        CHECK(retry.attempted==(scenario<4||scenario==7)&&retry.recovered==(scenario==0));
        if(scenario<4)CHECK(retry.first.beforeFlags==1&&retry.first.afterFlags==17&&retry.firstNs==200);
        if(scenario==3)CHECK(d.failure==ReloadServerBoundaryFailure::Lease&&retry.retryNs==200000100);
        if(scenario==7)CHECK(retry.clockRejected);
    }
    Fixture f;const auto s=f.Read();CHECK(s);f.raceAt=f.cs+0x114;f.raceTarget=f.cs+0x114;f.raceValue=17;
    ReloadServerBoundaryDiagnostic d;ReloadPhaseRetryAttempt retry;
    auto boundary=ReadReloadPhaseCoherent([&](std::int64_t now,ReloadServerBoundaryDiagnostic& diagnostic){
        return ReadReloadServerBoundary(f.Memory(),f.binding,f.state,f.base,*s,200000100,now,&diagnostic);
    },[&]{f.Put(f.firing+0x7c,4);f.Put(f.firing+0x80,23);f.Put(f.firing+0x50,.4f);return 201ll;},200,d,retry);
    CHECK(boundary&&retry.recovered&&boundary->loaded==4&&boundary->reserve==23&&Near(boundary->phaseTimer,.4f));return 0;
}
int Installed(const char* path){
    std::ifstream stream(path,std::ios::binary);CHECK(stream.good());const std::vector<char> file((std::istreambuf_iterator<char>(stream)),std::istreambuf_iterator<char>());
    std::vector<std::byte> bytes(file.size());std::memcpy(bytes.data(),file.data(),file.size());const auto inspect=fvr::engine::InspectPe(bytes);CHECK(inspect.valid);auto pe=inspect.image;
    const auto binding=DiscoverReloadServer(bytes,pe);CHECK(binding&&binding->contextRva==0x1166b18&&binding->managerVtableRva==0x1017680);
    struct File {std::vector<std::byte>& bytes;fvr::engine::PeImage& pe;unsigned base;};File memory{bytes,pe,binding->preferredBase};
    const ReloadStateMemory reader{&memory,[](void* context,unsigned at,void* out,std::size_t size){auto& f=*static_cast<File*>(context);if(at<f.base)return false;const auto rva=at-f.base;
        for(const auto& s:f.pe.sections)if(rva>=s.rva){const auto d=std::uint64_t(rva)-s.rva,pos=std::uint64_t(s.rawOffset)+d;if(d<=s.rawSize&&size<=s.rawSize-d&&pos<=f.bytes.size()&&size<=f.bytes.size()-pos){std::memcpy(out,f.bytes.data()+pos,size);return true;}}return false;},nullptr};
    const auto offset=[&](unsigned rva){for(const auto& s:pe.sections)if(rva>=s.rva&&rva-s.rva<s.rawSize)return std::size_t(s.rawOffset)+rva-s.rva;return bytes.size();};
    CHECK(ValidateReloadServerLive(reader,*binding,binding->preferredBase));
    for(const auto& p:binding->code){const auto at=offset(p.rva+p.size-1);CHECK(at<bytes.size());bytes[at]^=std::byte{1};
        CHECK(!ValidateReloadServerLive(reader,*binding,binding->preferredBase));CHECK(!DiscoverReloadServer(bytes,pe));bytes[at]^=std::byte{1};}
    auto malformed=*binding;malformed.contextRva+=4;CHECK(!ValidateReloadServerLive(reader,malformed,binding->preferredBase));
    malformed=*binding;malformed.controlledGetterRva+=4;CHECK(!ValidateReloadServerLive(reader,malformed,binding->preferredBase));
    malformed=*binding;malformed.code[0].rva=0xfffffff0;CHECK(!ValidateReloadServerLive(reader,malformed,binding->preferredBase));
    const auto at=offset(binding->code[8].rva),raw=bytes.size();std::vector<std::byte> duplicate(bytes.begin()+at,bytes.begin()+at+binding->code[8].size);
    bytes.insert(bytes.end(),duplicate.begin(),duplicate.end());pe.sections.push_back({"duplicate",pe.imageSize,unsigned(duplicate.size()),unsigned(raw),unsigned(duplicate.size()),0x20000000});pe.imageSize+=0x1000;
    CHECK(!DiscoverReloadServer(bytes,pe));std::printf("Installed server ownership: 11 complete function proofs, mutation/ambiguity rejection passed.\n");return 0;
}
}
int main(int argc,char** argv){if(ValidSnapshotAndBoundary()||RelationRejections()||LeasesAndPointerRaces()||MalformedState()||PublishedFlagDiagnostic()||StructuralOwnerRetainsExactLinks()||ServerPhaseRetryUsesOnlyFreshCompleteReads())return 1;
    std::printf("Seven read-only server ownership groups passed. Native authority remains unproven.\n");return argc==2?Installed(argv[1]):0;}
