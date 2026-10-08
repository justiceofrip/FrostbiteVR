#include "Test.h"
#include "Bc2MagazineInteraction.h"
#include <cstdio>
#include <array>
#include <cstring>
#include <string>
#include <unordered_map>
using namespace fvr;
namespace {
struct Fixture {
    std::array<std::byte,0x10000> bytes{};
    std::unordered_map<unsigned,std::string> types;
    unsigned currentReads=0;bool changeCurrent=false;
    static constexpr unsigned soldier=0x10000,inventory=0x11000,items=0x12000,
        rifle=0x13000,launcher=0x13200,rifleData=0x14000,launcherData=0x14400,
        persistent=0x15000,switching=0x17000,map=0x18000,forward=0x19000,backward=0x19100;
    void Word(unsigned at,unsigned value){std::memcpy(bytes.data()+at-0x10000,&value,4);}
    void Text(unsigned at,const char* value){std::memcpy(bytes.data()+at-0x10000,value,std::strlen(value)+1);}
    Fixture(){
        types={{soldier,"ClientSoldierEntity"},{rifleData,"SoldierWeaponData"},
            {launcherData,"SoldierWeaponData"},{persistent,"PersistentWeapon"},{switching,"WeaponSwitchingData"}};
        Word(soldier+0x248,inventory);Word(soldier+0x260,items);Word(soldier+0x264,items+9*4);
        Word(inventory+0x14c,1);Word(inventory+4,switching);
        Word(items+4,rifle);Word(items+12,launcher);
        Word(rifle+4,rifleData);Word(launcher+4,launcherData);
        Word(rifleData+0xc,0x16000);Text(0x16000,"XM8_sp_s");
        Word(launcherData+0xc,0x16100);Text(0x16100,"40mmgl");
        Word(rifleData+0x40,0x16200);Text(0x16200,"Objects/Weapons/Handheld/US_rgl_XM8/SP_rgl_XM8_Scoped");
        Word(launcherData+0x40,0x16300);Text(0x16300,"Objects/Weapons/Handheld/US_rgl_XM8/SP_rgl_XM320_Scoped");
        Word(rifleData+0x64,persistent);Word(launcherData+0x64,persistent);
        Word(persistent+0x40,0x16400);Text(0x16400,"sp_xm8_s");
        Word(switching+0x18,map);Word(switching+0x1c,map+48);
        Word(map+8,forward);Word(map+12,forward+8);Word(map+16,33);Word(map+20,1);
        Word(forward,3);Word(forward+4,2);
        Word(map+24+8,backward);Word(map+24+12,backward+4);Word(map+24+16,36);Word(map+24+20,3);
        Word(backward,1);
    }
    bc2::WeaponModeMemory Memory(){
        return {this,[](void* ctx,unsigned at,void* out,std::size_t n){
            auto& f=*static_cast<Fixture*>(ctx);
            if(at<0x10000||std::uint64_t(at)+n>0x20000)return false;
            if(at==inventory+0x14c&&f.changeCurrent&&++f.currentReads>1){unsigned changed=3;std::memcpy(out,&changed,4);return true;}
            std::memcpy(out,f.bytes.data()+at-0x10000,n);return true;
        },[](void* ctx,unsigned at,const char* expected){
            const auto& f=*static_cast<Fixture*>(ctx);const auto found=f.types.find(at);
            return found!=f.types.end()&&found->second==expected;
        }};
    }
};
}
int main(){
    Fixture f;constexpr std::int64_t now=1000000000;
    bc2::ReloadStateOwner owner{0x20000,f.soldier,0x30000,f.rifle,5,3,7};
    interaction::HandInteractionSample input{{(std::uint64_t(owner.weak)<<32)|owner.soldier,5,17,7},1,now,now+100000000,now,true,{true,true},{true,false}};
    const interaction::HandInteractionKey key{f.persistent,17};const auto bytes=f.bytes;
    const auto proof=bc2::ResolveMagazineFamily(f.Memory(),owner,input,key);
    CHECK(proof&&proof->binding.owner==owner&&proof->binding.weapon==key&&key.id!=owner.weapon);
    CHECK(proof->binding.inventory==f.inventory&&proof->binding.launcher==f.launcher&&proof->binding.launcherSlot==3&&f.bytes==bytes);
    CHECK(bc2::MagazineFamilyFresh(*proof,owner,input.owner,key,now));
    CHECK(!bc2::MagazineFamilyFresh(*proof,owner,input.owner,key,proof->deadlineNs));
    CHECK(!bc2::ResolveMagazineFamily(f.Memory(),owner,input,{f.rifle,17}));
    CHECK(!bc2::ResolveMagazineFamily(f.Memory(),owner,input,{f.persistent,18}));
    auto other=input;++other.owner.actorGeneration;CHECK(!bc2::ResolveMagazineFamily(f.Memory(),owner,other,key));
    Fixture launcher;launcher.Word(launcher.inventory+0x14c,3);auto switched=owner;switched.weapon=launcher.launcher;
    CHECK(!bc2::ResolveMagazineFamily(launcher.Memory(),switched,input,key));
    Fixture forged;forged.Word(forged.launcherData+0x64,forged.persistent+4);forged.types[forged.persistent+4]="PersistentWeapon";
    CHECK(!bc2::ResolveMagazineFamily(forged.Memory(),owner,input,key));
    Fixture race;race.changeCurrent=true;CHECK(!bc2::ResolveMagazineFamily(race.Memory(),owner,input,key));
    std::puts("BC2 magazine family: fresh native-to-persistent resolver and negative mappings passed");return 0;
}
