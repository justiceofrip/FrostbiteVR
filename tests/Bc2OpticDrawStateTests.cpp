#include "Test.h"
#include "Bc2OpticDrawState.h"
#include <d3d11.h>
#include <iostream>
using namespace fvr::bc2;
int main(){CHECK(!CaptureOpticDrawState(nullptr).complete);
    ID3D11Device* d=nullptr;ID3D11DeviceContext* c=nullptr;D3D_FEATURE_LEVEL level{};
    CHECK(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&d,&level,&c)));
    D3D11_TEXTURE2D_DESC desc{};desc.Width=160;desc.Height=96;desc.MipLevels=1;desc.ArraySize=1;desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;desc.SampleDesc.Count=1;desc.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
    ID3D11Texture2D* target=nullptr;ID3D11Texture2D* source=nullptr;CHECK(SUCCEEDED(d->CreateTexture2D(&desc,nullptr,&target)));CHECK(SUCCEEDED(d->CreateTexture2D(&desc,nullptr,&source)));
    ID3D11RenderTargetView* rtv=nullptr;ID3D11ShaderResourceView* srv=nullptr;CHECK(SUCCEEDED(d->CreateRenderTargetView(target,nullptr,&rtv)));CHECK(SUCCEEDED(d->CreateShaderResourceView(source,nullptr,&srv)));
    c->OMSetRenderTargets(1,&rtv,nullptr);c->PSSetShaderResources(2,1,&srv);D3D11_VIEWPORT vp{3,5,150,80,0,1};c->RSSetViewports(1,&vp);D3D11_RECT scissor{4,6,140,78};c->RSSetScissorRects(1,&scissor);
    const auto a=CaptureOpticDrawState(c),b=CaptureOpticDrawState(c);CHECK(a.complete&&a.viewportCount==1&&a.viewports[0][2]==150&&a.scissors[0][0]==4);
    CHECK(a.targets[0].width==160&&a.targets[0].height==96&&a.targets[0].resource==reinterpret_cast<std::uintptr_t>(target));
    CHECK(a.pixelResources[2].resource==reinterpret_cast<std::uintptr_t>(source)&&a.pixelResources[2].width==160&&a.targets[0].view==b.targets[0].view);
    CHECK(!a.pixelResources[0].resource&&!a.depth.resource);
    ID3D11RenderTargetView* after=nullptr;ID3D11ShaderResourceView* sourceAfter=nullptr;c->OMGetRenderTargets(1,&after,nullptr);c->PSGetShaderResources(2,1,&sourceAfter);CHECK(after==rtv&&sourceAfter==srv);after->Release();sourceAfter->Release();
    D3D11_VIEWPORT many[5]{vp,vp,vp,vp,vp};c->RSSetViewports(5,many);CHECK(!CaptureOpticDrawState(c).complete);
    c->ClearState();srv->Release();rtv->Release();source->Release();target->Release();c->Release();d->Release();std::cout<<"Bc2OpticDrawState: 4 WARP cases passed\n";
}
