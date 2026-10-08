#include "Bc2BodyAmmoGeometryCache.h"
#include "Bc2BodyAmmoAssetProfiles.h"
#include <algorithm>
#include <bit>
#include <cstring>
#include <fstream>
namespace fvr::bc2 {
namespace {
constexpr std::size_t MaxBytes=16*1024*1024;
struct Reader {
    std::span<const std::byte> bytes;std::size_t at=0;bool okay=true;
    std::span<const std::byte> Take(std::size_t count)noexcept {
        if(!okay||count>bytes.size()-at){okay=false;return {};}
        const auto out=bytes.subspan(at,count);at+=count;return out;
    }
    std::uint64_t Number(unsigned count)noexcept {
        const auto b=Take(count);std::uint64_t out=0;
        for(unsigned i=0;i<b.size();++i)out|=std::uint64_t(std::to_integer<unsigned char>(b[i]))<<(8*i);return out;
    }
    std::string Text(){const auto size=Number(2);if(!size||size>512){okay=false;return {};}
        const auto b=Take(std::size_t(size));if(!okay)return {};
        for(auto c:b)if(std::to_integer<unsigned>(c)<32||std::to_integer<unsigned>(c)>126){okay=false;return {};}
        return {reinterpret_cast<const char*>(b.data()),b.size()};
    }
};
}
const char* BodyAmmoCacheStatusName(BodyAmmoCacheStatus s)noexcept {
    constexpr const char* names[]{"loaded","missing","size","read","header","count","identity","section","geometry","trailing","exception"};
    const auto n=unsigned(s);return n<std::size(names)?names[n]:"unknown";
}
BodyAmmoCacheResult ParseBodyAmmoGeometryCache(std::span<const std::byte> bytes)noexcept {
    return ParseBodyAmmoGeometryCacheForProfiles(bytes,BodyAmmoAssetProfiles);
}
BodyAmmoCacheResult ParseBodyAmmoGeometryCacheForProfiles(std::span<const std::byte> bytes,std::span<const BodyAmmoAssetProfile> profiles)noexcept {
    BodyAmmoCacheResult out;out.bytes=bytes.size();
    if(bytes.size()<12||bytes.size()>MaxBytes){out.status=BodyAmmoCacheStatus::Size;return out;}
    try {
        Reader r{bytes};const auto magic=r.Take(8);if(std::memcmp(magic.data(),"BC2PROP1",8)){out.status=BodyAmmoCacheStatus::Header;return out;}
        const auto count=r.Number(4);if(!count||count>64){out.status=BodyAmmoCacheStatus::Count;return out;}
        auto catalog=std::make_shared<BodyAmmoGeometryCatalog>();catalog->reserve(std::size_t(count));
        for(unsigned part=0;part<count;++part){out.part=part;const auto asset=r.Text(),mesh=r.Text(),bone=r.Text();const auto rig=r.Number(8),sectionCount=r.Number(4);
            const BodyAmmoAssetProfile* profile=nullptr;
            for(const auto& candidate:profiles)if(candidate.asset&&candidate.mesh&&candidate.part&&asset==candidate.asset&&mesh==candidate.mesh&&bone==candidate.part&&rig==candidate.rig){
                if(profile){out.status=BodyAmmoCacheStatus::Identity;return out;}profile=&candidate;}
            if(!r.okay||!profile){out.status=BodyAmmoCacheStatus::Identity;return out;}
            for(const auto& previous:*catalog)if(previous->asset==asset&&previous->mesh==mesh&&previous->part==bone&&previous->rigFingerprint==rig){out.status=BodyAmmoCacheStatus::Identity;return out;}
            if(!sectionCount||sectionCount>8||sectionCount!=profile->sections.size()){out.status=BodyAmmoCacheStatus::Section;return out;}
            std::vector<BodyAmmoSectionBytes> sections;sections.reserve(std::size_t(sectionCount));
            for(unsigned section=0;section<sectionCount;++section){out.section=section;const auto name=r.Text();
                const auto vertexBytes=r.Number(4),indexBytes=r.Number(4),width=r.Number(4),offset=r.Number(4),base=r.Number(4);
                const BeltPropSectionProfile* descriptor=nullptr;
                for(const auto& candidate:profile->sections)if(name==candidate.section){if(descriptor){out.status=BodyAmmoCacheStatus::Section;return out;}descriptor=&candidate;}
                if(!r.okay||!descriptor||!vertexBytes||vertexBytes>4*1024*1024||(width!=2&&width!=4)||
                    indexBytes!=descriptor->geometry.indexCount*width){out.status=BodyAmmoCacheStatus::Section;return out;}
                for(const auto& previous:sections)if(previous.profile==descriptor){out.status=BodyAmmoCacheStatus::Section;return out;}
                const auto vertices=r.Take(std::size_t(vertexBytes)),indices=r.Take(std::size_t(indexBytes));if(!r.okay){out.status=BodyAmmoCacheStatus::Read;return out;}
                std::array<float,4> color{.2f,.2f,.2f,1};
                if(name.find("Brass")!=std::string::npos)color={.65f,.48f,.15f,1};
                else if(asset=="SPAS12_sp"&&bone=="jntWpn_7")color={.45f,.055f,.035f,1};
                sections.push_back({descriptor,{vertices,indices,unsigned(width),unsigned(offset),std::bit_cast<std::int32_t>(std::uint32_t(base))},
                    profile->inverseBind,graphics::RigidPropBasis::NativeRightHanded,color});
            }
            auto geometry=BuildBodyAmmoGeometry(asset,mesh,bone,rig,sections);if(!geometry){out.status=BodyAmmoCacheStatus::Geometry;return out;}
            catalog->push_back(std::move(geometry));
        }
        if(!r.okay||r.at!=bytes.size()){out.status=BodyAmmoCacheStatus::Trailing;return out;}
        out.catalog=std::move(catalog);out.status=BodyAmmoCacheStatus::Loaded;return out;
    }catch(...){out.status=BodyAmmoCacheStatus::Exception;return out;}
}
BodyAmmoCacheResult LoadBodyAmmoGeometryCache(const std::filesystem::path& path)noexcept {
    BodyAmmoCacheResult out;
    try {
        std::error_code ec;const auto size=std::filesystem::file_size(path,ec);
        if(ec){out.status=BodyAmmoCacheStatus::Missing;return out;}
        if(size<12||size>MaxBytes){out.status=BodyAmmoCacheStatus::Size;return out;}
        std::ifstream stream(path,std::ios::binary);if(!stream){out.status=BodyAmmoCacheStatus::Read;return out;}
        std::vector<std::byte> bytes(std::size_t(size),std::byte{});
        if(!stream.read(reinterpret_cast<char*>(bytes.data()),std::streamsize(bytes.size()))||stream.peek()!=std::char_traits<char>::eof()){
            out.status=BodyAmmoCacheStatus::Read;return out;}
        return ParseBodyAmmoGeometryCache(bytes);
    }catch(...){out.status=BodyAmmoCacheStatus::Exception;return out;}
}
} // namespace fvr::bc2

