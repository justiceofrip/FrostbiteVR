#include "Bc2ReloadFlow.h"
#include "fvr/engine/BindingValidation.h"
#include <cmath>
#include <cstring>
namespace fvr::bc2 {
namespace {
constexpr std::array<unsigned,8> sizes{397,52,757,197,1987,968,276,206};
constexpr std::array<std::uint64_t,8> hashes{0x4a6ecc3d95700d53ull,0xdfc4b050cc5dec21ull,
    0x5daedff1fec9744full,0x1e4427abd9d63fb7ull,0xd1149c25862772d7ull,0x5b3e2b8525dc8c36ull,0xf1240aaf85772b50ull,0xd2217ee6ded18b97ull};
constexpr std::array<const char*,8> patterns{
    "83 EC 08 56 8B F1 D9 46 48 57 8B 7C 24 14 D8 67 18 D9 5C 24 14 D9 EE D9",
    "56 8B F1 8B 4E 14 85 C9 57 8B 7C 24 0C 74 13 8B 54 24 10 D9 47 18 8B 01",
    "83 EC 0C 0F 57 C9 55 8B 6C 24 14 F3 0F 10 55 18 0F 2F D1 56 8B F1 F3 0F 11",
    "51 56 8B F1 83 7E 7C 00 8B 46 08 8B 40 40 57 8B 78 24 75 38 83 BE 80 00 00 00 00 7E 2F D9",
    "55 8B EC 83 E4 F0 81 EC E4 00 00 00 53 56 8B F1 F6 86 14 01 00 00 01 57 74",
    "55 8B EC 83 E4 F0 81 EC 84 00 00 00 53 56 57 8B F9 8B 8F 4C 02 00 00 E8",
    "56 57 8B 7C 24 0C 8A 47 39 8B F1 3A 86 A6 00 00 00 74 24 53 8B 5E 18 3B",
    "8B 51 3C 8B 44 24 04 89 10 8B 51 44 89 50 04 D9 41 50 D9 58 08 D9 41 48"};
constexpr std::array<unsigned,16> stepOffsets{0x3e,0x52,0x13d,0x6ba,0x6ba,0x2bc,0x2e5,0x45d,0x4b5,0x384,0x4ed,0x52a,0x54e,0x5cb,0x601,0x62a};
std::uint64_t Hash(std::span<const std::byte> bytes){std::uint64_t h=14695981039346656037ull;for(auto b:bytes){h^=std::to_integer<unsigned char>(b);h*=1099511628211ull;}return h;}
template<class T>T Value(std::span<const std::byte> bytes,std::size_t at){T value{};std::memcpy(&value,bytes.data()+at,sizeof(value));return value;}
std::optional<std::size_t> Offset(std::span<const std::byte> bytes,const engine::PeImage& pe,unsigned rva,std::size_t size,bool executable=false){
    for(const auto& s:pe.sections)if(rva>=s.rva&&(!executable||(s.flags&0x20000000))){
        const auto delta=std::uint64_t(rva)-s.rva,at=std::uint64_t(s.rawOffset)+delta;
        if(delta<=s.rawSize&&size<=s.rawSize-delta&&at<=bytes.size()&&size<=bytes.size()-at)return std::size_t(at);
    }return {};
}
std::optional<unsigned> Unique(std::span<const std::byte> bytes,const engine::PeImage& pe,const char* text){
    const auto pattern=engine::ParsePattern(text);if(!pattern)return {};
    std::optional<unsigned> result;
    for(const auto& s:pe.sections)if((s.flags&0x20000000)&&s.rawOffset<=bytes.size()&&s.rawSize<=bytes.size()-s.rawOffset){
        // Explicit scan retains duplicate rejection across and within sections.
        if(pattern->size()>s.rawSize)continue;
        for(std::size_t at=0;at<=s.rawSize-pattern->size();++at){bool match=true;
            for(std::size_t n=0;n<pattern->size();++n)if(!(*pattern)[n].wildcard&&std::to_integer<unsigned char>(bytes[s.rawOffset+at+n])!=(*pattern)[n].value){match=false;break;}
            if(match){if(result)return {};result=s.rva+unsigned(at);}
        }
    }return result;
}
std::optional<unsigned> Call(std::span<const std::byte> bytes,const engine::PeImage& pe,unsigned at){
    const auto offset=Offset(bytes,pe,at,5,true);if(!offset||bytes[*offset]!=std::byte{0xe8})return {};
    const auto target=std::int64_t(at)+5+Value<std::int32_t>(bytes,*offset+1);
    if(target<0||target>UINT32_MAX||!Offset(bytes,pe,unsigned(target),1,true))return {};return unsigned(target);
}
bool ExpectedTables(const ReloadFlowBinding& b){
    // This classifier consumes discovered proofs, but still rejects malformed
    // caller metadata rather than allowing uint32 arithmetic to wrap.
    if(!b.state.imageSize)return false;
    for(const auto& proof:b.state.code)if(std::uint64_t(proof.rva)+proof.size>b.state.imageSize)return false;
    for(const auto& proof:b.code)if(std::uint64_t(proof.rva)+proof.size>b.state.imageSize)return false;
    if(std::uint64_t(b.state.code[1].rva)+0x704>b.state.imageSize||
       std::uint64_t(b.state.code[2].rva)+0x1b9>b.state.imageSize||
       std::uint64_t(b.code[2].rva)+0x200>b.state.imageSize||
       std::uint64_t(b.code[3].rva)+0x4c>b.state.imageSize)return false;
    const auto step=b.state.code[1].rva,timed=b.state.code[2].rva;
    const std::array<ReloadTransferSite,5> wanted{{{step+0x561,ReloadTransferPath::OrdinaryState12},
        {timed+0x1b9,ReloadTransferPath::TimedInterruption},{b.code[2].rva+0x109,ReloadTransferPath::SpecialLogic4},
        {b.code[2].rva+0x200,ReloadTransferPath::SpecialLogic4},{b.code[3].rva+0x4c,ReloadTransferPath::ZeroDurationPreparation}}};
    for(unsigned n=0;n<wanted.size();++n)if(b.transfers[n].returnRva!=wanted[n].returnRva||b.transfers[n].path!=wanted[n].path)return false;
    for(unsigned n=0;n<stepOffsets.size();++n)if(b.stepDispatchRvas[n]!=step+stepOffsets[n])return false;
    return true;
}
}
std::optional<ReloadFlowBinding> DiscoverReloadFlow(std::span<const std::byte> bytes,const engine::PeImage& pe){
    const auto state=DiscoverReloadState(bytes,pe);if(!state)return {};ReloadFlowBinding b;b.state=*state;
    for(unsigned n=0;n<b.code.size();++n){const auto rva=Unique(bytes,pe,patterns[n]);if(!rva)return {};
        const auto offset=Offset(bytes,pe,*rva,sizes[n],true);if(!offset||Hash(bytes.subspan(*offset,sizes[n]))!=hashes[n])return {};
        b.code[n]={*rva,sizes[n],hashes[n]};}
    const auto link=[&](unsigned from,unsigned to){const auto call=Call(bytes,pe,from);return call&&*call==to;};
    const auto update=b.code[0].rva,forward=b.code[1].rva,step=state->code[1].rva,timed=state->code[2].rva;
    if(!link(update+0x9c,b.code[2].rva)||!link(update+0x151,state->code[3].rva)||
       !link(update+0x163,step)||!link(update+0x172,timed)||!link(forward+0x2a,update)||
       !link(b.code[4].rva+0x463,forward)||!link(b.code[4].rva+0x563,forward)||
       !link(b.code[5].rva+0x2a5,forward)||!link(b.code[5].rva+0x38e,forward))return {};
    const auto getter3c=Call(bytes,pe,b.code[4].rva+0x96),getter40=Call(bytes,pe,b.code[5].rva+0x31);
    if(!getter3c||!getter40)return {};
    b.branch3cGetterRva=*getter3c;b.branch40GetterRva=*getter40;
    for(auto pair:{std::pair{b.branch3cGetterRva,0xc33c418bu},std::pair{b.branch40GetterRva,0xc340418bu}}){
        const auto at=Offset(bytes,pe,pair.first,4,true);if(!at||Value<unsigned>(bytes,*at)!=pair.second)return {};}
    b.transfers={ReloadTransferSite{step+0x561,ReloadTransferPath::OrdinaryState12},
        {timed+0x1b9,ReloadTransferPath::TimedInterruption},{b.code[2].rva+0x109,ReloadTransferPath::SpecialLogic4},
        {b.code[2].rva+0x200,ReloadTransferPath::SpecialLogic4},{b.code[3].rva+0x4c,ReloadTransferPath::ZeroDurationPreparation}};
    for(const auto& site:b.transfers)if(!link(site.returnRva-5,state->code[4].rva))return {};
    const auto table=Offset(bytes,pe,step+0x6c4,64,true);if(!table)return {};
    for(unsigned n=0;n<b.stepDispatchRvas.size();++n){const auto target=Value<unsigned>(bytes,*table+4*n);
        if(target<state->preferredBase)return {};b.stepDispatchRvas[n]=target-state->preferredBase;}
    if(!ExpectedTables(b))return {};return b;
}
bool ValidateReloadFlowLive(const ReloadStateMemory& memory,const ReloadFlowBinding& b,unsigned base)noexcept {
    if(!ValidateReloadStateLive(memory,b.state,base)||!ExpectedTables(b))return false;
    const auto read=[&](unsigned rva,void* output,std::size_t count){return memory.read&&std::uint64_t(rva)+count<=b.state.imageSize&&
        std::uint64_t(base)+rva+count<=UINT32_MAX&&memory.read(memory.context,base+rva,output,count);};
    std::array<std::byte,2048> code{};
    for(unsigned n=0;n<b.code.size();++n){const auto& proof=b.code[n];
        if(proof.size!=sizes[n]||proof.fingerprint!=hashes[n]||!read(proof.rva,code.data(),proof.size)||Hash(std::span(code).first(proof.size))!=proof.fingerprint)return false;}
    std::array<unsigned,16> table{};if(!read(b.state.code[1].rva+0x6c4,table.data(),64))return false;
    for(unsigned n=0;n<table.size();++n)if(table[n]!=base+b.stepDispatchRvas[n])return false;
    for(auto pair:{std::pair{b.branch3cGetterRva,0xc33c418bu},std::pair{b.branch40GetterRva,0xc340418bu}}){
        unsigned word=0;if(!read(pair.first,&word,4)||word!=pair.second)return false;}
    return true;
}
std::optional<ReloadHoldCodeProof> DiscoverReloadHoldCode(std::span<const std::byte> bytes,const engine::PeImage& pe,const ReloadFlowBinding& b){
    const auto cool=Call(bytes,pe,b.code[0].rva+0xb2),abort=Call(bytes,pe,b.code[0].rva+0x88);
    if(!cool||!abort)return {};
    ReloadHoldCodeProof out{{{*cool,212,0xcdbc52c92612cbc9ull},{*abort,128,0x32cc32ebc7884376ull}}};
    for(const auto& proof:out){const auto at=Offset(bytes,pe,proof.rva,proof.size,true);
        if(!at||Hash(bytes.subspan(*at,proof.size))!=proof.fingerprint)return {};}
    return out;
}
bool ValidateReloadHoldCodeLive(const ReloadStateMemory& memory,const ReloadHoldCodeProof& proofs,unsigned base,unsigned size)noexcept {
    if(!memory.read||base!=0x400000||std::uint64_t(base)+size>UINT32_MAX)return false;
    const std::array<unsigned,2> sizes{212,128};
    const std::array<std::uint64_t,2> hashes{0xcdbc52c92612cbc9ull,0x32cc32ebc7884376ull};std::array<std::byte,212> raw{};
    for(unsigned n=0;n<proofs.size();++n){const auto& p=proofs[n];if(p.size!=sizes[n]||p.fingerprint!=hashes[n]||
        std::uint64_t(p.rva)+p.size>size||!memory.read(memory.context,base+p.rva,raw.data(),p.size)||Hash(std::span(raw).first(p.size))!=p.fingerprint)return false;}
    return true;
}
std::optional<ReloadUpdateContext> DecodeReloadUpdateContext(std::span<const std::byte> bytes)noexcept {
    if(bytes.size()!=0x30)return {};ReloadUpdateContext out;
    out.deltaSeconds=Value<float>(bytes,0x18);out.rawWord1c=Value<unsigned>(bytes,0x1c);
    out.reloadTimeMultiplier=Value<float>(bytes,0x20);out.inputFlags=Value<unsigned>(bytes,0x2c);
    if(!std::isfinite(out.deltaSeconds)||out.deltaSeconds<0||out.deltaSeconds>1||
       !std::isfinite(out.reloadTimeMultiplier)||out.reloadTimeMultiplier<0||out.reloadTimeMultiplier>1024||(out.inputFlags&~7u))return {};
    for(unsigned n=0;n<5;++n){const auto flag=std::to_integer<unsigned char>(bytes[0x24+n]);if(flag>1)return {};out.flags24Through28[n]=bool(flag);}
    out.fireRequested=(out.inputFlags&1)!=0;out.orderRequested=(out.inputFlags&2)!=0;out.reloadRequested=(out.inputFlags&4)!=0;
    return out;
}
std::optional<ReloadFiringSnapshot> DecodeReloadFiringSnapshot(std::span<const std::byte> bytes)noexcept {
    if(bytes.size()!=0x40)return {};
    ReloadFiringSnapshot out;out.current=Value<unsigned>(bytes,0);out.next=Value<unsigned>(bytes,4);
    out.phaseTimer=Value<float>(bytes,8);out.loaded=Value<std::int32_t>(bytes,0x18);out.reserve=Value<std::int32_t>(bytes,0x1c);
    if(out.current>15||out.next>15||!std::isfinite(out.phaseTimer)||std::abs(out.phaseTimer)>1000000||
       out.loaded< -1||out.loaded>1000000||out.reserve< -1||out.reserve>1000000)return {};
    return out;
}
std::optional<ReloadTransferInvocation> ClassifyReloadTransfer(const ReloadFlowBinding& binding,unsigned base,
    unsigned returnAddress,unsigned firing,const ReloadStateSnapshot& snapshot)noexcept {
    if(base!=binding.state.preferredBase||!base||returnAddress<base||std::uint64_t(returnAddress)>=std::uint64_t(base)+binding.state.imageSize||
       !ExpectedTables(binding)||!snapshot.sequence||snapshot.observedNs<=0||!snapshot.owner.player||!snapshot.owner.soldier||
       !snapshot.owner.weak||!snapshot.owner.weapon||!snapshot.owner.actorGeneration||!snapshot.owner.equipGeneration||!snapshot.owner.space||
       !firing||!snapshot.branches[0].address||!snapshot.branches[1].address||
       snapshot.branches[0].address==snapshot.branches[1].address)return {};
    const ReloadTransferSite* site=nullptr;
    for(const auto& candidate:binding.transfers)if(candidate.returnRva==returnAddress-base){if(site)return {};site=&candidate;}
    if(!site)return {};
    if(site->path==ReloadTransferPath::SpecialLogic4&&snapshot.config.fireLogicType!=4)return {};
    for(unsigned n=0;n<2;++n)if(snapshot.branches[n].address==firing&&snapshot.branches[n].wrapperOffset==(n?0x40u:0x3cu))
        return ReloadTransferInvocation{site->path,std::uint8_t(n),firing,snapshot.branches[n].wrapperOffset,site->returnRva};
    return {};
}
}
