#include "OpenXrAmmoHud.h"
#include "Test.h"
#include <cstring>
#include <iostream>
using namespace fvr;using Microsoft::WRL::ComPtr;
namespace {
template<class T>T Handle(std::uintptr_t n){return reinterpret_cast<T>(n);}
ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;ComPtr<ID3D11Texture2D> texture;
unsigned acquired=0,released=0,destroyed=0;XrResult waitResult=XR_SUCCESS;
std::int64_t now=1000000000;std::int64_t Clock()noexcept{return now;}
XrResult XRAPI_CALL CreateSwapchain(XrSession,const XrSwapchainCreateInfo* info,XrSwapchain* out){
    if(info->width!=256||info->height!=48)return XR_ERROR_VALIDATION_FAILURE;
    D3D11_TEXTURE2D_DESC d{};d.Width=info->width;d.Height=info->height;d.MipLevels=d.ArraySize=d.SampleDesc.Count=1;
    d.Format=DXGI_FORMAT_R8G8B8A8_TYPELESS;d.Usage=D3D11_USAGE_DEFAULT;d.BindFlags=D3D11_BIND_RENDER_TARGET;
    if(FAILED(device->CreateTexture2D(&d,nullptr,&texture)))return XR_ERROR_RUNTIME_FAILURE;
    *out=Handle<XrSwapchain>(3);return XR_SUCCESS;
}
XrResult XRAPI_CALL DestroySwapchain(XrSwapchain){++destroyed;texture.Reset();return XR_SUCCESS;}
XrResult XRAPI_CALL EnumerateSwapchainImages(XrSwapchain,std::uint32_t capacity,std::uint32_t* count,XrSwapchainImageBaseHeader* images){
    *count=1;if(capacity)reinterpret_cast<XrSwapchainImageD3D11KHR*>(images)[0].texture=texture.Get();return XR_SUCCESS;
}
XrResult XRAPI_CALL AcquireSwapchainImage(XrSwapchain,const XrSwapchainImageAcquireInfo*,std::uint32_t* index){++acquired;*index=0;return XR_SUCCESS;}
XrResult XRAPI_CALL WaitSwapchainImage(XrSwapchain,const XrSwapchainImageWaitInfo* info){return info->timeout==0?waitResult:XR_ERROR_VALIDATION_FAILURE;}
XrResult XRAPI_CALL ReleaseSwapchainImage(XrSwapchain,const XrSwapchainImageReleaseInfo*){++released;return XR_SUCCESS;}
XrResult XRAPI_CALL Get(XrInstance,const char* name,PFN_xrVoidFunction* out){
#define FN(n) if(std::strcmp(name,"xr" #n)==0){*out=reinterpret_cast<PFN_xrVoidFunction>(n);return XR_SUCCESS;}
    FN(CreateSwapchain) FN(DestroySwapchain) FN(EnumerateSwapchainImages)
    FN(AcquireSwapchainImage) FN(WaitSwapchainImage) FN(ReleaseSwapchainImage)
#undef FN
    *out=nullptr;return XR_ERROR_FUNCTION_UNSUPPORTED;
}
int PixelsMatch(int loaded,int reserve){D3D11_TEXTURE2D_DESC d{};texture->GetDesc(&d);d.Usage=D3D11_USAGE_STAGING;d.BindFlags=0;d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> readback;CHECK(SUCCEEDED(device->CreateTexture2D(&d,nullptr,&readback)));
    context->CopyResource(readback.Get(),texture.Get());D3D11_MAPPED_SUBRESOURCE mapped{};
    CHECK(SUCCEEDED(context->Map(readback.Get(),0,D3D11_MAP_READ,0,&mapped)));graphics::AmmoCounterBitmap expected;CHECK(expected.Render(loaded,reserve));
    bool same=true;for(unsigned y=0;y<expected.Height;++y)same&=std::memcmp(static_cast<const char*>(mapped.pData)+y*mapped.RowPitch,
        expected.pixels.data()+y*expected.Width*4,expected.Width*4)==0;
    context->Unmap(readback.Get(),0);CHECK(same);return 0;
}
int Run(){
    CHECK(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&context)));
    graphics::PairTicket ticket;ticket.frameId=1;ticket.spaceGeneration=30;ticket.trackingGeneration=40;
    graphics::BodyPropFrame frame;frame.frameId=ticket.frameId;frame.spaceGeneration=ticket.spaceGeneration;frame.trackingGeneration=ticket.trackingGeneration;
    for(auto& e:frame.eyes)e.ammo={1,now,now+100000000,10,20,30,30,180,30};
    {
        xr::OpenXrAmmoHud hud;CHECK(hud.Initialize(Handle<XrInstance>(1),Handle<XrSession>(2),Get,device.Get(),DXGI_FORMAT_R8G8B8A8_UNORM_SRGB));
        hud.Receive(&frame,ticket,now);const auto* layer=hud.Layer(Handle<XrSpace>(4),30,Clock,true);CHECK(layer);
        CHECK(layer->space==Handle<XrSpace>(4)&&layer->eyeVisibility==XR_EYE_VISIBILITY_BOTH);
        CHECK(layer->pose.position.y<0&&layer->pose.position.z<0&&Near(layer->size.width,.42f));CHECK(PixelsMatch(30,180)==0);
        CHECK(hud.Uploads()==1&&acquired==released);CHECK(hud.Layer(Handle<XrSpace>(4),30,Clock,true));CHECK(hud.Uploads()==1);
        now+=100000000;CHECK(!hud.Layer(Handle<XrSpace>(4),30,Clock,true)); // Retained world cannot retain stale digits.
        for(auto& e:frame.eyes){++e.ammo.sequence;e.ammo.observedNs=now;e.ammo.deadlineNs=now+100000000;e.ammo.loaded=0;e.ammo.reserve=0;}
        hud.Receive(&frame,ticket,now);CHECK(hud.Layer(Handle<XrSpace>(4),30,Clock,true));CHECK(PixelsMatch(0,0)==0);
        CHECK(!hud.Layer(Handle<XrSpace>(4),30,Clock,false));CHECK(!hud.Layer(Handle<XrSpace>(4),30,Clock,true));
        hud.Receive(&frame,ticket,now);CHECK(hud.Layer(Handle<XrSpace>(4),30,Clock,true));
        ++frame.eyes[1].ammo.equipmentGeneration;hud.Receive(&frame,ticket,now);CHECK(!hud.Layer(Handle<XrSpace>(4),30,Clock,true));
        --frame.eyes[1].ammo.equipmentGeneration;
        for(auto& e:frame.eyes){++e.ammo.sequence;e.ammo.loaded=1;}
        hud.Receive(&frame,ticket,now);waitResult=XR_TIMEOUT_EXPIRED;
        CHECK(!hud.Layer(Handle<XrSpace>(4),30,Clock,true));CHECK(acquired==released+1);
        CHECK(!hud.Layer(Handle<XrSpace>(4),30,Clock,true));CHECK(acquired==released+1); // No double acquire.
        waitResult=XR_SUCCESS;CHECK(hud.Layer(Handle<XrSpace>(4),30,Clock,true));CHECK(acquired==released);CHECK(PixelsMatch(1,0)==0);
        hud.Receive(nullptr,ticket,now);CHECK(!hud.Layer(Handle<XrSpace>(4),30,Clock,true));CHECK(hud.Errors()==0);
        CHECK(hud.ValidSamples()==4&&hud.InvalidSamples()==2);
    }
    CHECK(destroyed==1);return 0;
}
}
int main(){if(Run())return 1;std::cout<<"AmmoCounterHud: WARP pixel readback and mocked XR lifecycle/freshness passed; no headset acceptance\n";}
