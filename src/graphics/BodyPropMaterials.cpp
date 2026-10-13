#include "fvr/graphics/BodyPropMaterials.h"
#include <bit>
#include <cstring>
#include <fstream>
#include <set>
namespace fvr::graphics {
namespace {
constexpr std::size_t MaxBytes=96*1024*1024;
struct Reader {
    std::span<const std::byte> data;std::size_t at=0;bool okay=true;
    auto Take(std::size_t n)noexcept {if(!okay||n>data.size()-at){okay=false;return std::span<const std::byte>{};}
        auto out=data.subspan(at,n);at+=n;return out;}
    std::uint64_t Number(unsigned n)noexcept {std::uint64_t out=0;const auto b=Take(n);
        for(unsigned i=0;i<b.size();++i)out|=std::uint64_t(std::to_integer<unsigned char>(b[i]))<<(i*8);return out;}
    float Float()noexcept{return std::bit_cast<float>(static_cast<std::uint32_t>(Number(4)));}
};
}
BodyPropMaterialResult ApplyBodyPropMaterials(std::span<const std::byte> bytes,const BodyPropCatalog& source)noexcept {
    BodyPropMaterialResult result;
    try {
        result.error="size";if(bytes.size()<16||bytes.size()>MaxBytes||source.empty()||source.size()>MaxBodyPropAssets)return result;
        Reader r{bytes};const auto magic=r.Take(8);result.error="header";if(std::memcmp(magic.data(),"FVRTEX01",8))return result;
        const auto textures=r.Number(4),assignments=r.Number(4);result.error="count";
        if(!textures||textures>128||!assignments||assignments>MaxBodyPropAssets*8)return result;
        std::vector<std::shared_ptr<const PropAlbedoTexture>> images;std::size_t total=0;
        for(unsigned n=0;n<textures;++n){auto t=std::make_shared<PropAlbedoTexture>();
            t->width=unsigned(r.Number(4));t->height=unsigned(r.Number(4));
            t->colorSpace=static_cast<PropTextureColorSpace>(r.Number(4));t->address=static_cast<PropTextureAddress>(r.Number(4));
            const auto mips=r.Number(4);result.error="texture";
            if(!r.okay||!mips||mips>12||!t->width||!t->height||t->width>2048||t->height>2048)return result;
            unsigned w=t->width,h=t->height;
            for(unsigned mip=0;mip<mips;++mip){const auto size=r.Number(4);total+=std::size_t(size);
                if(size!=std::size_t(w)*h*4||total>64*1024*1024)return result;
                const auto pixels=r.Take(std::size_t(size));if(!r.okay)return result;
                PropTextureMip level;level.rgba.resize(pixels.size());std::memcpy(level.rgba.data(),pixels.data(),pixels.size());t->mips.push_back(std::move(level));
                w=w>1?w/2:1;h=h>1?h/2:1;}
            if(!ValidPropTexture(*t))return result;images.push_back(std::move(t));}
        // Only the completed catalog escapes: a malformed later assignment
        // cannot partially replace an earlier asset's material.
        auto next=std::make_shared<BodyPropCatalog>(source);std::set<std::pair<unsigned,unsigned>> assigned;
        std::set<unsigned> used;result.error="catalog";
        for(std::size_t n=0;n<next->size();++n){if(!ValidRigidPropKey((*next)[n].key)||(*next)[n].sections.empty()||(*next)[n].sections.size()>8)return result;
            for(std::size_t j=0;j<n;++j)if((*next)[n].key==(*next)[j].key)return result;}
        for(unsigned n=0;n<assignments;++n){RigidPropGeometryKey key{r.Number(8),r.Number(8),r.Number(8)};
            const auto section=r.Number(4),texture=r.Number(4),skin=r.Number(8),position=r.Number(8),count=r.Number(4);
            std::array<float,4> color{};for(auto& c:color)c=r.Float();result.error="identity";
            if(!r.okay||texture>=images.size()||!count||count>32768*3||!skin||!position)return result;
            unsigned asset=0;for(;asset<next->size()&&(*next)[asset].key!=key;++asset){}
            if(asset==next->size()||section>=(*next)[asset].sections.size()||!assigned.emplace(asset,unsigned(section)).second)return result;
            auto& target=(*next)[asset].sections[std::size_t(section)];
            if(target.mesh.sourceVertexSkinHash!=skin||target.mesh.sourcePositionHash!=position||target.mesh.vertices.size()!=count)return result;
            result.error="color";for(float c:color)if(!std::isfinite(c)||c<0||c>1)return result;if(color[3]!=1)return result;
            result.error="coordinates";if(count*8>bytes.size()-r.at)return result;
            std::vector<std::array<float,2>> uv;uv.reserve(std::size_t(count));for(unsigned v=0;v<count;++v)uv.push_back({r.Float(),r.Float()});
            if(!r.okay||!ValidPropCoordinates(uv,std::size_t(count)))return result;
            target.albedo=images[std::size_t(texture)];target.uv=std::move(uv);target.color=color;used.insert(unsigned(texture));
        }
        result.error="trailing";if(!r.okay||r.at!=bytes.size()||used.size()!=images.size())return result;
        result.catalog=std::move(next);result.textures=unsigned(textures);result.sections=unsigned(assignments);result.error="loaded";return result;
    }catch(...){result.error="exception";return result;}
}
BodyPropMaterialResult LoadBodyPropMaterials(const std::filesystem::path& path,const BodyPropCatalog& catalog)noexcept {
    try {std::error_code ec;const auto size=std::filesystem::file_size(path,ec);if(ec)return {{},0,0,"missing"};
        if(size<16||size>MaxBytes)return {{},0,0,"size"};
        std::ifstream f(path,std::ios::binary);if(!f)return {{},0,0,"read"};
        std::vector<std::byte> data(std::size_t(size),std::byte{});
        if(!f.read(reinterpret_cast<char*>(data.data()),std::streamsize(data.size()))||f.peek()!=std::char_traits<char>::eof())return {{},0,0,"read"};
        return ApplyBodyPropMaterials(data,catalog);
    }catch(...){return {{},0,0,"exception"};}
}
} // namespace fvr::graphics
