#pragma once
#include "fvr/graphics/D3D11SharedPair.h"
#include <wrl/client.h>
#include <atomic>
namespace fvr::graphics {
// Compatibility path for devices accepting legacy shared resources but not
// keyed mutexes. A private device exports the final NT/keyed pair. Both GPU
// queues are fenced before ownership returns; no CPU image readback is used.
class D3D11LegacyRelay {
public:
    bool Create(ID3D11Device* game,UINT width,UINT height,DXGI_FORMAT format)noexcept;
    void Reset()noexcept;
    void Capture(unsigned eye,ID3D11Texture2D* source)noexcept;
    void SealGameCopies()noexcept;
    HRESULT GameCopiesReady()noexcept;
    void SealRelayRead()noexcept;
    HRESULT PollRelayRead()noexcept;
    bool SourceWritable()const noexcept{return writable_.load(std::memory_order_acquire);}
    ID3D11Device* Device()const noexcept{return relayDevice_.Get();}
    std::array<TextureSlice,2> Sources()const noexcept{return {TextureSlice{opened_[0].Get(),0},TextureSlice{opened_[1].Get(),0}};}
    unsigned LastOperation()const noexcept{return operation_;}
    HRESULT LastError()const noexcept{return error_;}
private:
    Microsoft::WRL::ComPtr<ID3D11Device> gameDevice_,relayDevice_;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> gameContext_,relayContext_;
    std::array<Microsoft::WRL::ComPtr<ID3D11Texture2D>,2> gameEyes_,opened_;
    Microsoft::WRL::ComPtr<ID3D11Query> gameDone_,relayDone_;
    std::atomic<bool> writable_=true;
    bool waitingRelay_=false;HRESULT error_=S_OK;unsigned operation_=0;
};
}
