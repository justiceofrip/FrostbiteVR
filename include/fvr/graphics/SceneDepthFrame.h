#pragma once
#include "fvr/graphics/SharedTextureProtocol.h"
namespace fvr::graphics {
// Optional attachment to a legacy-fenced color pair. Fixed-width DXGI legacy
// resource identifiers are not NT handles and must never be CloseHandle'd.
// Each eye owns [world, firstPerson] copies in canonical eye clip-depth space.
struct alignas(8) SceneDepthFrame {
    std::uint32_t version=1,bytes=184,flags=0,reserved=0;
    PairTicket color{};
    std::uint32_t width=0,height=0,samples=0,quality=0;
    std::uint32_t format=0,adapterLow=0;
    std::int32_t adapterHigh=0;
    std::uint32_t reserved1=0;
    std::array<std::array<std::uint64_t,2>,2> resources{};
    std::uint64_t resourceGeneration=0;
};
inline constexpr std::uint32_t SceneDepthCanonicalClip=1,SceneDepthReversed=2;
static_assert(sizeof(SceneDepthFrame)==184&&offsetof(SceneDepthFrame,resources)==144);
static_assert(std::is_trivially_copyable_v<SceneDepthFrame>);
inline bool SceneDepthMatches(const SceneDepthFrame& depth,const TextureDescriptor& descriptor,const PairTicket& color)noexcept {
    if(!Valid(color,descriptor)||descriptor.sharing!=TextureSharing::LegacyFenced||
       depth.version!=1||depth.bytes!=sizeof(depth)||depth.reserved||depth.reserved1||
       (depth.flags!=SceneDepthCanonicalClip&&depth.flags!=(SceneDepthCanonicalClip|SceneDepthReversed))||
       std::memcmp(&depth.color,&color,sizeof(color))||!depth.resourceGeneration||
       depth.width!=descriptor.width||depth.height!=descriptor.height||
       depth.adapterLow!=descriptor.adapterLow||depth.adapterHigh!=descriptor.adapterHigh||
       depth.format!=44||!depth.samples||depth.samples>32||(depth.samples&(depth.samples-1)))return false;
    const auto nativeColors=LegacyResourceIds(descriptor);
    std::array<std::uint64_t,4> ids{};
    for(unsigned eye=0;eye<2;++eye)for(unsigned stage=0;stage<2;++stage){
        const auto index=eye*2+stage;const auto id=depth.resources[eye][stage];
        if(!id||id==nativeColors[0]||id==nativeColors[1])return false;
        for(unsigned n=0;n<index;++n)if(ids[n]==id)return false;ids[index]=id;
    }
    return true;
}
}
