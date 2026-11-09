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
// Optional passive diagnostics. Each value comes from the existing read;
// no retry, additional native read or authority is introduced.
struct ReadDiagnostic {
    enum class Kind:unsigned {None,Bytes,Equal,Type};
    Kind kind=Kind::None;std::uint64_t address=0;unsigned bytes=0,value=0,expected=0;
    bool known=false,result=false;const char* type=nullptr;
};
struct Reader {
    const ReloadStateMemory& memory;ReadDiagnostic* diagnostic=nullptr;
    bool Read(std::uint64_t at,void* out,std::size_t count)const noexcept {
        const bool okay=memory.read&&at>=0x10000&&count&&count<=4096&&at+count<=UINT32_MAX&&
            memory.read(memory.context,unsigned(at),out,count);
        if(diagnostic){*diagnostic={};diagnostic->kind=ReadDiagnostic::Kind::Bytes;
            diagnostic->address=at;diagnostic->bytes=unsigned(count);diagnostic->known=diagnostic->result=okay;
            if(okay&&count<=4)std::memcpy(&diagnostic->value,out,count);}
        return okay;
    }
    template<class T>bool Get(std::uint64_t at,T& out)const noexcept{return Read(at,&out,sizeof(out));}
    bool Eq(std::uint64_t at,unsigned wanted)const noexcept {
        unsigned value=0;const bool okay=Get(at,value)&&value==wanted;
        if(diagnostic){diagnostic->kind=ReadDiagnostic::Kind::Equal;diagnostic->expected=wanted;diagnostic->result=okay;}
        return okay;
    }
    bool Type(unsigned at,const char* name)const noexcept {
        const bool okay=memory.type&&at>=0x10000&&memory.type(memory.context,at,name);
        if(diagnostic){*diagnostic={};diagnostic->kind=ReadDiagnostic::Kind::Type;
            diagnostic->address=at;diagnostic->type=name;diagnostic->known=bool(memory.type)&&at>=0x10000;diagnostic->result=okay;}
        return okay;
    }
};
struct ActorDiagnostic {
    const char* predicate=nullptr;ReadDiagnostic lastRead{};Selection partial{};
    unsigned differenceIndex=UINT32_MAX,actualItem=0,expectedItem=0;
};
// Same stable actor/inventory membership used by accepted316/318/336. The real
// current selectors are recorded separately and may converge asynchronously.
// Unlike the firearm target reader, no firearm-specific melee +d4/+128 layout
// is presumed: exact inventory membership owns the separate melee effects.
inline bool Actor(const Reader& reader,const ReloadServerBinding& binding,unsigned base,
    const Anchor& a,const ReloadStateOwner& current,Selection& out,ActorDiagnostic* diagnostic=nullptr)noexcept {
    if(diagnostic)*diagnostic={};
    const Reader r{reader.memory,diagnostic?&diagnostic->lastRead:nullptr};
    const auto fail=[&](const char* predicate){if(diagnostic){diagnostic->predicate=predicate;diagnostic->partial=out;}return false;};
#define FVR_KNIFE_ACTOR_REFUSE(predicate) if(predicate)return fail(#predicate)
    const auto& s=a.original;const auto& o=s.client.owner;const auto& v=s.links;
    FVR_KNIFE_ACTOR_REFUSE(base!=binding.preferredBase);
    FVR_KNIFE_ACTOR_REFUSE(base>UINT32_MAX-binding.imageSize);
    FVR_KNIFE_ACTOR_REFUSE(binding.imageSize<12);
    FVR_KNIFE_ACTOR_REFUSE(binding.contextRva>binding.imageSize-12);
    FVR_KNIFE_ACTOR_REFUSE(binding.managerVtableRva>binding.imageSize-4);
    FVR_KNIFE_ACTOR_REFUSE(binding.controlledGetterRva>=binding.imageSize);
    FVR_KNIFE_ACTOR_REFUSE(!s.client.sequence);
    FVR_KNIFE_ACTOR_REFUSE(s.client.observedNs<=0);
    FVR_KNIFE_ACTOR_REFUSE(o.player<0x10000);
    FVR_KNIFE_ACTOR_REFUSE(o.soldier<0x10000);
    FVR_KNIFE_ACTOR_REFUSE(o.soldier>UINT32_MAX-4);
    FVR_KNIFE_ACTOR_REFUSE(o.weak<0x10000);
    FVR_KNIFE_ACTOR_REFUSE(o.weapon<0x10000);
    FVR_KNIFE_ACTOR_REFUSE(!o.actorGeneration);
    FVR_KNIFE_ACTOR_REFUSE(current.player!=o.player);
    FVR_KNIFE_ACTOR_REFUSE(current.soldier!=o.soldier);
    FVR_KNIFE_ACTOR_REFUSE(current.weak!=o.weak);
    FVR_KNIFE_ACTOR_REFUSE(current.actorGeneration!=o.actorGeneration);
    FVR_KNIFE_ACTOR_REFUSE(!current.equipGeneration);
    FVR_KNIFE_ACTOR_REFUSE(!current.space);
    FVR_KNIFE_ACTOR_REFUSE(!a.knifeWeapon);
    FVR_KNIFE_ACTOR_REFUSE(a.knifeSlot>=64);
    FVR_KNIFE_ACTOR_REFUSE(a.knifeSlot==s.client.selectedSlot);
    FVR_KNIFE_ACTOR_REFUSE(v.clientItems[a.knifeSlot]!=a.knifeWeapon);
    FVR_KNIFE_ACTOR_REFUSE(!a.knifeData);
    FVR_KNIFE_ACTOR_REFUSE(!a.knifeFiringData);
    FVR_KNIFE_ACTOR_REFUSE(v.clientBegin<0x10000);
    FVR_KNIFE_ACTOR_REFUSE(v.clientEnd<=v.clientBegin);
    FVR_KNIFE_ACTOR_REFUSE(v.clientEnd-v.clientBegin>256);
    FVR_KNIFE_ACTOR_REFUSE((v.clientEnd-v.clientBegin)%4);
    FVR_KNIFE_ACTOR_REFUSE(a.knifeSlot>=(v.clientEnd-v.clientBegin)/4);
    FVR_KNIFE_ACTOR_REFUSE(s.client.selectedSlot>=(v.clientEnd-v.clientBegin)/4);
    FVR_KNIFE_ACTOR_REFUSE(v.begin<0x10000);
    FVR_KNIFE_ACTOR_REFUSE(v.end<=v.begin);
    FVR_KNIFE_ACTOR_REFUSE(v.end-v.begin>256);
    FVR_KNIFE_ACTOR_REFUSE((v.end-v.begin)%4);
    FVR_KNIFE_ACTOR_REFUSE(a.knifeSlot>=(v.end-v.begin)/4);
    FVR_KNIFE_ACTOR_REFUSE(s.client.selectedSlot>=(v.end-v.begin)/4);
    FVR_KNIFE_ACTOR_REFUSE(v.clientItems[s.client.selectedSlot]!=o.weapon);
    FVR_KNIFE_ACTOR_REFUSE(v.items[s.client.selectedSlot]!=v.item);
    unsigned manager=0,attached=0;std::uint8_t playerFlags=0;
    FVR_KNIFE_ACTOR_REFUSE(!r.Get(std::uint64_t(o.player)+0xc60,attached));
    FVR_KNIFE_ACTOR_REFUSE(attached);
    FVR_KNIFE_ACTOR_REFUSE(!r.Get(std::uint64_t(o.player)+0xccd,playerFlags));
    FVR_KNIFE_ACTOR_REFUSE(!(playerFlags&8));
    FVR_KNIFE_ACTOR_REFUSE(!r.Eq(std::uint64_t(o.player)+0xc54,o.weak));
    FVR_KNIFE_ACTOR_REFUSE(!r.Eq(o.weak,o.soldier+4));
    FVR_KNIFE_ACTOR_REFUSE(!r.Eq(std::uint64_t(o.player)+0xc68,o.soldier));
    FVR_KNIFE_ACTOR_REFUSE(!r.Eq(std::uint64_t(o.soldier)+0x220,o.player));
    FVR_KNIFE_ACTOR_REFUSE(!r.Get(std::uint64_t(o.soldier)+0x114,out.soldierFlags));
    FVR_KNIFE_ACTOR_REFUSE((out.soldierFlags&~0x18u)!=(s.client.soldierFlags&~0x18u));
    FVR_KNIFE_ACTOR_REFUSE(!r.Eq(std::uint64_t(o.soldier)+((out.soldierFlags&1)?0x24c:0x248),v.clientInventory));
    FVR_KNIFE_ACTOR_REFUSE(!r.Eq(std::uint64_t(o.soldier)+0x260,v.clientBegin));
    FVR_KNIFE_ACTOR_REFUSE(!r.Eq(std::uint64_t(o.soldier)+0x264,v.clientEnd));
    FVR_KNIFE_ACTOR_REFUSE(!r.Get(std::uint64_t(base)+binding.contextRva+8,manager));
    FVR_KNIFE_ACTOR_REFUSE(manager!=v.manager);
    FVR_KNIFE_ACTOR_REFUSE(!r.Eq(manager,base+binding.managerVtableRva));
    FVR_KNIFE_ACTOR_REFUSE(!r.Eq(std::uint64_t(manager)+4,v.capacity));
    FVR_KNIFE_ACTOR_REFUSE(!r.Eq(std::uint64_t(manager)+0x6c,v.players));
    FVR_KNIFE_ACTOR_REFUSE(!v.capacity);
    FVR_KNIFE_ACTOR_REFUSE(v.capacity>256);
    FVR_KNIFE_ACTOR_REFUSE(v.playerId>=v.capacity);
    FVR_KNIFE_ACTOR_REFUSE(!r.Eq(std::uint64_t(o.player)+0x154,v.playerId));
    FVR_KNIFE_ACTOR_REFUSE(!r.Eq(std::uint64_t(v.players)+v.playerId*4,v.player));
    FVR_KNIFE_ACTOR_REFUSE(!r.Eq(std::uint64_t(v.player)+0x154,v.playerId));
    FVR_KNIFE_ACTOR_REFUSE(!r.Eq(v.player,v.playerTable));
    FVR_KNIFE_ACTOR_REFUSE(!r.Eq(std::uint64_t(v.playerTable)+0x30,base+binding.controlledGetterRva));
    FVR_KNIFE_ACTOR_REFUSE(!r.Eq(std::uint64_t(v.player)+0xc3c,v.soldier));
    FVR_KNIFE_ACTOR_REFUSE(!r.Eq(std::uint64_t(v.player)+0xc6c,v.soldier));
    FVR_KNIFE_ACTOR_REFUSE(!r.Eq(v.soldier,v.soldierTable));
    FVR_KNIFE_ACTOR_REFUSE(!r.Type(v.soldier,"ServerSoldierEntity"));
    FVR_KNIFE_ACTOR_REFUSE(!r.Eq(std::uint64_t(v.soldier)+0xc,v.soldierData));
    FVR_KNIFE_ACTOR_REFUSE(!r.Eq(std::uint64_t(o.soldier)+0xc,v.soldierData));
    FVR_KNIFE_ACTOR_REFUSE(!r.Eq(std::uint64_t(v.soldier)+0x220,v.player));
    FVR_KNIFE_ACTOR_REFUSE(!r.Eq(std::uint64_t(v.soldier)+0x2b4,v.inventory));
    FVR_KNIFE_ACTOR_REFUSE(!r.Eq(std::uint64_t(v.inventory)+4,v.inventoryData));
    FVR_KNIFE_ACTOR_REFUSE(!r.Eq(std::uint64_t(v.soldier)+0x2b8,v.begin));
    FVR_KNIFE_ACTOR_REFUSE(!r.Eq(std::uint64_t(v.soldier)+0x2bc,v.end));
    FVR_KNIFE_ACTOR_REFUSE(!r.Eq(std::uint64_t(v.item)+0xc,v.effects));
    FVR_KNIFE_ACTOR_REFUSE(!r.Eq(v.effects,v.effectsTable));
    FVR_KNIFE_ACTOR_REFUSE(!r.Eq(std::uint64_t(v.effects)+0x128,v.player));
    FVR_KNIFE_ACTOR_REFUSE(!r.Eq(std::uint64_t(v.effects)+0xd4,v.callback));
    FVR_KNIFE_ACTOR_REFUSE(!r.Eq(std::uint64_t(v.callback)+4,s.client.config.firingData));
    decltype(v.clientItems) clients{};decltype(v.items) servers{};
    FVR_KNIFE_ACTOR_REFUSE(!r.Read(v.clientBegin,clients.data(),v.clientEnd-v.clientBegin));
    if(clients!=v.clientItems){if(diagnostic)for(unsigned n=0;n<clients.size();++n)if(clients[n]!=v.clientItems[n]){
        diagnostic->differenceIndex=n;diagnostic->actualItem=clients[n];diagnostic->expectedItem=v.clientItems[n];break;}
        return fail("clients!=v.clientItems");}
    FVR_KNIFE_ACTOR_REFUSE(!r.Read(v.begin,servers.data(),v.end-v.begin));
    if(servers!=v.items){if(diagnostic)for(unsigned n=0;n<servers.size();++n)if(servers[n]!=v.items[n]){
        diagnostic->differenceIndex=n;diagnostic->actualItem=servers[n];diagnostic->expectedItem=v.items[n];break;}
        return fail("servers!=v.items");}
    FVR_KNIFE_ACTOR_REFUSE(std::count(clients.begin(),clients.end(),a.knifeWeapon)!=1);
    FVR_KNIFE_ACTOR_REFUSE(std::count(servers.begin(),servers.end(),v.items[a.knifeSlot])!=1);
    FVR_KNIFE_ACTOR_REFUSE(!r.Eq(std::uint64_t(o.weapon)+4,s.client.config.weaponData));
    FVR_KNIFE_ACTOR_REFUSE(!r.Eq(std::uint64_t(v.item)+4,s.client.config.weaponData));
    FVR_KNIFE_ACTOR_REFUSE(!r.Eq(std::uint64_t(a.knifeWeapon)+4,a.knifeData));
    FVR_KNIFE_ACTOR_REFUSE(!r.Eq(std::uint64_t(v.items[a.knifeSlot])+4,a.knifeData));
    FVR_KNIFE_ACTOR_REFUSE(!r.Type(a.knifeData,"SoldierWeaponData"));
    FVR_KNIFE_ACTOR_REFUSE(!r.Eq(std::uint64_t(a.knifeData)+0x84,11));
    FVR_KNIFE_ACTOR_REFUSE(!r.Eq(std::uint64_t(a.knifeData)+0x98,a.knifeFiringData));
    out.inventory={v.clientInventory,v.inventory,0};
    FVR_KNIFE_ACTOR_REFUSE(!r.Get(std::uint64_t(o.soldier)+0x248,out.inventory[2]));
    for(unsigned n=0;n<3;++n){
        FVR_KNIFE_ACTOR_REFUSE(!r.Get(std::uint64_t(out.inventory[n])+0x14c,out.current[n]));
        FVR_KNIFE_ACTOR_REFUSE(!r.Get(std::uint64_t(out.inventory[n])+0x150,out.previous[n]));
        FVR_KNIFE_ACTOR_REFUSE(out.previous[n]>=32);
        FVR_KNIFE_ACTOR_REFUSE((out.current[n]!=s.client.selectedSlot&&out.current[n]!=a.knifeSlot));
    }
    if(clients[out.current[0]]!=current.weapon){if(diagnostic){diagnostic->differenceIndex=out.current[0];
        diagnostic->actualItem=clients[out.current[0]];diagnostic->expectedItem=current.weapon;}
        return fail("clients[out.current[0]]!=current.weapon");}
#undef FVR_KNIFE_ACTOR_REFUSE
    return true;
}
template<class T>T Word(const std::array<std::byte,0xb0>& bytes,unsigned at)noexcept {
    T out{};std::memcpy(&out,bytes.data()+at,sizeof(out));return out;
}
struct SelectionDiagnostic {
    const char* stage=nullptr;unsigned slot=0,actualWeapon=0;ReadDiagnostic slotRead{};
    ActorDiagnostic actor{};Selection before{},after{};
};
inline bool CurrentSelection(const Reader& reader,const ReloadServerBinding& binding,unsigned base,
    const Anchor& a,Selection& out,SelectionDiagnostic* diagnostic=nullptr)noexcept {
    if(diagnostic)*diagnostic={};
    const Reader r{reader.memory,diagnostic?&diagnostic->slotRead:nullptr};
    const auto fail=[&](const char* stage){if(diagnostic)diagnostic->stage=stage;return false;};
    unsigned slot=0;
    if(!r.Get(std::uint64_t(a.original.links.clientInventory)+0x14c,slot))return fail("slot_read");
    if(diagnostic)diagnostic->slot=slot;
    if(slot>=a.original.links.clientItems.size())return fail("slot_range");
    auto actual=a.original.client.owner;actual.weapon=a.original.links.clientItems[slot];
    if(diagnostic)diagnostic->actualWeapon=actual.weapon;
    Selection before,after;
    if(!Actor(reader,binding,base,a,actual,before,diagnostic?&diagnostic->actor:nullptr))return fail("actor_before");
    if(diagnostic)diagnostic->before=before;
    if(!Actor(reader,binding,base,a,actual,after,diagnostic?&diagnostic->actor:nullptr))return fail("actor_after");
    if(diagnostic)diagnostic->after=after;
    if(before!=after)return fail("selection_changed");
    out=before;return true;
}
struct CallbackStateDiagnostic {
    const char* predicate=nullptr;ReadDiagnostic read{};
    unsigned vtable=0,expectedVtable=0,data=0,expectedData=0,current=0,next=0;
};
inline bool CallbackState(const Reader& reader,const ReloadStateBinding& binding,unsigned base,
    const Anchor* anchor,unsigned firing,std::array<std::byte,0xb0>& bytes,CallbackStateDiagnostic* diagnostic=nullptr)noexcept {
    if(diagnostic)*diagnostic={};
    const Reader r{reader.memory,diagnostic?&diagnostic->read:nullptr};
    const auto fail=[&](const char* predicate){if(diagnostic)diagnostic->predicate=predicate;return false;};
    if(!anchor)return fail("anchor_missing");
    if(!r.Read(firing,bytes.data(),bytes.size()))return fail("state_read");
    if(diagnostic){diagnostic->vtable=Word<unsigned>(bytes,0);diagnostic->expectedVtable=base+binding.firingVtableRva;
        diagnostic->data=Word<unsigned>(bytes,8);diagnostic->expectedData=anchor->knifeFiringData;
        diagnostic->current=Word<unsigned>(bytes,0x3c);diagnostic->next=Word<unsigned>(bytes,0x44);}
    if(Word<unsigned>(bytes,0)!=base+binding.firingVtableRva)return fail("vtable");
    if(Word<unsigned>(bytes,8)!=anchor->knifeFiringData)return fail("firing_data");
    if(Word<unsigned>(bytes,0x3c)>15)return fail("current_state");
    if(Word<unsigned>(bytes,0x44)>15)return fail("next_state");
    return true;
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
