#include "Bc2Profile.h"
#include "fvr/engine/BindingValidation.h"
#include <cstring>
#include <limits>
namespace fvr::bc2 {
namespace {
std::uint32_t U32(std::span<const std::byte> b,std::size_t o){std::uint32_t v=0;for(unsigned i=0;i<4;++i)v|=std::uint32_t(std::to_integer<unsigned char>(b[o+i]))<<(i*8);return v;}
std::optional<std::size_t> Offset(const engine::PeImage& pe,std::uint32_t rva,std::size_t bytes){
    for(const auto& s:pe.sections)if(rva>=s.rva){const auto d=std::uint64_t(rva)-s.rva;if(d<=s.rawSize&&bytes<=s.rawSize-d)return std::size_t(s.rawOffset+d);}return {};
}
bool Executable(const engine::PeImage& pe,std::uint32_t rva){for(const auto& s:pe.sections)if((s.flags&0x20000000)&&rva>=s.rva&&rva-s.rva<s.rawSize)return true;return false;}
std::optional<std::uint32_t> Find(std::span<const std::byte> b,const engine::PeImage& pe,const char* text){
    const auto pattern=engine::ParsePattern(text);if(!pattern)return {};std::optional<std::uint32_t> result;
    for(const auto& section:pe.sections)if(section.flags&0x20000000){
        const auto data=b.subspan(section.rawOffset,section.rawSize);
        // UniqueMatch returns none for zero AND multiple matches. Count here so
        // an ambiguous section cannot be hidden by a unique second section.
        unsigned count=0;std::size_t at=0;
        if(pattern->size()>data.size())continue;
        for(std::size_t i=0;i<=data.size()-pattern->size();++i){bool okay=true;for(std::size_t j=0;j<pattern->size();++j)if(!(*pattern)[j].wildcard&&std::to_integer<unsigned char>(data[i+j])!=(*pattern)[j].value){okay=false;break;}if(okay){++count;at=i;if(count>1)return {};}}
        if(count){if(result)return {};result=section.rva+static_cast<std::uint32_t>(at);}
    }return result;
}
}
std::optional<DiscoveryProfile> DiscoverProfile(std::span<const std::byte> b,const engine::PeImage& pe){
    // Re-validate the input to keep public parser usage bounded even if a caller
    // supplies stale or fabricated section metadata.
    const auto validated=engine::InspectPe(b);if(!validated.valid||pe.machine!=0x14c||validated.image.machine!=pe.machine||validated.image.imageSize!=pe.imageSize||validated.image.sections.size()!=pe.sections.size())return {};
    for(std::size_t i=0;i<pe.sections.size();++i){const auto& a=pe.sections[i];const auto& v=validated.image.sections[i];if(a.rva!=v.rva||a.rawOffset!=v.rawOffset||a.rawSize!=v.rawSize||a.virtualSize!=v.virtualSize||a.flags!=v.flags)return {};}
    const auto nt=U32(b,0x3c);const auto base=U32(b,std::size_t(nt)+24+28);if(base<0x10000||std::uint64_t(base)+pe.imageSize>UINT32_MAX)return {};
    const auto frame=Find(b,pe,"55 8B EC 83 E4 F0 81 EC 44 01 00 00 53 56 8B 35 ?? ?? ?? ?? 8B 06 8B 50 18 57 8B F9 8B CE FF D2");
    const auto dispatch=Find(b,pe,"56 8B 35 ?? ?? ?? ?? 8B 8E D8 09 00 00 E8 ?? ?? ?? ?? 8B CE E8 ?? ?? ?? ?? 8B 8E D8 09 00 00 5E E9");
    const auto present=Find(b,pe,"83 EC 20 53 56 8B F1 8B 06 8B 50 18 57 FF D2 E8 ?? ?? ?? ?? 8A D8 E8 ?? ?? ?? ?? 8B 4E 40 8A 51 35 8A 8E 86 01 00 00");
    const auto wrapper=Find(b,pe,"56 8B F1 8B 96 68 01 00 00 33 C9 38 8E 84 01 00 00 74 0F 38 8E 85 01 00 00 74 07 B9 01 00 00 00 33 D2 8B 86 88 00 00 00 57 8B 38 51 52 50 8B 47 20 FF D0");
    if(!frame||!dispatch||!present||!wrapper)return {};
    const auto fo=Offset(pe,*frame,32),doo=Offset(pe,*dispatch,35);if(!fo||!doo)return {};
    const auto renderer=U32(b,*fo+16),game=U32(b,*doo+3);
    if(renderer<base||game<base)return {};
    const auto globalOkay=[&](std::uint32_t va){for(const auto& s:pe.sections)if((s.flags&0x80000000)&&!(s.flags&0x20000000)&&va-base>=s.rva&&std::uint64_t(va-base-s.rva)+4<=s.virtualSize)return true;return false;};
    if(!globalOkay(renderer)||!globalOkay(game))return {};
    const auto target=std::int64_t(*dispatch)+25+std::int32_t(U32(b,*doo+21));if(target!=*frame)return {};
    // The unique native Present routine must be virtual slot 5. Dimension
    // getters at slots 10/11 independently identify this renderer layout.
    std::optional<std::uint32_t> vtable;
    for(const auto& s:pe.sections)if(!(s.flags&0x20000000)&&!(s.flags&0x80000000)){
        for(std::size_t i=20;i+28<=s.rawSize;i+=4){if(U32(b,s.rawOffset+i)!=base+*present)continue;
            const auto table=s.rawOffset+i-20;bool okay=true;
            for(unsigned slot=0;slot<12;++slot){auto fn=U32(b,table+slot*4);if(fn<base||!Executable(pe,fn-base)){okay=false;break;}}
            if(!okay)continue;
            const auto width=Offset(pe,U32(b,table+40)-base,4),height=Offset(pe,U32(b,table+44)-base,4);
            if(!width||!height||U32(b,*width)!=0xc318418b||U32(b,*height)!=0xc31c418b)continue;
            if(vtable)return {};vtable=s.rva+static_cast<std::uint32_t>(i)-20;
        }
    }
    if(!vtable)return {};
    return DiscoveryProfile{base,pe.imageSize,renderer-base,game-base,*frame,*dispatch,*present,*wrapper,*vtable};
}
}
namespace fvr::bc2 {
std::optional<RenderPathCandidates> DiscoverRenderPath(std::span<const std::byte> b,const engine::PeImage& pe){
    const auto baseProfile=DiscoverProfile(b,pe);if(!baseProfile)return {};
    const auto subsystem=Find(b,pe,"55 8B EC 83 E4 F0 81 EC 88 04 00 00 56 57 8B F9 80 7F 38 00 74 22 8B 4F 2C 8B 01 8B 57 30 8B 40 14 52 FF D0");
    const auto world=Find(b,pe,"83 EC 54 57 8B F9 8B 87 94 00 00 00 80 B8 D2 00 00 00 00 0F 84 ?? ?? ?? ?? 56 8B 74 24 60 83 BE C4 00 00 00 01 0F 85 ?? ?? ?? ?? 53 8B 9F 9C 00 00 00");
    const auto prepare=Find(b,pe,"81 EC 84 0F 00 00 53 55 56 57 89 4C 24 44 C7 44 24 14 00 00 00 00 8D B9 58 01 00 00 C7 44 24 48 09 00 00 00 BB 08 00 00 00");
    const auto draw=Find(b,pe,"55 8B EC 83 E4 F0 81 EC 94 03 00 00 53 56 8B F1 8B 0D ?? ?? ?? ?? 8B 01 8B 50 0C 57 FF D2 83 F8 02");
    const auto cache=Find(b,pe,"55 8B EC 83 E4 F0 81 EC 94 00 00 00 53 56 8B F1 57 8D 46 50 50 8D 4C 24 64 51 E8 ?? ?? ?? ?? 0F 10 08 0F 57 C0");
    if(!subsystem||!world||!prepare||!draw||!cache)return {};
    const auto wo=Offset(pe,*world,0x199),d=Offset(pe,*draw,33),c=Offset(pe,*cache,0x1df);if(!wo||!d||!c)return {};
    const auto relativeCall=[&](std::size_t offset,std::uint32_t target){return b[*wo+offset]==std::byte{0xe8}&&std::int64_t(*world)+std::int64_t(offset)+5+std::int32_t(U32(b,*wo+offset+1))==target;};
    if(!relativeCall(0xa7,*prepare)||!relativeCall(0x128,*draw))return {};
    if(U32(b,*d+18)!=baseProfile->preferredBase+baseProfile->rendererGlobal)return {};
    const unsigned char consumed[]={0xc7,0x86,0xc4,0,0,0,3,0,0,0};
    const unsigned char cacheTail[]={0x83,0x26,0xfe,0x5f,0x5e,0x5b,0x8b,0xe5,0x5d,0xc3};
    if(std::memcmp(b.data()+*wo+0x175,consumed,sizeof(consumed))||std::memcmp(b.data()+*c+0x1d5,cacheTail,sizeof(cacheTail)))return {};
    if(b[*wo+0x196]!=std::byte{0xc2}||b[*wo+0x197]!=std::byte{4}||b[*wo+0x198]!=std::byte{0})return {};
    const auto tableFor=[&](std::uint32_t function,unsigned entries)->std::optional<std::uint32_t>{
        std::optional<std::uint32_t> result;
        for(const auto& s:pe.sections)if(!(s.flags&0x20000000)&&!(s.flags&0x80000000)){
            const auto tableBytes=std::size_t(entries)*4;if(tableBytes>s.rawSize)continue;
            for(std::size_t offset=0;offset<=s.rawSize-tableBytes;offset+=4){const auto at=std::size_t(s.rawOffset)+offset;
                if(U32(b,at+20)!=baseProfile->preferredBase+function)continue;
                bool valid=true;for(unsigned slot=0;slot<entries;++slot){const auto fn=U32(b,at+slot*4);if(fn<baseProfile->preferredBase||!Executable(pe,fn-baseProfile->preferredBase)){valid=false;break;}}
                if(!valid)continue;if(result)return {};result=s.rva+static_cast<std::uint32_t>(offset);
            }
        }return result;
    };
    const auto subsystemTable=tableFor(*subsystem,7),worldTable=tableFor(*world,12);if(!subsystemTable||!worldTable)return {};
    return RenderPathCandidates{*subsystemTable,*subsystem,*worldTable,*world,*prepare,*draw,*cache};
}
}
namespace fvr::bc2 {
std::optional<ViewLayoutCandidates> DiscoverViewLayout(std::span<const std::byte> b,const engine::PeImage& pe){
    const auto profile=DiscoverProfile(b,pe);if(!profile)return {};
    const auto constructor=Find(b,pe,"8B 44 24 08 56 57 33 FF 57 57 8B F1 8B 4C 24 14 50 51 8B CE E8 ?? ?? ?? ?? C7 06 ?? ?? ?? ?? 89 BE 70 16 00 00 89 BE 74 16 00 00");
    if(!constructor)return {};const auto ctor=Offset(pe,*constructor,0xe6);if(!ctor)return {};
    const unsigned char registration[]={0x8b,0x4e,0x70,0x8b,0x41,0x28,0x83,0xc1,0x24,0x3b,0x41,8};
    if(std::memcmp(b.data()+*ctor+0xda,registration,sizeof(registration)))return {};
    const auto concreteVtable=U32(b,*ctor+27);
    const auto setters=Find(b,pe,"81 C1 90 00 00 00 E9 ?? ?? ?? ?? CC CC CC CC CC 81 C1 F0 04 00 00 E9 ?? ?? ?? ??");
    if(!setters)return {};const auto so=Offset(pe,*setters,27);if(!so)return {};
    const auto copy=std::int64_t(*setters)+11+std::int32_t(U32(b,*so+7));
    if(copy<0||copy>UINT32_MAX||copy!=std::int64_t(*setters)+27+std::int32_t(U32(b,*so+23))||!Executable(pe,std::uint32_t(copy)))return {};
    const auto co=Offset(pe,std::uint32_t(copy),0x1c8);if(!co)return {};
    const unsigned char head[]={0x55,0x8b,0xec,0x83,0xe4,0xf0,0x83,0xec,8,0x56,0x8b,0xf1,0x57,0x8b,0x7d,8};
    if(std::memcmp(b.data()+*co,head,sizeof(head))||b[*co+0x1c5]!=std::byte{0xc2}||b[*co+0x1c6]!=std::byte{4}||b[*co+0x1c7]!=std::byte{0})return {};
    const auto member=[&](std::uint32_t fn,bool lea)->std::optional<std::uint32_t>{
        if(fn<profile->preferredBase||!Executable(pe,fn-profile->preferredBase))return {};
        const auto o=Offset(pe,fn-profile->preferredBase,7);if(!o)return {};
        if(b[*o]!=std::byte(lea?0x8d:0x8a)||b[*o+1]!=std::byte{0x81}||b[*o+6]!=std::byte{0xc3})return {};
        return U32(b,*o+2);
    };
    std::optional<ViewLayoutCandidates> result;
    for(const auto& section:pe.sections)if(!(section.flags&0x20000000)&&!(section.flags&0x80000000)&&section.rawSize>=19*4){
        for(std::size_t offset=0;offset<=section.rawSize-19*4;offset+=4){const auto at=std::size_t(section.rawOffset)+offset;
            if(profile->preferredBase+section.rva+offset!=concreteVtable)continue;
            if(U32(b,at+5*4)!=profile->preferredBase+*setters||U32(b,at+7*4)!=profile->preferredBase+*setters+16)continue;
            bool allCode=true;for(unsigned slot=0;slot<19;++slot){const auto fn=U32(b,at+slot*4);if(fn<profile->preferredBase||!Executable(pe,fn-profile->preferredBase)){allCode=false;break;}}if(!allCode)continue;
            const auto primary=member(U32(b,at+6*4),true),secondary=member(U32(b,at+8*4),true),third=member(U32(b,at+9*4),true),fourth=member(U32(b,at+10*4),true);
            const auto active=member(U32(b,at+18*4),false),viewport=member(U32(b,at+16*4),true);
            const auto owner=Offset(pe,U32(b,at+2*4)-profile->preferredBase,4);
            if(!primary||!secondary||!third||!fourth||!active||!viewport||!owner||U32(b,*owner)!=0xc370418b)continue;
            // This observed layout has four complete RenderView blocks, not just
            // the top-level camera. No semantic meaning is assigned to history.
            if(*primary!=0x90||*secondary!=0x4f0||*third!=0xc70||*fourth!=0x10d0||*active!=0xc52||*viewport!=0xc58)continue;
            if(result)return {};
            result=ViewLayoutCandidates{section.rva+std::uint32_t(offset),*setters,*setters+16,std::uint32_t(copy),0x70,*primary,*secondary,*third,*fourth,*active,*viewport};
        }
    }return result;
}
}
namespace fvr::bc2 {
std::optional<CameraCacheCandidates> DiscoverCameraCaches(std::span<const std::byte> b,const engine::PeImage& pe){
    if(!DiscoverProfile(b,pe))return {};
    const auto view=Find(b,pe,"56 8B F1 F6 06 01 74 05 E8 ?? ?? ?? ?? 8D 86 20 02 00 00 5E C3");
    const auto projection=Find(b,pe,"56 8B F1 F6 06 02 74 05 E8 ?? ?? ?? ?? 8D 86 E0 02 00 00 5E C3");
    const auto frustum=Find(b,pe,"56 8B F1 F6 06 08 74 05 E8 ?? ?? ?? ?? 8D 86 90 00 00 00 5E C3");
    const auto updateView=Find(b,pe,"55 8B EC 83 E4 F0 81 EC 94 00 00 00 53 56 8B F1 57 8D 46 50 50 8D 4C 24 64 51 E8 ?? ?? ?? ?? 0F 10 08 0F 57 C0");
    const auto updateProjection=Find(b,pe,"55 8B EC 83 E4 F0 83 EC 44 53 56 8B F1 83 7E 04 01 57 75 3B 0F B6");
    const auto updateFrustum=Find(b,pe,"51 56 8B F1 83 7E 04 01 D9 46 20 75 35 0F B6 46 08 50 8D 4E");
    if(!view||!projection||!frustum||!updateView||!updateProjection||!updateFrustum)return {};
    for(auto pair:{std::pair{*view,*updateView},std::pair{*projection,*updateProjection},std::pair{*frustum,*updateFrustum}}){
        const auto offset=Offset(pe,pair.first,21);if(!offset||std::int64_t(pair.first)+13+std::int32_t(U32(b,*offset+9))!=pair.second)return {};
    }
    const unsigned char viewTail[]={0x83,0x26,0xfe,0x5f,0x5e,0x5b,0x8b,0xe5,0x5d,0xc3};
    const unsigned char projectionTail[]={0x83,0x26,0xfd,0x5f,0x5e,0x5b,0x8b,0xe5,0x5d,0xc3};
    const unsigned char frustumTail[]={0x83,0x26,0xf7,0x5e,0x59,0xc3};
    const auto v=Offset(pe,*updateView+0x1d5,sizeof(viewTail)),p=Offset(pe,*updateProjection+0x1c1,sizeof(projectionTail)),f=Offset(pe,*updateFrustum+0x7c,sizeof(frustumTail));
    if(!v||!p||!f||std::memcmp(b.data()+*v,viewTail,sizeof(viewTail))||std::memcmp(b.data()+*p,projectionTail,sizeof(projectionTail))||std::memcmp(b.data()+*f,frustumTail,sizeof(frustumTail)))return {};
    return CameraCacheCandidates{*view,*projection,*frustum,*updateView,*updateProjection,*updateFrustum};
}
}

