#pragma once
#include "fvr/graphics/SceneDepthFrame.h"
#include <memory>
struct ID3D11Device;
struct ID3D11Texture2D;
struct ID3D11ShaderResourceView;
namespace fvr::graphics {
// Both classes belong to their device's immediate-context thread. They add
// copies only. The color transport supplies the GPU completion boundaries:
// producer copies BEFORE color publication; consumer copies BEFORE color read
// completion/ACK. No keyed/relay transport is accepted by these classes.
class D3D11SceneDepthProducer final {
public:
    D3D11SceneDepthProducer();~D3D11SceneDepthProducer();
    bool Initialize(ID3D11Device*);
    bool Begin(const FrameMetadata&);
    bool Capture(unsigned eye,unsigned stage,ID3D11Texture2D*);
    // Call only after the color publication fence is complete. On success,
    // copies are immutable until exact consumption ACK or resource retirement.
    bool Seal(const TextureDescriptor&,const PairTicket&,bool reversed,SceneDepthFrame&);
    void Acknowledge(const PairTicket&,bool consumed);
    void Cancel(); // unpublished frame only; cannot revoke consumer ownership
    void Retire(); // discard generation, never recycle potentially busy sources
    void Close();
private:struct Impl;std::unique_ptr<Impl> impl_;
};
struct SceneDepthViews {
    std::array<std::array<ID3D11ShaderResourceView*,2>,2> eyes{};
    bool reversed=false;
};
class D3D11SceneDepthConsumer final {
public:
    D3D11SceneDepthConsumer();~D3D11SceneDepthConsumer();
    bool Initialize(ID3D11Device*);
    // Queue local copies BEFORE calling legacy D3D11PairConsumer::Copy. Its
    // completion fence then covers every read from these shared resources.
    bool CopyBeforeColor(const SceneDepthFrame&,const TextureDescriptor&,const PairTicket&);
    bool Views(const PairTicket&,SceneDepthViews&)const;
    void Reset();
private:struct Impl;std::unique_ptr<Impl> impl_;
};
}
