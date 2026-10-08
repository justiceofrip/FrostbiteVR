#pragma once
#include <array>
#include <cstdint>
struct ID3D11DeviceContext;
namespace fvr::bc2 {
struct OpticTextureIdentity {
    std::uint64_t view=0,resource=0;
    std::uint32_t dimension=0,width=0,height=0,format=0,mips=0,arraySize=0,samples=0;
};
struct OpticDrawState {
    std::uint64_t context=0,vertexShader=0,pixelShader=0,blendState=0,depthState=0;
    std::uint32_t viewportCount=0,scissorCount=0,stencilReference=0,sampleMask=0,topology=0;
    std::array<std::array<float,6>,4> viewports{};
    std::array<std::array<std::int32_t,4>,4> scissors{};
    std::array<float,4> blendFactor{};
    std::array<OpticTextureIdentity,4> targets{};
    OpticTextureIdentity depth{};
    std::array<OpticTextureIdentity,8> pixelResources{};
    bool complete=false;
};
// Bounded read-only Get* calls, balanced COM references, no staging/readback,
// native draw, state changes, resource retention or viewport-size filtering.
OpticDrawState CaptureOpticDrawState(ID3D11DeviceContext*) noexcept;
}
