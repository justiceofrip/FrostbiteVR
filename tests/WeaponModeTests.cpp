#include "Test.h"
#include "Bc2WeaponMode.h"
#include <array>
#include <cstring>
#include <initializer_list>
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
    void Route(unsigned row,unsigned state,unsigned action,std::initializer_list<unsigned> targets){
        const auto at=map+row*24,list=forward+row*44;
        Word(at+8,list);Word(at+12,list+unsigned(targets.size())*4);Word(at+16,action);Word(at+20,state);
        unsigned n=0;for(const auto slot:targets)Word(list+4*n++,slot);
    }
    void CampaignMap(unsigned primarySlot=0){
        // All 44 rows from feedback68/destruction03/run01/native-mode-inspection.json.
        // Slot zero/launcher two is the observed XM8 inventory; slot one checks
        // the other native primary route against the same complete map.
        struct Row {unsigned state,action;std::initializer_list<unsigned> targets;};
        const Row rows[]{
            {0,36,{1}},{0,33,{2,3}},{0,35,{5}},{0,37,{7}},{0,38,{8}},{0,34,{4}},{0,7,{2,3,5,0,1}},
            {1,25,{0}},{1,33,{3,2}},{1,35,{5}},{1,37,{7}},{1,38,{8}},{1,34,{4,9,10}},{1,7,{0,3,2,5,1}},
            {2,25,{0}},{2,36,{1}},{2,35,{5}},{2,37,{7}},{2,38,{8}},{2,34,{4}},{2,33,{3,2}},{2,7,{5,2,1,0}},
            {3,25,{0}},{3,36,{1}},{3,35,{5}},{3,37,{7}},{3,38,{8}},{3,34,{4}},{3,33,{2,3}},{3,7,{5,3,1,0}},
            {5,25,{0}},{5,33,{2,3}},{5,36,{1}},{5,37,{7}},{5,38,{8}},{5,34,{4}},{5,7,{5,1,0,2,3}},
            {4,25,{0}},{4,33,{2,3}},{4,36,{1}},{4,36,{1}},{4,38,{8}},{4,37,{7}},{4,7,{9,10}}};
        static_assert(std::size(rows)==44);
        Word(soldier+0x114,3);Word(soldier+0x24c,inventory);Word(soldier+0x264,items+10*4);
        for(unsigned slot=0;slot<10;++slot)Word(items+slot*4,0);
        Word(items+primarySlot*4,rifle);Word(items+8,launcher);
        Word(items+16,0x13500);Word(items+28,0x13600);Word(items+32,0x13700);
        Word(inventory+0x14c,primarySlot);Word(switching+0x1c,map+44*24);
        unsigned n=0;for(const auto& row:rows)Route(n++,row.state,row.action,row.targets);
    }
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
    // A fixed consumer can reach the last live cache milliseconds with a fresh
    // 100 ms raw packet. Require a real native reread before publishing that
    // frame, while keeping the original 500 ms observation boundary exact.
    constexpr std::int64_t familyObserved=1000000000;
    constexpr auto familyDeadline=familyObserved+bc2::WeaponModeCacheMaxAgeNs;
    CHECK(bc2::WeaponModeCacheLeaseCovers(familyObserved,familyObserved,familyObserved+100000000));
    CHECK(bc2::WeaponModeCacheLeaseCovers(familyObserved,familyDeadline-100000000,familyDeadline));
    CHECK(!bc2::WeaponModeCacheLeaseCovers(familyObserved,familyDeadline-100000000,familyDeadline+1));
    CHECK(!bc2::WeaponModeCacheLeaseCovers(familyObserved,familyDeadline-9354300,familyDeadline+90645700));
    CHECK(bc2::WeaponModeCacheLeaseCovers(familyObserved,familyDeadline-1,familyDeadline));
    CHECK(!bc2::WeaponModeCacheLeaseCovers(familyObserved,familyDeadline,familyDeadline));
    CHECK(!bc2::WeaponModeCacheLeaseCovers(familyObserved,familyDeadline,familyDeadline+1));
    CHECK(!bc2::WeaponModeCacheLeaseCovers(0,familyObserved,familyDeadline));
    CHECK(!bc2::WeaponModeCacheLeaseCovers(familyObserved,familyObserved-1,familyDeadline));
    CHECK(!bc2::WeaponModeCacheLeaseCovers(familyObserved,familyObserved,0));
    CHECK(!bc2::WeaponModeCacheLeaseCovers(familyObserved,familyObserved,INT64_MAX));
    CHECK(bc2::WeaponModeCacheLeaseCovers(INT64_MAX-500000000,INT64_MAX-1,INT64_MAX));
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
    // Replay the observed slot-zero/launcher-two map for every enrolled family,
    // then exercise its slot-one path. Identity remains exact in both directions.
    for(const auto& profile:bc2::WeaponModeFamilies)for(unsigned primarySlot:{0u,1u}){Fixture g;g.CampaignMap(primarySlot);
        g.Text(0x16000,profile.primaryAsset.data());g.Text(0x16100,profile.secondaryAsset.data());
        g.Text(0x16200,profile.primaryConfiguration.data());g.Text(0x16300,profile.secondaryConfiguration.data());g.Text(0x16400,profile.persistentId.data());
        const auto original=g.bytes;Fixture without=g;
        bc2::WeaponModeCandidateObservation diagnosis;
        const auto valid=bc2::ObserveWeaponModeFamily(g.Memory(),g.soldier,g.rifle,profile,&diagnosis);
        const unsigned primaryAction=primarySlot==0?25:36;
        CHECK(valid&&valid->family==&profile&&valid->action==33&&valid->primaryAction==primaryAction&&valid->targetSlot==2);
        CHECK(std::string_view(diagnosis.status)=="resolved"&&diagnosis.routes[1].action==primaryAction&&diagnosis.routes[1].firstOccupiedSlot==primarySlot);
        CHECK(bc2::ObserveWeaponModeFamily(without.Memory(),without.soldier,without.rifle,profile));
        CHECK(g.bytes==original&&g.reads==without.reads&&g.typeReads==without.typeReads);
        CHECK(bc2::ResolveWeaponMode(g.Memory(),g.soldier,g.rifle));
        g.Word(g.inventory+0x14c,2);const auto reverse=bc2::ResolveWeaponMode(g.Memory(),g.soldier,g.launcher);
        CHECK(reverse&&reverse->family==&profile&&reverse->selectedSecondary&&reverse->action==primaryAction&&reverse->primaryAction==primaryAction&&reverse->targetSlot==primarySlot&&reverse->targetWeapon==g.rifle);
    }
    // No route fallback past foreign targets, duplicate rows, two exact inverse
    // actions, malformed targets, or a lost owner/selection bookend.
    {Fixture g;g.CampaignMap();g.Route(14,2,25,{4,0});bc2::WeaponModeCandidateObservation d;
        CHECK(!bc2::ObserveWeaponModeFamily(g.Memory(),g.soldier,g.rifle,bc2::WeaponModeFamilies[0],&d));
        CHECK(std::string_view(d.status)=="primary_route"&&std::string_view(d.routes[1].status)=="first_target_mismatch"&&d.routes[1].action==25&&d.routes[1].firstOccupiedSlot==4);}
    {Fixture g;g.CampaignMap();g.Route(15,2,36,{0});bc2::WeaponModeCandidateObservation d;
        CHECK(!bc2::ObserveWeaponModeFamily(g.Memory(),g.soldier,g.rifle,bc2::WeaponModeFamilies[0],&d));
        CHECK(std::string_view(d.routes[1].status)=="ambiguous_primary_routes");}
    for(unsigned action:{25u,36u}){Fixture g;g.CampaignMap();g.Route(44,2,action,{0});g.Word(g.switching+0x1c,g.map+45*24);
        CHECK(!bc2::ResolveWeaponMode(g.Memory(),g.soldier,g.rifle));}
    for(unsigned action:{7u,33u,35u}){Fixture g;g.CampaignMap();g.Route(14,2,action,{0});
        CHECK(!bc2::ResolveWeaponMode(g.Memory(),g.soldier,g.rifle));}
    {Fixture g;g.CampaignMap();g.Route(14,2,25,{9,0});CHECK(!bc2::ResolveWeaponMode(g.Memory(),g.soldier,g.rifle));}
    {Fixture g;g.CampaignMap();g.Route(14,2,25,{1,0});CHECK(bc2::ResolveWeaponMode(g.Memory(),g.soldier,g.rifle));}
    {Fixture g;g.CampaignMap();g.changeCurrent=true;CHECK(!bc2::ResolveWeaponMode(g.Memory(),g.soldier,g.rifle));}
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
