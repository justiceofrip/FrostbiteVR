#include "fvr/engine/PeImage.h"
#include <algorithm>
#include <optional>
namespace fvr::engine {
namespace {
class Reader {
public:
    std::span<const std::byte> b;
    bool Has(std::size_t p,std::size_t n)const{return p<=b.size()&&n<=b.size()-p;}
    std::uint16_t U16(std::size_t p)const{return std::to_integer<unsigned char>(b[p])|std::uint16_t(std::to_integer<unsigned char>(b[p+1])<<8);}
    std::uint32_t U32(std::size_t p)const{return U16(p)|(std::uint32_t(U16(p+2))<<16);}
    std::string Text(std::size_t p,std::size_t n)const{std::string s;for(std::size_t i=0;i<n&&b[p+i]!=std::byte{};++i)s.push_back(char(std::to_integer<unsigned char>(b[p+i])));return s;}
};
std::optional<std::size_t> RvaOffset(const Reader& r,const PeImage& image,std::uint32_t rva,std::size_t count){
    for(const auto& s:image.sections){if(rva<s.rva)continue;const auto delta=std::uint64_t(rva)-s.rva;
        if(delta>s.rawSize||count>s.rawSize-delta)continue;
        const auto offset=std::uint64_t(s.rawOffset)+delta;
        if(offset>r.b.size()||!r.Has(std::size_t(offset),count))return {};return std::size_t(offset);
    }return {};
}
}
PeResult InspectPe(std::span<const std::byte> bytes){
    Reader r{bytes};PeResult out;
    const auto fail=[&](const char* message){out.error=message;return out;};
    if(!r.Has(0,64)||r.U16(0)!=0x5a4d)return fail("Missing/truncated DOS header");
    const auto pe=std::size_t(r.U32(0x3c));if(!r.Has(pe,24)||r.U32(pe)!=0x4550)return fail("Missing/truncated PE header");
    auto& image=out.image;image.machine=r.U16(pe+4);image.timestamp=r.U32(pe+8);image.largeAddressAware=(r.U16(pe+22)&0x20)!=0;
    const auto count=r.U16(pe+6),optSize=r.U16(pe+20);const auto opt=pe+24;
    if(count==0||count>96||!r.Has(opt,optSize)||optSize<2)return fail("Invalid PE section count/optional header");
    const auto magic=r.U16(opt);const bool x64=magic==0x20b;
    if((magic!=0x10b&&!x64)||(x64?image.machine!=0x8664:image.machine!=0x14c))return fail("Unsupported/mismatched PE architecture");
    const std::size_t dirStart=x64?112:96;
    if(optSize<dirStart)return fail("Truncated optional header");image.imageSize=r.U32(opt+56);
    if(!image.imageSize)return fail("Empty image size");
    const auto table=opt+optSize;if(!r.Has(table,std::size_t(count)*40))return fail("Truncated section table");
    for(unsigned i=0;i<count;++i){const auto p=table+i*40;PeSection s{r.Text(p,8),r.U32(p+12),r.U32(p+8),r.U32(p+20),r.U32(p+16),r.U32(p+36)};
        if(!r.Has(s.rawOffset,s.rawSize)||std::uint64_t(s.rva)+std::max(s.virtualSize,s.rawSize)>image.imageSize)return fail("Invalid section bounds");
        for(const auto& previous:image.sections){
            const auto a=std::uint64_t(s.rva),b=std::uint64_t(previous.rva);
            if(a<b+std::max(previous.virtualSize,previous.rawSize)&&b<a+std::max(s.virtualSize,s.rawSize))return fail("Overlapping section RVAs");
        }image.sections.push_back(s);
    }
    const auto directories=r.U32(opt+dirStart-4);
    if(directories>(optSize-dirStart)/8)return fail("Invalid directory count");
    if(directories>1){const auto importRva=r.U32(opt+dirStart+8),importSize=r.U32(opt+dirStart+12);
        if(importRva||importSize){
            if(!importRva||importSize<20||importSize>1024*1024)return fail("Invalid import directory");
            bool terminated=false;
            for(std::uint32_t delta=0;delta<=importSize-20;delta+=20){
                if(std::uint64_t(importRva)+delta>UINT32_MAX)return fail("Import RVA overflow");
                const auto at=RvaOffset(r,image,importRva+delta,20);if(!at)return fail("Import descriptor outside file");
                bool zero=true;for(unsigned j=0;j<5;++j)zero&=r.U32(*at+j*4)==0;
                if(zero){terminated=true;break;}
                const auto nameRva=r.U32(*at+12);if(!nameRva)return fail("Missing import name");
                std::string name;bool end=false;
                for(unsigned j=0;j<512;++j){if(std::uint64_t(nameRva)+j>UINT32_MAX)return fail("Import name overflow");
                    const auto p=RvaOffset(r,image,nameRva+j,1);if(!p)return fail("Import name outside file");
                    const auto c=std::to_integer<unsigned char>(bytes[*p]);if(!c){end=true;break;}if(c<32||c>126)return fail("Invalid import name");name.push_back(char(c));
                }
                if(!end||name.empty())return fail("Unterminated import name");image.imports.push_back(name);
            }
            if(!terminated)return fail("Unterminated import directory");
        }
    }
    out.valid=true;return out;
}
}