#include "Test.h"
#include "Bc2ReloadState.h"
#include <cstring>
#include <fstream>
#include <iterator>
#include <limits>
#include <string>
#include <unordered_map>
#include <vector>
using namespace fvr::bc2;
namespace {
struct Fixture {
    static constexpr std::uint32_t base=0x400000,soldier=0x10000,player=0x11000,weak=0x13000,
        inventory=0x14000,switching=0x15000,weapon=0x16000,data=0x17000,firing=0x18000,primary=0x19000,
        name=0x1a000,path=0x1a100,items=0x1b000,authored=0x1c000,branch0=0x20000,branch1=0x21000,
        info=0x402000,meta=0x402100,fields=0x402200,booleanInfo=0x402400,booleanMeta=0x402500;
    std::vector<std::byte> heap=std::vector<std::byte>(0x20000),image=std::vector<std::byte>(0x10000);
    std::unordered_map<std::uint32_t,std::string> types;
    std::uint32_t failAt=0,changeAt=0,changeTarget=0,changeValue=0;unsigned calls=0,changeOccurrence=2;
    ReloadStateBinding binding{};ReloadStateOwner owner{player,soldier,weak,weapon,7,8,9};
    std::byte* At(std::uint32_t at){if(at>=base&&at<base+image.size())return image.data()+at-base;return heap.data()+at-0x10000;}
    template<class T>void Put(std::uint32_t at,T value){std::memcpy(At(at),&value,sizeof(value));}
    void Text(std::uint32_t at,const char* s){std::memcpy(At(at),s,std::strlen(s)+1);}
    Fixture(){
        binding.preferredBase=base;binding.imageSize=0x10000;binding.firingVtableRva=0x8000;
        types={{soldier,"ClientSoldierEntity"},{switching,"WeaponSwitchingData"},{data,"SoldierWeaponData"},
            {firing,"WeaponFiringData"},{primary,"FiringFunctionData"}};
        Put<std::uint8_t>(player+0xccd,8);Put(player+0xc54,weak);Put(weak,soldier+4);Put(soldier+0x220,player);Put(player+0xc68,soldier);
        Put<std::uint8_t>(soldier+0x114,1);Put(soldier+0x24c,inventory);Put(soldier+0x248,inventory);
        Put(inventory+4,switching);Put(inventory+0x14c,0u);Put(soldier+0x260,items);Put(soldier+0x264,items+8);Put(items,weapon);
        Put(weapon+4,data);Put(data+0x98,firing);Put(firing+0x40,primary);
        Put(data+0xc,name);Text(name,"SyntheticBoltWithSingleRoundReload");Put(data+0x40,path);Text(path,"Synthetic/ExplicitConfiguration");
        Put(primary+0xc+0x18,1);Put(primary+0xc+0x14,0);Put(primary+0xc+0x40,8);Put(primary+0xc+0x80,29);
        Put(primary+0xc+4,1.f);Put(primary+0xc+12,.72f);Put(primary+0xc,1.f);Put(primary+0xc+0x48,.5f);
        Put(primary+0x170+0x14,4);Put(primary+0x170+0x10,4);
        Put(data+0x8c,info);Put(data+0x90,authored);Put(data+0x94,authored+0xc8);
        Put(info+4,meta);Put(meta,0x403000u);Text(0x403000,"WeaponStateData");Put<std::uint16_t>(meta+4,0x29);Put<std::uint16_t>(meta+6,0xc8);
        Put<std::uint8_t>(meta+13,1);Put(meta+24,fields);Put(fields,0x403100u);Text(0x403100,"IsPumpAction");Put(fields+8,booleanInfo);Put(fields+16,0xc0u);
        Put(booleanInfo+4,booleanMeta);Put(booleanMeta,0x403200u);Text(0x403200,"Boolean");Put<std::uint16_t>(booleanMeta+6,1);
        Put<std::uint8_t>(authored+0xc0,1);
        Put(weapon+0x3c,branch0);Put(weapon+0x40,branch1);
        for(auto branch:{branch0,branch1}){
            Put(branch,base+binding.firingVtableRva);Put(branch+8,firing);Put(branch+12,primary+0x170);
            Put(branch+0x3c,2u);Put(branch+0x40,1u);Put(branch+0x44,2u);Put(branch+0x74,2.f);Put(branch+0x78,1.f);
            Put(branch+0x7c,4);Put(branch+0x80,8);Put(branch+0x98,-1);
        }
    }
    ReloadStateMemory Memory(){return {this,[](void* ctx,std::uint32_t at,void* out,std::size_t size){
        auto& f=*static_cast<Fixture*>(ctx);
        if(at==f.failAt)return false;
        if(at==f.changeAt&&++f.calls==f.changeOccurrence)f.Put(f.changeTarget,f.changeValue);
        const bool heap=at>=0x10000&&std::uint64_t(at)+size<=0x10000+f.heap.size();
        const bool image=at>=base&&std::uint64_t(at)+size<=std::uint64_t(base)+f.image.size();
        if(!heap&&!image)return false;
        std::memcpy(out,f.At(at),size);return true;
    },[](void* ctx,std::uint32_t at,const char* type){
        const auto& f=*static_cast<Fixture*>(ctx);const auto found=f.types.find(at);return found!=f.types.end()&&found->second==type;
    }};}
    ReloadStateResult Read(){return ReadReloadState(Memory(),binding,base,owner,1,1000000);}
};
int ValidObservedSingleRound(){
    Fixture f;const auto before=f.heap;auto result=f.Read();CHECK(result.status==ReloadStateStatus::Observed&&result.snapshot);
    const auto& s=*result.snapshot;CHECK(s.owner==f.owner&&s.sequence==1&&s.eligibilityBranch==0&&s.config.baseCapacity==4);
    CHECK(s.branches[0].loaded==4&&s.branches[0].reserve==8&&s.branches[0].effectiveCapacity==8);
    CHECK(s.branches[0].reloadAmmo==ReloadAmmoEligibility::ObservedCandidate&&s.branches[0].nativeBoltCycleConfigured);
    CHECK(s.branches[0].phase==ReloadObservedPhase::InputProcessing&&s.config.authoredPumpHandling[0]);
    CHECK(s.branchesAgreeOnAmmo&&s.branchesAgreeOnPhase&&s.branches[0].address!=s.branches[1].address&&f.heap==before);
    CHECK(!s.atomicNativeSnapshot&&!s.chamberKnown&&!s.authoritativeBranchKnown&&!s.manualCycleGate&&!s.manualReloadDispatch);
    f.Put<std::uint8_t>(f.soldier+0x114,0x11);f.Put(f.branch1+0x7c,5);f.Put(f.branch1+0x80,7);f.Put(f.branch1+0x3c,11u);
    result=f.Read();CHECK(result.snapshot&&result.snapshot->eligibilityBranch==1&&!result.snapshot->branchesAgreeOnAmmo&&!result.snapshot->branchesAgreeOnPhase);
    CHECK(result.snapshot->branches[1].phase==ReloadObservedPhase::ReloadWait&&result.snapshot->branches[1].reloadAmmo==ReloadAmmoEligibility::NativePhaseBusy);
    return 0;
}
int CapacityAndUnknowns(){
    Fixture f;f.Put(f.branch0+0x98,6);auto r=f.Read();CHECK(r.snapshot&&r.snapshot->branches[0].effectiveCapacity==6);
    f.Put(f.branch0+0x98,-1);f.Put(f.branch0+0x74,1.3f);r=f.Read();CHECK(r.snapshot&&!r.snapshot->branches[0].effectiveCapacity&&r.snapshot->branches[0].reloadAmmo==ReloadAmmoEligibility::Unknown);
    f.Put(f.branch0+0x74,2.f);f.Put(f.branch0+0x80,0);r=f.Read();CHECK(r.snapshot&&r.snapshot->branches[0].reloadAmmo==ReloadAmmoEligibility::NoReserve);
    f.Put(f.branch0+0x7c,8);r=f.Read();CHECK(r.snapshot&&r.snapshot->branches[0].reloadAmmo==ReloadAmmoEligibility::NoRoom);
    f.Put(f.branch0+0x7c,4);f.Put(f.branch0+0x80,-1);r=f.Read();CHECK(r.snapshot&&r.snapshot->branches[0].reloadAmmo==ReloadAmmoEligibility::Unknown);
    f.Put(f.branch0+0x80,8);f.Put<std::uint8_t>(f.branch0+0xa8,8);r=f.Read();CHECK(r.snapshot&&r.snapshot->branches[0].reloadAmmo==ReloadAmmoEligibility::Unknown);
    f.Put<std::uint8_t>(f.branch0+0xa8,0);f.Put(f.primary+0x170+0x14,-1);r=f.Read();CHECK(r.snapshot&&!r.snapshot->branches[0].effectiveCapacity);
    f.Put(f.primary+0x170+0x14,4);f.Put(f.primary+0xc+0x14,1);f.Put(f.primary+0xc+0x18,2);
    r=f.Read();CHECK(r.snapshot&&!r.snapshot->branches[0].nativeBoltCycleConfigured&&r.snapshot->branches[0].reloadAmmo==ReloadAmmoEligibility::ObservedCandidate);
    f.Put(f.primary+0xc+0x14,200);r=f.Read();CHECK(r.snapshot&&r.snapshot->branches[0].reloadAmmo==ReloadAmmoEligibility::Unknown);
    return 0;
}
int AllObservedPhases(){
    const std::array<std::pair<unsigned,ReloadObservedPhase>,9> phases{{{0,ReloadObservedPhase::Unknown},{1,ReloadObservedPhase::ReturnWait},
        {2,ReloadObservedPhase::InputProcessing},{6,ReloadObservedPhase::ShotStep},{7,ReloadObservedPhase::BoltHold},
        {8,ReloadObservedPhase::BoltCycle},{10,ReloadObservedPhase::ReloadBegin},{11,ReloadObservedPhase::ReloadWait},{12,ReloadObservedPhase::ReloadTransfer}}};
    for(auto [state,phase]:phases){Fixture f;f.Put(f.branch0+0x3c,state);auto r=f.Read();CHECK(r.snapshot&&r.snapshot->branches[0].phase==phase);}
    return 0;
}
int OwnershipAndConfigurationRejections(){
    const std::array<std::pair<std::uint32_t,std::uint32_t>,14> corrupt{{
        {Fixture::weak,Fixture::soldier+8},{Fixture::soldier+0x220,Fixture::player+4},{Fixture::player+0xc54,Fixture::weak+4},
        {Fixture::player+0xc68,Fixture::soldier+4},{Fixture::inventory+0x14c,2},{Fixture::soldier+0x264,Fixture::items+260},
        {Fixture::items+4,Fixture::weapon},{Fixture::weapon+4,Fixture::data+4},{Fixture::data+0x98,Fixture::firing+4},
        {Fixture::firing+0x40,Fixture::primary+4},{Fixture::branch0+8,Fixture::firing+4},
        {Fixture::branch0+12,Fixture::primary+0x174},{Fixture::branch0,Fixture::base+0x8004},{Fixture::weapon+0x40,Fixture::branch0}}};
    for(auto [at,value]:corrupt){Fixture f;f.Put(at,value);CHECK(!f.Read().snapshot);}
    {Fixture f;f.owner.actorGeneration=0;CHECK(!f.Read().snapshot);}
    {Fixture f;f.Put<std::uint8_t>(f.player+0xccd,0);CHECK(!f.Read().snapshot);}
    {Fixture f;f.Put(f.soldier+0x24c,0xfffffff0u);CHECK(!f.Read().snapshot);}
    {Fixture f;f.types[f.primary]="WrongType";CHECK(!f.Read().snapshot);}
    {Fixture f;f.failAt=f.branch0;CHECK(f.Read().status==ReloadStateStatus::ReadFailure);}
    return 0;
}
int MalformedAndReflectionRejections(){
    for(auto at:{Fixture::branch0+0x3c,Fixture::branch0+0x40,Fixture::branch0+0x44}){Fixture f;f.Put(at,16u);CHECK(!f.Read().snapshot);}
    for(auto at:{Fixture::branch0+0x50,Fixture::branch0+0x74,Fixture::branch0+0x78,Fixture::primary+0xc+12}){
        Fixture f;f.Put(at,std::numeric_limits<float>::quiet_NaN());CHECK(!f.Read().snapshot);}
    for(auto at:{Fixture::branch0+0x7c,Fixture::branch0+0x80,Fixture::branch0+0x98}){Fixture f;f.Put(at,-2);CHECK(!f.Read().snapshot);}
    {Fixture f;f.Put(f.primary+0xc+4,1.1f);CHECK(!f.Read().snapshot);}
    {Fixture f;f.Put<std::uint8_t>(f.primary+0xc+0x4d,2);CHECK(!f.Read().snapshot);}
    {Fixture f;f.Put(f.fields+16,0xc1u);CHECK(!f.Read().snapshot);}
    {Fixture f;f.Put<std::uint16_t>(f.meta+6,0xc4);CHECK(!f.Read().snapshot);}
    {Fixture f;f.Put<std::uint8_t>(f.meta+13,0);CHECK(!f.Read().snapshot);}
    {Fixture f;f.Text(0x403100,"WrongField");CHECK(!f.Read().snapshot);}
    {Fixture f;f.Put<std::uint8_t>(f.authored+0xc0,2);CHECK(!f.Read().snapshot);}
    {Fixture f;f.Put(f.data+0x94,f.authored+9*0xc8);CHECK(!f.Read().snapshot);}
    {Fixture f;f.Put(f.data+0x94,f.authored+2*0xc8);auto r=f.Read();CHECK(r.snapshot&&r.snapshot->config.authoredStateCount==2);
        CHECK(r.snapshot->config.authoredPumpHandling[0]&&!r.snapshot->config.authoredPumpHandling[1]);}
    return 0;
}
int RacingReadsRejected(){
    const std::array<std::pair<std::uint32_t,std::uint32_t>,6> changes{{
        {Fixture::inventory+0x14c,Fixture::inventory+0x14c},{Fixture::branch0,Fixture::branch0+0x7c},
        {Fixture::branch0,Fixture::weapon+0x3c},{Fixture::data+0x98,Fixture::data+0x98},
        {Fixture::soldier+0x114,Fixture::soldier+0x114},{Fixture::weak,Fixture::weak}}};
    for(auto [readAt,target]:changes){Fixture f;f.changeAt=readAt;f.changeTarget=target;f.changeValue=target==Fixture::soldier+0x114?17u:1u;
        const auto r=f.Read();CHECK(!r.snapshot&&(r.status==ReloadStateStatus::ChangedDuringRead||r.status==ReloadStateStatus::ReadFailure));}
    {Fixture f;auto memory=f.Memory();memory.read=nullptr;CHECK(!ReadReloadState(memory,f.binding,f.base,f.owner,1,1).snapshot);}
    {Fixture f;auto memory=f.Memory();memory.type=nullptr;CHECK(!ReadReloadState(memory,f.binding,f.base,f.owner,1,1).snapshot);}
    {Fixture f;CHECK(!ReadReloadState(f.Memory(),f.binding,f.base,f.owner,0,1).snapshot);}
    return 0;
}
}
namespace {
// Optional local-file proof check. The default deterministic suite never needs
// an installed game. This path reads file bytes only, never a process/module.
int InstalledImageProof(const char* path){
    std::ifstream stream(path,std::ios::binary);CHECK(stream.good());
    const std::vector<char> file((std::istreambuf_iterator<char>(stream)),std::istreambuf_iterator<char>());
    std::vector<std::byte> bytes(file.size());std::memcpy(bytes.data(),file.data(),file.size());
    const auto inspected=fvr::engine::InspectPe(bytes);CHECK(inspected.valid);
    auto pe=inspected.image;const auto binding=DiscoverReloadState(bytes,pe);CHECK(binding);
    struct FileMemory {std::vector<std::byte>* bytes;fvr::engine::PeImage* pe;std::uint32_t base;};
    FileMemory fileMemory{&bytes,&pe,binding->preferredBase};
    ReloadStateMemory memory{&fileMemory,[](void* context,std::uint32_t at,void* out,std::size_t size){
        const auto& f=*static_cast<FileMemory*>(context);if(at<f.base)return false;const auto rva=at-f.base;
        for(const auto& section:f.pe->sections)if(rva>=section.rva){
            const auto delta=std::uint64_t(rva)-section.rva,pos=std::uint64_t(section.rawOffset)+delta;
            if(delta<=section.rawSize&&size<=section.rawSize-delta&&pos<=f.bytes->size()&&size<=f.bytes->size()-pos){std::memcpy(out,f.bytes->data()+pos,size);return true;}
        }return false;
    },nullptr};
    CHECK(ValidateReloadStateLive(memory,*binding,binding->preferredBase));
    const auto offset=[&](std::uint32_t rva){for(const auto& section:pe.sections)if(rva>=section.rva&&rva-section.rva<section.rawSize)return std::size_t(section.rawOffset)+rva-section.rva;return bytes.size();};
    for(const auto& proof:binding->code){
        const auto at=offset(proof.rva+proof.size-1);CHECK(at<bytes.size());bytes[at]^=std::byte{1};
        CHECK(!ValidateReloadStateLive(memory,*binding,binding->preferredBase));
        CHECK(!DiscoverReloadState(bytes,pe));bytes[at]^=std::byte{1};
    }
    const auto table=offset(binding->firingVtableRva);CHECK(table<bytes.size());bytes[table]^=std::byte{1};
    CHECK(!ValidateReloadStateLive(memory,*binding,binding->preferredBase));bytes[table]^=std::byte{1};
    CHECK(!ValidateReloadStateLive(memory,*binding,binding->preferredBase+0x10000));
    const auto start=offset(binding->code[0].rva);CHECK(start<bytes.size());
    const std::vector<std::byte> duplicate(bytes.begin()+start,bytes.begin()+start+binding->code[0].size);
    const auto raw=std::uint32_t(bytes.size());bytes.insert(bytes.end(),duplicate.begin(),duplicate.end());
    pe.sections.push_back({"synthetic_duplicate",pe.imageSize,std::uint32_t(duplicate.size()),raw,std::uint32_t(duplicate.size()),0x20000000});pe.imageSize+=0x1000;
    CHECK(!DiscoverReloadState(bytes,pe));
    std::printf("Installed file: six native proof regions and mutation/ambiguity checks passed.\n");return 0;
}
}
int main(int argc,char** argv){
    if(ValidObservedSingleRound()||CapacityAndUnknowns()||AllObservedPhases()||OwnershipAndConfigurationRejections()||MalformedAndReflectionRejections()||RacingReadsRejected())return 1;
    return argc==2?InstalledImageProof(argv[1]):0;
}


