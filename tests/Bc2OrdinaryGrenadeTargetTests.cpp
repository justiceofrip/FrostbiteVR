#include "Test.h"
#include "Bc2ReloadState.h"
#include "Bc2ReloadServer.h"
#include "Bc2ReloadStateRetry.h"
#include "Bc2OrdinaryGrenadeProbe.h"
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

struct ServerFixture:Fixture {
 static constexpr unsigned manager=0x40000,players=0x41000,sp=0x43000,ss=0x45000,si=0x47000,sa=0x48000,sw=0x49000,effects=0x4a000,sf=0x4b000,callback=0x4c000;
 static constexpr unsigned target=0x23000,tb0=0x24000,tb1=0x25000,tsw=0x50000,teffects=0x51000,tsf=0x52000,tcallback=0x53000;
 ReloadServerBinding server{};
 ServerFixture(){
  heap.resize(0x50000);server.preferredBase=base;server.imageSize=0x10000;server.contextRva=0x100;server.managerVtableRva=0x800;server.controlledGetterRva=0x900;
  Put(base+0x108,manager);Put(manager,base+0x800);Put(manager+4,1u);Put(manager+0x6c,players);Put(players,sp);Put(player+0x154,0u);Put(sp+0x154,0u);
  Put(sp,base+0xa00);Put(base+0xa30,base+0x900);Put(sp+0xc3c,ss);Put(sp+0xc6c,ss);Put(ss,base+0xb00);Put(ss+0xc,data);Put(soldier+0xc,data);Put(ss+0x220,sp);
  Put(ss+0x2b4,si);Put(si+4,switching);Put(ss+0x2b8,sa);Put(ss+0x2bc,sa+8);Put(sa,sw);Put(si+0x14c,0u);
  types[ss]="ServerSoldierEntity";types[effects]=types[teffects]="ServerWeaponFiringEffects";
  std::memcpy(At(target),At(weapon),0x44);Put(items+4,target);Put(target+0x3c,tb0);Put(target+0x40,tb1);
  std::memcpy(At(tb0),At(branch0),0xb0);std::memcpy(At(tb1),At(branch1),0xb0);Put(sa+4,tsw);
  for(auto a:{std::array<unsigned,4>{sw,effects,sf,callback},std::array<unsigned,4>{tsw,teffects,tsf,tcallback}}){
   Put(a[0]+4,data);Put(a[0]+0xc,a[1]);Put(a[0]+0x10,a[2]);Put(a[1],base+0xc00);Put(a[1]+0x10,firing);Put(a[1]+0x128,sp);Put(a[1]+0xd4,a[3]);Put(a[3]+4,firing);
   std::memcpy(At(a[2]),At(branch0),0xb0);
  }
 }
 std::optional<ReloadServerInventoryItem> Target(){const auto current=Read();if(!current.snapshot)return {};const auto s=ReadReloadServerState(Memory(),server,binding,base,*current.snapshot);
  return s?ReadReloadServerInventoryItem(Memory(),server,binding,base,*s,target):std::nullopt;}
};
// This fixture traverses the actual full grenade reader, including reflected
// classes/flags, route, selected owner and all client/server firing joins.
struct GrenadeFixture:ServerFixture {
 static constexpr unsigned td=0x57000,tf=0x58000,tp=0x59000,ta=0x5a000,tn=0x5b000,
  routes=0x55000,targets=0x55100,weaponInfo=0x404000,weaponMeta=0x404100,weaponFields=0x404200,
  weaponTable=0x404400,weaponGetter=0x404420,enumInfo=0x404500,enumMeta=0x404600,enumFields=0x404700,
  firingInfo=0x405000,firingMeta=0x405100,firingFields=0x405200,firingTable=0x405400,firingGetter=0x405420;
 unsigned stringAt=0x406000;
 unsigned ImageText(const char* s){const auto at=stringAt;Text(at,s);stringAt+=128;return at;}
 void Meta(unsigned i,unsigned m,const char* typeName,unsigned flags,unsigned size,unsigned f,unsigned count){
  Put(i+4,m);Put(m,ImageText(typeName));Put<std::uint16_t>(m+4,static_cast<std::uint16_t>(flags));
  Put<std::uint16_t>(m+6,static_cast<std::uint16_t>(size));Put<std::uint8_t>(m+13,static_cast<std::uint8_t>(count));
  Put(m+24,f);Put(i+36,f);
 }
 void Field(unsigned f,const char* fieldName,unsigned type,unsigned offset){Put(f,ImageText(fieldName));Put(f+8,type);Put(f+16,offset);}
 void Getter(unsigned table,unsigned getter,unsigned typeInfo){Put(table+8,getter);Put<std::uint8_t>(getter,0xb8);Put(getter+1,typeInfo);Put<std::uint8_t>(getter+5,0xc3);}
 GrenadeFixture(){
  Text(name,"SCAR_sp");Put(primary+0xc+0x18,2);Put(data+0x84,2u);
  std::memcpy(At(td),At(data),0x130);std::memcpy(At(tf),At(firing),0x4c);
  std::memcpy(At(tp),At(primary),0x188);std::memcpy(At(ta),At(authored),0xc8);
  types[td]="SoldierWeaponData";types[tf]="WeaponFiringData";types[tp]="FiringFunctionData";
  Put(target+4,td);Put(tsw+4,td);Put(td+0x84,6u);Put(td+0xc,tn);Text(tn,"HG-2");
  Put(td+0x98,tf);Put(tf+0x40,tp);Put(tp+0xc+0x18,3);Put(td+0x90,ta);Put(td+0x94,ta+0xc8);
  Put(teffects+0x10,tf);Put(tcallback+4,tf);
  for(auto branch:{tb0,tb1,tsf}){Put(branch+8,tf);Put(branch+12,tp+0x170);}
  Put(switching+0x18,routes);Put(switching+0x1c,routes+24);Put(routes+8,targets);Put(routes+12,targets+4);
  Put(routes+16,38u);Put(routes+20,0u);Put(targets,1u);
  Meta(weaponInfo,weaponMeta,"SoldierWeaponData",0x35,0x130,weaponFields,5);
  Meta(enumInfo,enumMeta,"WeaponClassEnum",0x179,4,enumFields,7);
  Field(weaponFields,"WeaponClass",enumInfo,0x84);
  const char* classes[]={"wcAssault","wcShotgun","wcSmg","wcLmg","wcSniper","wcUgl","wcHgr"};
  for(unsigned n=0;n<7;++n)Field(enumFields+n*24,classes[n],0,n);
  const char* flags[]={"HideWhenOutOfAmmo","SwitchToPrimaryWhenOutOfAmmo","SwitchToPreviousWeaponAfterFire","AllowSwitchingToWeaponOutOfAmmo"};
  for(unsigned n=0;n<4;++n){Field(weaponFields+(n+1)*24,flags[n],booleanInfo,0x121+n);Put<std::uint8_t>(td+0x121+n,n<3?1:0);}
  Getter(weaponTable,weaponGetter,weaponInfo);Put(data,weaponTable);Put(td,weaponTable);
  Meta(firingInfo,firingMeta,"WeaponFiringData",0x35,0x4c,firingFields,1);
  Field(firingFields,"FireOnDeploy",booleanInfo,0x48);Getter(firingTable,firingGetter,firingInfo);
  Put(firing,firingTable);Put(tf,firingTable);Put<std::uint8_t>(firing+0x48,0);Put<std::uint8_t>(tf+0x48,1);
 }
 std::optional<private_grenade::Native> Grenade(){return private_grenade::ReadNative(Memory(),binding,server,base,owner,1,1000000);}
};
}
int main(int argc,char** argv){
 {
  {GrenadeFixture f;const auto before=f.heap;const auto n=f.Grenade();CHECK(n&&n->original.client.target==f.weapon&&n->target.client.target==f.target);CHECK(f.heap==before);}
  for(unsigned category:{0u,1u,2u,3u,4u}){GrenadeFixture f;f.Put(f.data+0x84,category);CHECK(f.Grenade());}
  for(unsigned category:{5u,6u,7u,8u,9u,10u,11u,12u,13u,14u,15u,16u,17u,18u,19u,20u,UINT32_MAX}){
   GrenadeFixture f;f.Put(f.data+0x84,category);CHECK(!f.Grenade());}
  {GrenadeFixture f;f.Put(f.enumFields+2*24+16,3u);CHECK(!f.Grenade());}
  {GrenadeFixture f;f.Put<std::uint8_t>(f.firing+0x48,1);CHECK(!f.Grenade());}
  {GrenadeFixture f;f.Put(f.primary+0xc+0x18,1);CHECK(!f.Grenade());}
  {GrenadeFixture f;f.Put(f.primary+0xc+0x40,9);CHECK(!f.Grenade());}
  {GrenadeFixture f;f.Put(f.routes+12,f.targets+8);CHECK(!f.Grenade());}
  {GrenadeFixture f;f.Put(f.tcallback+4,f.firing);CHECK(!f.Grenade());}
  {GrenadeFixture f;f.changeAt=f.target+4;f.changeTarget=f.inventory+0x14c;f.changeValue=1;CHECK(!f.Grenade());}
  std::puts("Full native grenade reader: SCAR/class2 and five reflected firearm classes PASS; 17 unsupported classes and seven ownership/configuration/reflection boundaries rejected; no native writes.");
  if(argc==2&&std::string(argv[1])=="--firearm-class")return 0;
 }

 {ServerFixture f;const auto before=f.heap;const auto value=f.Target();CHECK(value&&value->client.target==f.target&&value->client.selectedOwner==f.owner&&value->client.slot==1&&value->server.address==f.tsf);CHECK(f.heap==before);
  auto fabricated=f.owner;fabricated.weapon=f.target;CHECK(!ReadReloadInventoryItem(f.Memory(),f.binding,f.base,fabricated,f.target));}
 {ServerFixture f;f.Put(f.target+4,f.data+4);CHECK(!f.Target());}
 {ServerFixture f;f.Put(f.teffects+0x128,f.sp+4);CHECK(!f.Target());}
 {ServerFixture f;f.Put(f.tcallback+4,f.firing+4);CHECK(!f.Target());}
 {ServerFixture f;f.Put(f.tsf+12,f.primary+0x174);CHECK(!f.Target());}
 {ServerFixture f;f.Put(f.items,f.target);CHECK(!f.Target());}
 {ServerFixture f;f.changeAt=f.tb0;f.changeTarget=f.tb0+0x7c;f.changeValue=99;CHECK(!f.Target());}
 {ServerFixture f;f.changeAt=f.target+4;f.changeTarget=f.inventory+0x14c;f.changeValue=1;CHECK(!f.Target());}
 std::printf("Eight focused unselected-target reader cases PASS; actual selected owner retained, target configuration/server callbacks/torn reads rejected, no native writes.\n");return 0;
}