namespace fvr::bc2 {
std::optional<VisibilityPathCandidates> DiscoverVisibilityPath(std::span<const std::byte> b,const engine::PeImage& pe){
    const auto profile=DiscoverProfile(b,pe);const auto render=DiscoverRenderPath(b,pe);if(!profile||!render)return {};
    const auto update=Find(b,pe,"55 8B EC 83 E4 F0 81 EC F4 09 00 00 53 56 57 8B F1 E8 ?? ?? ?? ?? 84 C0 74 09 8B 06 8B 50 18 8B CE FF D2");
    const auto prepare=Find(b,pe,"55 8B EC 83 E4 F0 B8 04 68 00 00 E8 ?? ?? ?? ?? 53 8B 5D 0C 89 4C 24 2C 8B 8B 84 16 00 00 2B 8B 80 16 00 00");
    if(!update||!prepare)return {};
    const auto table=Offset(pe,render->worldRendererVtable+12,4),u=Offset(pe,*update,0x3f1),p=Offset(pe,*prepare+0x2e9a,9);
    if(!table||!u||!p||U32(b,*table)!=profile->preferredBase+*update)return {};
    if(b[*u+0x383]!=std::byte{0xe8}||std::int64_t(*update)+0x388+std::int32_t(U32(b,*u+0x384))!=*prepare)return {};
    const unsigned char updateTail[]={0x5f,0x5e,0x5b,0x8b,0xe5,0x5d,0xc2,0x18,0};
    const unsigned char prepareTail[]={0x5f,0x5e,0x5b,0x8b,0xe5,0x5d,0xc2,0x18,0};
    if(std::memcmp(b.data()+*u+0x3e8,updateTail,9)||std::memcmp(b.data()+*p,prepareTail,9))return {};
    return VisibilityPathCandidates{*update,*prepare};
}
}

