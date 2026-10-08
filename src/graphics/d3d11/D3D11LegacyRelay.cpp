#include "fvr/graphics/D3D11LegacyRelay.h"
#include <dxgi.h>
namespace fvr::graphics {
using Microsoft::WRL::ComPtr;
void D3D11LegacyRelay::Reset()noexcept {
    writable_.store(false,std::memory_order_release);waitingRelay_=false;
    relayDone_.Reset();gameDone_.Reset();for(auto& eye:opened_)eye.Reset();for(auto& eye:gameEyes_)eye.Reset();
    relayContext_.Reset();gameContext_.Reset();relayDevice_.Reset();gameDevice_.Reset();error_=S_OK;
}
bool D3D11LegacyRelay::Create(ID3D11Device* game,UINT width,UINT height,DXGI_FORMAT format)noexcept {
    Reset();operation_=1;if(!game){error_=E_INVALIDARG;return false;}
    gameDevice_=game;game->GetImmediateContext(&gameContext_);
    ComPtr<IDXGIDevice> dxgi;operation_=2;error_=game->QueryInterface(IID_PPV_ARGS(&dxgi));if(FAILED(error_))return false;
    ComPtr<IDXGIAdapter> adapter;operation_=3;error_=dxgi->GetAdapter(&adapter);if(FAILED(error_))return false;
    const D3D_FEATURE_LEVEL levels[]={D3D_FEATURE_LEVEL_11_0};D3D_FEATURE_LEVEL level{};
    operation_=4;error_=D3D11CreateDevice(adapter.Get(),D3D_DRIVER_TYPE_UNKNOWN,nullptr,D3D11_CREATE_DEVICE_BGRA_SUPPORT,levels,1,D3D11_SDK_VERSION,&relayDevice_,&level,&relayContext_);if(FAILED(error_))return false;
    D3D11_TEXTURE2D_DESC d{};d.Width=width;d.Height=height;d.MipLevels=d.ArraySize=d.SampleDesc.Count=1;
    // Storage interpretation is preserved by byte copies into the typed export.
    d.Format=CopyCompatibleFormats(format,28)?DXGI_FORMAT_R8G8B8A8_UNORM:DXGI_FORMAT_B8G8R8A8_UNORM;
    d.Usage=D3D11_USAGE_DEFAULT;d.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;d.MiscFlags=D3D11_RESOURCE_MISC_SHARED;
    for(unsigned eye=0;eye<2;++eye){
        operation_=5;error_=game->CreateTexture2D(&d,nullptr,&gameEyes_[eye]);if(FAILED(error_))return false;
        ComPtr<IDXGIResource> resource;operation_=6;error_=gameEyes_[eye].As(&resource);if(FAILED(error_))return false;
        HANDLE handle=nullptr;operation_=7;error_=resource->GetSharedHandle(&handle);if(FAILED(error_)||!handle)return false;
        // Legacy DXGI handles are not NT handles: never CloseHandle/duplicate.
        operation_=8;error_=relayDevice_->OpenSharedResource(handle,IID_PPV_ARGS(&opened_[eye]));if(FAILED(error_))return false;
    }
    D3D11_QUERY_DESC query{D3D11_QUERY_EVENT,0};operation_=9;error_=game->CreateQuery(&query,&gameDone_);if(FAILED(error_))return false;
    operation_=10;error_=relayDevice_->CreateQuery(&query,&relayDone_);if(FAILED(error_))return false;
    writable_.store(true,std::memory_order_release);return true;
}
void D3D11LegacyRelay::Capture(unsigned eye,ID3D11Texture2D* source)noexcept {gameContext_->CopyResource(gameEyes_[eye].Get(),source);}
void D3D11LegacyRelay::SealGameCopies()noexcept {writable_.store(false,std::memory_order_release);gameContext_->End(gameDone_.Get());gameContext_->Flush();}
HRESULT D3D11LegacyRelay::GameCopiesReady()noexcept {BOOL done=FALSE;const auto hr=gameContext_->GetData(gameDone_.Get(),&done,sizeof(done),D3D11_ASYNC_GETDATA_DONOTFLUSH);return hr==S_OK&&!done?S_FALSE:hr;}
void D3D11LegacyRelay::SealRelayRead()noexcept {relayContext_->End(relayDone_.Get());relayContext_->Flush();waitingRelay_=true;}
HRESULT D3D11LegacyRelay::PollRelayRead()noexcept {
    if(!waitingRelay_)return S_FALSE;BOOL done=FALSE;const auto hr=relayContext_->GetData(relayDone_.Get(),&done,sizeof(done),D3D11_ASYNC_GETDATA_DONOTFLUSH);
    if(hr==S_OK&&done){waitingRelay_=false;writable_.store(true,std::memory_order_release);return S_OK;}return hr==S_OK?S_FALSE:hr;
}
}
