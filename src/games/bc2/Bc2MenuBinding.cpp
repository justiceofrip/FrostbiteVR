#include "Bc2MenuBinding.h"
#include "fvr/engine/BindingValidation.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
namespace fvr::bc2 {
namespace {
// Complete original mapper, its five read-only display getters for each native
// display class, and every referenced numeric constant. Derived from the same
// inspected image as the mouse pump; these are also compared to the live image.
struct MappingEvidence {std::uint32_t rva,size;std::uint64_t hash;};
constexpr std::array<MappingEvidence,22> mappingEvidence{{
    {0x509060,0x230,0x5cfa1d3c3afd91edull},
    {0x5d8180,4,0x1d394ef6ff61ced8ull},{0x5d8190,4,0x1d4566f6ff6acefcull},
    {0x5d8290,0x37,0x23cd99f6a68ef458ull},{0x5f4050,7,0xe6a21f0c4d48e508ull},{0x5f4060,7,0x8a0abd0481432f01ull},
    {0x5d7270,4,0x1d394ef6ff61ced8ull},{0x5d7280,4,0x1d4566f6ff6acefcull},
    {0x5d7370,0x37,0x514a7b4ff3d0c17cull},{0x5f2dc0,7,0xef3463c54f5b74a9ull},{0x5f2dd0,7,0xe8b96c116596b46full},
    {0x5d60a0,4,0x1d7d46f6ff9b8e0cull},{0x5d60b0,4,0x1d1d9ef6ff49dc10ull},
    {0x5d6060,7,0x1ac04886b4bd94e9ull},{0x5fa2c0,7,0x1bb3ec6478523ed8ull},{0x5fa2d0,7,0xb819c8520ae97805ull},
    {0x103bb60,8,0xaa37403229527bd1ull},{0x103bb58,8,0xfbcb6c349aa1c7b3ull},
    {0x102a7c8,8,0x9ebd38b308d3cfecull},{0x10d94f0,4,0x4b72477f9c5c2f98ull},
    {0x10d9488,8,0xaae7e93229e886a8ull},{0x10d95f8,4,0x4b72b77f9c5cede8ull}}};
struct DisplayProfile {std::uint32_t vtable,width,height,aspect,rectangle,overridden;unsigned evidence;bool floatAspect;};
constexpr std::array<DisplayProfile,3> displays{{
    {0x10497a0,0x18,0x1c,0xcc,0xf0,0xec,1,false},
    {0x10496c8,0x18,0x1c,0xd8,0x170,0x165,6,false},
    {0x1049970,0x0c,0x10,0x124,0xc0,0xb0,11,true}}};
constexpr std::array<std::uint32_t,5> displaySlots{0x28,0x2c,0x98,0x9c,0xa0};
std::optional<std::size_t> Offset(std::span<const std::byte> b,const engine::PeImage& pe,std::uint32_t rva,std::size_t size){
    for(const auto& s:pe.sections)if(rva>=s.rva){const auto d=std::uint64_t(rva)-s.rva,at=std::uint64_t(s.rawOffset)+d;
        if(d<=s.rawSize&&size<=s.rawSize-d&&at<=b.size()&&size<=b.size()-at)return std::size_t(at);}
    return {};
}
std::uint32_t Word(std::span<const std::byte> b,std::size_t at){std::uint32_t v=0;std::memcpy(&v,b.data()+at,4);return v;}
bool Pattern(std::span<const std::byte> b,std::size_t at,const char* pattern){
    const auto p=engine::ParsePattern(pattern);if(!p||at>b.size()||p->size()>b.size()-at)return false;
    for(std::size_t n=0;n<p->size();++n)if(!(*p)[n].wildcard&&std::to_integer<unsigned char>(b[at+n])!=(*p)[n].value)return false;return true;
}
std::optional<std::uint32_t> Find(std::span<const std::byte> b,const engine::PeImage& pe,const char* pattern){
    const auto p=engine::ParsePattern(pattern);if(!p)return {};std::optional<std::uint32_t> found;
    for(const auto& s:pe.sections)if(s.flags&0x20000000){
        if(s.rawOffset>b.size()||s.rawSize>b.size()-s.rawOffset||p->size()>s.rawSize)continue;
        for(std::size_t i=0;i<=s.rawSize-p->size();++i){bool match=true;
            for(std::size_t j=0;j<p->size();++j)if(!(*p)[j].wildcard&&std::to_integer<unsigned char>(b[s.rawOffset+i+j])!=(*p)[j].value){match=false;break;}
            if(match){if(found)return {};found=s.rva+std::uint32_t(i);}}
    }return found;
}
std::optional<std::uint32_t> Call(std::span<const std::byte> b,const engine::PeImage& pe,std::uint32_t rva){
    const auto at=Offset(b,pe,rva,5);if(!at||b[*at]!=std::byte{0xe8})return {};
    const auto target=std::int64_t(rva)+5+std::int32_t(Word(b,*at+1));
    if(target<0||target>UINT32_MAX)return {};const auto result=std::uint32_t(target);
    for(const auto& s:pe.sections)if((s.flags&0x20000000)&&result>=s.rva&&std::uint64_t(result)-s.rva<s.rawSize)return result;
    return {};
}
bool Match(std::span<const std::byte> b,const engine::PeImage& pe,std::uint32_t rva,const char* text,std::size_t length){const auto at=Offset(b,pe,rva,length);return at&&Pattern(b,*at,text);}
bool Read(const MenuMemory& m,std::uint64_t at,void* dst,std::size_t size){return m.read&&at>=0x10000&&size&&size<=65536&&at<=UINT32_MAX&&size<=UINT32_MAX-at&&m.read(m.context,std::uint32_t(at),dst,size);}
bool ReadWord(const MenuMemory& m,std::uint64_t at,std::uint32_t& value){return Read(m,at,&value,4);}
bool SameOwner(const MenuTargetSnapshot& a,const MenuTargetSnapshot& b){return a.manager==b.manager&&a.ui==b.ui&&a.target==b.target&&a.queue==b.queue&&a.buffer==b.buffer&&a.capacity==b.capacity;}
struct DisplaySnapshot {
    std::uint32_t object=0,vtable=0,width=0,height=0;
    std::array<std::uint32_t,2> aspect{};
    std::array<std::uint32_t,4> rectangle{};
    std::uint8_t overridden=0;
    bool operator==(const DisplaySnapshot&)const=default;
};
std::optional<DisplaySnapshot> ReadDisplay(const MenuMemory& memory,const MenuBindingCandidates& binding,std::uint32_t base){
    if(binding.coordinateMapper!=0x509060||binding.displayGlobal!=0x1172a60||base<0x10000||
       std::uint64_t(base)+0x1172a64>UINT32_MAX)return {};
    const auto read=[&]()->std::optional<DisplaySnapshot>{
        DisplaySnapshot out;
        if(!ReadWord(memory,std::uint64_t(base)+binding.displayGlobal,out.object)||!ReadWord(memory,out.object,out.vtable))return {};
        const auto profile=std::find_if(displays.begin(),displays.end(),[&](const auto& p){return std::uint64_t(base)+p.vtable==out.vtable;});
        if(profile==displays.end())return {};
        for(unsigned i=0;i<displaySlots.size();++i){std::uint32_t fn=0;
            if(!ReadWord(memory,std::uint64_t(out.vtable)+displaySlots[i],fn)||std::uint64_t(base)+mappingEvidence[profile->evidence+i].rva!=fn)return {};}
        if(!ReadWord(memory,std::uint64_t(out.object)+profile->width,out.width)||
           !ReadWord(memory,std::uint64_t(out.object)+profile->height,out.height)||
           !out.width||!out.height||out.width>32768||out.height>32768||
           !Read(memory,std::uint64_t(out.object)+profile->overridden,&out.overridden,1)||out.overridden>1)return {};
        if(out.overridden){
            for(unsigned i=0;i<4;++i)if(!ReadWord(memory,std::uint64_t(out.object)+profile->rectangle+4*i,out.rectangle[i]))return {};
            const auto& r=out.rectangle;
            if(!r[2]||!r[3]||std::uint64_t(r[0])+r[2]>out.width||std::uint64_t(r[1])+r[3]>out.height)return {};
        }else{
            if(!ReadWord(memory,std::uint64_t(out.object)+profile->aspect,out.aspect[0]))return {};
            float aspect=0;
            if(profile->floatAspect)std::memcpy(&aspect,out.aspect.data(),4);
            else {
                if(!ReadWord(memory,std::uint64_t(out.object)+profile->aspect+4,out.aspect[1])||
                   !out.aspect[0]||!out.aspect[1]||out.aspect[0]>32768||out.aspect[1]>32768)return {};
                aspect=float(out.aspect[0])/float(out.aspect[1]);
            }
            if(!std::isfinite(aspect)||aspect<.1f||aspect>10)return {};
        }
        return out;
    };
    const auto first=read(),second=read();if(!first||!second||*first!=*second)return {};return first;
}
}
bool SameMenuTargetOwner(const MenuTargetSnapshot& a,const MenuTargetSnapshot& b) noexcept {
    return a.manager==b.manager&&a.ui==b.ui&&a.target==b.target;
}
std::optional<MenuBindingCandidates> DiscoverMenuBinding(std::span<const std::byte> b,const engine::PeImage& pe){
    if(pe.machine!=0x14c||b.size()<0x40)return {};
    const auto nt=Word(b,0x3c);if(nt>b.size()||56>b.size()-nt)return {};const auto base=Word(b,std::size_t(nt)+52);
    const auto cursor=Find(b,pe,"8B 41 14 85 C0 74 16 8B 4C 24 08 8B 54 24 04 8B 40 04 51 52 50 E8 ?? ?? ?? ?? 83 C4 0C C2 08 00");
    const auto event=Find(b,pe,"8B 49 14 85 C9 74 1B 8B 44 24 04 8B 50 08 8B 49 04 52 8B 50 04 8B 00 52 50 51 E8 ?? ?? ?? ?? 83 C4 10 C2 04 00");
    const auto pump=Find(b,pe,"83 EC 44 53 56 8B F1 8B 4C 24 50 8B 01 8B 50 34 FF D2 33 DB");
    if(!cursor||!event||!pump)return {};
    if(Call(b,pe,*pump+0x128)!=cursor||Call(b,pe,*pump+0x1fe)!=cursor||Call(b,pe,*pump+0x20e)!=event)return {};
    if(!Match(b,pe,*pump+0x22d,"5E 5B 83 C4 44 C2 04 00",8))return {};
    if(!Match(b,pe,*pump+0x38,"BF 01 00 00 00 8D 6B 02",8)||
       !Match(b,pe,*pump+0x73,"89 5C 24 38 E9 40 01 00 00 89 5C 24 38 89 6C 24 34 E9 37 01 00 00 89 7C 24 38 E9 2A 01 00 00",31)||
       !Match(b,pe,*pump+0x1b8,"89 6C 24 38 89 5C 24 34",8))return {};
    const auto po=Offset(b,pe,*pump,0x23c);if(!po||!Pattern(b,*po+0x11d,"8B 0D ?? ?? ?? ??"))return {};
    const auto manager=Word(b,*po+0x11f),table=Word(b,*po+0x5a);if(manager<base||table<base)return {};
    const auto jump=Offset(b,pe,table-base,36);if(!jump)return {};
    for(const auto pair:{std::pair{0u,0x73u},std::pair{1u,0x89u},std::pair{2u,0x7cu},std::pair{3u,0x92u}})
        if(std::uint64_t(base)+*pump+pair.second!=Word(b,*jump+4*pair.first))return {};
    const auto convert=Call(b,pe,*pump+0x10c);if(!convert)return {};
    if(base!=0x400000||*convert!=mappingEvidence[0].rva)return {};
    for(const auto& evidence:mappingEvidence){
        const auto at=Offset(b,pe,evidence.rva,evidence.size);if(!at)return {};
        std::uint64_t hash=14695981039346656037ull;
        for(const auto byte:b.subspan(*at,evidence.size)){hash^=std::to_integer<unsigned char>(byte);hash*=1099511628211ull;}
        if(hash!=evidence.hash)return {};
    }
    for(const auto& profile:displays)for(unsigned i=0;i<displaySlots.size();++i){
        const auto at=Offset(b,pe,profile.vtable+displaySlots[i],4);
        if(!at||Word(b,*at)!=base+mappingEvidence[profile.evidence+i].rva)return {};
    }
    if(!Match(b,pe,*convert+0x212,"D9 44 24 14 5E DC 0D ?? ?? ?? ?? D9 18 D9 44 24 14 DC 0D ?? ?? ?? ?? D9 58 04 83 C4 18 C3",30))return {};
    const auto co=Offset(b,pe,*convert+0x212,30);const auto wx=Word(b,*co+7),hy=Word(b,*co+19);if(wx<base||hy<base)return {};
    const auto w=Offset(b,pe,wx-base,8),h=Offset(b,pe,hy-base,8);if(!w||!h)return {};double width=0,height=0;std::memcpy(&width,b.data()+*w,8);std::memcpy(&height,b.data()+*h,8);
    if(width!=1280||height!=720)return {};
    const auto cursorWrapper=Call(b,pe,*cursor+0x15),eventWrapper=Call(b,pe,*event+0x1a);if(!cursorWrapper||!eventWrapper)return {};
    if(!Match(b,pe,*cursorWrapper,"8B 44 24 04 8B 4C 24 08 56 8B 35 ?? ?? ?? ?? A3 ?? ?? ?? ?? A3 ?? ?? ?? ?? 8B 44 24 10 50 51 E8",32)||
       !Match(b,pe,*eventWrapper,"8B 44 24 04 8B 4C 24 0C 8B 54 24 08 56 8B 35 ?? ?? ?? ?? A3 ?? ?? ?? ?? A3 ?? ?? ?? ?? 8B 44 24 14 50 51 52 E8",37))return {};
    const auto cursorSink=Call(b,pe,*cursorWrapper+0x1f),eventSink=Call(b,pe,*eventWrapper+0x24);if(!cursorSink||!eventSink)return {};
    if(!Match(b,pe,*cursorSink,"83 3D ?? ?? ?? ?? 00 74 31 83 3D ?? ?? ?? ?? 00 75 28 A1 ?? ?? ?? ?? 8B 48 18 85 C9 74 1C",30)||
       !Match(b,pe,*eventSink,"83 3D ?? ?? ?? ?? 00 74 29 83 3D ?? ?? ?? ?? 00 75 20 A1 ?? ?? ?? ?? 8B 48 18 85 C9 74 14",30))return {};
    const auto cs=Offset(b,pe,*cursorSink,0x3b),es=Offset(b,pe,*eventSink,0x33),cw=Offset(b,pe,*cursorWrapper,0x35),ew=Offset(b,pe,*eventWrapper,0x3a);
    if(!cs||!es||!cw||!ew)return {};
    const auto enabled=Word(b,*cs+2),blocked=Word(b,*cs+11),target=Word(b,*cs+19);
    if(enabled<base||blocked<base||target<base||Word(b,*es+2)!=enabled||Word(b,*es+11)!=blocked||Word(b,*es+19)!=target||
       Word(b,*cw+11)!=target||Word(b,*cw+16)!=target||Word(b,*cw+41)!=target||
       Word(b,*ew+15)!=target||Word(b,*ew+20)!=target||Word(b,*ew+46)!=target||
       target>UINT32_MAX-4||Word(b,*cw+21)!=target+4||Word(b,*cw+47)!=target+4||
       Word(b,*ew+25)!=target+4||Word(b,*ew+52)!=target+4||
       !Match(b,pe,*cursorWrapper+0x24,"83 C4 08 89 35 ?? ?? ?? ?? 89 35 ?? ?? ?? ?? 5E C3",17)||
       !Match(b,pe,*eventWrapper+0x29,"83 C4 0C 89 35 ?? ?? ?? ?? 89 35 ?? ?? ?? ?? 5E C3",17))return {};
    if(!Match(b,pe,*cursorSink+0x1e,"8B 54 24 04 8B 44 24 08 C1 E2 0F 25 FF 7F 00 00 0B D0 03 D2 03 D2 52 E8",24))return {};
    const auto add=Call(b,pe,*cursorSink+0x35),eventPack=Call(b,pe,*eventSink+0x2d);if(!add||!eventPack)return {};
    if(!Match(b,pe,*add,"8B 41 28 83 EC 08 3B 41 08 7C 08 32 C0 83 C4 08 C2 04 00",19)||
       !Match(b,pe,*add+0x2e,"8B 71 2C 89 14 86 83 41 28 01",10)||
       !Match(b,pe,*eventPack,"8B 44 24 08 8B 54 24 0C 83 E0 7F 83 FA 01 75 18 8B 54 24 04 C1 E2 07 0B C2 C1 E0 0A 83 C8 05 50 E8",33)||Call(b,pe,*eventPack+0x20)!=add)return {};
    for(const auto rva:{manager-base,enabled-base,blocked-base,target-base})if(std::uint64_t(rva)+4>pe.imageSize)return {};
    const auto caller=Find(b,pe,"83 EC 14 55 8B E9 F3 0F 10 45 4C 0F 2F 05 ?? ?? ?? ?? F3 0F 11 44 24 04");
    if(!caller||!Match(b,pe,*caller+0x2c,"53 6A 01 E8 ?? ?? ?? ?? 83 C4 04 E8 ?? ?? ?? ?? 8B D8 85 DB 0F 84 4E 01 00 00",26)||
       !Match(b,pe,*caller+0x194,"8B CD E8 ?? ?? ?? ?? 53 8B CD E8 ?? ?? ?? ?? 53 8B CD E8 ?? ?? ?? ?? 6A 00 E8 ?? ?? ?? ?? 83 C4 04 5B 5D 83 C4 14 C2 04 00",41)||
       Call(b,pe,*caller+0x1a6)!=pump)return {};
    const auto inputGetter=Call(b,pe,*caller+0x37);if(!inputGetter||!Match(b,pe,*inputGetter,"A1 ?? ?? ?? ?? C3",6))return {};
    const auto getterAt=Offset(b,pe,*inputGetter,6);if(!getterAt)return {};
    const auto inputGlobal=Word(b,*getterAt+1);if(inputGlobal<base||std::uint64_t(inputGlobal-base)+4>pe.imageSize)return {};
    return MenuBindingCandidates{*pump,*cursor,*event,manager-base,enabled-base,blocked-base,target-base,1280,720,
        *caller,*caller+0x1ab,inputGlobal-base,*convert,0x1172a60};
}
std::optional<std::array<MenuBindingCodeSpan,33>> MenuBindingCodeSpans(
    std::span<const std::byte> bytes,const engine::PeImage& pe,const MenuBindingCandidates& binding){
    const auto verified=DiscoverMenuBinding(bytes,pe);if(!verified||*verified!=binding)return {};
    const auto cursorWrapper=Call(bytes,pe,binding.setCursor+0x15),eventWrapper=Call(bytes,pe,binding.sendEvent+0x1a);
    if(!cursorWrapper||!eventWrapper)return {};
    const auto cursorSink=Call(bytes,pe,*cursorWrapper+0x1f),eventSink=Call(bytes,pe,*eventWrapper+0x24);
    if(!cursorSink||!eventSink)return {};
    const auto add=Call(bytes,pe,*cursorSink+0x35),pack=Call(bytes,pe,*eventSink+0x2d);if(!add||!pack)return {};
    const auto inputGetter=Call(bytes,pe,binding.mousePumpCaller+0x37);if(!inputGetter)return {};
    std::array<MenuBindingCodeSpan,33> spans{{
        {binding.mousePump,0x25c},{binding.setCursor,0x20},{binding.sendEvent,0x25},
        {*cursorWrapper,0x35},{*eventWrapper,0x3a},{*cursorSink,0x3b},{*eventSink,0x33},{*add,0x67},{*pack,0x4b},{binding.mousePumpCaller,0x1bd},{*inputGetter,6}}};
    for(unsigned i=0;i<mappingEvidence.size();++i)spans[11+i]={mappingEvidence[i].rva,mappingEvidence[i].size};
    for(const auto& span:spans)if(!Offset(bytes,pe,span.rva,span.size))return {};
    return spans;
}
bool IsMenuPumpBoundary(const MenuMemory& memory,const MenuBindingCandidates& b,std::uint32_t base,
    std::uint64_t returnAddress,std::uint64_t self,std::uint64_t inputNode,std::uint64_t expectedController){
    if(base<0x10000||!b.mousePumpCaller||!b.inputNodeGlobal||
       std::uint64_t(b.mousePumpCaller)+0x1ab!=b.mousePumpReturn||
       std::uint64_t(base)+b.mousePumpReturn>UINT32_MAX||
       returnAddress!=std::uint64_t(base)+b.mousePumpReturn||
       expectedController<0x10000||expectedController>UINT32_MAX||self!=expectedController||
       inputNode<0x10000||inputNode>UINT32_MAX)return false;
    std::uint32_t first=0,second=0;
    return ReadWord(memory,std::uint64_t(base)+b.inputNodeGlobal,first)&&first==inputNode&&
        ReadWord(memory,std::uint64_t(base)+b.inputNodeGlobal,second)&&first==second;
}
std::optional<MenuTargetSnapshot> ReadMenuTarget(const MenuMemory& m,const MenuBindingCandidates& b,std::uint32_t base){
    if(base<0x10000||!b.managerGlobal||!b.inputEnabled||!b.inputBlocked||b.logicalWidth!=1280||b.logicalHeight!=720)return {};
    const auto read=[&]()->std::optional<MenuTargetSnapshot>{
        MenuTargetSnapshot s;std::uint32_t enabled=0,blocked=0;
        if(!ReadWord(m,std::uint64_t(base)+b.inputEnabled,enabled)||!enabled||!ReadWord(m,std::uint64_t(base)+b.inputBlocked,blocked)||blocked||
           !ReadWord(m,std::uint64_t(base)+b.managerGlobal,s.manager)||!ReadWord(m,std::uint64_t(s.manager)+0x14,s.ui)||
           !ReadWord(m,std::uint64_t(s.ui)+4,s.target)||!ReadWord(m,std::uint64_t(s.target)+0x18,s.queue)||
           !ReadWord(m,std::uint64_t(s.queue)+8,s.capacity)||!ReadWord(m,std::uint64_t(s.queue)+0x28,s.count)||!ReadWord(m,std::uint64_t(s.queue)+0x2c,s.buffer)||
           !s.capacity||s.capacity>16384||s.count>s.capacity||s.buffer<0x10000||std::uint64_t(s.buffer)+std::uint64_t(s.capacity)*4>UINT32_MAX)return {};
        std::uint32_t first=0,last=0;if(!ReadWord(m,s.buffer,first)||!ReadWord(m,std::uint64_t(s.buffer)+(s.capacity-1)*4,last))return {};return s;
    };
    const auto first=read(),second=read();if(!first||!second||*first!=*second)return {};return first;
}
std::optional<MenuPointerCommand> MakeMenuPointerCommand(const MenuBindingCandidates& b,float u,float v,MenuPointerKind kind,std::uint64_t generation,std::uint64_t epoch,std::int64_t deadline)noexcept {
    if(b.logicalWidth!=1280||b.logicalHeight!=720||!generation||!epoch||deadline<=0||!std::isfinite(u)||!std::isfinite(v)||u<0||v<0||u>1||v>1||kind>MenuPointerKind::PrimaryUp)return {};
    return MenuPointerCommand{generation,epoch,deadline,std::int32_t(u*float(b.logicalWidth)),std::int32_t(v*float(b.logicalHeight)),kind};
}
MenuDispatchResult DispatchMenuPointer(const MenuMemory& memory,const MenuBindingCandidates& b,std::uint32_t base,const MenuTargetSnapshot& expected,
    const MenuPointerCommand& command,const MenuDispatchGuard& guard,const MenuNativeCalls& calls,MenuOwnedPress& owned){
    if(!guard.nativeUiThread||!guard.menuOwned||!guard.focused||!guard.tracked||!guard.menuEpoch||guard.menuEpoch!=command.menuEpoch||
       guard.nowNs<=0||command.deadlineNs<=guard.nowNs||!command.generation||!calls.cursor||
       !b.setCursor||!b.sendEvent||std::uint64_t(base)+b.setCursor>UINT32_MAX||std::uint64_t(base)+b.sendEvent>UINT32_MAX||
       command.x<0||command.y<0||command.x>1280||command.y>720||command.kind>MenuPointerKind::PrimaryUp||
       (command.kind!=MenuPointerKind::Move&&!calls.event))return MenuDispatchResult::Rejected;
    if(command.kind==MenuPointerKind::PrimaryDown&&owned.Active())return MenuDispatchResult::Rejected;
    if(command.kind==MenuPointerKind::PrimaryUp&&(!owned.Active()||owned.epoch_!=command.menuEpoch||
       owned.base_!=base||owned.binding_!=b||!SameMenuTargetOwner(owned.target_,expected)))return MenuDispatchResult::Rejected;
    const auto target=ReadMenuTarget(memory,b,base);if(!target||!SameOwner(*target,expected))return MenuDispatchResult::TargetChanged;
    const auto needed=command.kind==MenuPointerKind::Move?1u:2u;if(target->capacity-target->count<needed)return MenuDispatchResult::QueueFull;
    if(!calls.cursor(calls.context,base+b.setCursor,target->manager,command.x,command.y))return MenuDispatchResult::CallFailed;
    if(command.kind!=MenuPointerKind::Move){
        const auto after=ReadMenuTarget(memory,b,base);if(!after||!SameOwner(*after,*target))return MenuDispatchResult::TargetChanged;
        if(after->capacity-after->count<1)return MenuDispatchResult::QueueFull;
        const std::array<std::uint32_t,3> event{0,command.kind==MenuPointerKind::PrimaryDown?0u:1u,1};
        if(!calls.event(calls.context,base+b.sendEvent,after->manager,event))return MenuDispatchResult::CallFailed;
        if(command.kind==MenuPointerKind::PrimaryDown){
            owned.target_=*after;owned.binding_=b;owned.base_=base;owned.epoch_=command.menuEpoch;owned.generation_=command.generation;
        }else owned.Forget();
    }
    return MenuDispatchResult::Invoked;
}
MenuDispatchResult DispatchMenuRasterPointer(const MenuMemory& memory,const MenuBindingCandidates& b,std::uint32_t base,
    const MenuTargetSnapshot& expected,float u,float v,MenuPointerKind kind,std::uint64_t generation,std::int64_t deadline,
    const MenuDispatchGuard& guard,const MenuNativeCalls& calls,MenuOwnedPress& owned,MenuRasterDiagnostic* diagnostic){
    if(diagnostic)*diagnostic={};
    auto command=MakeMenuPointerCommand(b,u,v,kind,generation,guard.menuEpoch,deadline);
    if(!command||!guard.nativeUiThread||!guard.menuOwned||!guard.focused||!guard.tracked||
       guard.nowNs<=0||deadline<=guard.nowNs||!calls.mapPixels||!calls.cursor||
       (kind!=MenuPointerKind::Move&&!calls.event))return MenuDispatchResult::Rejected;
    const auto target=ReadMenuTarget(memory,b,base);
    if(!target||!SameOwner(*target,expected))return MenuDispatchResult::TargetChanged;
    const auto display=ReadDisplay(memory,b,base);if(!display)return MenuDispatchResult::Rejected;
    const std::array<float,2> pixels{u*float(display->width),v*float(display->height)};
    std::array<float,2> logical{std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::quiet_NaN()};
    if(!calls.mapPixels(calls.context,base+b.coordinateMapper,logical,pixels))return MenuDispatchResult::CallFailed;
    const auto after=ReadDisplay(memory,b,base);
    if(!after||*after!=*display)return MenuDispatchResult::TargetChanged;
    if(!std::isfinite(logical[0])||!std::isfinite(logical[1])||
       logical[0]<0||logical[0]>1280||logical[1]<0||logical[1]>720)return MenuDispatchResult::Rejected;
    // Native pump uses cvttss2si after this exact mapper: truncate, never clamp
    // the letterbox/viewport exterior onto a clickable canvas edge.
    command->x=std::int32_t(logical[0]);command->y=std::int32_t(logical[1]);
    if(diagnostic)*diagnostic={display->object,display->vtable,display->width,display->height,{u,v},pixels,logical};
    return DispatchMenuPointer(memory,b,base,expected,*command,guard,calls,owned);
}
MenuDispatchResult ReleaseMenuPointer(const MenuMemory& memory,const MenuBindingCandidates& b,std::uint32_t base,
    const MenuTargetSnapshot& expected,std::uint64_t epoch,const MenuDispatchGuard& guard,const MenuNativeCalls& calls,MenuOwnedPress& owned){
    if(!owned.Active()||!guard.nativeUiThread||guard.nowNs<=0||!epoch||epoch!=owned.epoch_||
       base!=owned.base_||b!=owned.binding_||!SameOwner(expected,owned.target_)||!calls.event||
       !b.sendEvent||std::uint64_t(base)+b.sendEvent>UINT32_MAX)return MenuDispatchResult::Rejected;
    const auto target=ReadMenuTarget(memory,b,base);
    if(!target)return MenuDispatchResult::TargetChanged;
    if(!SameMenuTargetOwner(*target,owned.target_)){
        // ReadMenuTarget proved a stable, complete replacement routing graph.
        // The lease belongs to the old active owner, never the replacement.
        owned.Forget();return MenuDispatchResult::Retired;
    }
    if(target->count>=target->capacity)return MenuDispatchResult::QueueFull;
    if(!calls.event(calls.context,base+b.sendEvent,target->manager,{0,1,1}))return MenuDispatchResult::CallFailed;
    owned.Forget();return MenuDispatchResult::Invoked;
}
}