namespace fvr::bc2 {
std::optional<ViewLifecycleCandidates> DiscoverViewLifecycle(std::span<const std::byte> b,const engine::PeImage& pe){
    const auto profile=DiscoverProfile(b,pe);const auto layout=DiscoverViewLayout(b,pe);if(!profile||!layout)return {};
    const auto factory=Find(b,pe,"56 68 B0 56 00 00 8B F1 E8 ?? ?? ?? ?? 83 C4 04 85 C0 74 11 8B 4C 24 08 51 56 8B C8 E8 ?? ?? ?? ?? 5E C2 04 00 33 C0 5E C2 04 00");
    const auto viewTable=Offset(pe,layout->vtable,240);if(!viewTable)return {};
    const auto add=U32(b,*viewTable)-profile->preferredBase;
    const auto addOffset=Offset(pe,add,14);
    const unsigned char addCode[]={0x83,0xc1,0x20,0xb8,1,0,0,0,0xf0,0x0f,0xc1,1,0x40,0xc3};
    if(!addOffset||!Executable(pe,add)||std::memcmp(b.data()+*addOffset,addCode,14))return {};
    const auto release=Find(b,pe,"56 8D 41 20 83 CE FF F0 0F C1 30 4E 75 10 85 C9 74 0C 8B 11 8B 82 EC 00 00 00 6A 01 FF D0 8B C6 5E C3");
    if(!factory||!release)return {};
    const auto f=Offset(pe,*factory,43),vt=Offset(pe,layout->vtable,240);if(!f||!vt)return {};
    const auto relative=[&](std::uint32_t address,unsigned at)->std::optional<std::uint32_t>{
        const auto o=Offset(pe,address+at,5);if(!o||b[*o]!=std::byte{0xe8})return {};
        const auto target=std::int64_t(address)+at+5+std::int32_t(U32(b,*o+1));if(target<0||target>UINT32_MAX||!Executable(pe,std::uint32_t(target)))return {};return std::uint32_t(target);
    };
    const auto constructor=relative(*factory,28);if(!constructor)return {};
    const auto ctor=Offset(pe,*constructor,0x111);if(!ctor||U32(b,*ctor+27)!=profile->preferredBase+layout->vtable)return {};
    const unsigned char registration[]={0x8b,0x4e,0x70,0x8b,0x41,0x28,0x83,0xc1,0x24,0x3b,0x41,8};
    if(std::memcmp(b.data()+*ctor+0xda,registration,sizeof(registration))||b[*ctor+0xfc]!=std::byte{0xc2}||b[*ctor+0xfd]!=std::byte{8}||b[*ctor+0xfe]!=std::byte{0})return {};
    if(U32(b,*vt)!=profile->preferredBase+add||U32(b,*vt+4)!=profile->preferredBase+*release)return {};
    const auto active=U32(b,*vt+17*4)-profile->preferredBase,del=U32(b,*vt+59*4)-profile->preferredBase;
    const auto a=Offset(pe,active,13),d=Offset(pe,del,30);if(!a||!d||!Executable(pe,active)||!Executable(pe,del))return {};
    const unsigned char activeCode[]={0x8a,0x44,0x24,4,0x88,0x81,0x52,0xc,0,0,0xc2,4,0};
    const unsigned char delHead[]={0x56,0x8b,0xf1};
    if(std::memcmp(b.data()+*a,activeCode,13)||std::memcmp(b.data()+*d,delHead,3)||b[*d+0x1b]!=std::byte{0xc2}||b[*d+0x1c]!=std::byte{4}||b[*d+0x1d]!=std::byte{0})return {};
    const auto destructor=relative(del,3);if(!destructor)return {};
    const auto dt=Offset(pe,*destructor,0x55);if(!dt||U32(b,*dt+13)!=profile->preferredBase+layout->vtable)return {};
    const unsigned char unregister[]={0x8b,0x5e,0x70,0x8b,0x4b,0x28,0x8b,0x43,0x24,0x3b,0xc1};
    const unsigned char shrink[]={0x83,0x43,0x28,0xfc};
    if(std::memcmp(b.data()+*dt+0x24,unregister,11)||std::memcmp(b.data()+*dt+0x51,shrink,4))return {};
    std::optional<std::uint32_t> requestTable;
    for(const auto& section:pe.sections)if(!(section.flags&0xa0000000)&&section.rawSize>=20){
        for(std::size_t offset=0;offset<=section.rawSize-20;offset+=4){const auto at=std::size_t(section.rawOffset)+offset;
            if(U32(b,at+12)!=profile->preferredBase+*factory)continue;
            bool valid=true;for(unsigned i=0;i<5;++i){const auto va=U32(b,at+i*4);if(va<profile->preferredBase||!Executable(pe,va-profile->preferredBase))valid=false;}
            if(!valid)continue;if(requestTable)return {};requestTable=section.rva+std::uint32_t(offset);
        }
    }
    if(!requestTable)return {};
    return ViewLifecycleCandidates{*requestTable,*factory,*constructor,add,*release,active,del,*destructor};
}
}

namespace fvr::bc2 {
std::optional<ViewCallbackCandidates> DiscoverViewCallbacks(std::span<const std::byte> b,const engine::PeImage& pe){
    if(!DiscoverProfile(b,pe))return {};
    const auto remember=Find(b,pe,"56 8B 74 24 08 8B 06 8B 50 0C 57 8B F9 8B CE FF D2 85 C0 74 0B 8B 06 8B 50 0C 8B CE FF D2 8B F0 8B 06 8B 90 9C 00 00 00 8B CE FF D2 85 C0 75 06 89 B7 BC 00 00 00 5F 5E C2 04 00");
    const auto registration=Find(b,pe,"56 8B F1 8B 46 24 3B 46 28 8D 4E 20 73 18 85 C0 8D 50 04 89 51 04 74 19 8B 4C 24 08 89 08 C6 46 30 01 5E C2 04 00 8D 54 24 08 52 50 E8 ?? ?? ?? ?? C6 46 30 01 5E C2 04 00");
    const auto unregistration=Find(b,pe,"56 8B F1 8B 4E 24 8B 46 20 3B C1 74 0F 8B 54 24 08 39 10 74 07 83 C0 04 3B C1 75 F5 8D 50 04 3B D1 73 0E 2B CA 51 52 50 FF 15 ?? ?? ?? ?? 83 C4 0C 83 46 24 FC C6 46 30 01 5E C2 04 00");
    if(!remember||!registration||!unregistration)return {};
    return ViewCallbackCandidates{*remember,*registration,*unregistration};
}
}

