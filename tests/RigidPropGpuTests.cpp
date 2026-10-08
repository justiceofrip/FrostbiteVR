// MANUAL GPU TEST ONLY. Never registered with CTest or executed by focused build.
// Call only when the user is no longer running another VR/graphics test.
#include "fvr/graphics/D3D11RigidPropRenderer.h"
#include <Windows.h>
#include <d3d11_1.h>
#include <wrl/client.h>
#include "Test.h"
#include <cstring>
#include <iostream>
using namespace fvr;using namespace fvr::graphics;using Microsoft::WRL::ComPtr;
int main(){
    ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;D3D_FEATURE_LEVEL level{},want=D3D_FEATURE_LEVEL_11_0;
    CHECK(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,D3D11_CREATE_DEVICE_SINGLETHREADED,&want,1,D3D11_SDK_VERSION,&device,&level,&context)));
    D3D11_TEXTURE2D_DESC desc{};desc.Width=desc.Height=32;desc.ArraySize=desc.MipLevels=1;desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count=1;desc.Usage=D3D11_USAGE_DEFAULT;desc.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
    ComPtr<ID3D11Texture2D> nativeColor,propColor,depth,readback;CHECK(SUCCEEDED(device->CreateTexture2D(&desc,nullptr,&nativeColor)));
    CHECK(SUCCEEDED(device->CreateTexture2D(&desc,nullptr,&propColor)));
    ComPtr<ID3D11RenderTargetView> nativeRtv,propRtv;ComPtr<ID3D11ShaderResourceView> nativeSrv;
    CHECK(SUCCEEDED(device->CreateRenderTargetView(nativeColor.Get(),nullptr,&nativeRtv)));
    CHECK(SUCCEEDED(device->CreateRenderTargetView(propColor.Get(),nullptr,&propRtv)));
    CHECK(SUCCEEDED(device->CreateShaderResourceView(propColor.Get(),nullptr,&nativeSrv)));
    desc.BindFlags=D3D11_BIND_DEPTH_STENCIL;desc.Format=DXGI_FORMAT_D32_FLOAT;CHECK(SUCCEEDED(device->CreateTexture2D(&desc,nullptr,&depth)));
    ComPtr<ID3D11DepthStencilView> dsv;CHECK(SUCCEEDED(device->CreateDepthStencilView(depth.Get(),nullptr,&dsv)));
    desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;desc.BindFlags=0;desc.Usage=D3D11_USAGE_STAGING;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    CHECK(SUCCEEDED(device->CreateTexture2D(&desc,nullptr,&readback)));
    const float black[4]={0,0,0,1};context->ClearRenderTargetView(propRtv.Get(),black);context->ClearDepthStencilView(dsv.Get(),D3D11_CLEAR_DEPTH,1,0);
    auto* priorRtv=nativeRtv.Get();auto* priorSrv=nativeSrv.Get();context->OMSetRenderTargets(1,&priorRtv,nullptr);context->PSSetShaderResources(7,1,&priorSrv);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINELIST);const D3D11_VIEWPORT vp{2,3,17,19,.1f,.9f};context->RSSetViewports(1,&vp);
    D3D11RigidPropRenderer renderer(true);CHECK(renderer.Initialize(context.Get()));RigidPropSectionUpload source;
    source.mesh.sourceVertexSkinHash=1;source.mesh.sourcePositionHash=2;source.color={1,.2f,.1f,1};
    source.mesh.vertices={{-.7f,-.7f,0,0,0,1},{0,.7f,0,0,0,1},{.7f,-.7f,0,0,0,1}};CHECK(renderer.Upload({1,2,3},std::span(&source,1)));
    math::Matrix4 identity{};for(unsigned n=0;n<4;++n)identity.values[n][n]=1;auto world=identity;world.values[3][2]=1;
    const auto projection=math::MakeLhProjectionFromFovTangents({-1,1,1,-1},.05f,100);CHECK(projection);
    RigidPropEye eye{1,2,3,4,5,6,7,8,9,10,0,1000000000,1100000000};
    CHECK(renderer.Draw({1,2,3},eye,eye,eye.observedNs,world,identity,*projection,{propRtv.Get(),dsv.Get(),32,32}));
    ComPtr<ID3D11RenderTargetView> restoredRtv;ComPtr<ID3D11DepthStencilView> restoredDsv;ComPtr<ID3D11ShaderResourceView> restoredSrv;
    context->OMGetRenderTargets(1,&restoredRtv,&restoredDsv);context->PSGetShaderResources(7,1,&restoredSrv);
    CHECK(restoredRtv==nativeRtv&&!restoredDsv&&restoredSrv==nativeSrv);
    D3D11_PRIMITIVE_TOPOLOGY topology{};context->IAGetPrimitiveTopology(&topology);CHECK(topology==D3D11_PRIMITIVE_TOPOLOGY_LINELIST);
    D3D11_VIEWPORT restoredVp{};UINT count=1;context->RSGetViewports(&count,&restoredVp);CHECK(count==1&&std::memcmp(&vp,&restoredVp,sizeof(vp))==0);
    eye.eye=1;eye.view=11;CHECK(renderer.Draw({1,2,3},eye,eye,eye.observedNs,world,identity,*projection,{propRtv.Get(),dsv.Get(),32,32}));
    CHECK(!renderer.Draw({1,2,3},eye,eye,eye.observedNs,world,identity,*projection,{propRtv.Get(),dsv.Get(),32,32}));
    // The prior native SRV of the prop target remains bound after BOTH draws.
    restoredSrv.Reset();context->PSGetShaderResources(7,1,&restoredSrv);CHECK(restoredSrv==nativeSrv);
    context->CopyResource(readback.Get(),propColor.Get());D3D11_MAPPED_SUBRESOURCE mapped{};CHECK(SUCCEEDED(context->Map(readback.Get(),0,D3D11_MAP_READ,0,&mapped)));
    const auto* pixel=static_cast<const unsigned char*>(mapped.pData)+16*mapped.RowPitch+16*4;const bool visible=pixel[0]>80&&pixel[1]>10;
    context->Unmap(readback.Get(),0);CHECK(visible);context->ClearState();
    std::cout<<"RigidPropGpu: WARP two-eye draws and conflicting native SRV/RTV state restored; no native-game/headset proof\n";
}
