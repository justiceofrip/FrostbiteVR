#include "Bc2RigWorkerBinding.h"
#include "fvr/engine/BindingValidation.h"
#include <algorithm>
#include <cstring>
namespace fvr::bc2 {namespace {
constexpr std::array<unsigned,6> sizes{0xe09,0x8e,0x59,0x66,0x54,0x34};
constexpr std::array<std::uint64_t,6> hashes{0x66f00c79c72351bcull,0xbf7f3963befd8f93ull,0x8ef7faf16d901a04ull,0x633a9b454df37912ull,0x5641970e756d092dull,0x366dee56c2d309bcull};
constexpr std::array<unsigned,3> viewSizes{0x305,0x8d,0x111};
constexpr std::array<std::uint64_t,3> viewHashes{0x4ec4581859653abfull,0x18e4bbe99a027680ull,0x56be655e8e43eacaull};
constexpr std::array<std::uint64_t,3> viewTableHashes{0x4fddbc5cb590fcb0ull,0x24f79f2cc322f817ull,0x718b4b568a262480ull};
constexpr std::array<const char*,3> viewSignatures{
    "51 F3 0F 10 05 ?? ?? ?? ?? 8B 44 24 08 53 55 56 8B F1 8B 4C 24 1C C7 06 ?? ?? ?? ?? F3 0F 11 46 04",
    "8B 44 24 14 8B 54 24 0C 56 57 50 8B 44 24 10 8B F1 8B 4C 24 1C 51 52 50 8B CE E8 ?? ?? ?? ?? 8B 4C 24 10 89 8E 70 16 00 00 C7 06",
    "8B 44 24 08 56 57 33 FF 57 57 8B F1 8B 4C 24 14 50 51 8B CE E8 ?? ?? ?? ?? C7 06 ?? ?? ?? ?? 89 BE 70 16 00 00"};
constexpr std::array<const char*,6> signatures{
    "55 8B EC 83 E4 F0 81 EC 24 02 00 00 8D 84 24 A4 01 00 00 8B C8 53 89 8C 24 98 01 00 00 8B 4D 08",
    "53 55 8B 6C 24 0C 83 BD B0 06 00 00 00 8D 9D B0 06 00 00 56 57 89 5C 24 14 75 24",
    "53 56 8B F1 57 8D 7E 10 8B CF E8 ?? ?? ?? ?? 6A 00 68 ?? ?? ?? ?? 8D 5E 14 6A 00",
    "8B 44 24 04 89 01 8B 40 08 89 41 04 8B 40 0C 8B 51 04 53 55 89 41 08 C7 81 5C 02 00 00 FF FF FF FF",
    "53 55 56 33 F6 8D 56 4C 8D 41 0C EB 03 8D 49 00 8B 58 04 2B 18 8B 29 8B 6D 0C C1 FB 02",
    "56 8B F1 E8 ?? ?? ?? ?? 8B 44 24 08 C7 06 ?? ?? ?? ?? C7 86 C0 06 00 00 00 00 00 00 50 8D 8E D0 06 00 00 C7 06 ?? ?? ?? ?? E8 ?? ?? ?? ?? 8B C6 5E C2 04 00"};
template<class T>T Value(const std::byte* p){T v{};std::memcpy(&v,p,sizeof(v));return v;}
std::uint64_t Hash(std::span<const std::byte> b){std::uint64_t h=14695981039346656037ull;for(auto c:b){h^=std::to_integer<unsigned char>(c);h*=1099511628211ull;}return h;}
std::optional<std::size_t> Offset(std::span<const std::byte> b,const engine::PeImage& pe,std::uint32_t rva,std::size_t n,bool code=false){
    for(const auto& s:pe.sections)if(rva>=s.rva&&(!code||(s.flags&0x20000000))){const auto d=std::uint64_t(rva)-s.rva,at=std::uint64_t(s.rawOffset)+d;
        if(d<=s.rawSize&&n<=s.rawSize-d&&at<=b.size()&&n<=b.size()-at)return std::size_t(at);}return {};
}
std::optional<std::uint32_t> Unique(std::span<const std::byte> b,const engine::PeImage& pe,const char* text){
    const auto p=engine::ParsePattern(text);if(!p)return {};std::optional<std::uint32_t> found;
    for(const auto& s:pe.sections)if((s.flags&0x20000000)&&s.rawOffset<=b.size()&&s.rawSize<=b.size()-s.rawOffset&&p->size()<=s.rawSize)
        for(std::size_t n=0;n<=s.rawSize-p->size();++n){bool match=true;for(std::size_t i=0;i<p->size();++i)if(!(*p)[i].wildcard&&std::to_integer<unsigned char>(b[s.rawOffset+n+i])!=(*p)[i].value){match=false;break;}
            if(match){if(found)return {};found=s.rva+std::uint32_t(n);}}return found;
}
struct Reader {
    const ReloadStateMemory& memory;bool failed=false;
    bool Read(std::uint64_t at,void* dst,std::size_t n){if(at<0x10000||!n||n>4096||at+n>UINT32_MAX||!memory.read||!memory.read(memory.context,std::uint32_t(at),dst,n)){failed=true;return false;}return true;}
    template<class T>bool Get(std::uint64_t at,T& value){return Read(at,&value,sizeof(value));}
};
RigWorkerStatus One(Reader& r,const RigWorkerBinding& b,const RigWorkerCall& call,const ReloadProducerOwner& owner,RigWorkerIdentity& out){
    out.call=call;out.owner=owner;std::uint32_t table=0,workspace=0;
    if(!r.Get(call.renderer,table)||!r.Get(std::uint64_t(call.renderer)+0x6b0,workspace))return RigWorkerStatus::ReadFailure;
    out.observedVtables[0]=table;
    if(table!=b.rendererTable||workspace!=call.workspace)return RigWorkerStatus::WrongType;
    if(!r.Get(call.workspace,out.job)||!r.Get(std::uint64_t(call.workspace)+4,out.batch)||!r.Get(std::uint64_t(call.workspace)+8,out.arena)||
       !r.Read(out.job,out.jobHeader.data(),16))return RigWorkerStatus::ReadFailure;
    if(out.jobHeader[2]!=out.batch||!out.jobHeader[0]||out.jobHeader[0]>4096||out.jobHeader[1]<0x10000||
       std::uint64_t(out.jobHeader[1])+out.jobHeader[0]*16>UINT32_MAX)return RigWorkerStatus::Bounds;
    // Exact invocation row is one of this job packet's bounded 16-byte entries.
    unsigned invocationMatches=0;
    for(unsigned n=0;n<out.jobHeader[0];++n){std::array<std::uint32_t,4> entry{};if(!r.Read(std::uint64_t(out.jobHeader[1])+n*16,entry.data(),16))return RigWorkerStatus::ReadFailure;
        if(entry[0]==call.unusedArg&&entry[1]==call.rowCount&&entry[2]==call.rows)++invocationMatches;
    }
    if(invocationMatches!=1)return RigWorkerStatus::Bounds;
    std::uint32_t arena=0;
    if(!r.Get(out.batch,out.view)||!r.Get(std::uint64_t(out.batch)+12,arena)||!r.Get(std::uint64_t(out.batch)+0x24,out.viewCount)||
       !r.Get(out.arena,out.arenaBase)||!r.Get(std::uint64_t(out.arena)+8,out.arenaAlignment))return RigWorkerStatus::ReadFailure;
    if(arena!=out.arena||out.arenaBase<0x10000||out.arenaAlignment<16||out.arenaAlignment>4096||(out.arenaAlignment&(out.arenaAlignment-1))||!out.viewCount||out.viewCount>16)return RigWorkerStatus::Bounds;
    if(!r.Read(std::uint64_t(out.batch)+0x28,out.views.data(),out.viewCount*4))return RigWorkerStatus::ReadFailure;
    for(unsigned i=0;i<out.viewCount;++i)if(out.views[i]<0x10000||std::count(out.views.begin(),out.views.begin()+out.viewCount,out.views[i])!=1)return RigWorkerStatus::Bounds;
    if(!r.Get(out.view,out.observedVtables[1])||out.observedVtables[1]!=b.scene.view)return r.failed?RigWorkerStatus::ReadFailure:RigWorkerStatus::WrongType;
    if(!r.Get(std::uint64_t(out.view)+0x70,out.request)||!r.Get(out.request,out.observedVtables[2])||out.observedVtables[2]!=b.scene.request)return r.failed?RigWorkerStatus::ReadFailure:RigWorkerStatus::WrongType;
    if(!r.Get(std::uint64_t(out.request)+8,out.world)||!r.Get(out.world,out.observedVtables[3])||out.observedVtables[3]!=b.scene.world)return r.failed?RigWorkerStatus::ReadFailure:RigWorkerStatus::WrongType;
    if(!r.Get(std::uint64_t(out.world)+0x88,out.nativeFrame)||!r.Get(std::uint64_t(out.request)+0xc4,out.requestState))return RigWorkerStatus::ReadFailure;
    for(unsigned i=0;i<out.viewCount;++i){out.rejectedViewIndex=i;
        if(!r.Get(out.views[i],out.viewTables[i]))return RigWorkerStatus::ReadFailure;
        const bool child=b.derivedViewTable&&out.viewTables[i]==b.derivedViewTable;
        if(out.viewTables[i]!=b.scene.view&&!child)return RigWorkerStatus::WrongType;
        if(!r.Get(std::uint64_t(out.views[i])+0x70,out.viewRequests[i]))return RigWorkerStatus::ReadFailure;
        if(out.viewRequests[i]!=out.request)return RigWorkerStatus::WrongType;
        if(child){if(!r.Get(std::uint64_t(out.views[i])+0x1670,out.viewParents[i]))return RigWorkerStatus::ReadFailure;
            if(out.viewParents[i]!=out.view)return RigWorkerStatus::WrongType;}
    }
    out.rejectedViewIndex=UINT32_MAX;
    unsigned ownerMatches=0;
    for(unsigned i=0;i<call.rowCount;++i){std::array<std::uint32_t,8> row{};if(!r.Read(std::uint64_t(call.rows)+i*32,row.data(),32))return RigWorkerStatus::ReadFailure;
        if(row[0]==owner.actor){++ownerMatches;out.selectedRowIndex=i;out.selectedRow=row;out.selectedViewMask=std::uint16_t(row[1]&0xffff);}}
    if(!ownerMatches)return RigWorkerStatus::OwnerMissing;if(ownerMatches!=1)return RigWorkerStatus::OwnerAmbiguous;
    return RigWorkerStatus::Observed;
}
}
std::optional<RigWorkerBinding> DiscoverRigWorker(std::span<const std::byte> bytes,const engine::PeImage& pe,RigWorkerSceneTypes scene){
    if(pe.machine!=0x14c||bytes.size()<64)return {};const auto nt=Value<std::uint32_t>(bytes.data()+0x3c);if(std::uint64_t(nt)+56>bytes.size())return {};
    RigWorkerBinding b;b.base=Value<std::uint32_t>(bytes.data()+nt+52);b.imageSize=pe.imageSize;b.scene=scene;
    if(b.base!=0x400000||!b.imageSize||std::uint64_t(b.base)+b.imageSize>UINT32_MAX)return {};
    for(auto t:{scene.view,scene.request,scene.world})if(t<b.base||!Offset(bytes,pe,t-b.base,12))return {};
    for(unsigned i=0;i<6;++i){const auto rva=Unique(bytes,pe,signatures[i]);if(!rva)return {};const auto off=Offset(bytes,pe,*rva,sizes[i],true);
        if(!off||Hash(bytes.subspan(*off,sizes[i]))!=hashes[i])return {};b.code[i]={*rva,sizes[i],hashes[i]};}
    const auto read=[&](unsigned i,unsigned offset){return Value<std::uint32_t>(bytes.data()+*Offset(bytes,pe,b.code[i].rva+offset,4));};
    b.prepare=b.base+b.code[0].rva;b.workerReturn=b.base+b.code[1].rva+0x75;b.rendererTable=read(5,0x25);
    const auto table=Offset(bytes,pe,b.rendererTable-b.base,0x28);
    if(!table||Value<std::uint32_t>(bytes.data()+*table+0x24)!=b.prepare||read(2,0x12)!=b.base+b.code[1].rva)return {};
    for(unsigned i=0;i<3;++i){const auto rva=Unique(bytes,pe,viewSignatures[i]);if(!rva)return {};const auto off=Offset(bytes,pe,*rva,viewSizes[i],true);
        if(!off||Hash(bytes.subspan(*off,viewSizes[i]))!=viewHashes[i])return {};b.viewCode[i]={*rva,viewSizes[i],viewHashes[i]};}
    const auto viewRead=[&](unsigned i,unsigned offset){return Value<std::uint32_t>(bytes.data()+*Offset(bytes,pe,b.viewCode[i].rva+offset,4));};
    b.commonViewTable=viewRead(0,0x3a);b.derivedViewTable=viewRead(1,0x2b);
    if(viewRead(2,0x1b)!=b.scene.view||
       std::int64_t(b.viewCode[1].rva)+0x1f+std::int32_t(viewRead(1,0x1b))!=b.viewCode[0].rva||
       std::int64_t(b.viewCode[2].rva)+0x19+std::int32_t(viewRead(2,0x15))!=b.viewCode[0].rva)return {};
    const std::array<unsigned,3> viewTables{b.commonViewTable,b.derivedViewTable,b.scene.view};
    std::array<unsigned,3> sharedGetters{};
    for(unsigned i=0;i<viewTables.size();++i){if(viewTables[i]<b.base)return {};const auto at=Offset(bytes,pe,viewTables[i]-b.base,240);
        if(!at||Hash(bytes.subspan(*at,240))!=viewTableHashes[i])return {};
        std::array<unsigned,3> getters{Value<unsigned>(bytes.data()+*at+8),Value<unsigned>(bytes.data()+*at+0x20),Value<unsigned>(bytes.data()+*at+0x9c)};
        if(i&&getters!=sharedGetters)return {};sharedGetters=getters;}
    // Full function hashes above retain four-argument ret16 and the verified
    // virtual +24 call, workspace initializer and finalizer code relationships.
    return b;
}
bool ValidateRigWorkerLive(const ReloadStateMemory& m,const RigWorkerBinding& b,std::uint32_t actualBase)noexcept {
    if(actualBase!=b.base||actualBase!=0x400000||!b.imageSize||!m.read)return false;Reader r{m};std::array<std::byte,4096> code{};
    for(unsigned i=0;i<6;++i){const auto& p=b.code[i];if(p.size!=sizes[i]||p.fingerprint!=hashes[i]||std::uint64_t(p.rva)+p.size>b.imageSize||
        !r.Read(std::uint64_t(actualBase)+p.rva,code.data(),p.size)||Hash({code.data(),p.size})!=p.fingerprint)return false;}
    for(unsigned i=0;i<3;++i){const auto& p=b.viewCode[i];if(p.size!=viewSizes[i]||p.fingerprint!=viewHashes[i]||std::uint64_t(p.rva)+p.size>b.imageSize||
        !r.Read(std::uint64_t(actualBase)+p.rva,code.data(),p.size)||Hash({code.data(),p.size})!=p.fingerprint)return false;}
    const std::array<unsigned,3> tables{b.commonViewTable,b.derivedViewTable,b.scene.view};
    const std::array<unsigned,3> slots{8,0x20,0x9c},getterSizes{4,7,4};
    const std::array<std::array<unsigned char,7>,3> getterBytes{{{0x8b,0x41,0x70,0xc3},{0x8d,0x81,0xf0,4,0,0,0xc3},{0x8b,0x41,0x74,0xc3}}};
    for(unsigned i=0;i<tables.size();++i){if(tables[i]<actualBase||std::uint64_t(tables[i])+240>std::uint64_t(actualBase)+b.imageSize||
        !r.Read(tables[i],code.data(),240)||Hash({code.data(),240})!=viewTableHashes[i])return false;
        for(unsigned j=0;j<slots.size();++j){const auto getter=Value<unsigned>(code.data()+slots[j]);std::array<std::byte,7> observed{};
            if(getter<actualBase||std::uint64_t(getter)+getterSizes[j]>std::uint64_t(actualBase)+b.imageSize||!r.Read(getter,observed.data(),getterSizes[j])||
               std::memcmp(observed.data(),getterBytes[j].data(),getterSizes[j]))return false;}}
    std::uint32_t target=0;return r.Get(std::uint64_t(b.rendererTable)+0x24,target)&&target==b.prepare;
}
RigWorkerRead ReadRigWorker(const ReloadStateMemory& m,const RigWorkerBinding& b,const RigWorkerCall& call,const ReloadProducerOwner& owner)noexcept {
    if(!m.read||!owner.actor||owner.actor>UINT32_MAX||!owner.weak||!owner.weapon||!owner.ownerGeneration||!owner.space||
       !call.rowCount||call.rowCount>1024||call.rows<0x10000||std::uint64_t(call.rows)+call.rowCount*32>UINT32_MAX||call.workspace<0x10000||call.renderer<0x10000)return {};
    if(call.caller!=b.workerReturn)return {RigWorkerStatus::WrongCaller,{}};
    Reader r{m};RigWorkerIdentity first{},again{};auto status=One(r,b,call,owner,first);if(status!=RigWorkerStatus::Observed)return {status,{},first};
    status=One(r,b,call,owner,again);if(status!=RigWorkerStatus::Observed||again!=first)return {r.failed?RigWorkerStatus::ReadFailure:RigWorkerStatus::ChangedDuringRead,{},again};
    return {RigWorkerStatus::Observed,first,first};
}
RigWorkerAssociation AssociateRigWorker(const RigWorkerIdentity& s,RigWorkerViewResolver resolver)noexcept {
    if(!resolver)return {};const auto key=resolver(s.world,s.request,s.view,s.nativeFrame);
    if(!key||key->world!=s.world||key->request!=s.request||key->view!=s.view||key->nativeFrame!=s.nativeFrame||key->eye>1)return {};
    for(unsigned i=0;i<s.viewCount;++i)if(s.views[i]!=s.view){const auto other=resolver(s.world,s.request,s.views[i],s.nativeFrame);
        if(other&&other->world==s.world&&other->request==s.request&&other->view==s.views[i]&&other->nativeFrame==s.nativeFrame)return {RigWorkerStatus::MultipleStereoViews,{}};}
    return {RigWorkerStatus::Observed,key};
}
std::optional<ReloadProducerView> LookupRigWorkerViewLease(const RigWorkerViewLease& lease,unsigned world,unsigned request,unsigned view,unsigned frame,std::int64_t nowNs)noexcept {
    const auto& left=lease.eyes[0];const auto& right=lease.eyes[1];
    if(lease.observedNs<=0||lease.deadlineNs<=lease.observedNs||lease.deadlineNs-lease.observedNs>250000000||nowNs<lease.observedNs||nowNs>=lease.deadlineNs||
       !left.world||!left.request||!left.view||!right.view||left.view==right.view||left.eye!=0||right.eye!=1||
       left.world!=right.world||left.request!=right.request||left.nativeFrame!=right.nativeFrame)return {};
    for(const auto& key:lease.eyes)if(key.world==world&&key.request==request&&key.view==view&&key.nativeFrame==frame)return key;
    return {};
}
RigWorkerViewLease AdvanceRigWorkerViewLease(const RigWorkerViewLease* previous,const RigWorkerViewLease& candidate)noexcept {
    if(!previous)return candidate;
    if(candidate.observedNs<previous->observedNs)return *previous;
    const auto& old=previous->eyes[0];const auto& incoming=candidate.eyes[0];
    if(old.world==incoming.world&&old.request==incoming.request){
        const auto delta=std::uint32_t(incoming.nativeFrame-old.nativeFrame);
        if(!delta){auto retained=*previous;if(candidate.eyes!=previous->eyes)retained.deadlineNs=retained.observedNs;return retained;}
        if(delta>=0x80000000u)return *previous;
    }
    return candidate;
}
}