namespace fvr::bc2 {
std::optional<ViewInitializationCandidates> DiscoverViewInitialization(std::span<const std::byte> b,const engine::PeImage& pe){
    const auto profile=DiscoverProfile(b,pe);const auto render=DiscoverRenderPath(b,pe);if(!profile||!render)return {};
    const auto rebuild=Find(b,pe,"55 8B EC 83 E4 F8 81 EC C8 00 00 00 53 55 56 8B F1 57 89 74 24 18 E8 ?? ?? ?? ?? 8B CE E8 ?? ?? ?? ?? 8B 86 AC 00 00 00");
    const auto parent=Find(b,pe,"8B 81 70 16 00 00 C3 CC CC CC CC CC CC CC CC CC");
    const auto refresh=Find(b,pe,"83 EC 18 53 55 8B D9 8B 4B 08 68 ?? ?? ?? ?? 8D 44 24 14 33 ED 50 C6 43 30 00 89 6B 0C E8");
    if(!rebuild||!parent||!refresh)return {};
    const auto refreshTail=Offset(pe,*refresh+0x15c,8);
    const unsigned char tailRefresh[]={0x5d,0x5b,0x83,0xc4,0x18,0xc2,4,0};
    if(!refreshTail||std::memcmp(b.data()+*refreshTail,tailRefresh,8))return {};
    const auto table=Offset(pe,render->worldRendererVtable+24,4),body=Offset(pe,*rebuild,0xa26);
    if(!table||!body||U32(b,*table)!=profile->preferredBase+*rebuild)return {};
    const unsigned char childGate[]={0x8b,0x8b,0x84,0x16,0,0,0x2b,0x8b,0x80,0x16,0,0,0x8d,0x83,0x80,0x16,0,0};
    const unsigned char tail[]={0x5f,0x5e,0x5d,0x5b,0x8b,0xe5,0x5d,0xc3};
    if(std::memcmp(b.data()+*body+0x7ad,childGate,sizeof(childGate))||std::memcmp(b.data()+*body+0xa1e,tail,sizeof(tail)))return {};
    return ViewInitializationCandidates{*rebuild,*parent,*refresh};
}
}

namespace fvr::bc2 {
std::optional<std::uint32_t> DiscoverContextCamera(std::span<const std::byte> b,const engine::PeImage& pe){
    if(!DiscoverProfile(b,pe))return {};
    const auto entry=Find(b,pe,"55 8B EC 83 E4 F0 83 EC 18 56 57 8B 7D 08 8A 47 08 8B F1 8B 4E 0C 81 4E 08 00 10 00 00 81 4E 08 00 08 00 00 80 46 07 02");
    if(!entry)return {};const auto at=Offset(pe,*entry,0xce);if(!at)return {};
    const unsigned char position[]={0x89,0x46,0x24},view[]={0x89,0x46,0x1c},tail[]={0x5f,0x5e,0x8b,0xe5,0x5d,0xc2,8,0};
    if(std::memcmp(b.data()+*at+0x57,position,3)||std::memcmp(b.data()+*at+0x8c,view,3)||std::memcmp(b.data()+*at+0xc6,tail,8))return {};
    return entry;
}
}

namespace fvr::bc2 {
std::optional<ProjectionOverrideCandidates> DiscoverProjectionOverrides(std::span<const std::byte> b,const engine::PeImage& pe){
    const auto camera=DiscoverContextCamera(b,pe);if(!camera)return {};
    const auto setter=Find(b,pe,"55 8B EC 83 E4 F0 83 EC 4C 56 8B F1 8B 46 0C 81 4E 08 00 80 00 00 80 46 07 01 89 46 0C");
    const auto mesh=Find(b,pe,"F6 84 24 90 00 00 00 02 74 0C 8D 8C 24 90 00 00 00 E8 ?? ?? ?? ?? 8D 8C 24 70 03 00 00 51 8B CE E8 ?? ?? ?? ?? 80 7D 34 00");
    const auto terrain=Find(b,pe,"F6 84 24 C0 00 00 00 02 8B D8 74 0C 8D 8C 24 C0 00 00 00 E8 ?? ?? ?? ?? 8D 8C 24 A0 03 00 00 51 8B CB E8 ?? ?? ?? ?? 8B 54 24 20");
    if(!setter||!mesh||!terrain)return {};
    const auto linked=[&](unsigned call){const auto at=Offset(pe,call,5);return at&&b[*at]==std::byte{0xe8}&&std::int64_t(call)+5+std::int32_t(U32(b,*at+1))==*setter;};
    const auto at=Offset(pe,*setter,0x6d);const unsigned char tail[]={0x89,0x46,0x20,0x5e,0x8b,0xe5,0x5d,0xc2,4,0};
    if(!at||std::memcmp(b.data()+*at+0x63,tail,sizeof(tail))||!linked(*camera+0xa4)||!linked(*mesh+0x20)||!linked(*terrain+0x22))return {};
    return ProjectionOverrideCandidates{*setter,*mesh+0x25,*terrain+0x27};
}
}

namespace fvr::bc2 {
std::optional<GameplayCandidates> DiscoverGameplay(std::span<const std::byte> b,const engine::PeImage& pe){
    const auto profile=DiscoverProfile(b,pe);if(!profile)return {};
    const auto context=Find(b,pe,"B8 01 00 00 00 84 05 ?? ?? ?? ?? 75 66 09 05 ?? ?? ?? ?? 50 B9 ?? ?? ?? ?? E8 ?? ?? ?? ?? 33 C0 68 ?? ?? ?? ?? C7 05");
    const auto update=Find(b,pe,"83 EC 08 53 55 56 57 8B F1 E8 ?? ?? ?? ?? D9 44 24 1C 51 8B 8E 74 0C 00 00 D9 1C 24 8B E8 8B 7D 0C");
    const auto gather=Find(b,pe,"83 EC 14 53 55 56 8B 74 24 24 57 8B D9 33 ED 90 8B 4B 18 33 D2 8B C5 F7 F1 8B 7B 14 8B 04 97 85 C0 8D 14 97");
    const auto soldier=Find(b,pe,"8B 81 54 0C 00 00 85 C0 74 0A 8B 00 85 C0 74 04 83 C0 FC C3 33 C0 C3");
    const auto setter=Find(b,pe,"8B 81 B4 00 00 00 8B 54 24 04 3B C2 74 1C 85 C0 74 07 80 A0 CD 0C 00 00 F7 85 D2 89 91 B4 00 00 00 74 07 80 8A CD 0C 00 00 08 C2 04 00");
    const auto manager=Find(b,pe,"51 53 55 8B 6C 24 10 56 57 55 8B F1 E8 ?? ?? ?? ?? 8D 4E 10 E8 ?? ?? ?? ?? 8D 7E 4C 8B CF E8 ?? ?? ?? ?? 8D 4F 14 33 DB");
    if(!context||!update||!gather||!soldier||!setter||!manager||*setter<16)return {};
    const auto co=Offset(pe,*context,0x79),up=Offset(pe,*update,0x1eb),ga=Offset(pe,*gather,0x232),ma=Offset(pe,*manager,0x74),lo=Offset(pe,*setter-16,7);
    if(!co||!up||!ga||!ma||!lo)return {};
    const auto base=profile->preferredBase,object=U32(b,*co+21),managerTable=U32(b,*ma+0x6f);
    if(object<base||managerTable<base||!Offset(pe,managerTable-base,12))return {};
    bool contextStorage=false;for(const auto& section:pe.sections)if((section.flags&0x80000000)&&!(section.flags&0x20000000)&&object-base>=section.rva&&std::uint64_t(object-base-section.rva)+0x5c<=section.virtualSize)contextStorage=true;
    if(!contextStorage)return {};
    if(b[*co+0x73]!=std::byte{0xb8}||U32(b,*co+0x74)!=object||b[*co+0x78]!=std::byte{0xc3})return {};
    if(std::int64_t(*update)+14+std::int32_t(U32(b,*up+10))!=*context)return {};
    const unsigned char ret4[]={0xc2,4,0},getter[]={0x8b,0x81,0xb4,0,0,0,0xc3};
    if(std::memcmp(b.data()+*up+0x1e8,ret4,3)||std::memcmp(b.data()+*ga+0x22f,ret4,3)||std::memcmp(b.data()+*lo,getter,7))return {};
    // The original player path passes its +c74 cache to the router vtable slot3.
    const unsigned char dispatch[]={0x8b,0x13,0x8b,0x86,0x74,0x0c,0,0,0x8b,0x52,0x0c,0x50,0x8b,0xcb,0xff,0xd2};
    if(std::memcmp(b.data()+*up+0xe5,dispatch,sizeof(dispatch)))return {};
    std::optional<std::uint32_t> table;
    for(const auto& section:pe.sections)if(!(section.flags&0xa0000000))for(std::size_t i=12;i+8<=section.rawSize;i+=4){
        if(U32(b,section.rawOffset+i)!=base+*gather)continue;
        bool valid=true;for(unsigned slot=0;slot<5;++slot){const auto fn=U32(b,section.rawOffset+i-12+4*slot);if(fn<base||!Executable(pe,fn-base))valid=false;}
        if(valid){if(table)return {};table=section.rva+std::uint32_t(i)-12;}
    }
    if(!table)return {};
    return GameplayCandidates{object-base,*context,managerTable-base,*setter-16,*soldier,*update,*gather,*table,0xb4,0xc54,0xc74};
}
}

