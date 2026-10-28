#include "Bc2MenuLoading.h"
#include "fvr/engine/BindingValidation.h"
#include <cstring>

namespace fvr::bc2 {
namespace {
constexpr std::uint32_t Base=0x400000;
constexpr MenuLoadingCandidates Binding{0x15a4ca8,0x102b210,0x102b1fc};

std::optional<std::size_t> At(std::span<const std::byte> bytes,
    const engine::PeImage& pe,std::uint32_t va,std::size_t size){
    if(va<Base)return {};
    const auto rva=std::uint64_t(va)-Base;
    for(const auto& section:pe.sections)if(rva>=section.rva){
        const auto delta=rva-section.rva,offset=std::uint64_t(section.rawOffset)+delta;
        if(delta<=section.rawSize&&size<=section.rawSize-delta&&
           offset<=bytes.size()&&size<=bytes.size()-offset)return std::size_t(offset);
    }
    return {};
}
std::uint32_t Word(std::span<const std::byte> bytes,std::size_t offset){
    std::uint32_t value=0;std::memcpy(&value,bytes.data()+offset,4);return value;
}
bool Match(std::span<const std::byte> bytes,const engine::PeImage& pe,
    std::uint32_t va,const char* pattern){
    const auto parsed=engine::ParsePattern(pattern);if(!parsed)return false;
    const auto at=At(bytes,pe,va,parsed->size());if(!at)return false;
    for(std::size_t i=0;i<parsed->size();++i)
        if(!(*parsed)[i].wildcard&&std::to_integer<unsigned char>(bytes[*at+i])!=(*parsed)[i].value)return false;
    return true;
}
bool EqualsWord(std::span<const std::byte> bytes,const engine::PeImage& pe,
    std::uint32_t va,std::uint32_t expected){
    const auto at=At(bytes,pe,va,4);return at&&Word(bytes,*at)==expected;
}
bool Call(std::span<const std::byte> bytes,const engine::PeImage& pe,
    std::uint32_t va,std::uint32_t target){
    const auto at=At(bytes,pe,va,5);
    return at&&bytes[*at]==std::byte{0xe8}&&
        std::int64_t(va)+5+std::int32_t(Word(bytes,*at+1))==target;
}
bool Read(const MenuMemory& memory,std::uint64_t at,std::uint32_t& out){
    return memory.read&&at>=0x10000&&at<=UINT32_MAX-4&&
        memory.read(memory.context,std::uint32_t(at),&out,4);
}
struct Roots {
    std::uint32_t manager=0,front=0,pause=0,loader=0;
    bool operator==(const Roots&)const=default;
};
std::optional<Roots> ReadRoots(const MenuMemory& memory,std::uint32_t base){
    Roots roots;
    if(!Read(memory,std::uint64_t(base)+Binding.managerGlobal,roots.manager)||
       roots.manager<0x10000||
       !Read(memory,std::uint64_t(roots.manager)+0x82b24,roots.front)||
       !Read(memory,std::uint64_t(roots.manager)+0x82b20,roots.pause)||
       !Read(memory,std::uint64_t(roots.manager)+0x82b28,roots.loader))return {};
    return roots;
}
}

std::array<MenuLoadingCodeSpan,9> MenuLoadingCodeSpans() noexcept {
    return {{{0x337380,0xb},{0x364610,0x2b},{0x5297b0,0x200},
             {0x4feca0,0x35},{0x556650,0x428},{0x5120f0,0x74},
             {0x35ad30,0x141},{0x102b1fc,0x4c},{0x102b14c,10}}};
}
std::optional<MenuLoadingCandidates> DiscoverMenuLoading(
    std::span<const std::byte> bytes,const engine::PeImage& pe){
    if(pe.machine!=0x14c||bytes.size()<0x40)return {};
    const auto nt=Word(bytes,0x3c);
    if(nt>bytes.size()||56>bytes.size()-nt||Word(bytes,std::size_t(nt)+52)!=Base)return {};
    for(const auto span:MenuLoadingCodeSpans())
        if(!At(bytes,pe,Base+span.rva,span.size))return {};
    if(!Match(bytes,pe,0x737380,"8B 0D A8 4C 9A 01 E9")||
       !Match(bytes,pe,0x764610,"53 55 56 57 8B F1 E8 ?? ?? ?? ?? 8D 7E 2C 8B CF E8 ?? ?? ?? ?? C7 07 FC B1 42 01")||
       !Call(bytes,pe,0x764616,0xf8d780)||!Call(bytes,pe,0x764620,0x949020)||
       !Match(bytes,pe,0x764633,"C7 06 10 B2 42 01"))return {};
    // The original loader creator owns allocation/publication and resource
    // initialization. No writable offset is exposed by this binding.
    if(!Match(bytes,pe,0x9297be,"8B 8E 28 2B 08 00 85 C9 74 21 E8")||
       !Call(bytes,pe,0x9297c8,0xfa8cc0)||
       !Match(bytes,pe,0x9297df,"C7 86 28 2B 08 00 00 00 00 00")||
       !Match(bytes,pe,0x92983b,"68 B8 01 00 00 E8")||
       !Call(bytes,pe,0x929840,0x420db0)||!Call(bytes,pe,0x92984e,0x764610)||
       !Match(bytes,pe,0x929862,"89 86 28 2B 08 00")||
       !Match(bytes,pe,0x929872,"8B 8E 28 2B 08 00 50 8D 54 24 14 52 E8")||
       !Call(bytes,pe,0x92987e,0x75ad30)||
       !Match(bytes,pe,0x75ad41,"8B D9 8B 08 51 8D 54 24 2C 68 4C B1 42 01 52 E8")||
       !Call(bytes,pe,0x75ad50,0x744620))return {};
    const auto path=At(bytes,pe,0x142b14c,10);
    if(!path||std::memcmp(bytes.data()+*path,"%s/loader",10)!=0)return {};
    if(!Match(bytes,pe,0x8feca0,"56 8B F1 E8 ?? ?? ?? ?? 8B 8E 28 2B 08 00 85 C9 74 21 E8")||
       !Call(bytes,pe,0x8feca3,0xf82250)||!Call(bytes,pe,0x8fecb2,0xfa8cc0)||
       !Match(bytes,pe,0x8fecb7,"8B 8E 28 2B 08 00 85 C9 74 08 8B 01 8B 10 6A 01 FF D2 C7 86 28 2B 08 00 00 00 00 00 5E C3"))return {};
    // A queued UI transition retires the existing loader before dispatching
    // the next state. Only transition cases1/2 construct another loader.
    if(!Match(bytes,pe,0x956780,"83 BE 28 2B 08 00 00 74 11 8B CE E8")||
       !Call(bytes,pe,0x95678b,0x8feca0)||
       !Match(bytes,pe,0x956836,"8B 01 83 F8 04 0F 87 46 01 00 00 FF 24 85 64 6A 95 00")||
       !EqualsWord(bytes,pe,0x956a68,0x9568af)||!EqualsWord(bytes,pe,0x956a6c,0x956860)||
       !Call(bytes,pe,0x956872,0x9297b0)||!Call(bytes,pe,0x9568ba,0x9297b0))return {};
    // The original native UI predicate has a distinct loader-present branch.
    // Its broader semantics are not inferred; the adapter never calls it.
    if(!Match(bytes,pe,0x912149,"83 FF 01 75 09 83 BE 28 2B 08 00 00 75 BB")||
       !Match(bytes,pe,0x912112,"5F 32 C0 5E C2 04 00")||
       !EqualsWord(bytes,pe,0x142b210,0x764770)||
       !EqualsWord(bytes,pe,0x142b220,0x74ea90)||
       !EqualsWord(bytes,pe,0x142b1fc,0x7370a0)||
       !EqualsWord(bytes,pe,0x142b20c,0x75f870))return {};
    return Binding;
}

std::optional<MenuLoadingSnapshot> ReadMenuLoading(
    const MenuMemory& memory,const MenuLoadingCandidates& binding,std::uint32_t base){
    if(binding!=Binding||base!=Base)return {};
    const auto one=[&]()->std::optional<Roots>{
        const auto before=ReadRoots(memory,base);if(!before)return {};
        if(before->loader){
            if(before->front||before->pause)return {};
            std::uint32_t primary=0,event=0;
            if(!Read(memory,before->loader,primary)||primary!=base+binding.ownerVtable||
               !Read(memory,std::uint64_t(before->loader)+0x2c,event)||event!=base+binding.eventVtable)return {};
        }
        const auto after=ReadRoots(memory,base);
        if(!after||*before!=*after)return {};
        return before;
    };
    const auto first=one(),second=one();
    if(!first||!second||*first!=*second)return {};
    return MenuLoadingSnapshot{first->manager,first->loader,first->loader!=0};
}
}
