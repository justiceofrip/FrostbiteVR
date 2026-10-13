#pragma once
#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace fvr::graphics {
// Converted render data only. The asset adapter must establish texture role,
// color space and UV mapping; the renderer never guesses them from filenames.
enum class PropTextureColorSpace : unsigned {Linear, Srgb};
enum class PropTextureAddress : unsigned {Wrap, Clamp};
struct PropTextureMip {std::vector<std::uint8_t> rgba;};
struct PropAlbedoTexture {
    unsigned width=0,height=0;
    PropTextureColorSpace colorSpace=PropTextureColorSpace::Linear;
    PropTextureAddress address=PropTextureAddress::Wrap;
    std::vector<PropTextureMip> mips;
};
inline bool ValidPropTexture(const PropAlbedoTexture& t)noexcept {
    if(!t.width||!t.height||t.width>2048||t.height>2048||t.mips.empty()||t.mips.size()>12||
       (t.colorSpace!=PropTextureColorSpace::Linear&&t.colorSpace!=PropTextureColorSpace::Srgb)||
       (t.address!=PropTextureAddress::Wrap&&t.address!=PropTextureAddress::Clamp))return false;
    unsigned w=t.width,h=t.height;std::size_t bytes=0;
    for(std::size_t n=0;n<t.mips.size();++n){
        const std::size_t size=std::size_t(w)*h*4;
        bytes+=size;if(t.mips[n].rgba.size()!=size||bytes>24*1024*1024)return false;
        if(w==1&&h==1&&n+1!=t.mips.size())return false;
        w=w>1?w/2:1;h=h>1?h/2:1;
    }
    return true;
}
inline bool ValidPropCoordinates(std::span<const std::array<float,2>> uv,std::size_t vertices)noexcept {
    if(!vertices||uv.size()!=vertices)return false;
    for(const auto& p:uv)for(float x:p)if(!std::isfinite(x)||std::abs(x)>65536)return false;
    return true; // Repeated and negative coordinates are intentional.
}
} // namespace fvr::graphics