namespace fvr::bc2 {
std::optional<InputBindingCandidates> DiscoverInputBinding(std::span<const std::byte> b,const engine::PeImage& pe){
    const auto game=DiscoverGameplay(b,pe);if(!game)return {};
    const auto base=U32(b,U32(b,0x3c)+24+28);
    const auto ctor=Find(b,pe,"56 8B F1 68 BC 00 00 00 8D 46 08 6A 00 50 C7 06 ?? ?? ?? ?? E8 ?? ?? ?? ?? F3 0F 10 05 ?? ?? ?? ?? 83 C4 0C F3 0F 11 86 B0 00 00 00 C7 86 C4 00 00 00 FF FF FF FF C6 86 C8 00 00 00 01 8B C6 5E C3");
    const auto boolean=Find(b,pe,"53 8B 5C 24 08 56 8B F1 57 B8 01 00 00 00 33 D2 8B CB E8 ?? ?? ?? ?? 8B C8 8B FA 25 FF 07 00 00 33 D2 81 E7 FF FF 01 00 0B C2 74 29");
    const auto scalar=Find(b,pe,"D9 44 24 08 83 EC 08 DD 05 ?? ?? ?? ?? 56 57 DF F1 DD D8 8B F9 76 0A F3 0F 10 05 ?? ?? ?? ?? EB 06 F3 0F 10 44 24 18 D9 E8");
    const auto controlled=Find(b,pe,"8B 81 68 0C 00 00 C3 CC CC CC CC CC CC CC CC CC 8B C1 C7 00 ?? ?? ?? ?? C3");
    const auto attached=Find(b,pe,"8B 81 54 0C 00 00 85 C0 74 39 8B 00 85 C0 74 33 83 C0 FC 74 2E 8B 81 54 0C 00 00 85 C0 74 0B");
    if(!ctor||!boolean||!scalar||!controlled||!attached)return {};
    const auto c=Offset(pe,*ctor,0x41),s=Offset(pe,*scalar,0x108),bo=Offset(pe,*boolean,0x8c),a=Offset(pe,*attached,0x46),u=Offset(pe,game->playerInputUpdate,0x1eb);
    if(!c||!s||!bo||!a||!u)return {};
    const auto table=U32(b,*c+16);if(table<base||!Offset(pe,table-base,4))return {};
    const unsigned char tail[]={0xc2,8,0},floatWrite[]={0xf3,0x0f,0x11,0x44,0xb7,8},attachedTail[]={0x39,0x81,0x68,0x0c,0,0,0x75,0x0f,0x83,0xb9,0x60,0x0c,0,0,0,0x74,6,0xb8,1,0,0,0,0xc3,0x33,0xc0,0xc3};
    if(std::memcmp(b.data()+*bo+0x89,tail,3)||U32(b,*bo+0x5b)!=0x98||U32(b,*bo+0x6a)!=0x9c||
       U32(b,*s+0x61)!=0x7ff||U32(b,*s+0x6c)!=0x6f3||std::memcmp(b.data()+*s+0xac,floatWrite,6)||
       std::memcmp(b.data()+*s+0xb7,tail,3)||std::memcmp(b.data()+*a+0x2c,attachedTail,sizeof(attachedTail)))return {};
    // The caller's predicate chooses the entry; this cannot use an arbitrary
    // soldier pointer while the local player is operating a vehicle.
    if(b[*u+0x6d]!=std::byte{0xe8}||std::int64_t(game->playerInputUpdate)+0x72+std::int32_t(U32(b,*u+0x6e))!=*attached)return {};
    const auto ga=Offset(pe,game->inputGather,0x232);if(!ga)return {};
    const auto linked=[&](unsigned at,unsigned target){return b[*ga+at]==std::byte{0xe8}&&std::int64_t(game->inputGather)+at+5+std::int32_t(U32(b,*ga+at+1))==target;};
    const unsigned char count49[]={0x83,0xf8,0x31};
    if(!linked(0x1c7,*scalar)||!linked(0x20a,*boolean)||std::memcmp(b.data()+*ga+0x21b,count49,3))return {};
    const auto named=[&](unsigned va,const char* name){const auto size=std::strlen(name)+1;if(va<base)return false;const auto at=Offset(pe,va-base,size);return at&&!std::memcmp(b.data()+*at,name,size);};
    std::optional<unsigned> actions;
    for(const auto& section:pe.sections)if(!(section.flags&0x20000000))for(std::size_t i=0;i+50*24<=section.rawSize;i+=4){
        const auto at=section.rawOffset+i;if(U32(b,at+16)!=0||!named(U32(b,at),"EiaThrottle"))continue;
        bool okay=true;
        for(auto item:{std::pair{1u,"EiaStrafe"},std::pair{7u,"EiaSwitchPrimaryWeapon"},std::pair{8u,"EiaFire"},std::pair{14u,"EiaZoom"},std::pair{15u,"EiaJump"},std::pair{16u,"EiaChangeVehicle"},std::pair{26u,"EiaChangePose"},std::pair{27u,"EiaInteract"},std::pair{29u,"EiaReload"},std::pair{31u,"EiaSprint"},std::pair{32u,"EiaIngameMenu"},std::pair{38u,"EiaThrowGrenade"},std::pair{49u,"EiaUndefined"}})
            if(U32(b,at+item.first*24+16)!=item.first||!named(U32(b,at+item.first*24),item.second))okay=false;
        if(okay){if(actions)return {};actions=section.rva+unsigned(i);}
    }
    if(!actions)return {};
    return InputBindingCandidates{*game,*ctor,table-base,*boolean,*scalar,*controlled,*attached,*actions};
}
}

namespace fvr::bc2 {
std::optional<AimCandidates> DiscoverAiming(std::span<const std::byte> b,const engine::PeImage& pe){
    const auto game=DiscoverGameplay(b,pe);if(!game)return {};
    const auto weapon=Find(b,pe,"56 8B F1 F6 86 14 01 00 00 01 74 08 8B 8E 4C 02 00 00 EB 06 8B 8E 48 02 00 00 E8 ?? ?? ?? ?? 8B 8E 64 02 00 00 2B 8E 60 02 00 00 C1 F9 02 3B C1 72 04 33 C0 5E C3 8B 96 60 02 00 00 8B 04 82 5E C3");
    const auto yawTail=Find(b,pe,"33 C0 8B C8 E8 ?? ?? ?? ?? D9 40 0C 5E C3 8B 96 60 02 00 00 8B 04 82 8B C8 E8 ?? ?? ?? ?? D9 40 0C 5E C3 D9 86 00 04 00 00 5E C3");
    const auto prepare=Find(b,pe,"81 EC AC 01 00 00 53 56 8B F1 8B 86 70 0C 00 00 50 8D 4C 24 10 E8 ?? ?? ?? ?? 8B 8E 74 0C 00 00 51 8D 4C 24 10 E8");
    const auto absolute=Find(b,pe,"F3 0F 10 44 24 04 F3 0F 11 81 A8 00 00 00 C2 04 00 CC CC CC CC CC CC CC CC CC CC CC CC CC CC CC 8B 81 9C 00 00 00 33 D2 25 00 00 04 00 F7 DA");
    if(!weapon||!yawTail||*yawTail<0x3b||!prepare||!absolute)return {};
    const auto w=Offset(pe,*weapon,0x41),y=Offset(pe,*yawTail-0x3b,0xa6),pr=Offset(pe,*prepare,0x1a3),up=Offset(pe,game->playerInputUpdate,0x1eb);
    if(!w||!y||!pr||!up)return {};
    const auto target=[&](unsigned rva,std::size_t at,unsigned offset)->std::optional<unsigned>{
        if(b[at+offset]!=std::byte{0xe8})return {};const auto va=std::int64_t(rva)+offset+5+std::int32_t(U32(b,at+offset+1));
        if(va<0||va>UINT32_MAX||!Executable(pe,unsigned(va)))return {};return unsigned(va);};
    const auto index=target(*weapon,*w,0x1a),aim=target(*yawTail-0x3b,*y,0x3f);if(!index||!aim)return {};
    const auto ix=Offset(pe,*index,7),ag=Offset(pe,*aim,4);if(!ix||!ag)return {};
    const unsigned char ig[]={0x8b,0x81,0x4c,1,0,0,0xc3},ap[]={0x8b,0x41,0x2c,0xc3},yp[]={0x56,0x8b,0xf1,0xe8};
    if(std::memcmp(b.data()+*ix,ig,7)||std::memcmp(b.data()+*ag,ap,4)||std::memcmp(b.data()+*y,yp,4))return {};
    const auto yaw=*yawTail-0x3b;
    if(target(yaw,*y,0x54)!=aim||target(*prepare,*pr,0xfe)!=yaw||target(*prepare,*pr,0x10d)!=absolute||target(game->playerInputUpdate,*up,0x1ba)!=prepare)return {};
    const auto copy=Find(b,pe,"56 57 8B 7C 24 0C F3 0F 10 47 0C 8B F1 8B 06 F3 0F 11 46 0C 83 78 28 00 F3 0F 11 44 24 0C 75 10 D9 44 24 0C 51 8B 4E 08 D9 1C 24 E8 ?? ?? ?? ?? F3 0F 10 47 10");
    if(!copy)return {};const auto cp=Offset(pe,*copy,0x5d);if(!cp)return {};
    const auto setter=target(*copy,*cp,0x2b);if(!setter)return {};
    const auto verifiedSetter=Find(b,pe,"51 56 8B F1 D9 46 0C 83 EC 08 D9 5C 24 0C D9 44 24 14 D9 44 24 0C D9 C0 DE EA D9 C9 D9 5C 24 14 D9 44 24 14 D9 5C 24 04 D9 1C 24 E8 ?? ?? ?? ?? D9 5E 0C 5E 59 C2 04 00");
    if(setter!=verifiedSetter)return {};
    const auto pitch=target(*copy,*cp,0x53);if(!pitch)return {};
    const auto verifiedPitch=Find(b,pe,"83 EC 08 8B 51 04 85 D2 F3 0F 10 44 24 0C F3 0F 11 41 10 F3 0F 11 44 24 04 74 39 D9 42 0C");
    if(pitch!=verifiedPitch)return {};
    return AimCandidates{*weapon,*index,*aim,yaw,*prepare,*absolute,*copy,*setter,*pitch};
}
}

