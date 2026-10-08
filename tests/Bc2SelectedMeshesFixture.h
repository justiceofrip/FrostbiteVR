// Reuses the existing resolver test fixture; production reads still use ReadSelectedMeshes1p.
#include "Bc2SelectedMeshes1p.h"
#include "fvr/engine/BindingValidation.h"
#include "Test.h"
#include <cstring>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <vector>
using namespace fvr::bc2;
namespace {
constexpr std::uint32_t Base=0x400000,Heap=0x1000000;
constexpr std::int64_t Now=1000000000;
const char* Spas="Objects/Weapons/Handheld/UL_shg_SPAS12/UL_shg_SPAS-12_Mesh";
const char* Xm8="Objects/Weapons/Handheld/US_rgl_XM8/US_rgl_XM8_Mesh";
const char* Acog="Objects/Weapons/Unlock/ACOG_4X/US_ACOG_4X_Mesh";
struct Fixture {
    std::vector<std::byte> file=std::vector<std::byte>(0x20000),live=std::vector<std::byte>(0x20000),heap=std::vector<std::byte>(0x20000);
    fvr::engine::PeImage pe{};SelectedMeshesBinding binding{};
    std::uint32_t next=Base+0x4000,nextCode=Base+0x2000;
    ReloadStateOwner owner{Heap,Heap+0x2000,Heap+0x4000,Heap+0x4100,11,12,13};
    std::uint32_t manager=Heap+0x5000,inventory=Heap+0x6000,data=Heap+0x7000,states=Heap+0x8000,items=Heap+0x9000,meshes=Heap+0xa000;
    std::uint32_t stateType=0,meshType=0,meshParent=0,meshNames[2]{},getterTable=0,fieldsState=0;
    unsigned callbackCalls=0,triggerCount=0;std::function<void(Fixture&)> mutation;
    std::byte* Ptr(std::uint32_t at){if(at>=Base&&at<Base+live.size())return live.data()+at-Base;return heap.data()+at-Heap;}
    template<class T>void Put(std::uint32_t at,T value){std::memcpy(Ptr(at),&value,sizeof(value));}
    template<class T>void Disk(std::uint32_t at,T value){std::memcpy(file.data()+at-Base,&value,sizeof(value));Put(at,value);}
    std::uint32_t Alloc(unsigned bytes){const auto at=next;next+=(bytes+3)&~3u;return at;}
    std::uint32_t Text(const char* value){const auto at=Alloc(unsigned(std::strlen(value)+1));std::memcpy(file.data()+at-Base,value,std::strlen(value)+1);std::memcpy(Ptr(at),value,std::strlen(value)+1);return at;}
    std::uint32_t Type(const char* name,std::uint16_t flags,std::uint16_t size){const auto info=Alloc(40),meta=Alloc(28),text=Text(name);
        Disk(info+4,meta);Disk(meta,text);Disk(meta+4,flags);Disk(meta+6,size);Disk(info+0x14,info);return info;}
    std::uint32_t Field(std::uint32_t type,const char* name,std::uint32_t kind,std::uint32_t element,std::uint32_t offset){
        std::uint32_t meta=0;std::memcpy(&meta,Ptr(type+4),4);std::uint16_t flags=0;std::memcpy(&flags,Ptr(meta+4),2);
        const auto field=Alloc(24);Disk(field,Text(name));Disk(field+8,kind);Disk(field+12,element);Disk(field+16,offset);
        Disk(meta+13,std::uint8_t{1});if(flags==0x35)Disk(type+36,field);else Disk(meta+24,field);return field;
    }
    void Object(std::uint32_t at,std::uint32_t info){const auto table=Alloc(12),code=nextCode;nextCode+=16;
        Disk(table+8,code);Disk(code,std::uint8_t{0xb8});Disk(code+1,info);Disk(code+5,std::uint8_t{0xc3});Put(at,table);
    }
    void ConfigureMeshes(bool xm8=false,unsigned stateCount=1){
        Put(data+0x88+12,states+stateCount*0xc8);
        for(unsigned s=0;s<stateCount;++s){const auto array=states+s*0xc8+0x80;Put(array,getterTable);Put(array+8,2u);Put(array+12,xm8?2u:1u);Put(array+16,meshes+s*16);
            Put(meshes+s*16,Heap+0xb000);Put(meshes+s*16+4,Heap+0xb100);}
        meshNames[0]=Text(xm8?Xm8:Spas);meshNames[1]=Text(Acog);
        Object(Heap+0xb000,meshType);Object(Heap+0xb100,meshType);Put(Heap+0xb000+12,meshNames[0]);Put(Heap+0xb100+12,meshNames[1]);
    }
    Fixture(){
        pe.machine=0x14c;pe.imageSize=0x20000;pe.sections={{".text",0x1000,0x2000,0x1000,0x2000,0x60000020},{".data",0x4000,0x1c000,0x4000,0x1c000,0xc0000040}};
        Disk(Base+0x3c,0x100u);Disk(Base+0x100+52,Base);
        const char* signatures[]={
            "B8 01 00 00 00 84 05 ?? ?? ?? ?? 75 66 09 05 ?? ?? ?? ?? 50 B9 ?? ?? ?? ?? E8 ?? ?? ?? ?? 33 C0 68 ?? ?? ?? ?? C7 05",
            "51 53 55 8B 6C 24 10 56 57 55 8B F1 E8 ?? ?? ?? ?? 8D 4E 10 E8 ?? ?? ?? ?? 8D 7E 4C 8B CF E8 ?? ?? ?? ?? 8D 4F 14 33 DB",
            "8B 81 54 0C 00 00 85 C0 74 0A 8B 00 85 C0 74 04 83 C0 FC C3 33 C0 C3",
            "8B 44 24 04 56 50 8B F1 E8 ?? ?? ?? ?? 8B 4C 24 10 8B 44 24 0C 8B 54 24 14 89 4E 10 33 C9 38 0D ?? ?? ?? ?? C7 06 ?? ?? ?? ?? 89 46 14 89 4E 18 66 89 4E 1C 66 89 4E 1E 89 4E 20 89 56 24"};
        for(unsigned i=0;i<4;++i){auto p=fvr::engine::ParsePattern(signatures[i]);for(unsigned k=0;k<p->size();++k)Disk(Base+0x1000+i*0x100+k,std::uint8_t((*p)[k].wildcard?0:(*p)[k].value));}
        Disk(Base+0x1000+21,Base+0x18000);Disk(Base+0x1100+0x6f,Base+0x18100);Put(Base+0x18008,manager);Put(manager,Base+0x18100);Put(manager+0xb4,owner.player);
        const auto soldier=Type("ClientSoldierEntity",0x35,0x300),switching=Type("WeaponSwitchingData",0x35,0x20);
        Object(owner.soldier,soldier);Object(Heap+0x6100,switching);Put(inventory+4,Heap+0x6100);
        Put(owner.player+0xccd,std::uint8_t{8});Put(owner.player+0xc54,owner.weak);Put(owner.weak,owner.soldier+4);
        Put(owner.player+0xc68,owner.soldier);Put(owner.soldier+0x220,owner.player);Put(owner.soldier+0x114,std::uint8_t{1});Put(owner.soldier+0x24c,inventory);
        Put(owner.soldier+0x260,items);Put(owner.soldier+0x264,items+8);Put(items,owner.weapon);Put(items+4,Heap+0x4200);Put(inventory+0x14c,0u);
        const auto array=Type("ArrayBase",0x101,20),string=Type("String",0x101,4),weapon=Type("SoldierWeaponData",0x35,0xb0);
        stateType=Type("WeaponStateData",0x29,0xc8);meshType=Type("SkinnedMeshAsset",0x35,0x44);meshParent=Type("MeshAsset",0x35,0x40);
        const auto asset=Type("Asset",0x35,0x18),container=Type("DataContainer",0x35,0xc);
        Put(meshType+0x14,meshParent);Put(meshParent+0x14,asset);Put(asset+0x14,container);
        Field(asset,"Name",string,0,12);Field(weapon,"WeaponStates",array,stateType,0x88);fieldsState=Field(stateType,"Meshes1p",array,meshType,0x80);
        Object(data,weapon);Put(owner.weapon+4,data);Put(data+12,Text("SPAS12_sp"));Put(data+0x88+4,stateType);Put(data+0x88+8,states);
        getterTable=Alloc(12);Disk(getterTable,Base+0x2900);Disk(getterTable+8,Base+0x2910);Disk(Base+0x2900,0xc30c418bu);Disk(Base+0x2910,0xc310418bu);
        ConfigureMeshes();binding=*DiscoverSelectedMeshes1p(file,pe);
    }
    static bool Read(void* opaque,std::uint32_t at,void* dst,std::size_t n){auto& f=*static_cast<Fixture*>(opaque);++f.callbackCalls;
        if(at==f.owner.weapon+4&&++f.triggerCount==2&&f.mutation)f.mutation(f);
        if(!((at>=Base&&std::uint64_t(at)+n<=Base+f.live.size())||(at>=Heap&&std::uint64_t(at)+n<=Heap+f.heap.size())))return false;
        std::memcpy(dst,f.Ptr(at),n);return true;}
    SelectedMeshesResult Run(bool enabled=true){return ReadSelectedMeshes1p({this,Read,nullptr},binding,Base,owner,1,Now,Now+100000000,enabled);}
};

} // anonymous fixture namespace
