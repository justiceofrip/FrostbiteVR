#include "Bc2ReloadServer.h"
#include "Bc2Profile.h"
#include "fvr/engine/BindingValidation.h"
#include <algorithm>
#include <cmath>
#include <cstring>
namespace fvr::bc2 {
namespace {
constexpr std::array<unsigned,11> sizes{157,57,113,90,33,16,40,31,103,7,3216};
constexpr std::array<std::uint64_t,11> hashes{0xa40d7b5abaf0cc9full,0x4d7009269d3d2debull,0x75970d8adf3ee4a9ull,
    0xec36384e07ae97daull,0x1d92659102bde8aeull,0x76bf142fd220e373ull,0xf7f08757d94c2ebcull,
    0x07b858cae5835e32ull,0x552e759b167c01bbull,0xc605056b535108f3ull,0x885fa17a32cef9b0ull};
std::uint64_t Hash(std::span<const std::byte> bytes){std::uint64_t h=14695981039346656037ull;for(auto b:bytes){h^=std::to_integer<unsigned char>(b);h*=1099511628211ull;}return h;}
template<class T>T Word(const std::byte* b,std::size_t at){T out{};std::memcpy(&out,b+at,sizeof(out));return out;}
std::optional<std::size_t> Offset(std::span<const std::byte> b,const engine::PeImage& pe,unsigned rva,std::size_t size){
    for(const auto& s:pe.sections)if((s.flags&0x20000000)&&rva>=s.rva){const auto delta=std::uint64_t(rva)-s.rva,at=std::uint64_t(s.rawOffset)+delta;
        if(delta<=s.rawSize&&size<=s.rawSize-delta&&at<=b.size()&&size<=b.size()-at)return std::size_t(at);}return {};
}
std::optional<unsigned> Find(std::span<const std::byte> b,const engine::PeImage& pe,const char* text){
    const auto p=engine::ParsePattern(text);if(!p)return {};std::optional<unsigned> result;
    for(const auto& s:pe.sections)if((s.flags&0x20000000)&&s.rawOffset<=b.size()&&s.rawSize<=b.size()-s.rawOffset&&p->size()<=s.rawSize)
        for(std::size_t at=0;at<=s.rawSize-p->size();++at){bool same=true;for(std::size_t n=0;n<p->size();++n)
            if(!(*p)[n].wildcard&&std::to_integer<unsigned char>(b[s.rawOffset+at+n])!=(*p)[n].value){same=false;break;}
            if(same){if(result)return {};result=s.rva+unsigned(at);}}
    return result;
}
struct Reader {
    const ReloadStateMemory& m;
    bool Read(std::uint64_t at,void* out,std::size_t size)const noexcept{return m.read&&at>=0x10000&&size&&size<=4096&&at+size<=UINT32_MAX&&m.read(m.context,unsigned(at),out,size);}
    template<class T>bool Get(std::uint64_t at,T& value)const noexcept{return Read(at,&value,sizeof(value));}
    bool Eq(std::uint64_t at,unsigned wanted)const noexcept{unsigned value=0;return Get(at,value)&&value==wanted;}
    bool Type(unsigned at,const char* name)const noexcept{return m.type&&at>=0x10000&&m.type(m.context,at,name);}
};
bool Valid(const ReloadServerBinding& b,unsigned base,const ReloadStateSnapshot& s){
    const auto& o=s.owner;
    return base&&base==b.preferredBase&&std::uint64_t(base)+b.imageSize<=UINT32_MAX&&s.sequence&&s.observedNs>0&&
        o.player>=0x10000&&o.soldier>=0x10000&&o.soldier<=UINT32_MAX-4&&o.weak>=0x10000&&o.weapon>=0x10000&&
        o.actorGeneration&&o.equipGeneration&&o.space&&s.config.weaponData>=0x10000&&s.config.firingData>=0x10000&&
        s.config.primaryFire>=0x10000&&std::uint64_t(s.config.primaryFire)+0x170==s.config.ammoAddress&&
        std::uint64_t(b.contextRva)+12<=b.imageSize&&std::uint64_t(b.managerVtableRva)+4<=b.imageSize&&b.controlledGetterRva<b.imageSize;
}
bool Scope(const Reader& r,const ReloadServerBinding& b,unsigned base,const ReloadStateSnapshot& s,ReloadServerLinks& v){
    const auto& o=s.owner;
    if(!r.Get(std::uint64_t(o.player)+0xccd,v.clientPlayerFlags)||!(v.clientPlayerFlags&8)||
       !r.Eq(std::uint64_t(o.player)+0xc54,o.weak)||!r.Eq(o.weak,o.soldier+4)||
       !r.Eq(std::uint64_t(o.soldier)+0x220,o.player)||!r.Eq(std::uint64_t(o.player)+0xc68,o.soldier)||
       !r.Get(std::uint64_t(o.soldier)+0x114,v.clientSoldierFlags)||
       !r.Get(std::uint64_t(o.soldier)+((v.clientSoldierFlags&1)?0x24c:0x248),v.clientInventory)||v.clientInventory!=s.inventory||
       !r.Get(std::uint64_t(o.soldier)+0x260,v.clientBegin)||!r.Get(std::uint64_t(o.soldier)+0x264,v.clientEnd)||
       v.clientBegin<0x10000||v.clientEnd<=v.clientBegin||v.clientEnd-v.clientBegin>256||(v.clientEnd-v.clientBegin)%4||
       !r.Read(v.clientBegin,v.clientItems.data(),v.clientEnd-v.clientBegin)||
       !r.Get(std::uint64_t(v.clientInventory)+0x14c,v.clientSlot)||v.clientSlot!=s.selectedSlot||v.clientSlot>=(v.clientEnd-v.clientBegin)/4||
       v.clientItems[v.clientSlot]!=o.weapon||std::count(v.clientItems.begin(),v.clientItems.end(),o.weapon)!=1||
       !r.Eq(std::uint64_t(o.weapon)+4,s.config.weaponData)||!r.Eq(std::uint64_t(s.config.weaponData)+0x98,s.config.firingData)||
       !r.Eq(std::uint64_t(s.config.firingData)+0x40,s.config.primaryFire)||
       !r.Eq(std::uint64_t(s.config.primaryFire)+0x24,unsigned(s.config.fireLogicType))||!r.Eq(std::uint64_t(s.config.primaryFire)+0x20,unsigned(s.config.reloadType))||
       !r.Get(std::uint64_t(base)+b.contextRva+8,v.manager)||!r.Eq(v.manager,base+b.managerVtableRva)||
       !r.Get(std::uint64_t(o.player)+0x154,v.playerId)||!r.Get(std::uint64_t(v.manager)+4,v.capacity)||!v.capacity||v.capacity>256||v.playerId>=v.capacity||
       !r.Get(std::uint64_t(v.manager)+0x6c,v.players)||v.players<0x10000||std::uint64_t(v.players)+v.capacity*4>UINT32_MAX||
       !r.Get(std::uint64_t(v.players)+v.playerId*4,v.player)||!r.Eq(std::uint64_t(v.player)+0x154,v.playerId)||
       !r.Get(v.player,v.playerTable)||v.playerTable<base||std::uint64_t(v.playerTable)+0x34>std::uint64_t(base)+b.imageSize||
       !r.Eq(std::uint64_t(v.playerTable)+0x30,base+b.controlledGetterRva)||
       !r.Get(std::uint64_t(v.player)+0xc3c,v.soldier)||!r.Eq(std::uint64_t(v.player)+0xc6c,v.soldier)||
       !r.Get(v.soldier,v.soldierTable)||!r.Get(std::uint64_t(v.soldier)+0xc,v.soldierData)||v.soldierData<0x10000||!r.Eq(std::uint64_t(o.soldier)+0xc,v.soldierData)||
       !r.Eq(std::uint64_t(v.soldier)+0x220,v.player)||!r.Get(std::uint64_t(v.soldier)+0x2b4,v.inventory)||
       !r.Get(std::uint64_t(v.inventory)+4,v.inventoryData)||!r.Get(std::uint64_t(v.soldier)+0x2b8,v.begin)||!r.Get(std::uint64_t(v.soldier)+0x2bc,v.end)||
       v.begin<0x10000||v.end<=v.begin||v.end-v.begin>256||(v.end-v.begin)%4||!r.Read(v.begin,v.items.data(),v.end-v.begin)||
       !r.Get(std::uint64_t(v.inventory)+0x14c,v.slot)||v.slot!=s.selectedSlot||v.slot>=(v.end-v.begin)/4)return false;
    v.item=v.items[v.slot];
    return v.item>=0x10000&&std::count(v.items.begin(),v.items.end(),v.item)==1&&r.Eq(std::uint64_t(v.item)+4,s.config.weaponData)&&
        r.Get(std::uint64_t(v.item)+0xc,v.effects)&&r.Get(v.effects,v.effectsTable)&&r.Eq(std::uint64_t(v.effects)+0x10,s.config.firingData)&&
        r.Eq(std::uint64_t(v.effects)+0x128,v.player)&&r.Get(std::uint64_t(v.effects)+0xd4,v.callback)&&r.Eq(std::uint64_t(v.callback)+4,s.config.firingData)&&
        r.Get(std::uint64_t(v.item)+0x10,v.firing)&&v.firing>=0x10000&&v.firing!=s.branches[0].address&&v.firing!=s.branches[1].address;
}
std::optional<ReloadFiringObservation> State(const Reader& r,const ReloadStateBinding& b,unsigned base,const ReloadServerSnapshot& s,ReloadServerBoundaryDiagnostic* diagnostic=nullptr){
    std::array<std::byte,0xb0> raw{},again{};
    const auto fail=[&](unsigned why)->std::optional<ReloadFiringObservation>{if(diagnostic)diagnostic->stateFailure=why;return {};};
    if(!r.Read(s.links.firing,raw.data(),raw.size()))return fail(1);
    if(!r.Read(s.links.firing,again.data(),again.size()))return fail(2);
    if(raw!=again){if(diagnostic){const auto offset=unsigned(std::mismatch(raw.begin(),raw.end(),again.begin()).first-raw.begin());
        diagnostic->changedOffset=offset;diagnostic->changedBefore=std::to_integer<std::uint8_t>(raw[offset]);diagnostic->changedAfter=std::to_integer<std::uint8_t>(again[offset]);}return fail(3);}
    if(Word<unsigned>(raw.data(),0)!=base+b.firingVtableRva||Word<unsigned>(raw.data(),8)!=s.client.config.firingData||Word<unsigned>(raw.data(),12)!=s.client.config.ammoAddress)return fail(4);
    ReloadFiringObservation out;out.address=s.links.firing;out.wrapperOffset=0x10;
    out.currentState=Word<unsigned>(raw.data(),0x3c);out.previousState=Word<unsigned>(raw.data(),0x40);out.nextState=Word<unsigned>(raw.data(),0x44);
    out.phaseTimer=Word<float>(raw.data(),0x50);out.loaded=Word<int>(raw.data(),0x7c);out.reserve=Word<int>(raw.data(),0x80);out.flagsA8=Word<unsigned char>(raw.data(),0xa8);
    if(out.currentState>15||out.previousState>15||out.nextState>15||!std::isfinite(out.phaseTimer)||std::abs(out.phaseTimer)>1000000||out.loaded< -1||out.loaded>1000000||out.reserve< -1||out.reserve>1000000)return fail(5);
    return out;
}
}
std::optional<ReloadServerBinding> DiscoverReloadServer(std::span<const std::byte> bytes,const engine::PeImage& pe){
    const auto fire=DiscoverFireOrigin(bytes,pe);if(!fire)return {};
    const auto count=Find(bytes,pe,"8B 81 BC 02 00 00 2B 81 B8 02 00 00 C1 F8 02 C3");
    const auto getter=Find(bytes,pe,"8B 91 BC 02 00 00 2B 91 B8 02 00 00 8B 44 24 04 C1 FA 02 3B C2 73 0C 8B 89 B8 02 00 00 8B 04 81 C2 04 00 33 C0 C2 04 00");
    const auto prefix=Find(bytes,pe,"55 8B EC 83 E4 F0 83 EC 74 53 8B 5D 08 56 8B F1 57 8B CB E8 ?? ?? ?? ?? D9 9E 9C 03 00 00");
    if(!count||!getter||!prefix)return {};
    const auto at=Offset(bytes,pe,*prefix,sizes[8]);if(!at)return {};
    const auto target=[&](unsigned delta)->std::optional<unsigned>{if(bytes[*at+delta]!=std::byte{0xe8})return {};
        const auto rva=std::int64_t(*prefix)+delta+5+Word<std::int32_t>(bytes.data()+*at,delta+1);if(rva<0||rva>UINT32_MAX)return {};return unsigned(rva);};
    const auto slot=target(0x24);if(!slot||target(0x45)!=slot)return {};
    const auto nt=Word<unsigned>(bytes.data(),0x3c);ReloadServerBinding out;out.preferredBase=Word<unsigned>(bytes.data(),nt+24+28);out.imageSize=pe.imageSize;
    out.contextRva=fire->serverContext;out.managerVtableRva=fire->serverManagerVtable;out.controlledGetterRva=fire->serverControlledGetter;
    const std::array<unsigned,11> rvas{fire->serverContextGetter,fire->serverManagerConstructor,fire->serverPlayerCreate,fire->playerConstructor,
        fire->serverControlledGetter,*count,*getter,fire->serverPlayerConstructor,*prefix,*slot,fire->serverShoot};
    for(unsigned n=0;n<rvas.size();++n){const auto pos=Offset(bytes,pe,rvas[n],sizes[n]);if(!pos||Hash(bytes.subspan(*pos,sizes[n]))!=hashes[n])return {};
        out.code[n]={rvas[n],sizes[n],hashes[n]};}
    return out;
}
bool ValidateReloadServerLive(const ReloadStateMemory& m,const ReloadServerBinding& b,unsigned base)noexcept{
    if(!m.read||base!=b.preferredBase||!base||std::uint64_t(base)+b.imageSize>UINT32_MAX||
       std::uint64_t(b.contextRva)+12>b.imageSize||std::uint64_t(b.managerVtableRva)+4>b.imageSize||b.controlledGetterRva>=b.imageSize)return false;
    const Reader r{m};std::array<std::byte,4096> buffer{};
    for(unsigned n=0;n<b.code.size();++n){const auto& proof=b.code[n];if(proof.size!=sizes[n]||proof.fingerprint!=hashes[n]||
        std::uint64_t(proof.rva)+proof.size>b.imageSize||!r.Read(std::uint64_t(base)+proof.rva,buffer.data(),proof.size)||Hash(std::span(buffer).first(proof.size))!=proof.fingerprint)return false;}
    // Metadata is decoded from the same inspected proof, not trusted independently.
    unsigned context=0,manager=0;
    return r.Get(std::uint64_t(base)+b.code[0].rva+0x1c,context)&&context==base+b.contextRva&&
        r.Get(std::uint64_t(base)+b.code[1].rva+0x2d,manager)&&manager==base+b.managerVtableRva&&b.code[4].rva==b.controlledGetterRva;
}
std::optional<ReloadServerSnapshot> ReadReloadServerState(const ReloadStateMemory& m,const ReloadServerBinding& b,
    const ReloadStateBinding& state,unsigned base,const ReloadStateSnapshot& client)noexcept{
    if(!Valid(b,base,client)||state.preferredBase!=base||std::uint64_t(state.firingVtableRva)+4>state.imageSize)return {};
    Reader r{m};ReloadServerSnapshot out;out.client=client;
    if(!Scope(r,b,base,client,out.links)||!r.Type(out.links.soldier,"ServerSoldierEntity")||
       !r.Type(out.links.inventoryData,"WeaponSwitchingData")||!r.Type(client.config.weaponData,"SoldierWeaponData")||
       !r.Type(out.links.effects,"ServerWeaponFiringEffects"))return {};
    const auto sampled=State(r,state,base,out);ReloadServerLinks again{};
    if(!sampled||!Scope(r,b,base,client,again)||again!=out.links)return {};out.state=*sampled;return out;
}
bool ReadReloadServerOwner(const ReloadStateMemory& m,const ReloadServerBinding& b,
    const ReloadStateBinding& state,unsigned base,const ReloadServerSnapshot& snapshot,std::int64_t deadline,std::int64_t now,ReloadServerBoundaryDiagnostic* diagnostic)noexcept{
    if(diagnostic){*diagnostic={};diagnostic->expectedSoldierFlags=snapshot.links.clientSoldierFlags;}
    const auto fail=[&](ReloadServerBoundaryFailure reason){if(diagnostic)diagnostic->failure=reason;return false;};
    if(!Valid(b,base,snapshot.client)||state.preferredBase!=base||std::uint64_t(state.firingVtableRva)+4>state.imageSize||
       now<snapshot.client.observedNs||deadline<=snapshot.client.observedNs||deadline-snapshot.client.observedNs>200000000||now>=deadline)return fail(ReloadServerBoundaryFailure::Lease);
    Reader r{m};ReloadServerLinks before{},after{};
    if(!Scope(r,b,base,snapshot.client,before))return fail(ReloadServerBoundaryFailure::ScopeBefore);
    if(diagnostic){diagnostic->observedSoldierFlags=before.clientSoldierFlags;
        auto same=before;same.clientSoldierFlags=snapshot.links.clientSoldierFlags;
        diagnostic->differsOnlySoldierFlags=before!=snapshot.links&&same==snapshot.links;}
    // Same published phase-only allowance as the full boundary reader. Both
    // scope reads remain exact, including all flags and bounded item arrays.
    auto leased=before;
    leased.clientSoldierFlags^=std::uint8_t((before.clientSoldierFlags^snapshot.links.clientSoldierFlags)&0x10);
    if(leased!=snapshot.links)return fail(ReloadServerBoundaryFailure::PublishedLinks);
    const auto identity=[&]{return r.Eq(before.firing,base+state.firingVtableRva)&&
        r.Eq(std::uint64_t(before.firing)+8,snapshot.client.config.firingData)&&
        r.Eq(std::uint64_t(before.firing)+12,snapshot.client.config.ammoAddress);};
    if(!identity()){if(diagnostic)diagnostic->stateFailure=4;return fail(ReloadServerBoundaryFailure::State);}
    if(!Scope(r,b,base,snapshot.client,after))return fail(ReloadServerBoundaryFailure::ScopeAfter);
    if(diagnostic){diagnostic->afterSoldierFlags=after.clientSoldierFlags;auto same=after;same.clientSoldierFlags=before.clientSoldierFlags;
        diagnostic->changedOnlySoldierFlags=before!=after&&same==before;}
    if(after!=before)return fail(ReloadServerBoundaryFailure::ChangedLinks);
    if(!identity()){if(diagnostic)diagnostic->stateFailure=4;return fail(ReloadServerBoundaryFailure::State);}
    return true;
}
std::optional<ReloadFiringObservation> ReadReloadServerBoundary(const ReloadStateMemory& m,const ReloadServerBinding& b,
    const ReloadStateBinding& state,unsigned base,const ReloadServerSnapshot& snapshot,std::int64_t deadline,std::int64_t now,ReloadServerBoundaryDiagnostic* diagnostic)noexcept{
    if(diagnostic){*diagnostic={};diagnostic->expectedSoldierFlags=snapshot.links.clientSoldierFlags;}
    const auto fail=[&](ReloadServerBoundaryFailure reason)->std::optional<ReloadFiringObservation>{if(diagnostic)diagnostic->failure=reason;return {};};
    if(!Valid(b,base,snapshot.client)||state.preferredBase!=base||std::uint64_t(state.firingVtableRva)+4>state.imageSize||
       now<snapshot.client.observedNs||deadline<=snapshot.client.observedNs||deadline-snapshot.client.observedNs>200000000||now>=deadline)return fail(ReloadServerBoundaryFailure::Lease);
    Reader r{m};ReloadServerLinks before{},after{};
    if(!Scope(r,b,base,snapshot.client,before))return fail(ReloadServerBoundaryFailure::ScopeBefore);
    if(diagnostic){diagnostic->observedSoldierFlags=before.clientSoldierFlags;
        auto same=before;same.clientSoldierFlags=snapshot.links.clientSoldierFlags;
        diagnostic->differsOnlySoldierFlags=before!=snapshot.links&&same==snapshot.links;}
    // The +40 client update temporarily sets soldier flag 0x10 (11 -> 27 in
    // native175727). It does not select an inventory; bit 0 does. Permit only
    // that phase difference against the published lease, never a pointer,
    // inventory, other flag, or within-read race. Both Scope reads stay exact.
    auto leased=before;
    leased.clientSoldierFlags^=std::uint8_t((before.clientSoldierFlags^snapshot.links.clientSoldierFlags)&0x10);
    if(leased!=snapshot.links)return fail(ReloadServerBoundaryFailure::PublishedLinks);
    const auto sampled=State(r,state,base,snapshot,diagnostic);
    if(!sampled)return fail(ReloadServerBoundaryFailure::State);
    if(!Scope(r,b,base,snapshot.client,after))return fail(ReloadServerBoundaryFailure::ScopeAfter);
    if(diagnostic){diagnostic->afterSoldierFlags=after.clientSoldierFlags;auto same=after;same.clientSoldierFlags=before.clientSoldierFlags;
        diagnostic->changedOnlySoldierFlags=before!=after&&same==before;}
    if(after!=before)return fail(ReloadServerBoundaryFailure::ChangedLinks);return sampled;
}
}
