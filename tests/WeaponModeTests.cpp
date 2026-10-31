#include "Test.h"
#include "Bc2WeaponMode.h"
#include <array>
#include <cstring>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>
using namespace fvr;
namespace {
struct Fixture {
    std::array<std::byte,0x10000> bytes{};
    std::unordered_map<unsigned,std::string> types;
    std::vector<std::pair<unsigned,std::size_t>> reads;
    std::vector<std::pair<unsigned,std::string>> typeReads;
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
            f.reads.emplace_back(at,n);
            if(at<0x10000||std::uint64_t(at)+n>0x20000)return false;
            if(at==inventory+0x14c&&f.changeCurrent&&++f.currentReads>1){unsigned changed=3;std::memcpy(out,&changed,4);return true;}
            std::memcpy(out,f.bytes.data()+at-0x10000,n);return true;
        },[](void* ctx,unsigned at,const char* expected){
            auto& f=*static_cast<Fixture*>(ctx);f.typeReads.emplace_back(at,expected);const auto found=f.types.find(at);
            return found!=f.types.end()&&found->second==expected;
        }};
    }
};
}
int main(){
    // Every enrolled exact pair uses the same owner/link/reciprocal-route reader.
    // Shared asset labels and measured geometry never substitute for those checks.
    for(const auto& profile:bc2::WeaponModeFamilies){
        Fixture pair;pair.Text(0x16000,profile.primaryAsset.data());pair.Text(0x16100,profile.secondaryAsset.data());
        pair.Text(0x16200,profile.primaryConfiguration.data());pair.Text(0x16300,profile.secondaryConfiguration.data());
        pair.Text(0x16400,profile.persistentId.data());const auto untouched=pair.bytes;Fixture observed=pair;
        auto mode=bc2::ResolveWeaponMode(pair.Memory(),pair.soldier,pair.rifle);
        CHECK(mode&&mode->family==&profile&&mode->action==33&&mode->targetWeapon==pair.launcher&&pair.bytes==untouched);
        bc2::WeaponModeResolution diagnosis;
        const auto same=bc2::ResolveWeaponMode(observed.Memory(),observed.soldier,observed.rifle,&diagnosis);
        CHECK(same&&same->family==mode->family&&same->targetWeapon==mode->targetWeapon&&same->action==mode->action);
        CHECK(observed.bytes==untouched&&observed.reads==pair.reads&&observed.typeReads==pair.typeReads);
        CHECK(std::string_view(diagnosis.status)=="resolved"&&diagnosis.candidatesDropped==0);
        const bc2::WeaponModeCandidateObservation* accepted=nullptr;
        for(unsigned i=0;i<diagnosis.candidateCount;++i)if(diagnosis.candidates[i].family==&profile)accepted=&diagnosis.candidates[i];
        CHECK(accepted&&std::string_view(accepted->status)=="resolved"&&accepted->inventory==pair.inventory);
        CHECK(accepted->primary.weapon==pair.rifle&&accepted->secondary.weapon==pair.launcher);
        CHECK(accepted->routes[0].targetsRead==2&&accepted->routes[0].targets[0]==3&&accepted->routes[0].firstOccupiedWeapon==pair.launcher);
        std::ostringstream json;bc2::WriteWeaponModeResolution(json,diagnosis);
        CHECK(json.str().size()<10000&&json.str().find("\"candidatesDropped\":0")!=std::string::npos);
        pair.Word(pair.inventory+0x14c,3);mode=bc2::ResolveWeaponMode(pair.Memory(),pair.soldier,pair.launcher);
        CHECK(mode&&mode->family==&profile&&mode->action==36&&mode->targetWeapon==pair.rifle);
        pair.Word(pair.map+24+16,33);CHECK(!bc2::ResolveWeaponMode(pair.Memory(),pair.soldier,pair.launcher));
        pair.Word(pair.map+24+16,36);pair.Word(pair.launcherData+0x64,pair.persistent+4);
        pair.types[pair.persistent+4]="PersistentWeapon";CHECK(!bc2::ResolveWeaponMode(pair.Memory(),pair.soldier,pair.launcher));
        pair.Word(pair.launcherData+0x64,pair.persistent);pair.Text(0x16400,"foreign_owner");
        CHECK(!bc2::ResolveWeaponMode(pair.Memory(),pair.soldier,pair.launcher));
    }
    Fixture f;const auto original=f.bytes;
    auto command=bc2::ResolveWeaponMode(f.Memory(),f.soldier,f.rifle);
    CHECK(command&&command->action==33&&command->targetSlot==3&&command->targetWeapon==f.launcher);
    CHECK(command->inventory==f.inventory&&command->persistent==f.persistent&&f.bytes==original);
    f.Word(f.inventory+0x14c,3);
    command=bc2::ResolveWeaponMode(f.Memory(),f.soldier,f.launcher);
    CHECK(command&&command->action==36&&command->targetSlot==1&&command->targetWeapon==f.rifle);
    CHECK(!bc2::ResolveWeaponMode(f.Memory(),f.soldier,f.rifle));
    {Fixture g;g.types[g.launcherData]="WeaponData";CHECK(!bc2::ResolveWeaponMode(g.Memory(),g.soldier,g.rifle));}
    {Fixture g;g.Word(g.launcherData+0x64,g.persistent+4);g.types[g.persistent+4]="PersistentWeapon";
        CHECK(!bc2::ResolveWeaponMode(g.Memory(),g.soldier,g.rifle));}
    {Fixture g;g.Text(0x16400,"sp_other");CHECK(!bc2::ResolveWeaponMode(g.Memory(),g.soldier,g.rifle));}
    {Fixture g;g.Text(0x16300,"Objects/Weapons/Handheld/OtherLauncher");CHECK(!bc2::ResolveWeaponMode(g.Memory(),g.soldier,g.rifle));}
    {Fixture g;g.Text(0x16100,"40mmgl_other");CHECK(!bc2::ResolveWeaponMode(g.Memory(),g.soldier,g.rifle));}
    // The first populated map target determines the action. Do not borrow a
    // later matching target after a different available item or pseudo-slot.
    {Fixture g;g.Word(g.items+8,0x13500);g.Word(g.forward,2);g.Word(g.forward+4,3);
        CHECK(!bc2::ResolveWeaponMode(g.Memory(),g.soldier,g.rifle));}
    {Fixture g;g.Word(g.forward,2);g.Word(g.forward+4,3);
        CHECK(bc2::ResolveWeaponMode(g.Memory(),g.soldier,g.rifle));} // Absent slot skipped.
    {Fixture g;g.Word(g.forward,9);CHECK(!bc2::ResolveWeaponMode(g.Memory(),g.soldier,g.rifle));}
    {Fixture g;g.Word(g.backward,0);g.Word(g.items,0x13500);CHECK(!bc2::ResolveWeaponMode(g.Memory(),g.soldier,g.rifle));}
    {Fixture g;g.Word(g.map+16,38);CHECK(!bc2::ResolveWeaponMode(g.Memory(),g.soldier,g.rifle));}
    {Fixture g;g.Word(g.map+24+16,33);CHECK(!bc2::ResolveWeaponMode(g.Memory(),g.soldier,g.rifle));}
    {Fixture g;g.Word(g.map+20,0);CHECK(!bc2::ResolveWeaponMode(g.Memory(),g.soldier,g.rifle));}
    {Fixture g;g.Word(g.switching+0x1c,g.map+24);CHECK(!bc2::ResolveWeaponMode(g.Memory(),g.soldier,g.rifle));}
    {Fixture g;g.Word(g.switching+0x1c,g.map+49);CHECK(!bc2::ResolveWeaponMode(g.Memory(),g.soldier,g.rifle));}
    {Fixture g;g.Word(g.switching+0x1c,g.map+129*24);CHECK(!bc2::ResolveWeaponMode(g.Memory(),g.soldier,g.rifle));}
    {Fixture g;g.Word(g.map+12,g.forward+45);CHECK(!bc2::ResolveWeaponMode(g.Memory(),g.soldier,g.rifle));}
    {Fixture g;g.Word(g.soldier+0x264,g.items+260);CHECK(!bc2::ResolveWeaponMode(g.Memory(),g.soldier,g.rifle));}
    {Fixture g;g.Word(g.items+20,g.rifle);CHECK(!bc2::ResolveWeaponMode(g.Memory(),g.soldier,g.rifle));}
    {Fixture g;g.changeCurrent=true;CHECK(!bc2::ResolveWeaponMode(g.Memory(),g.soldier,g.rifle));}
    {Fixture g;g.Word(g.soldier+0x248,0xfffffff0);CHECK(!bc2::ResolveWeaponMode(g.Memory(),g.soldier,g.rifle));}
    {Fixture g;auto mem=g.Memory();mem.type=nullptr;CHECK(!bc2::ResolveWeaponMode(mem,g.soldier,g.rifle));}
    // The observed campaign primary occupies slot zero. This is already valid
    // when both native routes point to the linked pair; no one-gun slot override.
    for(const auto& profile:bc2::WeaponModeFamilies){Fixture g;
        g.Text(0x16000,profile.primaryAsset.data());g.Text(0x16100,profile.secondaryAsset.data());
        g.Text(0x16200,profile.primaryConfiguration.data());g.Text(0x16300,profile.secondaryConfiguration.data());g.Text(0x16400,profile.persistentId.data());
        g.Word(g.items,g.rifle);g.Word(g.items+4,0x13500);g.Word(g.inventory+0x14c,0);g.Word(g.map+20,0);g.Word(g.backward,0);
        bc2::WeaponModeCandidateObservation diagnosis;
        const auto valid=bc2::ObserveWeaponModeFamily(g.Memory(),g.soldier,g.rifle,profile,&diagnosis);
        CHECK(valid&&valid->family==&profile&&std::string_view(diagnosis.status)=="resolved");
        g.Word(g.backward,1);Fixture without=g;g.reads.clear();g.typeReads.clear();without.reads.clear();without.typeReads.clear();
        CHECK(!bc2::ObserveWeaponModeFamily(g.Memory(),g.soldier,g.rifle,profile,&diagnosis));
        CHECK(!bc2::ObserveWeaponModeFamily(without.Memory(),without.soldier,without.rifle,profile));
        CHECK(g.reads==without.reads&&g.typeReads==without.typeReads);
        CHECK(std::string_view(diagnosis.status)=="primary_route"&&std::string_view(diagnosis.routes[1].status)=="first_target_mismatch");
        CHECK(diagnosis.selected==0&&diagnosis.routes[1].wanted==0&&diagnosis.routes[1].firstOccupiedSlot==1&&diagnosis.routes[1].firstOccupiedWeapon==0x13500);
    }
    const auto diagnose=[](Fixture& g){bc2::WeaponModeCandidateObservation out;
        if(bc2::ObserveWeaponModeFamily(g.Memory(),g.soldier,g.rifle,bc2::WeaponModeFamilies[0],&out))out.status="unexpected_success";
        return out;};
    {Fixture g;g.Text(0x16300,"Other/ExactConfiguration");const auto d=diagnose(g);
        CHECK(std::string_view(d.status)=="item_configuration_mismatch"&&d.lastItem.weapon==g.launcher&&d.lastItem.configuration=="Other/ExactConfiguration");}
    {Fixture g;g.Word(g.items+20,g.rifle);const auto d=diagnose(g);
        CHECK(std::string_view(d.status)=="duplicate_asset_item"&&d.lastItem.slot==5&&d.primary.weapon==g.rifle);}
    {Fixture g;g.Word(g.launcherData+0x64,g.persistent+4);g.types[g.persistent+4]="PersistentWeapon";const auto d=diagnose(g);
        CHECK(std::string_view(d.status)=="linked_persistence_mismatch"&&d.primary.persistent==g.persistent&&d.secondary.persistent==g.persistent+4);}
    {Fixture g;g.Word(g.forward,9);const auto d=diagnose(g);
        CHECK(std::string_view(d.status)=="secondary_route"&&std::string_view(d.routes[0].status)=="target_out_of_range"&&d.routes[0].targets[0]==9);}
    {Fixture g;g.changeCurrent=true;const auto d=diagnose(g);
        CHECK(std::string_view(d.status)=="owner_or_selection_changed"&&d.bookendAddress==g.inventory+0x14c&&d.bookendExpected==1&&d.bookendObserved==3);}
    {Fixture g;g.Word(g.items+12,0);const auto d=diagnose(g);
        CHECK(std::string_view(d.status)=="linked_pair_missing"&&d.itemCount==9&&d.items[3]==0&&d.namedItems==(1ull<<1));}
    return 0;
}
