#include "Bc2OpticFilter.h"
#include <algorithm>
#include <cmath>
#include <cstring>
namespace fvr::bc2 { namespace {
template<class T>T Word(const std::byte* p){T t{};std::memcpy(&t,p,sizeof(t));return t;}
std::optional<std::size_t> Offset(const OpticFilterBinding& b,std::uint32_t address,std::size_t size){
    const auto& i=b.image;if(address<i.preferredBase)return {};
    const auto rva=std::uint64_t(address)-i.preferredBase;
    for(const auto& s:i.pe.sections)if(rva>=s.rva){const auto delta=rva-s.rva,at=std::uint64_t(s.rawOffset)+delta;
        if(delta<=s.rawSize&&size<=s.rawSize-delta&&at<=i.executable.size()&&size<=i.executable.size()-at)return std::size_t(at);}return {};
}
struct Reader {
    const ReloadStateMemory& m;const OpticFilterBinding& b;unsigned calls=0,bytes=0;bool failed=false;
    bool Read(std::uint64_t a,void* out,std::size_t n){
        if(!m.read||a<0x10000||!n||n>4096||a+n>UINT32_MAX||calls>=1024||n>65536-bytes){failed=true;return false;}
        ++calls;bytes+=unsigned(n);if(!m.read(m.context,unsigned(a),out,n)){failed=true;return false;}return true;
    }
    template<class T>bool Get(std::uint64_t a,T& out){return Read(a,&out,sizeof(out));}
    bool Eq(std::uint64_t a,unsigned wanted){unsigned v=0;return Get(a,v)&&v==wanted;}
    bool File(unsigned a,std::size_t n){std::array<std::byte,2048> copy{};const auto off=Offset(b,a,n);
        return off&&n<=copy.size()&&Read(a,copy.data(),n)&&!std::memcmp(copy.data(),b.image.executable.data()+*off,n);}
    bool Name(unsigned address,const char* name){const auto n=std::strlen(name)+1;const auto off=Offset(b,address,n);
        return off&&File(address,n)&&!std::memcmp(b.image.executable.data()+*off,name,n);}
};
struct OwnerRead {
    unsigned inventory=0,begin=0,end=0,slot=0,data=0;unsigned char playerFlags=0,soldierFlags=0;
    std::array<unsigned,64> items{};std::array<unsigned,4> states{};
    bool operator==(const OwnerRead&)const=default;
};
bool Owner(Reader& r,const SelectedMeshesSnapshot& s,OwnerRead& out){const auto& o=s.owner;
    if(!r.Get(std::uint64_t(o.player)+0xccd,out.playerFlags)||!(out.playerFlags&8)||!r.Eq(std::uint64_t(o.player)+0xc54,o.weak)||
       !r.Eq(o.weak,o.soldier+4)||!r.Eq(std::uint64_t(o.player)+0xc68,o.soldier)||!r.Eq(std::uint64_t(o.soldier)+0x220,o.player)||
       !r.Get(std::uint64_t(o.soldier)+0x114,out.soldierFlags)||!r.Get(std::uint64_t(o.soldier)+((out.soldierFlags&1)?0x24c:0x248),out.inventory)||out.inventory!=s.inventory||
       !r.Get(std::uint64_t(out.inventory)+0x14c,out.slot)||out.slot!=s.selectedSlot||!r.Get(std::uint64_t(o.soldier)+0x260,out.begin)||!r.Get(std::uint64_t(o.soldier)+0x264,out.end)||
       out.begin<0x10000||out.end<=out.begin||out.end-out.begin>256||(out.end-out.begin)%4||out.slot>=(out.end-out.begin)/4||
       !r.Read(out.begin,out.items.data(),out.end-out.begin)||out.items[out.slot]!=o.weapon||std::count(out.items.begin(),out.items.end(),o.weapon)!=1||
       !r.Get(std::uint64_t(o.weapon)+4,out.data)||out.data!=s.weaponData||!r.Read(std::uint64_t(out.data)+0x88,out.states.data(),16)||
       out.states[1]!=s.stateTypeInfo||out.states[2]<0x10000||std::uint64_t(out.states[2])+unsigned(s.stateCount)*0xc8!=out.states[3])return false;
    for(unsigned n=0;n<s.stateCount;++n)if(s.states[n].state!=out.states[2]+n*0xc8)return false;return true;
}
bool StateMetadata(Reader& r,unsigned type){
    unsigned metadata=0,name=0,fields=0;unsigned short flags=0,size=0;unsigned char count=0;
    // TypeInfo storage is initialized by native registration; compare its
    // immutable pointed-to descriptor, not the uninitialized on-disk storage.
    if(!r.Get(std::uint64_t(type)+4,metadata)||!r.File(metadata,28)||!r.Get(metadata,name)||!r.Name(name,"WeaponStateData")||
       !r.Get(std::uint64_t(metadata)+4,flags)||flags!=0x29||!r.Get(std::uint64_t(metadata)+6,size)||size!=0xc8||
       !r.Get(std::uint64_t(metadata)+13,count)||count>64||!r.Get(std::uint64_t(metadata)+24,fields))return false;
    unsigned foundZoom=0,foundNormal=0,foundMesh=0;
    const auto scope=r.b.image.preferredBase+r.b.candidates.scopeFilter.typeInfo;
    for(unsigned n=0;n<count;++n){std::array<unsigned,6> f{};const auto a=std::uint64_t(fields)+n*24;
        if(a+24>UINT32_MAX||!r.Read(a,f.data(),24)||!r.File(unsigned(a),24))return false;
        if(r.Name(f[0],"ZoomedScopeFilter")){if(f[4]!=0xac||f[2]!=scope)return false;++foundZoom;}
        if(r.Name(f[0],"NonZoomedScopeFilter")){if(f[4]!=0xbc||f[2]!=scope)return false;++foundNormal;}
        if(r.Name(f[0],"MeshZoom1p")){if(f[4]!=0x94)return false;++foundMesh;}
    }return foundZoom==1&&foundNormal==1&&foundMesh==1;
}
bool ReadSample(Reader& r,const SelectedMeshesSnapshot& s,OpticFilterCall c,OpticFilterSample& out){
    out.owner=s.owner;out.sequence=s.sequence;out.observedNs=s.observedNs;out.deadlineNs=s.deadlineNs;out.call=c;out.assetName=s.weaponName;out.stateCount=s.stateCount;
    unsigned getter=0;std::array<std::byte,6> code{};
    if(!r.Get(c.filter,out.filterTable)||!r.Get(std::uint64_t(out.filterTable)+8,getter)||!r.File(getter,6)||!r.Read(getter,code.data(),6)||
       code[0]!=std::byte{0xb8}||code[5]!=std::byte{0xc3})return false;
    out.filterType=Word<unsigned>(code.data()+1);
    if(out.filterType!=r.b.image.preferredBase+r.b.candidates.sniperFilter.typeInfo)return false;
    if(!r.Read(std::uint64_t(c.filter)+0x20,out.scissor.data(),16)||!r.Read(std::uint64_t(c.filter)+0x60,out.blurCenter.data(),8)||!r.Get(std::uint64_t(c.filter)+0xc0,out.blurScale))return false;
    for(float v:out.scissor)if(!std::isfinite(v))return false;for(float v:out.blurCenter)if(!std::isfinite(v))return false;if(!std::isfinite(out.blurScale))return false;
    for(unsigned n=0;n<3;++n)if(!r.Get(std::uint64_t(c.renderer)+std::array<unsigned,3>{0xc8,0xd8,0xdc}[n],out.rendererWrappers[n]))return false;
    for(unsigned n=0;n<s.stateCount;++n){const auto state=s.states[n].state;out.stateAddresses[n]=state;
        if(!r.Get(std::uint64_t(state)+0xac,out.zoomedFilters[n])||!r.Get(std::uint64_t(state)+0xbc,out.nonZoomedFilters[n])||!r.Get(std::uint64_t(state)+0x94,out.zoomMeshes[n]))return false;
        if(out.zoomedFilters[n]==c.filter)out.zoomedMatchMask|=std::uint8_t(1u<<n);
        if(out.nonZoomedFilters[n]==c.filter)out.nonZoomedMatchMask|=std::uint8_t(1u<<n);
    }return true;
}
}
std::optional<OpticFilterBinding> DiscoverOpticFilter(std::span<const std::byte> bytes,const engine::PeImage& pe){
    const auto optics=DiscoverOpticObservation(bytes,pe);const auto selected=DiscoverSelectedMeshes1p(bytes,pe);
    if(!optics||!selected)return {};return OpticFilterBinding{*selected,*optics};
}
bool ValidateOpticFilterLive(const ReloadStateMemory& memory,const OpticFilterBinding& b,unsigned base)noexcept{
    if(base!=b.image.preferredBase||!base)return false;Reader r{memory,b};
    if(!r.File(base+b.candidates.filterRenderer,0x20a)||!r.File(base+b.candidates.filterCaller-0x6f,0x74))return false;
    for(const auto& t:{b.candidates.scopeFilter,b.candidates.sniperFilter})
        if(!r.File(base+t.registration,30)||!r.File(base+t.metadata,24)||!r.File(base+t.fields,std::size_t(t.fieldCount)*24))return false;
    return true;
}
OpticFilterRead ReadOpticFilter(const ReloadStateMemory& memory,const OpticFilterBinding& b,const SelectedMeshesSnapshot& s,
    const ReloadStateOwner& owner,OpticFilterCall call,std::int64_t now,bool enabled)noexcept {
    if(!enabled)return {};
    if(!memory.read||s.owner!=owner||!owner.player||!owner.soldier||!owner.weak||!owner.weapon||!owner.actorGeneration||!owner.equipGeneration||!owner.space||
       !s.sequence||!s.stateCount||s.stateCount>8||call.renderer<0x10000||call.filter<0x10000||!b.image.preferredBase)return {OpticFilterStatus::Arguments};
    if(s.observedNs<=0||now<s.observedNs||now>=s.deadlineNs||s.deadlineNs-s.observedNs>250000000)return {OpticFilterStatus::Expired};
    if(call.caller!=b.image.preferredBase+b.candidates.filterReturn)return {OpticFilterStatus::Caller};
    Reader r{memory,b};auto fail=[&](OpticFilterStatus status){return OpticFilterRead{r.failed?OpticFilterStatus::ReadFailure:status,{},r.calls,r.bytes};};
    OwnerRead before{},after{};if(!Owner(r,s,before))return fail(OpticFilterStatus::Owner);
    if(!StateMetadata(r,s.stateTypeInfo))return fail(OpticFilterStatus::Metadata);
    OpticFilterSample first{},second{};if(!ReadSample(r,s,call,first))return fail(OpticFilterStatus::FilterType);
    if(!first.zoomedMatchMask&&!first.nonZoomedMatchMask)return fail(OpticFilterStatus::Unmatched);
    if(!ReadSample(r,s,call,second)||!SameOpticFilterOwner(first,second)||first.rendererWrappers!=second.rendererWrappers||!Owner(r,s,after)||before!=after)return fail(OpticFilterStatus::Changed);
    return {OpticFilterStatus::Observed,first,r.calls,r.bytes};
}
bool SameOpticFilterOwner(const OpticFilterSample& a,const OpticFilterSample& b)noexcept{
    return a.owner==b.owner&&a.sequence==b.sequence&&a.observedNs==b.observedNs&&a.deadlineNs==b.deadlineNs&&
        a.call.renderer==b.call.renderer&&a.call.filter==b.call.filter&&a.call.caller==b.call.caller&&a.filterTable==b.filterTable&&a.filterType==b.filterType&&
        a.scissor==b.scissor&&a.blurCenter==b.blurCenter&&a.blurScale==b.blurScale&&a.stateCount==b.stateCount&&a.stateAddresses==b.stateAddresses&&
        a.zoomedFilters==b.zoomedFilters&&a.nonZoomedFilters==b.nonZoomedFilters&&a.zoomMeshes==b.zoomMeshes&&a.assetName==b.assetName&&
        a.zoomedMatchMask==b.zoomedMatchMask&&a.nonZoomedMatchMask==b.nonZoomedMatchMask;
}
}
