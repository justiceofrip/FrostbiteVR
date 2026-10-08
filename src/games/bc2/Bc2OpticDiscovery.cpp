#include "Bc2OpticDiscovery.h"
#include "fvr/engine/BindingValidation.h"
#include <cstring>
namespace fvr::bc2 {
namespace {
std::optional<std::size_t> At(std::span<const std::byte> b,const engine::PeImage& pe,std::uint32_t rva,std::size_t n){
    for(const auto& s:pe.sections)if(rva>=s.rva){const auto d=std::uint64_t(rva)-s.rva,a=std::uint64_t(s.rawOffset)+d;
        if(d<=s.rawSize&&n<=s.rawSize-d&&a<=b.size()&&n<=b.size()-a)return std::size_t(a);}return {};
}
std::uint32_t U(std::span<const std::byte> b,std::size_t at){std::uint32_t x=0;std::memcpy(&x,b.data()+at,4);return x;}
bool Text(std::span<const std::byte> b,const engine::PeImage& pe,std::uint32_t base,std::uint32_t va,const char* value){
    if(va<base)return false;const auto n=std::strlen(value)+1;const auto at=At(b,pe,va-base,n);return at&&!std::memcmp(b.data()+*at,value,n);
}
bool Pattern(std::span<const std::byte> b,std::size_t at,const char* text){const auto p=engine::ParsePattern(text);
    if(!p||at>b.size()||p->size()>b.size()-at)return false;
    for(std::size_t i=0;i<p->size();++i)if(!(*p)[i].wildcard&&std::to_integer<unsigned char>(b[at+i])!=(*p)[i].value)return false;return true;
}
std::optional<std::uint32_t> Find(std::span<const std::byte> b,const engine::PeImage& pe,const char* text){
    const auto pattern=engine::ParsePattern(text);if(!pattern)return {};std::optional<std::uint32_t> found;
    for(const auto& s:pe.sections)if((s.flags&0x20000000)&&s.rawOffset<=b.size()&&s.rawSize<=b.size()-s.rawOffset&&pattern->size()<=s.rawSize){
        for(std::size_t n=0;n<=s.rawSize-pattern->size();++n){bool okay=true;
            for(std::size_t i=0;i<pattern->size();++i)if(!(*pattern)[i].wildcard&&std::to_integer<unsigned char>(b[s.rawOffset+n+i])!=(*pattern)[i].value){okay=false;break;}
            if(okay){if(found)return {};found=s.rva+std::uint32_t(n);}}}return found;
}
std::optional<std::uint32_t> Call(std::span<const std::byte> b,const engine::PeImage& pe,std::uint32_t rva){const auto at=At(b,pe,rva,5);
    if(!at||b[*at]!=std::byte{0xe8})return {};const auto to=std::int64_t(rva)+5+std::int32_t(U(b,*at+1));
    if(to<0||to>UINT32_MAX||!At(b,pe,std::uint32_t(to),1))return {};return std::uint32_t(to);
}
std::uint64_t Hash(std::span<const std::byte> b){std::uint64_t h=14695981039346656037ull;for(auto x:b){h^=std::to_integer<unsigned char>(x);h*=1099511628211ull;}return h;}
std::optional<OpticTypeEvidence> Type(std::span<const std::byte> b,const engine::PeImage& pe,std::uint32_t base,const char* name){
    std::optional<OpticTypeEvidence> found;
    for(const auto& s:pe.sections)if((s.flags&0x20000000)&&s.rawOffset<=b.size()&&s.rawSize<=b.size()-s.rawOffset&&s.rawSize>=30){
        for(std::uint32_t n=0;n<=s.rawSize-30;++n){const auto at=std::size_t(s.rawOffset)+n;
            if(b[at]!=std::byte{0x68}||b[at+5]!=std::byte{0x68}||b[at+10]!=std::byte{0x68}||b[at+15]!=std::byte{0x68}||b[at+20]!=std::byte{0xb9}||b[at+25]!=std::byte{0xe8})continue;
            const auto meta=U(b,at+16),fields=U(b,at+1),info=U(b,at+21),parent=U(b,at+11);if(meta<base||fields<base||info<base||parent<base)continue;
            const auto m=At(b,pe,meta-base,24);if(!m||!Text(b,pe,base,U(b,*m),name))continue;
            const auto ctor=Call(b,pe,s.rva+n+25);if(!ctor)return {};const auto c=At(b,pe,*ctor,0x2a);
            if(!c||!Pattern(b,*c,"8B 44 24 10 8B 54 24 08 56 50 8B 44 24 0C 8B F1 8B 4C 24 14 51 52 50 8B CE E8")||
               !Pattern(b,*c+0x27,"C2 10 00"))return {};
            OpticTypeEvidence out{s.rva+n,info-base,meta-base,fields-base,parent-base};std::memcpy(&out.size,b.data()+*m+6,2);out.fieldCount=std::to_integer<std::uint8_t>(b[*m+13]);
            if(!out.size||out.size>4096||!out.fieldCount||out.fieldCount>64||!At(b,pe,out.fields,std::size_t(out.fieldCount)*24)||!At(b,pe,out.typeInfo,4)||found)return {};
            found=out;
        }
    }return found;
}
bool Field(std::span<const std::byte> b,const engine::PeImage& pe,std::uint32_t base,const OpticTypeEvidence& t,const char* name,std::uint32_t offset,std::optional<std::uint32_t> element={}){
    unsigned matches=0;for(unsigned n=0;n<t.fieldCount;++n){const auto at=At(b,pe,t.fields+n*24,24);if(!at)return false;
        if(!Text(b,pe,base,U(b,*at),name))continue;if(U(b,*at+16)!=offset||offset>=t.size)return false;
        if(element&&(U(b,*at+12)<base||U(b,*at+12)-base!=*element))return false;++matches;}return matches==1;
}
}
std::optional<OpticObservationCandidates> DiscoverOpticObservation(std::span<const std::byte> b,const engine::PeImage& pe){
    if(pe.machine!=0x14c||b.size()<64)return {};const auto nt=U(b,0x3c);if(nt>b.size()||56>b.size()-nt)return {};const auto base=U(b,std::size_t(nt)+52);if(base<0x10000)return {};
    const auto zoom=Type(b,pe,base,"ZoomLevelData"),aim=Type(b,pe,base,"SoldierAimingSimulationData"),scope=Type(b,pe,base,"ScopeFilterData"),sniper=Type(b,pe,base,"SniperLensScopeFilterData");
    if(!zoom||!aim||!scope||!sniper||zoom->size!=0x74||aim->size!=0x7c||scope->size!=0x30||sniper->size!=0xd0||sniper->parent!=scope->typeInfo)return {};
    if(!Field(b,pe,base,*aim,"ZoomLevels",0x4c,zoom->typeInfo)||!Field(b,pe,base,*aim,"ZoomType",0x48)||
       !Field(b,pe,base,*zoom,"FieldOfView",0x48)||!Field(b,pe,base,*zoom,"ForegroundBlurFilter",0x34)||
       !Field(b,pe,base,*zoom,"ForegroundBlurFilterDeviation",0x38)||!Field(b,pe,base,*zoom,"ForegroundBlurViewDistance",0x3c)||
       !Field(b,pe,base,*scope,"ScissorRegion",0x20)||!Field(b,pe,base,*sniper,"BlurScale",0xc0)||!Field(b,pe,base,*sniper,"BlurCenter",0x60))return {};
    const auto render=Find(b,pe,"55 8B EC 83 E4 F0 83 EC 74 53 56 8B F1 8B 86 DC 00 00 00 8B 0D ?? ?? ?? ?? 8B 9E D8 00 00 00");
    const auto branch=Find(b,pe,"8B 07 8B 50 70 8B CF FF D2 85 C0 0F 84 8F 00 00 00 8B 07 8B 50 70 8B CF FF D2 8B 10 8B C8 8B 42 08 68 ?? ?? ?? ?? FF D0 8B C8 E8");
    if(!render||!branch)return {};const auto r=At(b,pe,*render,0x20a),c=At(b,pe,*branch,0x74);if(!r||!c)return {};
    // Complete inspected bodies supplement ABI, consumer and metadata proof.
    if(Hash(b.subspan(*r,0x20a))!=0x8a68940587968955ull||Hash(b.subspan(*c,0x74))!=0xc106ebfa84f8c750ull||
       U(b,*c+0x22)!=base+sniper->typeInfo||U(b,*c+0x5b)!=base+sniper->typeInfo||Call(b,pe,*branch+0x6f)!=render||
       !Pattern(b,*r+0x9e,"8B 7D 08 F3 0F 10 87 C0 00 00 00 8B 4F 60 8B 57 64")||!Pattern(b,*r+0x207,"C2 04 00"))return {};
    return OpticObservationCandidates{*render,*branch+0x6f,*branch+0x74,*zoom,*aim,*scope,*sniper};
}
}