namespace fvr::bc2 {
std::optional<ViewAnchorCandidates> DiscoverViewAnchor(std::span<const std::byte> b,const engine::PeImage& pe){
    const auto layout=DiscoverViewLayout(b,pe);if(!layout)return {};
    const auto profile=DiscoverProfile(b,pe);if(!profile)return {};
    const auto table=Offset(pe,layout->vtable+0xb4,8);if(!table)return {};
    const auto getter=U32(b,*table)-profile->preferredBase,setter=U32(b,*table+4)-profile->preferredBase;
    const auto g=Offset(pe,getter,4),s=Offset(pe,setter,0x56);if(!g||!s||U32(b,*g)!=0xc330418d)return {};
    const auto pattern=engine::ParsePattern("55 8B EC 83 E4 F0 8B 45 08 D9 00 D9 59 30 D9 40 04 D9 59 34 D9 40 08 D9 59 38 D9 40 10 D9 59 40 D9 40 14 D9 59 44 D9 40 18 D9 59 48 D9 40 20 D9 59 50 D9 40 24 D9 59 54 D9 40 28 D9 59 58 D9 40 30 D9 59 60 D9 40 34 D9 59 64 D9 40 38 D9 59 68 8B E5 5D C2 04 00");
    if(!pattern||pattern->size()!=0x56)return {};
    for(std::size_t i=0;i<pattern->size();++i)if((*pattern)[i].wildcard||std::to_integer<unsigned char>(b[*s+i])!=(*pattern)[i].value)return {};
    const auto object=Find(b,pe,"55 8B EC 83 E4 F0 81 EC E4 04 00 00 53 8B 5D 14 56 8B 75 08 80 BE CC 00 00 00 00 57 74 09 83 3B 04");
    const auto projection=DiscoverProjectionOverrides(b,pe);if(!object||!projection)return {};
    const auto body=Offset(pe,*object,0x2a4);if(!body)return {};
    // The cc first-person branch copies the native camera, applies authored
    // weapon FOV, then overrides its transform and projection independently.
    if(U32(b,*body+0x92)!=0x217||b[*body+0x29f]!=std::byte{0xe8}||
       std::int64_t(*object)+0x2a4+std::int32_t(U32(b,*body+0x2a0))!=projection->setter)return {};
    return ViewAnchorCandidates{getter,setter,*object,*object+0x2a4};
}
}

namespace fvr::bc2 {
std::optional<FirstPersonPoseCandidates> DiscoverFirstPersonPose(std::span<const std::byte> b,const engine::PeImage& pe){
    if(!DiscoverProfile(b,pe))return {};
    const auto update=Find(b,pe,"55 8B EC 83 E4 F0 8A 45 10 81 EC C4 00 00 00 53 02 C0 56 8B F1 8B 96 EC FD FF FF 80 A6 61 02 00 00 F7");
    const auto builder=Find(b,pe,"55 8B EC 83 E4 F0 83 EC 44 53 56 57 8B F9 F6 87 74 04 00 00 08 74 74 E8");
    if(!update||!builder)return {};const auto u=Offset(pe,*update,0x190),w=Offset(pe,*builder,0x204);if(!u||!w)return {};
    const auto target=[&](unsigned offset)->std::optional<unsigned>{if(b[*u+offset]!=std::byte{0xe8})return {};const auto to=std::int64_t(*update)+offset+5+std::int32_t(U32(b,*u+offset+1));if(to<0||to>UINT32_MAX||!Executable(pe,unsigned(to)))return {};return unsigned(to);};
    if(target(0xf5)!=builder||b[*w+0x201]!=std::byte{0xc2}||b[*w+0x202]!=std::byte{4}||b[*w+0x203]!=std::byte{0})return {};
    // Native animation update calls the unique world builder, then writes the
    // selected first-person root. Verify the complete setter's twelve fields.
    const auto setter=target(0x18b);if(!setter)return {};const auto at=Offset(pe,*setter,0x56);if(!at)return {};
    const auto pattern=engine::ParsePattern("55 8B EC 83 E4 F0 8B 45 08 D9 00 D9 59 10 D9 40 04 D9 59 14 D9 40 08 D9 59 18 D9 40 10 D9 59 20 D9 40 14 D9 59 24 D9 40 18 D9 59 28 D9 40 20 D9 59 30 D9 40 24 D9 59 34 D9 40 28 D9 59 38 D9 40 30 D9 59 40 D9 40 34 D9 59 44 D9 40 38 D9 59 48 8B E5 5D C2 04 00");
    if(!pattern||pattern->size()!=0x56)return {};for(std::size_t i=0;i<pattern->size();++i)if((*pattern)[i].wildcard||std::to_integer<unsigned char>(b[*at+i])!=(*pattern)[i].value)return {};
    if(U32(b,*u+0x187)!=0x1a4)return {};
    return FirstPersonPoseCandidates{*update,*builder,*setter};
}
}

namespace fvr::bc2 {
std::optional<BodyPositionCandidates> DiscoverBodyPosition(std::span<const std::byte> b,const engine::PeImage& pe){
    if(!DiscoverProfile(b,pe))return {};
    const auto getter=Find(b,pe,"8B 81 BC 00 00 00 85 C0 74 06 05 10 01 00 00 C3 8B 49 40 E9");if(!getter)return {};
    const auto g=Offset(pe,*getter,0x18);if(!g)return {};const auto to=std::int64_t(*getter)+0x18+std::int32_t(U32(b,*g+0x14));
    if(to<0||to>UINT32_MAX||!Executable(pe,unsigned(to)))return {};const auto f=Offset(pe,unsigned(to),10);if(!f)return {};
    const unsigned char expected[]={0x8b,0x41,0x30,0x8b,0x40,0x18,0x83,0xc0,0x30,0xc3};
    if(std::memcmp(b.data()+*f,expected,10))return {};return BodyPositionCandidates{*getter,unsigned(to)};
}
}

