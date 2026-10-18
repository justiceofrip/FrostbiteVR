#include "fvr/graphics/D3D11DesktopMirror.h"
#include "fvr/graphics/SharedTextureProtocol.h"
#include <algorithm>
namespace fvr::graphics {namespace {
bool Shape(const D3D11_TEXTURE2D_DESC& d)noexcept {
    return d.Width&&d.Height&&d.Width<=16384&&d.Height<=16384&&d.ArraySize==1&&d.MipLevels==1&&
        d.SampleDesc.Count==1&&CopyCompatibleFormats(d.Format,d.Format);
}
}
void D3D11DesktopMirror::ObserveOwner(std::uint64_t owner,std::uint64_t device,std::uint64_t space)noexcept {
    if(owner_!=owner||deviceEpoch_!=device||space_!=space||!owner||!device||!space)ready_=false;
    owner_=owner;deviceEpoch_=device;space_=space;
}
bool D3D11DesktopMirror::CommitLeft(ID3D11Texture2D* source)noexcept {
    if(!source||!owner_||!deviceEpoch_||!space_){++failed_;return false;}
    D3D11_TEXTURE2D_DESC d{};source->GetDesc(&d);
    if(!Shape(d)){ready_=false;++failed_;return false;}
    Microsoft::WRL::ComPtr<ID3D11Device> device;source->GetDevice(&device);
    if(!left_||device.Get()!=device_.Get()||d.Width!=description_.Width||d.Height!=description_.Height||d.Format!=description_.Format){
        ready_=false;left_.Reset();device_=device;device_->GetImmediateContext(&context_);
        description_=d;d.Usage=D3D11_USAGE_DEFAULT;d.BindFlags=0;d.CPUAccessFlags=0;d.MiscFlags=0;
        if(FAILED(device_->CreateTexture2D(&d,nullptr,&left_))){++failed_;return false;}
    }
    context_->CopyResource(left_.Get(),source);ready_=true;++committed_;return true;
}
bool D3D11DesktopMirror::Present(IDXGISwapChain* chain,bool active,bool known,bool menu,bool test)noexcept {
    // Invalidate the pre-menu view even if the native menu needs no texture.
    if(!active||(known&&menu)){ready_=false;return false;}
    if(test||!chain)return false;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> target;
    if(FAILED(chain->GetBuffer(0,IID_PPV_ARGS(&target)))){++failed_;return false;}
    return Compose(target.Get(),active,known,menu,false);
}
bool D3D11DesktopMirror::Compose(ID3D11Texture2D* target,bool active,bool known,bool menu,bool test)noexcept {
    if(!active||(known&&menu)){ready_=false;return false;}
    if(test)return false;
    if(!target){++failed_;return false;}
    D3D11_TEXTURE2D_DESC d{};target->GetDesc(&d);
    if(!Shape(d)){++failed_;return false;}
    Microsoft::WRL::ComPtr<ID3D11Device> targetDevice;target->GetDevice(&targetDevice);
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;targetDevice->GetImmediateContext(&context);
    const bool copy=ready_&&left_&&device_.Get()==targetDevice.Get()&&CopyCompatibleFormats(description_.Format,d.Format);
    // A dropped/unfinished pair keeps the last complete image, never the native
    // partially composed stereo target. Unknown UI state retains that image
    // instead of returning to unstable native composition. A known menu passes
    // through unchanged; a new scene/menu/device starts neutral.
    if(!copy||description_.Width!=d.Width||description_.Height!=d.Height){
        D3D11_RENDER_TARGET_VIEW_DESC view{};view.ViewDimension=D3D11_RTV_DIMENSION_TEXTURE2D;
        view.Format=d.Format;
        if(d.Format==DXGI_FORMAT_R8G8B8A8_TYPELESS)view.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
        if(d.Format==DXGI_FORMAT_B8G8R8A8_TYPELESS)view.Format=DXGI_FORMAT_B8G8R8A8_UNORM;
        Microsoft::WRL::ComPtr<ID3D11RenderTargetView> rtv;
        if(FAILED(targetDevice->CreateRenderTargetView(target,&view,&rtv))){++failed_;return false;}
        constexpr float neutral[]{.025f,.025f,.025f,1};context->ClearRenderTargetView(rtv.Get(),neutral);
    }
    if(!copy){++neutral_;return true;}
    if(description_.Width==d.Width&&description_.Height==d.Height)context->CopyResource(target,left_.Get());
    else {
        const auto w=(std::min)(description_.Width,d.Width),h=(std::min)(description_.Height,d.Height);
        const auto x=(description_.Width-w)/2,y=(description_.Height-h)/2;
        const D3D11_BOX source{x,y,0,x+w,y+h,1};
        context->CopySubresourceRegion(target,0,(d.Width-w)/2,(d.Height-h)/2,0,left_.Get(),0,&source);
    }
    ++copied_;return true;
}
void D3D11DesktopMirror::Reset()noexcept {
    left_.Reset();context_.Reset();device_.Reset();description_={};ready_=false;owner_=deviceEpoch_=space_=0;
}
}
