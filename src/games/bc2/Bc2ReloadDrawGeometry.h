#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
namespace fvr::bc2 {
// Read-only evidence format. Index relocation/baseVertex and VB offset changes
// preserve this fingerprint; changed triangle order/layout remain unverified.
struct ReloadDrawGeometry {
    std::uint64_t vertexSkinHash=0,positionHash=0;
    unsigned section=0; // 0 unknown, 1 brass, 2 plastic, 3 ACOG dot. NOT ownership.
};
inline constexpr std::uint64_t ReloadDrawFnvOffset=14695981039346656037ull;
inline constexpr std::uint64_t ReloadDrawFnvPrime=1099511628211ull;
inline std::uint64_t ReloadDrawHash(std::span<const std::byte> bytes)noexcept {
    auto value=ReloadDrawFnvOffset;for(auto b:bytes){value^=std::to_integer<unsigned char>(b);value*=ReloadDrawFnvPrime;}return value;
}
inline unsigned IdentifyReloadDrawGeometry(unsigned count,std::uint64_t vertexSkin,std::uint64_t positions)noexcept {
    // Derived from the installed resource, not an exported game asset. Provenance:
    // reports/spas-shell-draw-fingerprints-20261001.json and derivation tool.
    if(count==270&&vertexSkin==0xe05c8e35ec306bd2ull&&positions==0x5fd96edbcded25baull)return 1;
    if(count==90&&vertexSkin==0xce6e99d2c3cd267eull&&positions==0xa9888be789a5c902ull)return 2;
    // ACOG exact first20 (float3 position + indices4 + weights4) and position12.
    // Source SHA256 801361ce3160ef3392a7bb3317d77ed70b8631fb9abc661b67517314059a36a9.
    if(count==12&&vertexSkin==0x1b481b02516969b5ull&&positions==0x9ad7a905a35a527dull)return 3;
    return 0;
}
inline std::optional<ReloadDrawGeometry> FingerprintReloadDraw(
    std::span<const std::byte> vertices,std::span<const std::byte> selectedIndices,
    unsigned count,unsigned indexBytes,unsigned stride,std::uint32_t vertexOffset,std::int32_t baseVertex)noexcept {
    const bool shell=(count==270||count==90)&&stride==48;
    const bool optic=count==12&&stride==68;
    const unsigned identityBytes=optic?20u:16u,positionBytes=optic?12u:8u;
    if((!shell&&!optic)||(indexBytes!=2&&indexBytes!=4)||
        selectedIndices.size()!=std::size_t(count)*indexBytes||vertices.empty()||vertices.size()>2*1024*1024)return {};
    ReloadDrawGeometry result{ReloadDrawFnvOffset,ReloadDrawFnvOffset,0};
    for(unsigned n=0;n<count;++n){
        std::uint32_t index=0;std::memcpy(&index,selectedIndices.data()+std::size_t(n)*indexBytes,indexBytes);
        const auto vertex=std::int64_t(index)+baseVertex;if(vertex<0)return {};
        const auto offset=std::uint64_t(vertexOffset)+std::uint64_t(vertex)*stride;
        if(offset>vertices.size()||vertices.size()-std::size_t(offset)<identityBytes)return {};
        for(unsigned b=0;b<identityBytes;++b){const auto v=std::to_integer<unsigned char>(vertices[std::size_t(offset)+b]);
            result.vertexSkinHash^=v;result.vertexSkinHash*=ReloadDrawFnvPrime;
            if(b<positionBytes){result.positionHash^=v;result.positionHash*=ReloadDrawFnvPrime;}}
    }
    result.section=IdentifyReloadDrawGeometry(count,result.vertexSkinHash,result.positionHash);return result;
}
}
