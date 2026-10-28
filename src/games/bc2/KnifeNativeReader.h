#pragma once
#include "Bc2ReloadServer.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace fvr::bc2::private_knife {
// Initial CURRENT ordinary firearm snapshot, obtained by the unchanged typed
// server reader. It is an inventory identity anchor, never a renewable input
// lease or a fabricated selected owner while the knife is selected.
struct Anchor {
    ReloadServerSnapshot original{};
    std::uint32_t knifeWeapon=0,knifeSlot=0,knifeData=0,knifeFiringData=0;
    std::array<unsigned,3> knifeFiring{};
    unsigned knifeClientEffects=0,knifeServerEffects=0;
};
struct Selection {
    std::array<unsigned,3> inventory{},current{},previous{}; // effective client, server, independent client
    std::uint8_t soldierFlags=0;
    bool operator==(const Selection&)const=default;
};
struct Item {
    ReloadInventoryItem client{};
    ReloadFiringObservation server{};
    unsigned serverItem=0,clientEffects=0,serverEffects=0;
};
struct Snapshot {
    ReloadStateOwner selectedOwner{};Selection selected{};Item original{},knife{};
    std::uint64_t sequence=0;std::int64_t observedNs=0;
    // Observation only. No hook, action, hide, selected-owner substitution or
    // manual-cycle admission can be inferred from a coherent read.
    static constexpr bool actionAuthority=false,atomic=false;
};
namespace detail {
struct Reader {
    const ReloadStateMemory& memory;
    bool Read(std::uint64_t at,void* out,std::size_t count)const noexcept {
        return memory.read&&at>=0x10000&&count&&count<=4096&&at+count<=UINT32_MAX&&
            memory.read(memory.context,unsigned(at),out,count);
    }
    template<class T>bool Get(std::uint64_t at,T& out)const noexcept{return Read(at,&out,sizeof(out));}
    bool Eq(std::uint64_t at,unsigned wanted)const noexcept {unsigned value=0;return Get(at,value)&&value==wanted;}
    bool Type(unsigned at,const char* name)const noexcept{return memory.type&&at>=0x10000&&memory.type(memory.context,at,name);}
};
// Same stable actor/inventory membership used by accepted316/318/336. The real
// current selectors are recorded separately and may converge asynchronously.
// Unlike the firearm target reader, no firearm-specific melee +d4/+128 layout
// is presumed: exact inventory membership owns the separate melee effects.
inline bool Actor(const Reader& r,const ReloadServerBinding& binding,unsigned base,
    const Anchor& a,const ReloadStateOwner& current,Selection& out)noexcept {
    const auto& s=a.original;const auto& o=s.client.owner;const auto& v=s.links;
    if(base!=binding.preferredBase||base>UINT32_MAX-binding.imageSize||binding.imageSize<12||
       binding.contextRva>binding.imageSize-12||binding.managerVtableRva>binding.imageSize-4||
       binding.controlledGetterRva>=binding.imageSize||
       !s.client.sequence||s.client.observedNs<=0||o.player<0x10000||o.soldier<0x10000||
       o.soldier>UINT32_MAX-4||o.weak<0x10000||o.weapon<0x10000||!o.actorGeneration||
       current.player!=o.player||current.soldier!=o.soldier||current.weak!=o.weak||
       current.actorGeneration!=o.actorGeneration||!current.equipGeneration||!current.space||
       !a.knifeWeapon||a.knifeSlot>=64||a.knifeSlot==s.client.selectedSlot||
       v.clientItems[a.knifeSlot]!=a.knifeWeapon||!a.knifeData||!a.knifeFiringData||
       v.clientBegin<0x10000||v.clientEnd<=v.clientBegin||v.clientEnd-v.clientBegin>256||
       (v.clientEnd-v.clientBegin)%4||a.knifeSlot>=(v.clientEnd-v.clientBegin)/4||
       s.client.selectedSlot>=(v.clientEnd-v.clientBegin)/4||
       v.begin<0x10000||v.end<=v.begin||v.end-v.begin>256||(v.end-v.begin)%4||
       a.knifeSlot>=(v.end-v.begin)/4||s.client.selectedSlot>=(v.end-v.begin)/4||
       v.clientItems[s.client.selectedSlot]!=o.weapon||v.items[s.client.selectedSlot]!=v.item)return false;
    unsigned manager=0,attached=0;std::uint8_t playerFlags=0;
    if(!r.Get(std::uint64_t(o.player)+0xc60,attached)||attached||
       !r.Get(std::uint64_t(o.player)+0xccd,playerFlags)||!(playerFlags&8)||
       !r.Eq(std::uint64_t(o.player)+0xc54,o.weak)||!r.Eq(o.weak,o.soldier+4)||
       !r.Eq(std::uint64_t(o.player)+0xc68,o.soldier)||!r.Eq(std::uint64_t(o.soldier)+0x220,o.player)||
       !r.Get(std::uint64_t(o.soldier)+0x114,out.soldierFlags)||
       (out.soldierFlags&~0x18u)!=(s.client.soldierFlags&~0x18u)||
       !r.Eq(std::uint64_t(o.soldier)+((out.soldierFlags&1)?0x24c:0x248),v.clientInventory)||
       !r.Eq(std::uint64_t(o.soldier)+0x260,v.clientBegin)||!r.Eq(std::uint64_t(o.soldier)+0x264,v.clientEnd)||
       !r.Get(std::uint64_t(base)+binding.contextRva+8,manager)||manager!=v.manager||
       !r.Eq(manager,base+binding.managerVtableRva)||!r.Eq(std::uint64_t(manager)+4,v.capacity)||
       !r.Eq(std::uint64_t(manager)+0x6c,v.players)||!v.capacity||v.capacity>256||v.playerId>=v.capacity||
       !r.Eq(std::uint64_t(o.player)+0x154,v.playerId)||!r.Eq(std::uint64_t(v.players)+v.playerId*4,v.player)||
       !r.Eq(std::uint64_t(v.player)+0x154,v.playerId)||!r.Eq(v.player,v.playerTable)||
       !r.Eq(std::uint64_t(v.playerTable)+0x30,base+binding.controlledGetterRva)||
       !r.Eq(std::uint64_t(v.player)+0xc3c,v.soldier)||!r.Eq(std::uint64_t(v.player)+0xc6c,v.soldier)||
       !r.Eq(v.soldier,v.soldierTable)||!r.Type(v.soldier,"ServerSoldierEntity")||
       !r.Eq(std::uint64_t(v.soldier)+0xc,v.soldierData)||!r.Eq(std::uint64_t(o.soldier)+0xc,v.soldierData)||
       !r.Eq(std::uint64_t(v.soldier)+0x220,v.player)||!r.Eq(std::uint64_t(v.soldier)+0x2b4,v.inventory)||
       !r.Eq(std::uint64_t(v.inventory)+4,v.inventoryData)||
       !r.Eq(std::uint64_t(v.soldier)+0x2b8,v.begin)||!r.Eq(std::uint64_t(v.soldier)+0x2bc,v.end)||
       !r.Eq(std::uint64_t(v.item)+0xc,v.effects)||!r.Eq(v.effects,v.effectsTable)||
       !r.Eq(std::uint64_t(v.effects)+0x128,v.player)||!r.Eq(std::uint64_t(v.effects)+0xd4,v.callback)||
       !r.Eq(std::uint64_t(v.callback)+4,s.client.config.firingData))return false;
    decltype(v.clientItems) clients{};decltype(v.items) servers{};
    if(!r.Read(v.clientBegin,clients.data(),v.clientEnd-v.clientBegin)||clients!=v.clientItems||
       !r.Read(v.begin,servers.data(),v.end-v.begin)||servers!=v.items||
       std::count(clients.begin(),clients.end(),a.knifeWeapon)!=1||
       std::count(servers.begin(),servers.end(),v.items[a.knifeSlot])!=1||
       !r.Eq(std::uint64_t(o.weapon)+4,s.client.config.weaponData)||
       !r.Eq(std::uint64_t(v.item)+4,s.client.config.weaponData)||
       !r.Eq(std::uint64_t(a.knifeWeapon)+4,a.knifeData)||
       !r.Eq(std::uint64_t(v.items[a.knifeSlot])+4,a.knifeData)||
       !r.Type(a.knifeData,"SoldierWeaponData")||!r.Eq(std::uint64_t(a.knifeData)+0x84,11)||
       !r.Eq(std::uint64_t(a.knifeData)+0x98,a.knifeFiringData))return false;
    out.inventory={v.clientInventory,v.inventory,0};
    if(!r.Get(std::uint64_t(o.soldier)+0x248,out.inventory[2]))return false;
    for(unsigned n=0;n<3;++n)if(!r.Get(std::uint64_t(out.inventory[n])+0x14c,out.current[n])||
        !r.Get(std::uint64_t(out.inventory[n])+0x150,out.previous[n])||out.previous[n]>=32||
        (out.current[n]!=s.client.selectedSlot&&out.current[n]!=a.knifeSlot))return false;
    return clients[out.current[0]]==current.weapon;
}
template<class T>T Word(const std::array<std::byte,0xb0>& bytes,unsigned at)noexcept {
    T out{};std::memcpy(&out,bytes.data()+at,sizeof(out));return out;
}
enum class BranchFailure:unsigned {None,ItemRead,StateRead,Identity,Malformed,Incoherent,Count};
struct BranchDiagnostic {
    BranchFailure failure=BranchFailure::None;
    unsigned firing=0,differenceOffset=UINT32_MAX,before=0,after=0;
    bool readyPair=false;
};
inline bool Branch(const Reader& r,const ReloadStateBinding& binding,unsigned base,
    unsigned item,const ReloadObservedConfig& config,ReloadFiringObservation& out,
    BranchDiagnostic* diagnostic=nullptr)noexcept {
    if(diagnostic)*diagnostic={};
    const auto fail=[&](BranchFailure reason){if(diagnostic)diagnostic->failure=reason;return false;};
    unsigned firing=0;if(!r.Get(std::uint64_t(item)+0x10,firing))return fail(BranchFailure::ItemRead);
    if(diagnostic)diagnostic->firing=firing;
    std::array<std::byte,0xb0> before{},after{};
    if(!r.Read(firing,before.data(),before.size())||!r.Read(firing,after.data(),after.size()))return fail(BranchFailure::StateRead);
    // Preserve the original default result and memory-read order. Classified
    // failure is optional evidence, never an accepted firing observation.
    if(before!=after){
        if(!diagnostic)return false;
        for(unsigned offset=0;offset<before.size();offset+=4)
            if(Word<unsigned>(before,offset)!=Word<unsigned>(after,offset)){
                diagnostic->differenceOffset=offset;diagnostic->before=Word<unsigned>(before,offset);
                diagnostic->after=Word<unsigned>(after,offset);break;
            }
        for(const auto* bytes:{&before,&after}){
            if(Word<unsigned>(*bytes,0)!=base+binding.firingVtableRva||Word<unsigned>(*bytes,8)!=config.firingData||
               Word<unsigned>(*bytes,12)!=config.ammoAddress)return fail(BranchFailure::Identity);
            const auto timer=Word<float>(*bytes,0x50);
            if(Word<unsigned>(*bytes,0x3c)>15||Word<unsigned>(*bytes,0x40)>15||Word<unsigned>(*bytes,0x44)>15||
               !std::isfinite(timer)||std::abs(timer)>1000000||Word<int>(*bytes,0x7c)<-1||Word<int>(*bytes,0x80)<-1||
               Word<int>(*bytes,0x7c)>1000000||Word<int>(*bytes,0x80)>1000000)return fail(BranchFailure::Malformed);
        }
        const auto ready=[](const auto& bytes){
            return Word<unsigned>(bytes,0x3c)==2&&Word<unsigned>(bytes,0x44)==2&&Word<float>(bytes,0x50)>=0&&
                Word<int>(bytes,0x7c)==-1&&Word<int>(bytes,0x80)==-1&&!(Word<std::uint8_t>(bytes,0xa8)&8);
        };
        diagnostic->readyPair=ready(before)&&ready(after);
        return fail(BranchFailure::Incoherent);
    }
    if(Word<unsigned>(before,0)!=base+binding.firingVtableRva||Word<unsigned>(before,8)!=config.firingData||
       Word<unsigned>(before,12)!=config.ammoAddress)return fail(BranchFailure::Identity);
    out.address=firing;out.wrapperOffset=0x10;out.currentState=Word<unsigned>(before,0x3c);
    out.previousState=Word<unsigned>(before,0x40);out.nextState=Word<unsigned>(before,0x44);
    out.phaseTimer=Word<float>(before,0x50);out.loaded=Word<int>(before,0x7c);out.reserve=Word<int>(before,0x80);
    out.flagsA8=Word<std::uint8_t>(before,0xa8);
    if(out.currentState>15||out.previousState>15||out.nextState>15||!std::isfinite(out.phaseTimer)||
       std::abs(out.phaseTimer)>1000000||out.loaded<-1||out.reserve<-1||out.loaded>1000000||out.reserve>1000000)
        return fail(BranchFailure::Malformed);
    return true;
}
inline bool SameState(const ReloadFiringObservation& a,const ReloadFiringObservation& b)noexcept {
    return a.address==b.address&&a.currentState==b.currentState&&a.previousState==b.previousState&&
        a.nextState==b.nextState&&a.phaseTimer==b.phaseTimer&&a.loaded==b.loaded&&a.reserve==b.reserve&&a.flagsA8==b.flagsA8;
}
inline bool ReadItem(const Reader& r,const ReloadStateBinding& state,unsigned base,
    const ReloadStateOwner& current,unsigned weapon,unsigned serverItem,bool knife,Item& out)noexcept {
    const auto client=ReadReloadInventoryItem(r.memory,state,base,current,weapon);if(!client)return false;
    out.client=*client;out.serverItem=serverItem;
    if(!r.Eq(std::uint64_t(serverItem)+4,client->config.weaponData)||
       !r.Get(std::uint64_t(serverItem)+0xc,out.serverEffects)||
       !r.Type(out.serverEffects,knife?"ServerMeleeFiringEffects":"ServerWeaponFiringEffects")||
       !r.Eq(std::uint64_t(out.serverEffects)+0x10,client->config.firingData)||
       !Branch(r,state,base,serverItem,client->config,out.server)||
       out.server.address==client->branches[0].address||out.server.address==client->branches[1].address)return false;
    if(knife&&(!r.Get(std::uint64_t(weapon)+0x38,out.clientEffects)||!r.Type(out.clientEffects,"ClientMeleeFiringEffects")||
       !r.Eq(std::uint64_t(out.clientEffects)+0x10,client->config.firingData)))return false;
    return true;
}
inline bool SameItem(const Item& a,const Item& b)noexcept {
    return a.serverItem==b.serverItem&&a.clientEffects==b.clientEffects&&a.serverEffects==b.serverEffects&&
        a.client.target==b.client.target&&a.client.slot==b.client.slot&&a.client.inventory==b.client.inventory&&
        a.client.config==b.client.config&&SameState(a.server,b.server)&&
        SameState(a.client.branches[0],b.client.branches[0])&&SameState(a.client.branches[1],b.client.branches[1]);
}
}
// Existing common readers retain exact -1 sentinel as data; no nonnegative ammo
// eligibility is broadened. This separate knife observer makes no memory writes.
inline std::optional<Snapshot> Read(const ReloadStateMemory& memory,const ReloadStateBinding& state,
    const ReloadServerBinding& server,unsigned base,const Anchor& a,const ReloadStateOwner& current,
    std::uint64_t sequence,std::int64_t observedNs)noexcept {
    if(!sequence||observedNs<=0||state.preferredBase!=base||
       std::uint64_t(state.firingVtableRva)+4>state.imageSize)return {};
    detail::Reader r{memory};Snapshot out;out.selectedOwner=current;out.sequence=sequence;out.observedNs=observedNs;
    if(!detail::Actor(r,server,base,a,current,out.selected)||
       !detail::ReadItem(r,state,base,current,a.original.client.owner.weapon,a.original.links.item,false,out.original)||
       !detail::ReadItem(r,state,base,current,a.knifeWeapon,a.original.links.items[a.knifeSlot],true,out.knife)||
       out.original.client.config!=a.original.client.config||out.knife.client.slot!=a.knifeSlot||
       out.original.serverEffects!=a.original.links.effects||out.original.server.address!=a.original.links.firing||
       out.original.client.branches[0].address!=a.original.client.branches[0].address||
       out.original.client.branches[1].address!=a.original.client.branches[1].address||
       out.knife.client.config.weaponData!=a.knifeData||out.knife.client.config.firingData!=a.knifeFiringData||
       out.knife.clientEffects!=a.knifeClientEffects||out.knife.serverEffects!=a.knifeServerEffects||
       out.knife.client.branches[0].address!=a.knifeFiring[0]||out.knife.client.branches[1].address!=a.knifeFiring[1]||
       out.knife.server.address!=a.knifeFiring[2]||
       std::strcmp(out.knife.client.config.assetName.data(),"KNV-1")||out.knife.client.config.fireLogicType!=0||
       out.knife.client.config.fireInputAction!=8||out.knife.client.config.baseCapacity!=-1||
       out.knife.client.config.numberOfMagazines!=-1)return {};
    Item originalAgain,knifeAgain;Selection after;
    if(!detail::ReadItem(r,state,base,current,a.original.client.owner.weapon,a.original.links.item,false,originalAgain)||
       !detail::ReadItem(r,state,base,current,a.knifeWeapon,a.original.links.items[a.knifeSlot],true,knifeAgain)||
       !detail::SameItem(out.original,originalAgain)||!detail::SameItem(out.knife,knifeAgain)||
       !detail::Actor(r,server,base,a,current,after)||after!=out.selected)return {};
    return out;
}
inline bool KnifeSentinel(const Snapshot& s)noexcept {
    for(const auto* b:{&s.knife.client.branches[0],&s.knife.client.branches[1],&s.knife.server})
        if(b->loaded!=-1||b->reserve!=-1)return false;
    return true; // Separate observation: changed counts remain visible to quarantine.
}
// Start only with a fresh ACTUAL selected ordinary item, then read its separate
// existing knife member. This captures stable identity, not an action lease.
// Live code validation remains a prerequisite of the common typed readers.
inline std::optional<Anchor> MakeAnchor(const ReloadStateMemory& memory,const ReloadStateBinding& state,
    const ReloadServerBinding& server,unsigned base,const ReloadStateOwner& current,unsigned knifeWeapon,
    std::uint64_t sequence,std::int64_t observedNs)noexcept {
    const auto selected=ReadReloadState(memory,state,base,current,sequence,observedNs);
    if(!selected.snapshot||selected.snapshot->selectedSlot>1||current.weapon==knifeWeapon)return {};
    const auto original=ReadReloadServerState(memory,server,state,base,*selected.snapshot);
    const auto knife=ReadReloadInventoryItem(memory,state,base,current,knifeWeapon);
    if(!original||!knife||knife->slot>=original->links.items.size())return {};
    Anchor a;a.original=*original;a.knifeWeapon=knifeWeapon;a.knifeSlot=knife->slot;
    a.knifeData=knife->config.weaponData;a.knifeFiringData=knife->config.firingData;
    a.knifeFiring[0]=knife->branches[0].address;a.knifeFiring[1]=knife->branches[1].address;
    detail::Reader r{memory};const auto item=original->links.items[a.knifeSlot];
    if(!r.Get(std::uint64_t(knifeWeapon)+0x38,a.knifeClientEffects)||
       !r.Get(std::uint64_t(item)+0xc,a.knifeServerEffects)||
       !r.Get(std::uint64_t(item)+0x10,a.knifeFiring[2]))return {};
    const auto checked=Read(memory,state,server,base,a,current,sequence,observedNs);
    if(!checked||!KnifeSentinel(*checked))return {};
    return a;
}
}
