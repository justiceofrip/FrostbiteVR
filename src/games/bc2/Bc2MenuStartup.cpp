#include "Bc2MenuStartup.h"
#include "fvr/engine/BindingValidation.h"
#include <cstring>

namespace fvr::bc2 {
namespace {
constexpr std::uint32_t PreferredBase=0x400000;
constexpr MenuStartupBinding Binding{0x15a4ca8,0x1040860,0x559900,0x3438b0};
std::optional<std::size_t> At(std::span<const std::byte> b,const engine::PeImage& pe,std::uint32_t va,std::size_t n){
    if(va<PreferredBase)return {};const auto rva=va-PreferredBase;
    for(const auto& s:pe.sections)if(rva>=s.rva){
        const auto d=std::uint64_t(rva)-s.rva,at=std::uint64_t(s.rawOffset)+d;
        if(d<=s.rawSize&&n<=s.rawSize-d&&at<=b.size()&&n<=b.size()-at)return std::size_t(at);
    }return {};
}
std::uint32_t Word(std::span<const std::byte> b,std::size_t at){std::uint32_t v=0;std::memcpy(&v,b.data()+at,4);return v;}
bool Match(std::span<const std::byte> b,const engine::PeImage& pe,std::uint32_t va,const char* text){
    const auto p=engine::ParsePattern(text);if(!p)return false;const auto at=At(b,pe,va,p->size());if(!at)return false;
    for(std::size_t i=0;i<p->size();++i)if(!(*p)[i].wildcard&&std::to_integer<unsigned char>(b[*at+i])!=(*p)[i].value)return false;
    return true;
}
bool Call(std::span<const std::byte> b,const engine::PeImage& pe,std::uint32_t va,std::uint32_t target){
    const auto at=At(b,pe,va,5);return at&&b[*at]==std::byte{0xe8}&&std::int64_t(va)+5+std::int32_t(Word(b,*at+1))==target;
}
bool Read(const MenuMemory& m,std::uint64_t at,void* out,std::size_t n){
    return m.read&&at>=0x10000&&n&&n<=256&&at<UINT32_MAX&&n<=UINT32_MAX-at&&m.read(m.context,std::uint32_t(at),out,n);
}
bool String(const MenuMemory& m,std::uint64_t at,std::array<char,257>& out){
    std::uint32_t range[2]{};if(!Read(m,at,range,8)||range[1]<range[0]||range[1]-range[0]>256)return false;
    const auto n=range[1]-range[0];return !n||(Read(m,range[0],out.data(),n)&&std::memchr(out.data(),0,n)==nullptr);
}
}
std::array<MenuStateCodeSpan,5> MenuStartupCodeSpans()noexcept {
    // Cleanup body, native progression, native skipMovie branch, pool removal,
    // and update prologue. Update slot is validated separately before swapping.
    return {{{0x3438b0,0xb7},{0x360270,0x1d0},{0x37bfcf,0x65},{0xb9ab50,0x93},{0x559900,16}}};
}
std::optional<MenuStartupBinding> DiscoverMenuStartup(std::span<const std::byte> b,const engine::PeImage& pe){
    if(pe.machine!=0x14c||pe.timestamp!=1314210909||pe.imageSize!=0x1913000||b.size()<0x40)return {};
    const auto nt=Word(b,0x3c);if(nt>b.size()||56>b.size()-nt||Word(b,std::size_t(nt)+52)!=PreferredBase)return {};
    for(const auto& s:MenuStartupCodeSpans())if(!At(b,pe,PreferredBase+s.rva,s.size))return {};
    const auto slot=At(b,pe,PreferredBase+Binding.updateSlot,4);
    if(!slot||Word(b,*slot)!=PreferredBase+Binding.update||
       !Match(b,pe,0x959900,"55 8B EC 83 E4 F0 83 EC 24 53 56 57 8B F1 E8 1D")||
       !Match(b,pe,0x7438b0,"53 8B 1D A8 4C 9A 01 56 57 8B F9 8B 87 24 01 00 00")||
       !Match(b,pe,0x760270,"51 53 55 8B 2D A8 4C 9A 01 56 8B F1")||
       !Call(b,pe,0x77c01b,0x7438b0)||!Call(b,pe,0x760390,0x7438b0)||!Call(b,pe,0x7438da,0xf9ab50))return {};
    return Binding;
}
std::optional<MenuStartupSnapshot> ReadMenuStartup(const MenuMemory& m,const MenuStartupBinding& b,
    std::uint32_t base,std::uint32_t expectedManager){
    if(b!=Binding||base!=PreferredBase)return {};
    MenuStartupSnapshot s;std::uint32_t pause=0,a=0,c=0,d=0,range[2]{};std::uint8_t enabled=0;
    if(!Read(m,std::uint64_t(base)+b.managerGlobal,&s.manager,4)||s.manager!=expectedManager||
       !Read(m,std::uint64_t(s.manager)+0x82b20,&pause,4)||pause||
       !Read(m,std::uint64_t(s.manager)+0x82b24,&s.owner,4)||s.owner<0x10000||
       !Read(m,s.owner,&a,4)||a!=base+0x102b4e8||
       !Read(m,std::uint64_t(s.owner)+0x2c,&c,4)||c!=base+0x102b4d4||
       !Read(m,std::uint64_t(s.owner)+0x90,&d,4)||d!=base+0x102b4c8||
       !Read(m,std::uint64_t(s.manager)+0x82ccc,&enabled,1)||enabled>1||
       !Read(m,std::uint64_t(s.manager)+0x82cc8,&s.index,4)||
       !Read(m,std::uint64_t(s.owner)+0x114,range,8)||range[1]<range[0]||
       (range[1]-range[0])%12||(range[1]-range[0])/12>16||
       !String(m,std::uint64_t(s.owner)+0x124,s.current))return {};
    s.enabled=enabled==1;s.count=(range[1]-range[0])/12;
    if(s.index&&s.index<=s.count&&!String(m,std::uint64_t(range[0])+(s.index-1)*12,s.previous))return {};
    std::uint32_t ownerAfter=0,managerAfter=0;
    if(!Read(m,std::uint64_t(base)+b.managerGlobal,&managerAfter,4)||managerAfter!=s.manager||
       !Read(m,std::uint64_t(s.manager)+0x82b24,&ownerAfter,4)||ownerAfter!=s.owner)return {};
    return s;
}
}
