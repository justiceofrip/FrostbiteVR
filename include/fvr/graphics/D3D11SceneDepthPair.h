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
    // Queue local copies once BEFORE legacy D3D11PairConsumer::Copy or
    // SubmitCopy. Copy/PollCopy success then fences every shared-depth read.
    // Keep these local views/resources unchanged while the color copy is pending;
    // pending or cancelled work must never receive a positive consumption ACK.
    // Explicit source counts 1/2/3 select separate shared imports only. Every
    // count retains the same single local quartet and exact-ticket Views gate.
    bool CopyBeforeColor(const SceneDepthFrame&,const TextureDescriptor&,const PairTicket&,
        unsigned sourceSlot=0,unsigned sourceSlots=1);
    struct ImportStats {std::uint64_t sourceOpens=0,localCreates=0;};
    ImportStats ImportStatistics()const noexcept;
    bool Views(const PairTicket&,SceneDepthViews&)const;
    void Reset();
private:struct Impl;std::unique_ptr<Impl> impl_;
};
}
