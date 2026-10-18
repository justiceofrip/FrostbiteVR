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
namespace detail {
using Reader=private_knife::detail::Reader;
inline bool Scope(const Reader& r,const ReloadServerBinding& b,const ReloadStateBinding& state,
    unsigned base,const ReloadStateSnapshot& s,ReloadServerLinks& v,unsigned& clientEffects,bool selected=true)noexcept {
    const auto& o=s.owner;unsigned attached=0;
    if(base!=0x400000||b.preferredBase!=base||state.preferredBase!=base||b.imageSize!=0x1913000||
       b.contextRva>b.imageSize-12||b.managerVtableRva>b.imageSize-4||b.controlledGetterRva>=b.imageSize||
       s.selectedSlot!=0||s.config.fireInputAction!=37||s.config.fireLogicType!=0||
       s.config.baseCapacity!=-1||s.config.numberOfMagazines!=-1||
       authored_grip_detail::Text(s.config.assetPath)!="Objects/Weapons/Handheld/Knife_Equipable/Knife_Equipable"||
       !r.Get(std::uint64_t(o.player)+0xc60,attached)||attached||
       !r.Get(std::uint64_t(o.player)+0xccd,v.clientPlayerFlags)||!(v.clientPlayerFlags&8)||
       !r.Eq(std::uint64_t(o.player)+0xc54,o.weak)||!r.Eq(o.weak,o.soldier+4)||
       !r.Eq(std::uint64_t(o.player)+0xc68,o.soldier)||!r.Eq(std::uint64_t(o.soldier)+0x220,o.player)||
       !r.Get(std::uint64_t(o.soldier)+0x114,v.clientSoldierFlags)||
       !r.Get(std::uint64_t(o.soldier)+((v.clientSoldierFlags&1)?0x24c:0x248),v.clientInventory)||v.clientInventory!=s.inventory||
       !r.Get(std::uint64_t(o.soldier)+0x260,v.clientBegin)||!r.Get(std::uint64_t(o.soldier)+0x264,v.clientEnd)||
       v.clientBegin<0x10000||v.clientEnd<=v.clientBegin||v.clientEnd-v.clientBegin>256||(v.clientEnd-v.clientBegin)%4||
       !r.Read(v.clientBegin,v.clientItems.data(),v.clientEnd-v.clientBegin)||
       !r.Get(std::uint64_t(v.clientInventory)+0x14c,v.clientSlot)||v.clientSlot>=(v.clientEnd-v.clientBegin)/4||
       (selected&&v.clientSlot!=s.selectedSlot)||
       v.clientItems[0]!=o.weapon||std::count(v.clientItems.begin(),v.clientItems.end(),o.weapon)!=1||
       !r.Eq(std::uint64_t(o.weapon)+4,s.config.weaponData)||!r.Eq(std::uint64_t(s.config.weaponData)+0x98,s.config.firingData)||
       !r.Eq(std::uint64_t(s.config.firingData)+0x40,s.config.primaryFire)||
       !r.Eq(std::uint64_t(s.config.primaryFire)+0x24,0)||!r.Eq(std::uint64_t(s.config.primaryFire)+0x4c,37)||
       !r.Get(std::uint64_t(o.weapon)+0x38,clientEffects)||!r.Type(clientEffects,"ClientMeleeFiringEffects")||
       !r.Eq(std::uint64_t(clientEffects)+0x10,s.config.firingData)||
       !r.Get(std::uint64_t(base)+b.contextRva+8,v.manager)||!r.Eq(v.manager,base+b.managerVtableRva)||
       !r.Get(std::uint64_t(o.player)+0x154,v.playerId)||!r.Get(std::uint64_t(v.manager)+4,v.capacity)||
       !v.capacity||v.capacity>256||v.playerId>=v.capacity||
       !r.Get(std::uint64_t(v.manager)+0x6c,v.players)||v.players<0x10000||std::uint64_t(v.players)+v.capacity*4>UINT32_MAX||
       !r.Get(std::uint64_t(v.players)+v.playerId*4,v.player)||!r.Eq(std::uint64_t(v.player)+0x154,v.playerId)||
       !r.Get(v.player,v.playerTable)||v.playerTable<base||std::uint64_t(v.playerTable)+0x34>std::uint64_t(base)+b.imageSize||
       !r.Eq(std::uint64_t(v.playerTable)+0x30,base+b.controlledGetterRva)||
       !r.Get(std::uint64_t(v.player)+0xc3c,v.soldier)||!r.Eq(std::uint64_t(v.player)+0xc6c,v.soldier)||
       !r.Get(v.soldier,v.soldierTable)||v.soldierTable!=0x1424850||!r.Type(v.soldier,"ServerSoldierEntity")||
       !r.Get(std::uint64_t(v.soldier)+0xc,v.soldierData)||!r.Eq(std::uint64_t(o.soldier)+0xc,v.soldierData)||
       !r.Eq(std::uint64_t(v.soldier)+0x220,v.player)||!r.Get(std::uint64_t(v.soldier)+0x2b4,v.inventory)||
       !r.Get(std::uint64_t(v.inventory)+4,v.inventoryData)||!r.Type(v.inventoryData,"WeaponSwitchingData")||
       !r.Get(std::uint64_t(v.soldier)+0x2b8,v.begin)||!r.Get(std::uint64_t(v.soldier)+0x2bc,v.end)||
       v.begin<0x10000||v.end<=v.begin||v.end-v.begin>256||(v.end-v.begin)%4||
       !r.Read(v.begin,v.items.data(),v.end-v.begin)||!r.Get(std::uint64_t(v.inventory)+0x14c,v.slot)||
       v.slot>=(v.end-v.begin)/4||(selected&&v.slot!=0))return false;
    v.item=v.items[0];
    if(v.item<0x10000||std::count(v.items.begin(),v.items.end(),v.item)!=1||!r.Eq(std::uint64_t(v.item)+4,s.config.weaponData)||
       !r.Get(std::uint64_t(v.item)+0xc,v.effects)||!r.Get(v.effects,v.effectsTable)||v.effectsTable!=0x1417cd0||
       !r.Type(v.effects,"ServerMeleeFiringEffects")||!r.Eq(std::uint64_t(v.effects)+0x10,s.config.firingData)||
       !r.Eq(std::uint64_t(v.effects)+0xd0,0x1417c70)||!r.Eq(std::uint64_t(v.effects)+0x128,v.player)||
       !r.Get(std::uint64_t(v.item)+0x10,v.firing)||v.firing==s.branches[0].address||v.firing==s.branches[1].address||
       !r.Eq(v.firing,base+state.firingVtableRva)||!r.Eq(std::uint64_t(v.firing)+8,s.config.firingData)||
       !r.Eq(std::uint64_t(v.firing)+12,s.config.ammoAddress))return false;
    for(unsigned n=0;n<2;++n)if(!r.Eq(std::uint64_t(o.weapon)+s.branches[n].wrapperOffset,s.branches[n].address)||
        !r.Eq(s.branches[n].address,base+state.firingVtableRva)||!r.Eq(std::uint64_t(s.branches[n].address)+8,s.config.firingData)||
        !r.Eq(std::uint64_t(s.branches[n].address)+12,s.config.ammoAddress))return false;
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
// Callback-time ownership only: original input freshness is checked separately
// on acquisition. Accepted native work outlives that lease through attack return.
inline bool OwnerCurrent(const ReloadStateMemory& memory,const ReloadServerBinding& server,
    const ReloadStateBinding& state,unsigned base,const Native& source)noexcept {
    detail::Reader r{memory};ReloadServerLinks a{},b{};unsigned ca=0,cb=0;
    if(!detail::Scope(r,server,state,base,source.client,a,ca)||
       !detail::Scope(r,server,state,base,source.client,b,cb)||a!=b||ca!=cb||ca!=source.clientEffects)return false;
    // Native state bits are not equipment ownership. Both current reads must
    // still agree; only the existing verified state bits may differ from source.
    a.clientSoldierFlags^=std::uint8_t((a.clientSoldierFlags^source.links.clientSoldierFlags)&0x18u);
    return a==source.links;
}
// Accepted callback observation only. Selection can move while the original
// native attack is completing; exact item membership/effects ownership remains
// mandatory. This never admits an input pulse or renews a blade/input lease.
inline bool AcceptedOwnerRetained(const ReloadStateMemory& memory,const ReloadServerBinding& server,
    const ReloadStateBinding& state,unsigned base,const Native& source)noexcept {
    detail::Reader r{memory};ReloadServerLinks a{},b{};unsigned ca=0,cb=0;
    if(!detail::Scope(r,server,state,base,source.client,a,ca,false)||
       !detail::Scope(r,server,state,base,source.client,b,cb,false)||a!=b||ca!=cb||ca!=source.clientEffects)return false;
    a.clientSoldierFlags^=std::uint8_t((a.clientSoldierFlags^source.links.clientSoldierFlags)&0x18u);
    a.clientSlot=source.links.clientSlot;a.slot=source.links.slot;
    return a==source.links;
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
