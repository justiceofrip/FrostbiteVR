#include "Bc2WeaponMode.h"
#include "Bc2Profile.h"
#include "fvr/engine/BindingValidation.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <string>
#include <vector>
#include <limits>
namespace fvr::bc2 {
namespace {
struct Reader {
    const WeaponModeMemory& m;
    bool Read(std::uint64_t at,void* out,std::size_t size)const{
        return m.read&&at>=0x10000&&size&&size<=65536&&at+size<=UINT32_MAX&&m.read(m.context,std::uint32_t(at),out,size);
    }
    bool U32(std::uint64_t at,std::uint32_t& value)const{return Read(at,&value,4);}
    bool Type(std::uint32_t at,const char* name)const{return m.type&&at>=0x10000&&m.type(m.context,at,name);}
    std::optional<std::string> Name(std::uint32_t at)const{
        std::string out;
        for(unsigned i=0;i<128;++i){unsigned char c=0;if(!Read(std::uint64_t(at)+i,&c,1))return {};
            if(!c)return out.empty()?std::nullopt:std::optional(out);
            if(c<32||c>126)return {};out.push_back(char(c));}
        return {};
    }
    std::optional<std::string> StringAt(std::uint32_t at,unsigned offset)const{
        std::uint32_t pointer=0;if(!U32(std::uint64_t(at)+offset,pointer))return {};return Name(pointer);
    }
};
struct Item {std::uint32_t weapon=0,data=0,slot=0,persistent=0;};
bool FirstTarget(const Reader& r,std::uint32_t begin,std::uint32_t end,
    std::uint32_t from,std::uint32_t action,std::span<const std::uint32_t> items,std::uint32_t wanted,
    WeaponModeRouteObservation* observation){
    if(observation){*observation={};observation->from=from;observation->action=action;observation->wanted=wanted;}
    const auto fail=[&](const char* status){if(observation)observation->status=status;return false;};
    const auto row=[&](std::uint64_t at,unsigned state,unsigned input){if(observation){
        observation->row=std::uint32_t(at);observation->state=state;observation->input=input;
        observation->first=observation->last=observation->targetsRead=observation->firstOccupiedWeapon=0;
        observation->firstOccupiedSlot=UINT32_MAX;observation->targets={};}};
    unsigned matching=0;bool correct=false;
    for(std::uint64_t at=begin;at<end;at+=24){
        std::uint32_t state=0,input=0;
        if(!r.U32(at+20,state)||!r.U32(at+16,input)){row(at,state,input);return fail("route_tags_read_failed");}
        if(state>10||input>=49){row(at,state,input);return fail("invalid_route_tags");}
        if(state!=from||input!=action)continue;
        row(at,state,input);++matching;if(observation)observation->matching=matching;
        if(matching>1)return fail("duplicate_route");
        std::uint32_t first=0,last=0;
        const bool boundsRead=r.U32(at+8,first)&&r.U32(at+12,last);
        if(observation){observation->first=first;observation->last=last;}
        if(!boundsRead)return fail("target_bounds_read_failed");
        if(last<=first||last-first>44||(last-first)%4)return fail("invalid_target_bounds");
        bool resolved=false;
        for(std::uint64_t target=first;target<last;target+=4){
            std::uint32_t slot=0;if(!r.U32(target,slot))return fail("target_read_failed");
            if(observation)observation->targets[observation->targetsRead++]=slot;
            if(slot>=9||slot>=items.size())return fail("target_out_of_range");
            if(!resolved&&items[slot]){correct=slot==wanted;resolved=true;
                if(observation){observation->firstOccupiedSlot=slot;observation->firstOccupiedWeapon=items[slot];}}
        }
        if(!resolved)return fail("no_occupied_target");
    }
    if(observation)observation->status=matching!=1?"no_matching_route":correct?"resolved":"first_target_mismatch";
    return matching==1&&correct;
}
std::uint32_t U32(std::span<const std::byte> b,std::size_t at){
    std::uint32_t value=0;std::memcpy(&value,b.data()+at,4);return value;
}
std::optional<std::size_t> Offset(std::span<const std::byte> b,const engine::PeImage& pe,std::uint32_t rva,std::size_t size){
    for(const auto& s:pe.sections)if(rva>=s.rva){
        const auto delta=std::uint64_t(rva)-s.rva,at=std::uint64_t(s.rawOffset)+delta;
        if(delta<=s.rawSize&&size<=s.rawSize-delta&&at<=b.size()&&size<=b.size()-at)return std::size_t(at);
    }return {};
}
std::optional<std::uint32_t> Find(std::span<const std::byte> b,const engine::PeImage& pe,const char* text){
    const auto pattern=engine::ParsePattern(text);if(!pattern)return {};std::optional<std::uint32_t> result;
    for(const auto& s:pe.sections)if(s.flags&0x20000000){
        if(s.rawOffset>b.size()||s.rawSize>b.size()-s.rawOffset||pattern->size()>s.rawSize)continue;
        for(std::size_t i=0;i<=s.rawSize-pattern->size();++i){
            bool match=true;for(std::size_t j=0;j<pattern->size();++j)if(!(*pattern)[j].wildcard&&std::to_integer<unsigned char>(b[s.rawOffset+i+j])!=(*pattern)[j].value){match=false;break;}
            if(match){if(result)return {};result=s.rva+std::uint32_t(i);}
        }
    }return result;
}
bool Bytes(std::span<const std::byte> b,std::size_t at,std::initializer_list<unsigned char> expected){
    if(at>b.size()||expected.size()>b.size()-at)return false;
    for(auto value:expected)if(std::to_integer<unsigned char>(b[at++])!=value)return false;
    return true;
}
}
std::optional<WeaponModeCommand> ObserveWeaponModeFamily(const WeaponModeMemory& memory,std::uint32_t soldier,std::uint32_t currentWeapon,const WeaponModeFamilyProfile& profile,
    WeaponModeCandidateObservation* observation){
    if(observation){*observation={};observation->family=&profile;}
    const auto fail=[&](const char* status)->std::optional<WeaponModeCommand>{if(observation)observation->status=status;return {};};
    if(profile.primaryAsset.empty()||profile.secondaryAsset.empty()||profile.primaryAsset==profile.secondaryAsset||
       profile.primaryConfiguration.empty()||profile.secondaryConfiguration.empty()||profile.primaryConfiguration==profile.secondaryConfiguration||
       profile.secondaryAction!=33||profile.primaryAction!=36)return fail("invalid_profile");
    Reader r{memory};if(!r.Type(soldier,"ClientSoldierEntity")||!currentWeapon)return fail("owner_type_or_weapon");
    unsigned char flags=0;std::uint32_t inventory=0,begin=0,end=0,current=0,data=0;
    if(!r.Read(std::uint64_t(soldier)+0x114,&flags,1))return fail("owner_flags_read");
    if(observation)observation->flags=flags;
    const auto header=[&](const char* status)->std::optional<WeaponModeCommand>{if(observation){
        observation->inventory=inventory;observation->begin=begin;observation->end=end;
        observation->selected=current;observation->switching=data;}return fail(status);};
    if(!r.U32(std::uint64_t(soldier)+((flags&1)?0x24c:0x248),inventory))return header("inventory_read");
    if(!r.U32(std::uint64_t(soldier)+0x260,begin)||!r.U32(std::uint64_t(soldier)+0x264,end))return header("item_bounds_read");
    if(!r.U32(std::uint64_t(inventory)+0x14c,current)||!r.U32(std::uint64_t(inventory)+4,data))return header("selection_or_switching_read");
    if(!r.Type(data,"WeaponSwitchingData"))return header("switching_type");
    if(end<=begin||end-begin>256||(end-begin)%4||current>=(end-begin)/4)return header("invalid_item_bounds_or_selection");
    header("items_read");
    std::vector<std::uint32_t> items((end-begin)/4);
    if(!r.Read(begin,items.data(),items.size()*4))return fail("items_read");
    if(observation){observation->itemCount=unsigned(items.size());std::copy(items.begin(),items.end(),observation->items.begin());}
    if(items[current]!=currentWeapon)return fail("selected_weapon_mismatch");
    std::optional<Item> rifle,launcher;
    for(unsigned slot=0;slot<items.size();++slot)if(items[slot]){
        if(observation)observation->lastItem={items[slot],0,slot,0};
        std::uint32_t weaponData=0;const bool typed=r.U32(std::uint64_t(items[slot])+4,weaponData)&&r.Type(weaponData,"SoldierWeaponData");
        if(observation)observation->lastItem.data=weaponData;
        if(!typed)continue;
        if(observation)observation->typedItems|=std::uint64_t(1)<<slot;
        const auto name=r.StringAt(weaponData,0xc);if(!name)continue;
        if(observation){observation->lastItem.asset=*name;observation->namedItems|=std::uint64_t(1)<<slot;}
        const bool isRifle=*name==profile.primaryAsset,isLauncher=*name==profile.secondaryAsset;
        if(!isRifle&&!isLauncher)continue;
        auto& item=isRifle?rifle:launcher;if(item)return fail("duplicate_asset_item");
        std::uint32_t persistent=0;const bool persistentTyped=r.U32(std::uint64_t(weaponData)+0x64,persistent)&&r.Type(persistent,"PersistentWeapon");
        if(observation)observation->lastItem.persistent=persistent;
        if(!persistentTyped)return fail("persistence_read_or_type");
        const auto path=r.StringAt(weaponData,0x40);
        if(observation&&path)observation->lastItem.configuration=*path;
        if(!path)return fail("item_configuration_read");
        if(*path!=(isRifle?profile.primaryConfiguration:profile.secondaryConfiguration))return fail("item_configuration_mismatch");
        item=Item{items[slot],weaponData,slot,persistent};
        if(observation)(isRifle?observation->primary:observation->secondary)=observation->lastItem;
    }
    if(!rifle||!launcher)return fail("linked_pair_missing");
    if(rifle->persistent!=launcher->persistent)return fail("linked_persistence_mismatch");
    if(currentWeapon!=rifle->weapon&&currentWeapon!=launcher->weapon)return fail("current_not_in_pair");
    const auto persistentId=r.StringAt(rifle->persistent,0x40);
    if(observation&&persistentId)observation->persistentId=*persistentId;
    if(!persistentId)return fail("persistence_id_read");
    if(!profile.persistentId.empty()&&*persistentId!=profile.persistentId)return fail("persistence_id_mismatch");
    std::uint32_t mapBegin=0,mapEnd=0;
    const bool mapRead=r.U32(std::uint64_t(data)+0x18,mapBegin)&&r.U32(std::uint64_t(data)+0x1c,mapEnd);
    if(observation){observation->mapBegin=mapBegin;observation->mapEnd=mapEnd;}
    if(!mapRead)return fail("map_bounds_read");
    if(mapEnd<=mapBegin||mapEnd-mapBegin>128*24||(mapEnd-mapBegin)%24)return fail("invalid_map_bounds");
    // Require both directions to resolve to this same physical item family.
    // Reject an earlier occupied foreign target rather than guessing eligibility.
    if(!FirstTarget(r,mapBegin,mapEnd,rifle->slot,profile.secondaryAction,items,launcher->slot,observation?&observation->routes[0]:nullptr))return fail("secondary_route");
    if(!FirstTarget(r,mapBegin,mapEnd,launcher->slot,profile.primaryAction,items,rifle->slot,observation?&observation->routes[1]:nullptr))return fail("primary_route");
    std::uint32_t again=0;
    const auto unchanged=[&](std::uint64_t at,unsigned expected){const bool read=r.U32(at,again);
        if(observation){observation->bookendAddress=at;observation->bookendExpected=expected;observation->bookendObserved=again;observation->bookendRead=read;}
        return read&&again==expected;};
    if(!unchanged(std::uint64_t(inventory)+0x14c,current)||
       !unchanged(std::uint64_t(soldier)+((flags&1)?0x24c:0x248),inventory)||
       !unchanged(std::uint64_t(soldier)+0x260,begin)||!unchanged(std::uint64_t(soldier)+0x264,end))return fail("owner_or_selection_changed");
    std::vector<std::uint32_t> after(items.size());
    if(!r.Read(begin,after.data(),after.size()*4)||after!=items)return fail("items_changed");
    const bool toLauncher=currentWeapon==rifle->weapon;const auto& target=toLauncher?*launcher:*rifle;
    if(observation)observation->status="resolved";
    return WeaponModeCommand{toLauncher?profile.secondaryAction:profile.primaryAction,target.weapon,target.slot,inventory,rifle->persistent,&profile,!toLauncher};
}
std::optional<WeaponModeCommand> ResolveWeaponMode(const WeaponModeMemory& memory,std::uint32_t soldier,std::uint32_t currentWeapon,WeaponModeResolution* observation){
    if(observation){*observation={};observation->soldier=soldier;observation->weapon=currentWeapon;observation->status="selected_data_read_or_type";}
    // The roster has many variants sharing an asset name. Read the exact
    // selected configuration once to bound full inventory scans to its pairs;
    // ObserveWeaponModeFamily still independently checks all ownership/routes.
    Reader r{memory};std::uint32_t data=0;
    const bool typed=r.U32(std::uint64_t(currentWeapon)+4,data)&&r.Type(data,"SoldierWeaponData");
    if(observation)observation->data=data;
    if(!typed)return {};
    const auto asset=r.StringAt(data,0xc),configuration=r.StringAt(data,0x40);
    if(observation){observation->status="selected_identity_read";if(asset)observation->asset=*asset;if(configuration)observation->configuration=*configuration;}
    if(!asset||!configuration)return {};
    if(observation)observation->status="no_enrolled_configuration";
    std::optional<WeaponModeCommand> found;
    for(const auto& p:WeaponModeFamilies)if(WeaponModeFamilyEligible(p)&&!p.persistentId.empty()){
        if(!((*asset==p.primaryAsset&&*configuration==p.primaryConfiguration)||
             (*asset==p.secondaryAsset&&*configuration==p.secondaryConfiguration)))continue;
        WeaponModeCandidateObservation* attempt=nullptr;
        if(observation){observation->status="no_valid_linked_pair";
            if(observation->candidateCount<observation->candidates.size())attempt=&observation->candidates[observation->candidateCount++];
            else ++observation->candidatesDropped;}
        const auto candidate=ObserveWeaponModeFamily(memory,soldier,currentWeapon,p,attempt);
        if(candidate){if(found){if(observation)observation->status="ambiguous_families";return {};}found=candidate;}
    }
    if(found&&observation)observation->status="resolved";
    return found;
}
void WriteWeaponModeResolution(std::ostream& out,const WeaponModeResolution& observation){
    const auto text=[&](std::string_view value){out<<'"';for(const unsigned char c:value){
        if(c=='"'||c=='\\')out<<'\\'<<char(c);else if(c<32||c>=127){constexpr char hex[]="0123456789abcdef";out<<"\\u00"<<hex[c>>4]<<hex[c&15];}else out<<char(c);
    }out<<'"';};
    const auto item=[&](const WeaponModeItemObservation& value){out<<"{\"slot\":"<<value.slot<<",\"weapon\":"<<value.weapon
        <<",\"data\":"<<value.data<<",\"persistence\":"<<value.persistent<<",\"asset\":";text(value.asset);
        out<<",\"configuration\":";text(value.configuration);out<<'}';};
    out<<"{\"status\":";text(observation.status);out<<",\"soldier\":"<<observation.soldier<<",\"weapon\":"<<observation.weapon
        <<",\"data\":"<<observation.data<<",\"asset\":";text(observation.asset);out<<",\"configuration\":";text(observation.configuration);
    out<<",\"candidatesDropped\":"<<observation.candidatesDropped<<",\"candidates\":[";
    for(unsigned i=0;i<std::min<unsigned>(observation.candidateCount,unsigned(observation.candidates.size()));++i){
        if(i)out<<',';const auto& c=observation.candidates[i];
        unsigned family=UINT32_MAX;for(unsigned j=0;j<WeaponModeFamilies.size();++j)if(c.family==&WeaponModeFamilies[j]){family=j;break;}
        out<<"{\"familyIndex\":"<<family<<",\"status\":";text(c.status);
        out<<",\"inventory\":"<<c.inventory<<",\"switching\":"<<c.switching<<",\"flags\":"<<c.flags
            <<",\"begin\":"<<c.begin<<",\"end\":"<<c.end<<",\"selectedSlot\":"<<c.selected<<",\"itemCount\":"<<c.itemCount;
        // Compression only: every candidate has its own original reads. An
        // equal vector in the diagnostic is not borrowed for runtime admission.
        const bool sameItems=i&&c.itemCount==observation.candidates[0].itemCount&&c.items==observation.candidates[0].items;
        out<<",\"itemsEqualCandidateZero\":"<<(sameItems?"true":"false")<<",\"items\":";
        if(sameItems)out<<"null";else{out<<'[';for(unsigned j=0;j<std::min<unsigned>(c.itemCount,unsigned(c.items.size()));++j){if(j)out<<',';out<<c.items[j];}out<<']';}
        out<<",\"typedItemsMask\":"<<c.typedItems<<",\"namedItemsMask\":"<<c.namedItems<<",\"primary\":";item(c.primary);
        out<<",\"secondary\":";item(c.secondary);out<<",\"lastItem\":";item(c.lastItem);
        out<<",\"persistenceId\":";text(c.persistentId);out<<",\"mapBegin\":"<<c.mapBegin<<",\"mapEnd\":"<<c.mapEnd<<",\"routes\":[";
        for(unsigned j=0;j<c.routes.size();++j){if(j)out<<',';const auto& route=c.routes[j];out<<"{\"status\":";text(route.status);
            out<<",\"from\":"<<route.from<<",\"action\":"<<route.action<<",\"wanted\":"<<route.wanted<<",\"row\":"<<route.row
                <<",\"state\":"<<route.state<<",\"input\":"<<route.input<<",\"first\":"<<route.first<<",\"last\":"<<route.last
                <<",\"matching\":"<<route.matching<<",\"firstOccupiedSlot\":"<<route.firstOccupiedSlot
                <<",\"firstOccupiedWeapon\":"<<route.firstOccupiedWeapon<<",\"targets\":[";
            for(unsigned k=0;k<std::min<unsigned>(route.targetsRead,unsigned(route.targets.size()));++k){if(k)out<<',';out<<route.targets[k];}out<<"]}";}
        out<<"],\"lastBookend\":{\"address\":"<<c.bookendAddress<<",\"read\":"<<(c.bookendRead?"true":"false")
            <<",\"expected\":"<<c.bookendExpected<<",\"observed\":"<<c.bookendObserved<<"}}";
    }
    out<<"]}";
}
std::optional<WeaponModeCandidates> DiscoverWeaponMode(std::span<const std::byte> b,const engine::PeImage& pe){
    const auto input=DiscoverInputBinding(b,pe);if(!input)return {};
    const auto nt=U32(b,0x3c),base=U32(b,std::size_t(nt)+24+28);
    const auto table=Offset(b,pe,input->entryActions,50*24);if(!table)return {};
    for(const auto pair:{std::pair{33u,"EiaGrenadeLauncher"},std::pair{36u,"EiaDynamicGadget2"}}){
        const auto at=*table+pair.first*24;const auto name=U32(b,at);const auto size=std::strlen(pair.second)+1;
        if(name<base||U32(b,at+16)!=pair.first)return {};
        const auto text=Offset(b,pe,name-base,size);if(!text||std::memcmp(b.data()+*text,pair.second,size))return {};
    }
    const auto selector=Find(b,pe,"83 EC 20 8B 81 4C 01 00 00 53 C1 E0 05 55 8D 5C 08 08 8B 43 04 56 8B 30 83 CD FF 85 F6 57 89 4C 24 14");
    const auto update=Find(b,pe,"83 EC 08 56 8B F1 D9 86 58 01 00 00 57 8B 7C 24 14 DD 5C 24 08 8B CF E8");
    const auto edge=Find(b,pe,"56 57 8B 7C 24 0C 8B F1 8B 8E A4 01 00 00 57 E8 ?? ?? ?? ?? 84 C0 75 1A 8B 8E A0 01 00 00 57 E8");
    if(!selector||!update||!edge)return {};
    const auto s=Offset(b,pe,*selector,0x1b1),up=Offset(b,pe,*update,0x86),e=Offset(b,pe,*edge,0x37);
    if(!s||!up||!e)return {};
    const auto target=[&](unsigned rva,std::size_t at,unsigned delta,unsigned char opcode=0xe8)->std::optional<unsigned>{
        if(!Bytes(b,at+delta,{opcode}))return {};
        const auto value=std::int64_t(rva)+delta+5+std::int32_t(U32(b,at+delta+1));
        if(value<0||value>UINT32_MAX||!Offset(b,pe,unsigned(value),8))return {};return unsigned(value);
    };
    const auto boolean=target(*edge,*e,0xf),booleanAgain=target(*edge,*e,0x1f),scalarThunk=target(*selector,*s,0x100);
    if(target(*update,*up,0x4d)!=selector||target(*selector,*s,0x4f)!=edge||!boolean||booleanAgain!=boolean||!scalarThunk)return {};
    const auto bo=Offset(b,pe,*boolean,0x88),th=Offset(b,pe,*scalarThunk,11);if(!bo||!th)return {};
    if(!Bytes(b,*th,{0x8b,0x89,0xa0,1,0,0}))return {};
    const auto scalar=target(*scalarThunk,*th,6,0xe9);if(!scalar)return {};
    const auto sc=Offset(b,pe,*scalar,0x7c);if(!sc)return {};
    if(!Bytes(b,*bo,{0x53,0x8b,0x5c,0x24,8,0x83,0xfb,0x31,0x56,0x8b,0xf1})||
       !Bytes(b,*bo+0x20,{0x81,0xe1,0xff,7,0,0,0x81,0xe2,0xff,0xff,1,0})||
       !Bytes(b,*bo+0x55,{0x8b,0x8e,0x98,0,0,0,0x8b,0xb6,0x9c,0,0,0})||
       !Bytes(b,*sc+0x3a,{0x8b,0x8e,0x98,0,0,0,0x23,0xc8,0x8b,0x86,0x9c,0,0,0,0x23,0xc2})||
       !Bytes(b,*s+0x10b,{0x76,0x1b})||!Bytes(b,*s+0x1ae,{0xc2,4,0})||
       !Bytes(b,*up+0x83,{0xc2,8,0})||!Bytes(b,*e+0x2f,{0xc2,4,0}))return {};
    return WeaponModeCandidates{*selector,*update,*edge,*boolean,*scalar};
}
}
