#pragma once
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>

namespace fvr::graphics {
// Readback evidence, not a rendering-health verdict: a native loading/menu
// frame may legitimately be solid white or black. Alpha is recorded separately.
struct DesktopImageEvidence {
    std::uint64_t pixels=0,nearWhite=0,nearBlack=0,opaque=0;
    std::array<std::uint8_t,3> minimum{255,255,255},maximum{};
    std::array<std::uint64_t,3> channelSum{};
    std::uint64_t rgbaHash=14695981039346656037ULL;
};
inline std::optional<DesktopImageEvidence> InspectDesktopRgba(
    std::span<const unsigned char> bytes,std::uint32_t width,std::uint32_t height,
    std::size_t rowPitch) noexcept {
    if(!width||!height||width>4096||height>4096)return {};
    const auto stride=std::size_t(width)*4;
    if(rowPitch<stride||std::size_t(height-1)>(std::numeric_limits<std::size_t>::max()-stride)/rowPitch||
       bytes.size()<std::size_t(height-1)*rowPitch+stride)return {};
    DesktopImageEvidence out{};
    for(std::uint32_t y=0;y<height;++y)for(std::uint32_t x=0;x<width;++x){
        const auto* p=bytes.data()+std::size_t(y)*rowPitch+std::size_t(x)*4;
        ++out.pixels;out.nearWhite+=p[0]>=250&&p[1]>=250&&p[2]>=250;
        out.nearBlack+=p[0]<=5&&p[1]<=5&&p[2]<=5;out.opaque+=p[3]==255;
        for(unsigned c=0;c<3;++c){out.minimum[c]=(std::min)(out.minimum[c],p[c]);
            out.maximum[c]=(std::max)(out.maximum[c],p[c]);out.channelSum[c]+=p[c];}
        for(unsigned c=0;c<4;++c){out.rgbaHash^=p[c];out.rgbaHash*=1099511628211ULL;}
    }
    return out;
}
}