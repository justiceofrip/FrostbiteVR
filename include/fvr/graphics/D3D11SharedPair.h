#pragma once
#include "fvr/graphics/SharedTextureProtocol.h"
#include <d3d11.h>
#include <memory>

namespace fvr::graphics {
enum class TransferResult { Ok, Busy, Invalid, Unavailable, Faulted };
struct TextureSlice { ID3D11Texture2D* texture=nullptr; UINT subresource=0; };
// One producer and one consumer, each called from its device's immediate-context
// thread. Caller serializes all context use. No game offsets or OpenXR dependency.
// Neither side blocks on mutexes. Async consumer Busy may retain an in-flight
// pair; HasPendingCopy distinguishes that ownership from admission backpressure.
// A fault requires recreation with a NEW descriptor/session (never retry its keys).
class D3D11PairProducer {
public:
    D3D11PairProducer();~D3D11PairProducer();
    D3D11PairProducer(const D3D11PairProducer&)=delete;
    D3D11PairProducer& operator=(const D3D11PairProducer&)=delete;
    TransferResult Create(ID3D11Device*,UINT width,UINT height,DXGI_FORMAT,std::uint64_t epoch,TextureSharing sharing=TextureSharing::NamedKeyed) noexcept;
    TransferResult Publish(const std::array<TextureSlice,2>&,const FrameMetadata&,PairTicket&) noexcept;
    // Graphics-owner observation only. Both source reads for this exact frame
    // have been queued on this producer's context; this is NOT GPU completion.
    // An old awaiting-ACK frame cannot authorize overwriting a new frame's input.
    bool ExportReadsQueued(ID3D11Device*,ID3D11DeviceContext*,const FrameMetadata&)const noexcept;
    void Acknowledge(const PairTicket&,bool consumed)noexcept;
    TextureDescriptor Descriptor() const noexcept;
    HRESULT LastError() const noexcept;
    unsigned LastOperation() const noexcept{return operation_;}
    void Reset() noexcept;
private: struct State;std::unique_ptr<State> state_;HRESULT error_=S_OK;unsigned operation_=0;
};
class D3D11PairConsumer {
public:
    D3D11PairConsumer();~D3D11PairConsumer();
    D3D11PairConsumer(const D3D11PairConsumer&)=delete;
    D3D11PairConsumer& operator=(const D3D11PairConsumer&)=delete;
    TransferResult Open(ID3D11Device*,const TextureDescriptor&) noexcept;
    // Copies both eyes into caller-owned resources (e.g. acquired XR slices).
    // On failure their contents must not be submitted. No resource is exposed
    // while another process owns it. Ticket is supplied over reliable IPC.
    TransferResult Copy(const PairTicket&,const std::array<TextureSlice,2>&) noexcept;
    // Legacy-fenced copies are queued once and return Busy while owned here.
    // PollCopy checks the event once with DONOTFLUSH: ordinary pending is Busy
    // with LastError()==S_FALSE, never a timeout/fault. Named-keyed copies keep
    // their existing immediate completion semantics and do not become pending.
    TransferResult SubmitCopy(const PairTicket&,const std::array<TextureSlice,2>&) noexcept;
    TransferResult PollCopy(const PairTicket&) noexcept;
    bool HasPendingCopy() const noexcept;
    // Exact-ticket cancellation retires this consumer generation. It does not
    // cancel queued GPU commands or imply completion: send negative producer
    // feedback. Open/Copy cannot replace an outstanding asynchronous copy.
    TransferResult CancelCopy(const PairTicket&) noexcept;
    HRESULT LastError() const noexcept;
    // Before Reset/destruction, the caller must negatively acknowledge any
    // outstanding ticket (or positively acknowledge proven PollCopy success).
    void Reset() noexcept;
private: struct State;std::unique_ptr<State> state_;HRESULT error_=S_OK;
};
}