namespace fvr::bc2 {
std::optional<RigCandidates> DiscoverRig(std::span<const std::byte> b,const engine::PeImage& pe){
    const auto poses=DiscoverFirstPersonPose(b,pe);if(!poses)return {};
    const auto getter=Find(b,pe,"8B 44 24 04 83 F8 FF 75 19 0F B6 81 75 04 00 00 F6 D0 83 E0 01 8D 04 40 8B 84 C1 B4 03 00 00 C2 04 00 8D 14 40 8B 84 D1 B4 03 00 00 C2 04 00");
    const auto weapon=Find(b,pe,"8B 44 24 04 8B 91 38 09 00 00 50 52 E8 ?? ?? ?? ?? C2 04 00");
    const auto bone=Find(b,pe,"55 8B EC 83 E4 F0 8B 41 50 8B 55 08 83 EC 7C 3B 50 10 56 72 09 32 C0 5E 8B E5 5D C2 08 00 8B 49 54 8B 41 18 52 50 E8");
    const auto index=Find(b,pe,"8B 4C 24 08 8B 54 24 04 33 C0 3B 0A 7D 08 C1 E1 06 03 4A 04 8B C1 C3");
    if(!getter||!weapon||!bone||!index)return {};
    const auto target=[&](unsigned rva,unsigned delta)->std::optional<unsigned>{
        const auto at=Offset(pe,rva+delta,5);if(!at||b[*at]!=std::byte{0xe8})return {};
        const auto to=std::int64_t(rva)+delta+5+std::int32_t(U32(b,*at+1));
        if(to<0||to>UINT32_MAX||!Executable(pe,unsigned(to)))return {};return unsigned(to);
    };
    const auto u=Offset(pe,poses->animationUpdate,0x2f3);if(!u)return {};
    if(U32(b,*u+0x35)!=0x160||b[*u+0x2f0]!=std::byte{0xc2}||b[*u+0x2f1]!=std::byte{12}||b[*u+0x2f2]!=std::byte{0})return {};
    const auto evaluate=target(poses->animationUpdate,0x5c),post=target(poses->animationUpdate,0x74);
    if(!evaluate||!post||target(poses->animationUpdate,0x80)!=weapon||target(*weapon,0xc)!=bone)return {};
    const auto e=Offset(pe,*evaluate,0xda),p=Offset(pe,*post,0xe9);
    if(!e||!p)return {};
    const auto ep=Find(b,pe,"53 8A 5C 24 0C 84 DB 56 8B F1 0F 84 93 00 00 00 8B 86 20 03 00 00");
    const auto pp=Find(b,pe,"F3 0F 10 05 ?? ?? ?? ?? 83 EC 10 0F 2F 44 24 14 56 8B F1 0F 87 C9 00 00 00 83 BE 20 03 00 00 00");
    if(evaluate!=ep||post!=pp||b[*e+0xd7]!=std::byte{0xc2}||b[*e+0xd8]!=std::byte{8}||b[*e+0xd9]!=std::byte{0}||b[*p+0xe6]!=std::byte{0xc2}||b[*p+0xe7]!=std::byte{4}||b[*p+0xe8]!=std::byte{0})return {};
    const auto thunk=target(*bone,0x26);if(!thunk)return {};
    const auto t=Offset(pe,*thunk,0x15);if(!t||target(*thunk,0xa)!=index)return {};
    const unsigned char prefix[]={0x8b,0x44,0x24,8,0x8b,0x4c,0x24,4,0x50,0x51};
    const unsigned char suffix[]={0x83,0xc4,8,0xc2,8,0};
    if(std::memcmp(b.data()+*t,prefix,sizeof(prefix))||std::memcmp(b.data()+*t+0xf,suffix,sizeof(suffix)))return {};
    const auto select=Find(b,pe,"80 B9 9C 09 00 00 00 74 07 8B 81 1C 03 00 00 C3 E9");if(!select)return {};
    const auto jump=[&](unsigned rva,unsigned delta)->std::optional<unsigned>{const auto at=Offset(pe,rva+delta,5);if(!at||b[*at]!=std::byte{0xe9})return {};const auto to=std::int64_t(rva)+delta+5+std::int32_t(U32(b,*at+1));if(to<0||to>UINT32_MAX||!Executable(pe,unsigned(to)))return {};return unsigned(to);};
    const auto skin=jump(*select,0x10);if(!skin)return {};const auto sk=Offset(pe,*skin,8);if(!sk||b[*sk]!=std::byte{0x8b}||b[*sk+1]!=std::byte{0x49}||b[*sk+2]!=std::byte{0x54})return {};
    const auto palette=jump(*skin,3);if(!palette)return {};const auto pal=Offset(pe,*palette,10);if(!pal||U32(b,*pal)!=0x5018418b||b[*pal+9]!=std::byte{0xc3})return {};
    const auto skinThunk=target(*palette,4);if(!skinThunk)return {};const auto st=Offset(pe,*skinThunk,16);
    if(!st||U32(b,*st)!=0x0424448b||b[*st+4]!=std::byte{0x50}||U32(b,*st+10)!=0xc204c483||b[*st+14]!=std::byte{4}||b[*st+15]!=std::byte{0})return {};
    const auto skinData=target(*skinThunk,5);if(!skinData)return {};const auto sd=Offset(pe,*skinData,8);
    const unsigned char dataCode[]={0x8b,0x44,0x24,4,0x8b,0x40,8,0xc3};if(!sd||std::memcmp(b.data()+*sd,dataCode,8))return {};
    return RigCandidates{*getter,poses->animationUpdate,*evaluate,*post,*weapon,*bone,*thunk,*index,*select,*skin,*palette,*skinThunk,*skinData};
}
}

namespace fvr::bc2 {
std::optional<RigConsumerCandidates> DiscoverRigConsumer(std::span<const std::byte> b,const engine::PeImage& pe){
    const auto rig=DiscoverRig(b,pe);if(!rig)return {};
    const auto prepare=Find(b,pe,"55 8B EC 83 E4 F0 81 EC 24 02 00 00 8D 84 24 A4 01 00 00 8B C8 53 89 8C 24 98 01 00 00 8B 4D 08");if(!prepare)return {};
    const auto at=Offset(pe,*prepare,0x314);if(!at)return {};
    const auto call=[&](unsigned offset)->std::optional<unsigned>{if(b[*at+offset]!=std::byte{0xe8})return {};const auto to=std::int64_t(*prepare)+offset+5+std::int32_t(U32(b,*at+offset+1));if(to<0||to>UINT32_MAX||!Executable(pe,unsigned(to)))return {};return unsigned(to);};
    const auto a=call(0x11f),c=call(0x12a),pack=call(0x2cd);if(!a||!c||!pack||a==c||call(0x30e)!=pack)return {};
    const unsigned char getterCode[]={0x8b,1,0x8b,0x90,0x60,1,0,0,0x6a,0xff,0xff,0xd2,0x8b,0xc8,0xe9};
    for(auto g:{*a,*c}){const auto at=Offset(pe,g,0x13);if(!at||std::memcmp(b.data()+*at,getterCode,sizeof(getterCode))||std::int64_t(g)+0x13+std::int32_t(U32(b,*at+0xf))!=rig->skinSelect)return {};}
    const auto code=Offset(pe,*pack,0xce);if(!code)return {};
    // Complete scalar-read/SIMD-store loop: synchronous 64-byte affine input to
    // 48-byte transposed palette output, cdecl. It stores no source pointers.
    const auto signature=engine::ParsePattern("55 8b ec 83 e4 f0 8b 55 10 83 ec 30 85 d2 0f 86 b6 00 00 00 8b 4d 0c 8b 45 08 83 c1 20 83 c0 20 f3 0f 10 40 e0 f3 0f 11 04 24 f3 0f 10 40 f0 f3 0f 11 44 24 04 f3 0f 10 00 f3 0f 11 44 24 08 f3 0f 10 40 10 f3 0f 11 44 24 0c 0f 28 04 24 0f 29 41 e0 f3 0f 10 40 e4 f3 0f 11 44 24 10 f3 0f 10 40 f4 f3 0f 11 44 24 14 f3 0f 10 40 04 f3 0f 11 44 24 18 f3 0f 10 40 14 f3 0f 11 44 24 1c 0f 28 44 24 10 0f 29 41 f0 f3 0f 10 40 e8 f3 0f 11 44 24 20 f3 0f 10 40 f8 f3 0f 11 44 24 24 f3 0f 10 40 08 f3 0f 11 44 24 28 f3 0f 10 40 18 f3 0f 11 44 24 2c 0f 28 44 24 20 0f 29 01 83 c0 40 83 c1 30 83 ea 01 0f 85 56 ff ff ff 8b e5 5d c3");
    if(!signature||signature->size()!=0xce)return {};
    for(unsigned n=0;n<0xce;++n)if((*signature)[n].wildcard||std::to_integer<unsigned char>(b[*code+n])!=(*signature)[n].value)return {};
    return RigConsumerCandidates{*prepare,*a,*c,*pack,*prepare+0x124,*prepare+0x12f,*prepare+0x2d2,*prepare+0x313};
}
}

