#include "fvr/graphics/D3D11DesktopMirror.h"
#include "Test.h"
#include <array>
#include <cstring>
#include <iostream>
#include <vector>
using namespace fvr::graphics;using Microsoft::WRL::ComPtr;
namespace {
ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;
ComPtr<ID3D11Texture2D> Texture(unsigned w,unsigned h,std::uint32_t color){
    D3D11_TEXTURE2D_DESC d{};d.Width=w;d.Height=h;d.MipLevels=d.ArraySize=d.SampleDesc.Count=1;
    d.Format=DXGI_FORMAT_R8G8B8A8_UNORM;d.Usage=D3D11_USAGE_DEFAULT;d.BindFlags=D3D11_BIND_RENDER_TARGET;
    const std::vector<std::uint32_t> pixels(std::size_t(w)*h,color);D3D11_SUBRESOURCE_DATA data{pixels.data(),w*4,0};
    ComPtr<ID3D11Texture2D> out;device->CreateTexture2D(&d,&data,&out);return out;
}
std::vector<std::uint32_t> Pixels(ID3D11Texture2D* t){
    D3D11_TEXTURE2D_DESC d{};t->GetDesc(&d);d.Usage=D3D11_USAGE_STAGING;d.BindFlags=0;d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> staging;if(FAILED(device->CreateTexture2D(&d,nullptr,&staging)))return {};
    context->CopyResource(staging.Get(),t);D3D11_MAPPED_SUBRESOURCE map{};
    if(FAILED(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&map)))return {};
    std::vector<std::uint32_t> out(std::size_t(d.Width)*d.Height);
    for(unsigned y=0;y<d.Height;++y)std::memcpy(out.data()+std::size_t(y)*d.Width,static_cast<char*>(map.pData)+y*map.RowPitch,d.Width*4);
    context->Unmap(staging.Get(),0);return out;
}
int Run(){
    CHECK(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&context)));
    auto left=Texture(8,6,0xff332211),right=Texture(8,6,0xffccbb99),target=Texture(8,6,0xffffffff);CHECK(left&&right&&target);
    ComPtr<ID3D11RenderTargetView> nativeRtv;CHECK(SUCCEEDED(device->CreateRenderTargetView(target.Get(),nullptr,&nativeRtv)));
    ID3D11RenderTargetView* bound=nativeRtv.Get();context->OMSetRenderTargets(1,&bound,nullptr);
    D3D11_VIEWPORT viewport{2,3,4,2,0,1};context->RSSetViewports(1,&viewport);
    D3D11DesktopMirror mirror;mirror.ObserveOwner(1,2,3);
    CHECK(mirror.Compose(target.Get(),true,true,false));CHECK(mirror.Neutral()==1);
    CHECK(mirror.CommitLeft(left.Get()));CHECK(mirror.Compose(target.Get(),true,true,false));
    CHECK(Pixels(target.Get())==Pixels(left.Get()));CHECK(mirror.Copied()==1);
    // Native subsequent eye frames cannot overwrite the retained completed left.
    context->CopyResource(left.Get(),right.Get());context->CopyResource(target.Get(),right.Get());
    CHECK(mirror.Compose(target.Get(),true,true,false));CHECK(Pixels(target.Get())[0]==0xff332211);
    CHECK(Pixels(right.Get())[0]==0xffccbb99);CHECK(Pixels(left.Get())[0]==0xffccbb99);
    ComPtr<ID3D11RenderTargetView> observed;context->OMGetRenderTargets(1,&observed,nullptr);CHECK(observed.Get()==nativeRtv.Get());
    UINT count=1;D3D11_VIEWPORT actual{};context->RSGetViewports(&count,&actual);CHECK(count==1&&std::memcmp(&actual,&viewport,sizeof(actual))==0);
    // Test Present must not paint or invalidate an accepted mirror.
    context->CopyResource(target.Get(),right.Get());CHECK(!mirror.Compose(target.Get(),true,true,false,true));CHECK(Pixels(target.Get())[0]==0xffccbb99);
    CHECK(mirror.Compose(target.Get(),true,true,false));CHECK(Pixels(target.Get())[0]==0xff332211);
    // Native menu is untouched, and gameplay cannot reuse a pre-menu scene.
    context->CopyResource(target.Get(),right.Get());CHECK(!mirror.Compose(target.Get(),true,true,true));CHECK(Pixels(target.Get())[0]==0xffccbb99);
    CHECK(mirror.Compose(target.Get(),true,true,false));CHECK(mirror.Neutral()==2);
    CHECK(mirror.CommitLeft(left.Get()));CHECK(mirror.Compose(target.Get(),true,false,false));CHECK(Pixels(target.Get())[0]==0xffccbb99);
    CHECK(mirror.Compose(target.Get(),true,true,false));CHECK(mirror.Neutral()==2);
    CHECK(mirror.CommitLeft(left.Get()));mirror.ObserveOwner(4,2,3);CHECK(mirror.Compose(target.Get(),true,true,false));CHECK(mirror.Neutral()==3);
    CHECK(mirror.CommitLeft(left.Get()));mirror.ObserveOwner(4,2,5);CHECK(mirror.Compose(target.Get(),true,true,false));CHECK(mirror.Neutral()==4);
    // Resize/crop stays GPU-only; borders are initialized rather than stale.
    CHECK(mirror.CommitLeft(left.Get()));auto larger=Texture(12,10,0xffffffff);CHECK(larger);
    CHECK(mirror.Compose(larger.Get(),true,true,false));auto p=Pixels(larger.Get());CHECK(p.size()==120&&p[0]!=0xffffffff&&p[2*12+2]==0xffccbb99&&p[7*12+9]==0xffccbb99&&p[8*12+10]==p[0]);
    auto smaller=Texture(4,2,0xffffffff);CHECK(smaller);CHECK(mirror.Compose(smaller.Get(),true,true,false));CHECK(Pixels(smaller.Get())==std::vector<std::uint32_t>(8,0xffccbb99));
    CHECK(!mirror.Compose(target.Get(),false,true,false));CHECK(mirror.Failed()==0);
    mirror.Reset();context->ClearState();return 0;
}
}
int main(){if(Run())return 1;std::cout<<"Desktop mirror WARP: completed-left isolation, retained frames, untouched sources/state, menu/test/inactive/owner/space transitions and resize/crop passed; no live game/headset verification\n";}
