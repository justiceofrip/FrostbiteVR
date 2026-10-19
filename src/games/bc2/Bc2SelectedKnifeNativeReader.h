#pragma once
#include "Bc2SelectedKnifeRendering.h"
#include "KnifeNativeReader.h"

namespace fvr::bc2::selectedKnife {
// Selected action37 owns a different effects class from normal firearms. These
// relationships are independently measured in418/419; firearm +D4 is never used.
struct Native {
    ReloadStateSnapshot client{};
    ReloadServerLinks links{};
    ReloadFiringObservation server{};
    unsigned clientEffects=0;
};
// Sparse callback diagnostics only. The same reads and comparison decide the
// result with or without this output; rejected evidence is never retried here.
struct OwnerScopeDiagnostic {
    const char* stage="none";unsigned address=0,bytes=0,expected=0,observed=0;
    bool readFailed=false,typeFailed=false;
};
enum class OwnerPhase:unsigned {None,ScopeBefore,ScopeAfter,BracketLinks,BracketEffects,SourceEffects,SourceLinks};
struct OwnerDiagnostic {
    OwnerPhase phase=OwnerPhase::None;OwnerScopeDiagnostic scope{};
    bool beforeKnown=false,afterKnown=false;
    unsigned sourceEffects=0,beforeEffects=0,afterEffects=0;
    unsigned sourcePlayerFlags=0,beforePlayerFlags=0,afterPlayerFlags=0;
    unsigned sourceSoldierFlags=0,beforeSoldierFlags=0,afterSoldierFlags=0;
    unsigned sourceInventory=0,beforeInventory=0,afterInventory=0;
    // Extra read-only evidence ONLY after an owner rejection; these are later
    // samples, not a retry or an atomic join with the first failed predicate.
    unsigned inventory248=0,inventory24c=0,inventoryReadMask=0;
    ReloadServerLinkDifference bracketDifference{},sourceDifference{},normalizedDifference{};
};
namespace detail {
using Reader=private_knife::detail::Reader;
inline ReloadServerLinkDifference LinkDifference(const ReloadServerLinks& expected,const ReloadServerLinks& observed)noexcept {
    ReloadServerLinkDifference out;
    const auto field=[&](const char* name,unsigned before,unsigned after,unsigned index=UINT32_MAX){
        if(before==after)return;if(out.count<out.first.size())out.first[out.count]={name,index,before,after};++out.count;};
#define FVR_SELECTED_LINK_FIELD(name) field(#name,expected.name,observed.name)
    FVR_SELECTED_LINK_FIELD(manager);FVR_SELECTED_LINK_FIELD(players);FVR_SELECTED_LINK_FIELD(capacity);FVR_SELECTED_LINK_FIELD(playerId);
    FVR_SELECTED_LINK_FIELD(player);FVR_SELECTED_LINK_FIELD(playerTable);FVR_SELECTED_LINK_FIELD(soldier);FVR_SELECTED_LINK_FIELD(soldierTable);
    FVR_SELECTED_LINK_FIELD(soldierData);FVR_SELECTED_LINK_FIELD(inventory);FVR_SELECTED_LINK_FIELD(inventoryData);FVR_SELECTED_LINK_FIELD(begin);
    FVR_SELECTED_LINK_FIELD(end);FVR_SELECTED_LINK_FIELD(slot);FVR_SELECTED_LINK_FIELD(item);FVR_SELECTED_LINK_FIELD(effects);
    FVR_SELECTED_LINK_FIELD(effectsTable);FVR_SELECTED_LINK_FIELD(callback);FVR_SELECTED_LINK_FIELD(firing);FVR_SELECTED_LINK_FIELD(clientInventory);
    FVR_SELECTED_LINK_FIELD(clientBegin);FVR_SELECTED_LINK_FIELD(clientEnd);FVR_SELECTED_LINK_FIELD(clientSlot);
    FVR_SELECTED_LINK_FIELD(clientPlayerFlags);FVR_SELECTED_LINK_FIELD(clientSoldierFlags);
#undef FVR_SELECTED_LINK_FIELD
    for(unsigned n=0;n<expected.items.size();++n)field("items",expected.items[n],observed.items[n],n);
    for(unsigned n=0;n<expected.clientItems.size();++n)field("clientItems",expected.clientItems[n],observed.clientItems[n],n);
    return out;
}
struct ScopeReader {
    const Reader& raw;OwnerScopeDiagnostic* diagnostic;
    bool Value(const char* stage,bool okay,unsigned observed=0,unsigned expected=0,unsigned address=0)const noexcept {
        if(!okay&&diagnostic)*diagnostic={stage,address,0,expected,observed,false,false};return okay;}
    bool Read(const char* stage,std::uint64_t at,void* out,std::size_t size)const noexcept {
        const bool okay=raw.Read(at,out,size);
        if(!okay&&diagnostic)*diagnostic={stage,unsigned(at),unsigned(size),0,0,true,false};return okay;}
    template<class T>bool Get(const char* stage,std::uint64_t at,T& out)const noexcept{return Read(stage,at,&out,sizeof(out));}
    bool Eq(const char* stage,std::uint64_t at,unsigned expected)const noexcept {
        unsigned observed=0;if(!Get(stage,at,observed))return false;
        if(observed!=expected&&diagnostic)*diagnostic={stage,unsigned(at),4,expected,observed,false,false};return observed==expected;}
    bool Type(const char* stage,unsigned at,const char* name)const noexcept {
        const bool okay=raw.Type(at,name);
        if(!okay&&diagnostic)*diagnostic={stage,at,0,0,0,false,true};return okay;}
};
inline bool Scope(const Reader& r,const ReloadServerBinding& b,const ReloadStateBinding& state,
    unsigned base,const ReloadStateSnapshot& s,ReloadServerLinks& v,unsigned& clientEffects,bool selected=true,
    OwnerScopeDiagnostic* diagnostic=nullptr)noexcept {
    const ScopeReader d{r,diagnostic};
    const auto& o=s.owner;unsigned attached=0;
    if(!d.Value("profile",!(base!=0x400000||b.preferredBase!=base||state.preferredBase!=base||b.imageSize!=0x1913000||
       b.contextRva>b.imageSize-12||b.managerVtableRva>b.imageSize-4||b.controlledGetterRva>=b.imageSize||
       s.selectedSlot!=0||s.config.fireInputAction!=37||s.config.fireLogicType!=0||
       s.config.baseCapacity!=-1||s.config.numberOfMagazines!=-1||
       authored_grip_detail::Text(s.config.assetPath)!="Objects/Weapons/Handheld/Knife_Equipable/Knife_Equipable")))return false;
    if(!d.Get("client_attached_read",std::uint64_t(o.player)+0xc60,attached)||!d.Value("client_attached",!attached,attached,0)||
       !d.Get("client_player_flags_read",std::uint64_t(o.player)+0xccd,v.clientPlayerFlags)||!d.Value("client_local",v.clientPlayerFlags&8,v.clientPlayerFlags,8)||
       !d.Eq("client_weak_slot",std::uint64_t(o.player)+0xc54,o.weak)||!d.Eq("client_weak_target",o.weak,o.soldier+4)||
       !d.Eq("client_controlled",std::uint64_t(o.player)+0xc68,o.soldier)||!d.Eq("client_player_backlink",std::uint64_t(o.soldier)+0x220,o.player)||
       !d.Get("client_soldier_flags_read",std::uint64_t(o.soldier)+0x114,v.clientSoldierFlags)||
       !d.Get("client_inventory_read",std::uint64_t(o.soldier)+((v.clientSoldierFlags&1)?0x24c:0x248),v.clientInventory)||
       !d.Value("client_inventory",v.clientInventory==s.inventory,v.clientInventory,s.inventory,o.soldier+((v.clientSoldierFlags&1)?0x24c:0x248))||
       !d.Get("client_begin",std::uint64_t(o.soldier)+0x260,v.clientBegin)||!d.Get("client_end",std::uint64_t(o.soldier)+0x264,v.clientEnd)||
       !d.Value("client_span",!(v.clientBegin<0x10000||v.clientEnd<=v.clientBegin||v.clientEnd-v.clientBegin>256||(v.clientEnd-v.clientBegin)%4),v.clientEnd,v.clientBegin)||
       !d.Read("client_items",v.clientBegin,v.clientItems.data(),v.clientEnd-v.clientBegin)||
       !d.Get("client_slot_read",std::uint64_t(v.clientInventory)+0x14c,v.clientSlot)||
       !d.Value("client_slot_bounds",v.clientSlot<(v.clientEnd-v.clientBegin)/4,v.clientSlot,(v.clientEnd-v.clientBegin)/4)||
       !d.Value("client_selected",!selected||v.clientSlot==s.selectedSlot,v.clientSlot,s.selectedSlot)||
       !d.Value("client_item",v.clientItems[0]==o.weapon,v.clientItems[0],o.weapon)||
       !d.Value("client_item_unique",std::count(v.clientItems.begin(),v.clientItems.end(),o.weapon)==1,unsigned(std::count(v.clientItems.begin(),v.clientItems.end(),o.weapon)),1)||
       !d.Eq("client_weapon_data",std::uint64_t(o.weapon)+4,s.config.weaponData)||!d.Eq("weapon_firing_data",std::uint64_t(s.config.weaponData)+0x98,s.config.firingData)||
       !d.Eq("primary_fire",std::uint64_t(s.config.firingData)+0x40,s.config.primaryFire)||
       !d.Eq("fire_logic",std::uint64_t(s.config.primaryFire)+0x24,0)||!d.Eq("fire_action",std::uint64_t(s.config.primaryFire)+0x4c,37)||
       !d.Get("client_effects_read",std::uint64_t(o.weapon)+0x38,clientEffects)||!d.Type("client_effects_type",clientEffects,"ClientMeleeFiringEffects")||
       !d.Eq("client_effects_data",std::uint64_t(clientEffects)+0x10,s.config.firingData)||
       !d.Get("server_manager_read",std::uint64_t(base)+b.contextRva+8,v.manager)||!d.Eq("server_manager_table",v.manager,base+b.managerVtableRva)||
       !d.Get("player_id",std::uint64_t(o.player)+0x154,v.playerId)||!d.Get("player_capacity_read",std::uint64_t(v.manager)+4,v.capacity)||
       !d.Value("player_capacity",v.capacity&&v.capacity<=256,v.capacity,256)||!d.Value("player_index",v.playerId<v.capacity,v.playerId,v.capacity)||
       !d.Get("players_read",std::uint64_t(v.manager)+0x6c,v.players)||
       !d.Value("players_bounds",v.players>=0x10000&&std::uint64_t(v.players)+v.capacity*4<=UINT32_MAX,v.players,0x10000)||
       !d.Get("server_player_read",std::uint64_t(v.players)+v.playerId*4,v.player)||!d.Eq("server_player_id",std::uint64_t(v.player)+0x154,v.playerId)||
       !d.Get("server_player_table_read",v.player,v.playerTable)||
       !d.Value("server_player_table_bounds",v.playerTable>=base&&std::uint64_t(v.playerTable)+0x34<=std::uint64_t(base)+b.imageSize,v.playerTable,base)||
       !d.Eq("server_controlled_getter",std::uint64_t(v.playerTable)+0x30,base+b.controlledGetterRva)||
       !d.Get("server_soldier_read",std::uint64_t(v.player)+0xc3c,v.soldier)||!d.Eq("server_soldier_link",std::uint64_t(v.player)+0xc6c,v.soldier)||
       !d.Get("server_soldier_table_read",v.soldier,v.soldierTable)||!d.Value("server_soldier_table",v.soldierTable==0x1424850,v.soldierTable,0x1424850)||
       !d.Type("server_soldier_type",v.soldier,"ServerSoldierEntity")||
       !d.Get("server_soldier_data_read",std::uint64_t(v.soldier)+0xc,v.soldierData)||!d.Eq("shared_soldier_data",std::uint64_t(o.soldier)+0xc,v.soldierData)||
       !d.Eq("server_player_backlink",std::uint64_t(v.soldier)+0x220,v.player)||!d.Get("server_inventory_read",std::uint64_t(v.soldier)+0x2b4,v.inventory)||
       !d.Get("server_inventory_data_read",std::uint64_t(v.inventory)+4,v.inventoryData)||!d.Type("server_inventory_type",v.inventoryData,"WeaponSwitchingData")||
       !d.Get("server_begin",std::uint64_t(v.soldier)+0x2b8,v.begin)||!d.Get("server_end",std::uint64_t(v.soldier)+0x2bc,v.end)||
       !d.Value("server_span",!(v.begin<0x10000||v.end<=v.begin||v.end-v.begin>256||(v.end-v.begin)%4),v.end,v.begin)||
       !d.Read("server_items",v.begin,v.items.data(),v.end-v.begin)||!d.Get("server_slot_read",std::uint64_t(v.inventory)+0x14c,v.slot)||
       !d.Value("server_slot_bounds",v.slot<(v.end-v.begin)/4,v.slot,(v.end-v.begin)/4)||!d.Value("server_selected",!selected||v.slot==0,v.slot,0))return false;
    v.item=v.items[0];
    if(!d.Value("server_item_bounds",v.item>=0x10000,v.item,0x10000)||
       !d.Value("server_item_unique",std::count(v.items.begin(),v.items.end(),v.item)==1,unsigned(std::count(v.items.begin(),v.items.end(),v.item)),1)||
       !d.Eq("server_weapon_data",std::uint64_t(v.item)+4,s.config.weaponData)||
       !d.Get("server_effects_read",std::uint64_t(v.item)+0xc,v.effects)||!d.Get("server_effects_table_read",v.effects,v.effectsTable)||
       !d.Value("server_effects_table",v.effectsTable==0x1417cd0,v.effectsTable,0x1417cd0)||
       !d.Type("server_effects_type",v.effects,"ServerMeleeFiringEffects")||!d.Eq("server_effects_data",std::uint64_t(v.effects)+0x10,s.config.firingData)||
       !d.Eq("server_effects_secondary",std::uint64_t(v.effects)+0xd0,0x1417c70)||!d.Eq("server_effects_player",std::uint64_t(v.effects)+0x128,v.player)||
       !d.Get("server_firing_read",std::uint64_t(v.item)+0x10,v.firing)||
       !d.Value("server_firing_distinct",v.firing!=s.branches[0].address&&v.firing!=s.branches[1].address,v.firing,0)||
       !d.Eq("server_firing_table",v.firing,base+state.firingVtableRva)||!d.Eq("server_firing_data",std::uint64_t(v.firing)+8,s.config.firingData)||
       !d.Eq("server_firing_ammo",std::uint64_t(v.firing)+12,s.config.ammoAddress))return false;
    for(unsigned n=0;n<2;++n)if(!d.Eq("client_firing_slot",std::uint64_t(o.weapon)+s.branches[n].wrapperOffset,s.branches[n].address)||
        !d.Eq("client_firing_table",s.branches[n].address,base+state.firingVtableRva)||!d.Eq("client_firing_data",std::uint64_t(s.branches[n].address)+8,s.config.firingData)||
        !d.Eq("client_firing_ammo",std::uint64_t(s.branches[n].address)+12,s.config.ammoAddress))return false;
    return true;
}
}
inline std::optional<Native> ReadNative(const ReloadStateMemory& memory,const ReloadServerBinding& server,
    const ReloadStateBinding& state,unsigned base,const ReloadStateSnapshot& client)noexcept {
    detail::Reader r{memory};Native out;out.client=client;ReloadServerLinks after{};unsigned effectsAfter=0;
    if(!detail::Scope(r,server,state,base,client,out.links,out.clientEffects)||
       !private_knife::detail::Branch(r,state,base,out.links.item,client.config,out.server)||
       !detail::Scope(r,server,state,base,client,after,effectsAfter)||after!=out.links||effectsAfter!=out.clientEffects)return {};
    return out;
}
namespace detail {
// Native 0x890c90 assigns0x08 and client prediction assigns0x10 independently
// of inventory ownership. Bit0 still selects the exact client inventory. These
// same context bits may change within a callback's bracket (motion42702:3->19).
inline constexpr std::uint8_t OwnerContextFlags=0x18;
inline bool OwnerCheck(const ReloadStateMemory& memory,const ReloadServerBinding& server,
    const ReloadStateBinding& state,unsigned base,const Native& source,bool selected,OwnerDiagnostic* diagnostic)noexcept {
    if(diagnostic)*diagnostic={};
    Reader r{memory};ReloadServerLinks a{},b{};unsigned ca=0,cb=0;
    bool first=false,second=false;
    const auto fail=[&](OwnerPhase phase){
        if(diagnostic){auto& d=*diagnostic;d.phase=phase;d.beforeKnown=first;d.afterKnown=second;
            d.sourceEffects=source.clientEffects;d.beforeEffects=ca;d.afterEffects=cb;
            d.sourcePlayerFlags=source.links.clientPlayerFlags;d.beforePlayerFlags=a.clientPlayerFlags;d.afterPlayerFlags=b.clientPlayerFlags;
            d.sourceSoldierFlags=source.links.clientSoldierFlags;d.beforeSoldierFlags=a.clientSoldierFlags;d.afterSoldierFlags=b.clientSoldierFlags;
            d.sourceInventory=source.links.clientInventory;d.beforeInventory=a.clientInventory;d.afterInventory=b.clientInventory;
            if(first)d.sourceDifference=LinkDifference(source.links,a);
            if(first&&second)d.bracketDifference=LinkDifference(a,b);
            if(r.Get(std::uint64_t(source.client.owner.soldier)+0x248,d.inventory248))d.inventoryReadMask|=1;
            if(r.Get(std::uint64_t(source.client.owner.soldier)+0x24c,d.inventory24c))d.inventoryReadMask|=2;
        }return false;};
    if(!Scope(r,server,state,base,source.client,a,ca,selected,diagnostic?&diagnostic->scope:nullptr))return fail(OwnerPhase::ScopeBefore);
    first=true;
    if(!Scope(r,server,state,base,source.client,b,cb,selected,diagnostic?&diagnostic->scope:nullptr))return fail(OwnerPhase::ScopeAfter);
    second=true;
    auto bracket=b;
    bracket.clientSoldierFlags^=std::uint8_t((bracket.clientSoldierFlags^a.clientSoldierFlags)&OwnerContextFlags);
    if(a!=bracket)return fail(OwnerPhase::BracketLinks);
    if(ca!=cb)return fail(OwnerPhase::BracketEffects);
    if(ca!=source.clientEffects)return fail(OwnerPhase::SourceEffects);
    // Apply the same context-only comparison to the retained source. Raw
    // diagnostics preserve observed flags; no pointer or selector is normalized.
    auto normalized=a;
    normalized.clientSoldierFlags^=std::uint8_t((normalized.clientSoldierFlags^source.links.clientSoldierFlags)&OwnerContextFlags);
    if(!selected){normalized.clientSlot=source.links.clientSlot;normalized.slot=source.links.slot;}
    if(normalized!=source.links){
        if(diagnostic)diagnostic->normalizedDifference=LinkDifference(source.links,normalized);
        return fail(OwnerPhase::SourceLinks);
    }
    return true;
}
}
// Callback-time ownership only: original input freshness is checked separately
// on acquisition. Accepted native work outlives that lease through attack return.
inline bool OwnerCurrent(const ReloadStateMemory& memory,const ReloadServerBinding& server,
    const ReloadStateBinding& state,unsigned base,const Native& source,OwnerDiagnostic* diagnostic=nullptr)noexcept {
    return detail::OwnerCheck(memory,server,state,base,source,true,diagnostic);
}
// Accepted callback observation only. Selection can move while the original
// native attack is completing; exact item membership/effects ownership remains
// mandatory. This never admits an input pulse or renews a blade/input lease.
inline bool AcceptedOwnerRetained(const ReloadStateMemory& memory,const ReloadServerBinding& server,
    const ReloadStateBinding& state,unsigned base,const Native& source,OwnerDiagnostic* diagnostic=nullptr)noexcept {
    return detail::OwnerCheck(memory,server,state,base,source,false,diagnostic);
}
inline bool Ready(const Native& native)noexcept {
    for(const auto* b:{&native.client.branches[0],&native.client.branches[1],&native.server})
        if(b->currentState!=2||b->nextState!=2||b->loaded!=-1||b->reserve!=-1||
           !std::isfinite(b->phaseTimer)||b->phaseTimer<0||b->phaseTimer>1000000||(b->flagsA8&8))return false;
    return true;
}
struct Target {unsigned handle=0,soldier=0;bool operator==(const Target&)const=default;};
inline std::optional<Target> ReadTarget(const ReloadStateMemory& memory,unsigned effects)noexcept {
    detail::Reader r{memory};Target out;unsigned handleAfter=0,pointee=0,again=0;
    if(!r.Get(std::uint64_t(effects)+0x130,out.handle))return {};
    if(!out.handle)return out;
    if(!r.Get(out.handle,pointee))return {};
    if(pointee){if(pointee<0x10004)return {};out.soldier=pointee-4;
        if(!r.Eq(out.soldier,0x1424850)||!r.Type(out.soldier,"ServerSoldierEntity"))return {};}
    if(!r.Get(out.handle,again)||again!=pointee||!r.Get(std::uint64_t(effects)+0x130,handleAfter)||handleAfter!=out.handle)return {};
    return out;
}
}