namespace fvr::bc2 {
std::optional<FireOriginCandidates> DiscoverFireOrigin(std::span<const std::byte> b,const engine::PeImage& pe){
    const auto checked=engine::InspectPe(b);if(!checked.valid||pe.machine!=0x14c||checked.image.machine!=pe.machine)return {};
    const auto builder=Find(b,pe,"55 8B EC 83 E4 F0 81 EC 84 00 00 00 53 56 8B F1 57 8D 5E 70 8D 7E 30 53 8D 47 20 50 8D 4C 24 28 51 E8");
    const auto shoot=Find(b,pe,"55 8B EC 83 E4 F0 81 EC F4 01 00 00 53 56 57 8D 44 24 60 50 8B F9 E8 ?? ?? ?? ?? 8B 75 08 80 7E 4A 00");
    const auto getter=Find(b,pe,"8B 81 3C 0C 00 00 C3 CC CC CC CC CC CC CC CC CC 8B 81 3C 0C 00 00 85 C0 74 06 05 C0 00 00 00 C3");
    if(!builder||!shoot||!getter)return {};
    const auto at=Offset(pe,*builder,0x158);if(!at)return {};
    const unsigned char tail[]={0xd9,0x58,0x38,0x8b,0xe5,0x5d,0xc2,4,0};
    if(std::memcmp(b.data()+*at+0x14f,tail,sizeof(tail)))return {};
    for(unsigned delta:{0x16u,0x382u}){const auto call=Offset(pe,*shoot+delta,5);if(!call||b[*call]!=std::byte{0xe8}||std::int64_t(*shoot)+delta+5+std::int32_t(U32(b,*call+1))!=*builder)return {};}
    // Verify the index written by Player construction and used to populate the
    // server manager array. Matching names/data pointers alone cannot own a shot.
    const auto context=Find(b,pe,"B8 01 00 00 00 84 05 ?? ?? ?? ?? 0F 85 86 00 00 00 09 05 ?? ?? ?? ?? 53 33 DB 53 B9 ?? ?? ?? ?? E8");
    const auto manager=Find(b,pe,"51 53 55 8B 6C 24 10 56 57 55 8B F1 E8 ?? ?? ?? ?? 8D 4E 10 E8 ?? ?? ?? ?? C7 46 4C ?? ?? ?? ?? 33 DB 89 5E 50 89 5E 54 8D 7E 4C C7 06 ?? ?? ?? ?? C7 46 10");
    const auto create=Find(b,pe,"56 8B F1 8B 4E 5C 2B 4E 58 33 C0 C1 F9 02 3B 4E 04 73 58 57 8D 4E 78 E8");
    const auto player=Find(b,pe,"0F 57 C0 53 33 DB 56 8B F1 C7 06 ?? ?? ?? ?? C7 46 10 ?? ?? ?? ?? B8 ?? ?? ?? ?? 89 46 08 89 46 0C 8B 44 24 0C 89 5E 14 89 5E 18 89 5E 1C 88 5E 20");
    if(!context||!manager||!create||!player)return {};
    const auto co=Offset(pe,*context,0x9d),mo=Offset(pe,*manager,0x39),cr=Offset(pe,*create,0x71),po=Offset(pe,*player,0x5a);
    if(!co||!mo||!cr||!po)return {};
    const auto nt=U32(b,0x3c),base=U32(b,std::size_t(nt)+24+28);
    const auto contextVa=U32(b,*co+0x1c),managerVtable=U32(b,*mo+0x2d);
    if(b[*co+0x97]!=std::byte{0xb8}||U32(b,*co+0x98)!=contextVa||b[*co+0x9c]!=std::byte{0xc3}||contextVa<base||managerVtable<base||!Offset(pe,managerVtable-base,4)||contextVa-base>=pe.imageSize-0x30)return {};
    const unsigned char storeId[]={0x89,0x86,0x54,1,0,0};
    const unsigned char arrayStore[]={0x8b,0x56,0x6c,0x89,4,0xba};
    if(std::memcmp(b.data()+*po+0x54,storeId,sizeof(storeId))||std::memcmp(b.data()+*cr+0x57,arrayStore,sizeof(arrayStore))||b[*cr+0x4f]!=std::byte{0x57}||b[*cr+0x52]!=std::byte{0xe8})return {};
    const auto serverCtor=std::int64_t(*create)+0x57+std::int32_t(U32(b,*cr+0x53));
    if(serverCtor<0||serverCtor>UINT32_MAX)return {};const auto sc=Offset(pe,unsigned(serverCtor),0x1f);if(!sc)return {};
    const unsigned char constructor[]={0x8b,0x44,0x24,4,0x83,0xec,0x30,0x53,0x55,0x56,0x57,0x50,0x8b,0xf1,0xe8};
    if(std::memcmp(b.data()+*sc,constructor,sizeof(constructor))||serverCtor+0x13+std::int32_t(U32(b,*sc+0xf))!=*player)return {};
    const auto client=Find(b,pe,"55 8B EC 83 E4 F0 81 EC 54 02 00 00 53 56 57 8B F1 E8 ?? ?? ?? ?? 84 C0 0F 84 77 0B 00 00 8B CE E8");
    const auto copy=Find(b,pe,"55 8B EC 83 E4 F0 8B C1 8B 4D 08 D9 01 D9 18 D9 41 04 D9 58 04 D9 41 08 D9 58 08 D9 41 10 D9 58 10 D9 41 14 D9 58 14 D9 41 18 D9 58 18 D9 41 20 D9 58 20 D9 41 24 D9 58 24 D9 41 28 D9 58 28 D9 41 30 D9 58 30 D9 41 34 D9 58 34 D9 41 38 D9 58 38 8B E5 5D C2 04 00");
    if(!client||!copy)return {};
    const auto cl=Offset(pe,*client,0xb9e);if(!cl)return {};
    if(b[*cl+0xb9b]!=std::byte{0xc2}||b[*cl+0xb9c]!=std::byte{0x10}||b[*cl+0xb9d]!=std::byte{0})return {};
    for(unsigned delta:{0x193u,0x1f9u})if(b[*cl+delta]!=std::byte{0xe8}||std::int64_t(*client)+delta+5+std::int32_t(U32(b,*cl+delta+1))!=*copy)return {};
    const auto compose=Find(b,pe,"55 8B EC 83 E4 F0 83 EC 54 53 56 57 8B 7D 0C 8B F1 57 8D 46 30 50 8D 4C 24 28 51 E8 ?? ?? ?? ?? 57 8D 56 20 8B D8 52 8D 44 24 44 50 E8");
    if(!compose)return {};const auto cp=Offset(pe,*compose,0xb0),sv=Offset(pe,*shoot,0xc90);if(!cp||!sv)return {};
    if(b[*cp+0xad]!=std::byte{0xc2}||b[*cp+0xae]!=std::byte{8}||b[*cp+0xaf]!=std::byte{0}||
       b[*sv+0xc8d]!=std::byte{0xc2}||b[*sv+0xc8e]!=std::byte{0x10}||b[*sv+0xc8f]!=std::byte{0})return {};
    for(auto site:{std::pair{*shoot,0x155u},std::pair{*client,0x1efu}}){const auto at=Offset(pe,site.first+site.second,5);
        if(!at||b[*at]!=std::byte{0xe8}||std::int64_t(site.first)+site.second+5+std::int32_t(U32(b,*at+1))!=*compose)return {};
    }
    const unsigned char contextSeed[]={0x8b,0x40,0x1c};
    if(std::memcmp(b.data()+*sv+0x6d2,contextSeed,3)||std::memcmp(b.data()+*cl+0x4e3,contextSeed,3))return {};
    const auto target=[&](unsigned rva,unsigned delta,unsigned char op)->std::optional<unsigned>{const auto at=Offset(pe,rva+delta,5);
        if(!at||b[*at]!=std::byte{op})return {};const auto to=std::int64_t(rva)+delta+5+std::int32_t(U32(b,*at+1));
        if(to<0||to>UINT32_MAX||!Executable(pe,unsigned(to)))return {};return unsigned(to);};
    const auto seed=target(*shoot,0x6f7,0xe8);if(!seed||target(*client,0x50e,0xe8)!=seed)return {};
    const auto st=Offset(pe,*seed,8);if(!st||std::memcmp(b.data()+*st,"\x83\xc1\x0c\xe9",4))return {};
    const auto random=target(*seed,3,0xe9);if(!random)return {};const auto ra=Offset(pe,*random,0x15);
    const unsigned char randomCode[]={0x8b,0x44,0x24,4,0x25,0xfd,0xff,0xff,0x7f,0x83,0xc8,2,0x89,1,0xc6,0x41,0xc,0,0xc2,4,0};
    if(!ra||std::memcmp(b.data()+*ra,randomCode,sizeof(randomCode)))return {};
    // The output stores all twelve affine scalars, returns that output in EAX,
    // and cleans exactly one pointer argument. Native padding is unspecified.
    return FireOriginCandidates{*builder,*shoot,*shoot+0x1b,*shoot+0x387,*getter,*context,contextVa-base,*manager,managerVtable-base,*create,unsigned(serverCtor),*player,*client,*copy,*client+0x198,*client+0x1fe,*compose,*client+0x1f4,*shoot+0x15a,*seed,*random};
}
}
