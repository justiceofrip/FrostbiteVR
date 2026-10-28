#include "Bc2ResolutionBinding.h"
#include "fvr/engine/BindingValidation.h"
#include <cstring>

namespace fvr::bc2 {
namespace {
constexpr unsigned Base=0x400000;
std::optional<std::size_t> At(std::span<const std::byte> b,const engine::PeImage& pe,unsigned va,std::size_t n){
    if(va<Base)return {};const auto rva=va-Base;
    for(const auto& s:pe.sections)if(rva>=s.rva){const auto d=std::uint64_t(rva)-s.rva,at=std::uint64_t(s.rawOffset)+d;
        if(d<=s.rawSize&&n<=s.rawSize-d&&at<=b.size()&&n<=b.size()-at)return std::size_t(at);}
    return {};
}
bool Match(std::span<const std::byte> b,const engine::PeImage& pe,unsigned va,const char* text){
    const auto p=engine::ParsePattern(text);if(!p)return false;const auto at=At(b,pe,va,p->size());if(!at)return false;
    for(std::size_t i=0;i<p->size();++i)if(!(*p)[i].wildcard&&std::to_integer<unsigned char>(b[*at+i])!=(*p)[i].value)return false;
    return true;
}
bool Read(const MenuMemory& m,std::uint64_t at,void* out,std::size_t n){
    return m.read&&at>=0x10000&&n&&n<=256&&at<UINT32_MAX&&n<=UINT32_MAX-at&&m.read(m.context,unsigned(at),out,n);
}
bool Snapshot(const MenuMemory& m,const ResolutionBinding& b,unsigned base,ResolutionSnapshot& s){
    unsigned table=0,slots[4]{},menuTables[3]{},pause=0,optionsTable=0;unsigned char fullscreen=0,optionsFullscreen=0,optionsDirty=0;
    if(!Read(m,base+b.menu.managerGlobal,&s.manager,4)||s.manager<0x10000||
       !Read(m,std::uint64_t(s.manager)+0x82b20,&pause,4)||pause||
       !Read(m,std::uint64_t(s.manager)+0x82b24,&s.menu,4)||s.menu<0x10000||
       !Read(m,s.menu,&menuTables[0],4)||!Read(m,std::uint64_t(s.menu)+0x2c,&menuTables[1],4)||
       !Read(m,std::uint64_t(s.menu)+0x90,&menuTables[2],4)||
       menuTables[0]!=base+0x102b4e8||menuTables[1]!=base+0x102b4d4||menuTables[2]!=base+0x102b4c8||
       !Read(m,base+b.renderer.rendererGlobal,&s.renderer,4)||s.renderer<0x10000||
       !Read(m,s.renderer,&table,4)||table!=base+b.renderer.rendererVtable||
       !Read(m,std::uint64_t(table)+20,&slots[0],4)||slots[0]!=base+b.renderer.present||
       !Read(m,std::uint64_t(table)+40,&slots[1],4)||slots[1]!=0x9d8180||
       !Read(m,std::uint64_t(table)+44,&slots[2],4)||slots[2]!=0x9d8190||
       !Read(m,std::uint64_t(table)+0x90,&slots[3],4)||slots[3]!=base+b.applySettings||
       !Read(m,std::uint64_t(s.renderer)+0x18,&s.width,4)||!Read(m,std::uint64_t(s.renderer)+0x1c,&s.height,4)||
       !Read(m,std::uint64_t(s.renderer)+0x40,&s.settings,4)||s.settings<0x10000||
       !Read(m,std::uint64_t(s.renderer)+0x88,&s.swapchain,4)||s.swapchain<0x10000||
       !Read(m,base+b.registryGlobal,&s.registry,4)||s.registry<0x10000||
       !Read(m,std::uint64_t(s.settings)+0x0c,&s.desired,sizeof(s.desired))||
       !Read(m,std::uint64_t(s.settings)+0x35,&fullscreen,1)||fullscreen>1||
       !Read(m,base+b.optionsGlobal,&s.options,4)||s.options<0x10000||
       !Read(m,s.options,&optionsTable,4)||optionsTable!=base+b.optionsVtable||
       !Read(m,std::uint64_t(s.options)+0xa0,&s.optionsDesired,sizeof(s.optionsDesired))||
       !Read(m,std::uint64_t(s.options)+0xac,&optionsFullscreen,1)||optionsFullscreen>1||
       !Read(m,std::uint64_t(s.options)+0xc0,&optionsDirty,1)||optionsDirty>1)return false;
    s.fullscreen=fullscreen!=0;s.optionsFullscreen=optionsFullscreen!=0;s.optionsDirty=optionsDirty!=0;
    return ValidResolutionSize(s.desired)&&ValidResolutionSize(s.optionsDesired)&&ValidResolutionSize({float(s.width),float(s.height)});
}
}
std::array<MenuStateCodeSpan,24> ResolutionCodeSpans()noexcept {
    return {{{0x3da860,0x7d},{0x0f4c00,0x13},{0x0f4c40,0x13},
        {0x3de38f,0x18},{0x5e1860,0x80},{0x5eeadc,0x8e},{0x159560,0x76},{0x3de40d,0x17},
        {0x5d8180,4},{0x5d8190,4},{0x3de53c,0x10},
        {0x3bc0a0,0x1f},{0x3bc0c0,0x14},{0x560fcf,0x16},{0x16ade9,0x17},
        {0x3de030,0x1b},{0x1d1685,0x0f},{0x3e44d9,8},
        {0x55e680,17},{0x561008,3},{0x563da1,3},
        {0x555a18,0x20},{0x557f6d,0x20},{0x1040830,0x34}}};
}
std::optional<ResolutionBinding> DiscoverResolutionBinding(std::span<const std::byte> b,const engine::PeImage& pe){
    const auto renderer=DiscoverProfile(b,pe);const auto menu=DiscoverMenuStartup(b,pe);
    if(!renderer||!menu||renderer->preferredBase!=Base||menu->updateSlot!=0x1040830+0x30)return {};
    for(const auto& s:ResolutionCodeSpans())if(!At(b,pe,Base+s.rva,s.size))return {};
    if(!Match(b,pe,0x7da860,"83 EC 14 56 B8 50 C4 54 01")||
       !Match(b,pe,0x7da88e,"B9 2C D0 55 01")||
       !Match(b,pe,0x7da8ad,"68 2C D0 55 01 50 8B CE E8 46 A3 D1 FF")||
       !Match(b,pe,0x7da8d7,"83 C4 14 C2 08 00")||
       !Match(b,pe,0x4f4c00,"85 C9 74 08 83 C1 FC E9 D4 F5 FF FF 33 C9 E9 CD F5 FF FF")||
       !Match(b,pe,0x4f4c40,"85 C9 74 08 83 C1 FC E9 B4 F9 FF FF 33 C9 E9 AD F9 FF FF")||
       !Match(b,pe,0x7de38f,"8D 86 A0 00 00 00 50 8D 4C 24 2C 51 8B 0D DC CF 55 01 E8 BA C4 FF FF")||
       !Match(b,pe,0x9e1860,"56 8B F1 8B 56 40 8B 4E 44 8A 42 35 3A 41 35")||
       !Match(b,pe,0x9e18d1,"C6 86 86 01 00 00 01 52 E8 82 A2 B0 FF 5E C3")||
       !Match(b,pe,0x9eeb34,"8B 41 34 FF D0")||
       !Match(b,pe,0x9eeb4a,"E8 41 FD FF FF")||
       !Match(b,pe,0x9eeb5c,"89 4E 1C 52 8B CE 89 46 18")||
       !Match(b,pe,0x559560,"83 EC 0C 8B 44 24 14 8A 00 56 8B F1")||
       !Match(b,pe,0x559587,"B9 54 03 BF 01")||
       !Match(b,pe,0x5595a6,"68 54 03 BF 01 51 8B CE E8 4D B6 F9 FF")||
       !Match(b,pe,0x5595d0,"83 C4 0C C2 08 00")||
       !Match(b,pe,0x7de40d,"8B 0D DC CF 55 01 8D 96 AC 00 00 00 52 8D 44 24 2C 50 E8 3C B1 D7 FF")||
       !Match(b,pe,0x7de53c,"8B 0D 60 2A 57 01 8B 11 8B 82 90 00 00 00 FF D0")||
       !Match(b,pe,Base+renderer->rendererVtable+0x90,"60 18 9E 00")||
       !Match(b,pe,0x7bc0a0,"8B 44 24 04 8B 10 89 91 A0 00 00 00 8B 40 04 89 81 A4 00 00 00 C6 81 C0 00 00 00 01 C2 04 00")||
       !Match(b,pe,0x7bc0c0,"8A 44 24 04 88 81 AC 00 00 00 C6 81 C0 00 00 00 01 C2 04 00")||
       !Match(b,pe,0x960fcf,"8D 84 24 24 01 00 00 D9 9C 24 28 01 00 00 50 8B CB E8 BB B0 E5 FF")||
       !Match(b,pe,0x56ade9,"8B 35 A0 0E 57 01 38 9E AC 00 00 00 8B CE 0F 94 C0 50 E8 C0 12 25 00")||
       !Match(b,pe,0x7de030,"83 EC 4C 53 56 8B F1 33 DB 38 9E C0 00 00 00 0F 84 5E 11 00 00 88 9E C0 00 00 00")||
       !Match(b,pe,0x5d1685,"8B 0D A0 0E 57 01 85 C9 74 05 E8 9C C9 20 00")||
       !Match(b,pe,0x7e44d9,"8B F1 C7 06 70 E5 42 01")||
       !Match(b,pe,0x95e680,"B8 B8 23 00 00 E8 66 37 2F 00 8B 84 24 BC 23 00 00")||
       !Match(b,pe,0x961008,"C2 08 00")||!Match(b,pe,0x963da1,"C2 08 00")||
       // UIManager's primary table is distinct from its secondary interface
       // at +0x82afc. The update slot is primary+0x30, not secondary+0x70.
       !Match(b,pe,0x955a18,"C7 06 30 08 44 01 C7 86 F0 2A 08 00 10 08 44 01 C7 07 04 08 44 01 C7 86 FC 2A 08 00 F0 07 44 01")||
       !Match(b,pe,0x957f6d,"C7 06 30 08 44 01 C7 86 F0 2A 08 00 10 08 44 01 C7 07 04 08 44 01 C7 86 FC 2A 08 00 F0 07 44 01")||
       !Match(b,pe,0x1440830,"E0 98 95 00")||!Match(b,pe,0x1440860,"00 99 95 00"))return {};
    constexpr char key[]="DX11DisplaySettings.WindowSize";
    const auto keyAt=At(b,pe,0x142fde0,sizeof(key));if(!keyAt||std::memcmp(b.data()+*keyAt,key,sizeof(key)))return {};
    return ResolutionBinding{*renderer,*menu,0x115cfdc,0x3da860,0x159560,0x0f4c40,0x5e1860,0x1170ea0,0x102e570,0x3bc0a0,0x3bc0c0,0x55e680,0x1040830};
}
bool ResolutionSettingsMatch(const ResolutionSnapshot& s,ResolutionSize size,bool fullscreen)noexcept {
    return ValidResolutionSize(size)&&s.desired==size&&s.fullscreen==fullscreen&&
        s.optionsDesired==size&&s.optionsFullscreen==fullscreen;
}
bool CanRestoreResolutionSettings(const ResolutionSnapshot& s,const ResolutionSnapshot& original,ResolutionSize target)noexcept {
    // Partial original setter completion may leave either old or attempted
    // values. Any third-party setting or replacement owner denies restoration.
    return ValidResolutionSize(target)&&s.SameOwner(original)&&
        ResolutionSettingsMatch(original,original.desired,original.fullscreen)&&
        (s.desired==target||s.desired==original.desired)&&
        (s.fullscreen==false||s.fullscreen==original.fullscreen)&&
        (s.optionsDesired==target||s.optionsDesired==original.optionsDesired)&&
        (s.optionsFullscreen==false||s.optionsFullscreen==original.optionsFullscreen);
}
std::optional<ResolutionSnapshot> ReadResolutionSnapshot(const MenuMemory& memory,const ResolutionBinding& b,unsigned base){
    if(base!=Base||b.registryGlobal!=0x115cfdc||b.setWindowSize!=0x3da860||b.setFullscreen!=0x159560||
       b.getField!=0x0f4c40||b.applySettings!=0x5e1860||b.optionsGlobal!=0x1170ea0||b.optionsVtable!=0x102e570||
       b.setOptionsSize!=0x3bc0a0||b.setOptionsFullscreen!=0x3bc0c0)return {};
    ResolutionSnapshot first,second;
    if(!Snapshot(memory,b,base,first)||!Snapshot(memory,b,base,second)||first!=second)return {};
    return first;
}
std::optional<ResolutionGuardOwner> ReadResolutionGuardOwner(const MenuMemory& memory,const ResolutionBinding& b,unsigned base){
    if(base!=Base||b.renderer.preferredBase!=Base||b.menu.managerGlobal!=0x15a4ca8||b.registryGlobal!=0x115cfdc||
       b.optionsGlobal!=0x1170ea0||b.optionsVtable!=0x102e570||b.dispatcher!=0x55e680||b.managerVtable!=0x1040830)return {};
    const auto read=[&](ResolutionGuardOwner& s){
        unsigned managerTable=0,rendererTable=0,optionsTable=0;
        return Read(memory,std::uint64_t(base)+b.menu.managerGlobal,&s.manager,4)&&s.manager>=0x10000&&
            Read(memory,s.manager,&managerTable,4)&&managerTable==std::uint64_t(base)+b.managerVtable&&
            Read(memory,std::uint64_t(base)+b.renderer.rendererGlobal,&s.renderer,4)&&s.renderer>=0x10000&&
            Read(memory,s.renderer,&rendererTable,4)&&rendererTable==std::uint64_t(base)+b.renderer.rendererVtable&&
            Read(memory,std::uint64_t(base)+b.optionsGlobal,&s.options,4)&&s.options>=0x10000&&
            Read(memory,s.options,&optionsTable,4)&&optionsTable==std::uint64_t(base)+b.optionsVtable&&
            Read(memory,std::uint64_t(s.renderer)+0x40,&s.settings,4)&&s.settings>=0x10000&&
            Read(memory,std::uint64_t(base)+b.registryGlobal,&s.registry,4)&&s.registry>=0x10000;
    };
    ResolutionGuardOwner first,second;
    if(!read(first)||!read(second)||first!=second)return {};
    return first;
}
}
