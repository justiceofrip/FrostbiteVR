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
    context->Unmap(readback.Get(),0);CHECK(visible);
    // Actual texture sampling, not a mock of the upload's validation. Keep a
    // conflicting native SRV and sampler at the slots used by the prop shader.
    context->PSSetShaderResources(0,1,&priorSrv);
    D3D11_SAMPLER_DESC sd{};sd.Filter=D3D11_FILTER_MIN_MAG_MIP_POINT;
    sd.AddressU=sd.AddressV=sd.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;sd.MaxLOD=D3D11_FLOAT32_MAX;
    ComPtr<ID3D11SamplerState> nativeSampler;CHECK(SUCCEEDED(device->CreateSamplerState(&sd,&nativeSampler)));
    auto* priorSampler=nativeSampler.Get();context->PSSetSamplers(0,1,&priorSampler);
    source.color={1,1,1,1};for(auto& v:source.mesh.vertices){v.ny=1;v.nz=0;}
    auto texture=std::make_shared<PropAlbedoTexture>();texture->width=texture->height=2;
    texture->mips={{{255,0,0,255,0,255,0,255,0,0,255,255,255,255,255,255}}};
    auto drawPixel=[&]()->std::array<unsigned char,4>{
        context->ClearRenderTargetView(propRtv.Get(),black);context->ClearDepthStencilView(dsv.Get(),D3D11_CLEAR_DEPTH,1,0);
        ++eye.frame;++eye.input;
        if(!renderer.Draw({1,2,3},eye,eye,eye.observedNs,world,identity,*projection,{propRtv.Get(),dsv.Get(),32,32}))return {};
        context->CopyResource(readback.Get(),propColor.Get());D3D11_MAPPED_SUBRESOURCE sample{};
        if(FAILED(context->Map(readback.Get(),0,D3D11_MAP_READ,0,&sample)))return {};
        const auto* p=static_cast<const unsigned char*>(sample.pData)+16*sample.RowPitch+16*4;
        std::array<unsigned char,4> result{p[0],p[1],p[2],p[3]};context->Unmap(readback.Get(),0);return result;
    };
    for(unsigned c=0;c<4;++c){
        auto t=std::make_shared<PropAlbedoTexture>(*texture);t->address=c==3?PropTextureAddress::Clamp:PropTextureAddress::Wrap;
        source.albedo=t;source.uv.assign(3,{c==0?.25f:c==1?.75f:-.25f,.25f});
        CHECK(renderer.Upload({1,2,3},std::span(&source,1)));const auto p=drawPixel();
        CHECK(p[3]==255&&((c==0||c==3)?(p[0]>250&&p[1]<5):(p[0]<5&&p[1]>250)));
    }
    for(auto space:{PropTextureColorSpace::Linear,PropTextureColorSpace::Srgb}){
        auto t=std::make_shared<PropAlbedoTexture>();t->width=t->height=1;t->colorSpace=space;t->mips={{{128,128,128,255}}};
        source.albedo=t;source.uv.assign(3,{0,0});CHECK(renderer.Upload({1,2,3},std::span(&source,1)));
        const auto p=drawPixel();const int expected=space==PropTextureColorSpace::Linear?128:55;
        CHECK(std::abs(int(p[0])-expected)<=1&&p[0]==p[1]&&p[1]==p[2]);
    }
    // A minified supplied mip is sampled, rather than silently dropping mips.
    auto mipTexture=std::make_shared<PropAlbedoTexture>(*texture);
    mipTexture->mips[0].rgba={255,0,0,255,255,0,0,255,255,0,0,255,255,0,0,255};
    mipTexture->mips.push_back({{0,255,0,255}});source.albedo=mipTexture;source.uv={{{0,0}},{{0,64}},{{64,0}}};
    CHECK(renderer.Upload({1,2,3},std::span(&source,1)));{const auto p=drawPixel();CHECK(p[0]<5&&p[1]>250);}
    // Invalid replacements preserve the preceding GPU upload transactionally.
    for(unsigned c=0;c<6;++c){auto bad=source;auto t=std::make_shared<PropAlbedoTexture>(*mipTexture);bad.albedo=t;
        if(c==0)bad.uv.pop_back();if(c==1)bad.uv[0][0]=NAN;if(c==2)t->mips[0].rgba.pop_back();
        if(c==3)t->mips.push_back({{0,0,0,255}});if(c==4)t->width=4096;if(c==5)bad.albedo.reset();
        CHECK(!renderer.Upload({1,2,3},std::span(&bad,1)));const auto p=drawPixel();CHECK(p[0]<5&&p[1]>250);
    }
    restoredSrv.Reset();context->PSGetShaderResources(0,1,&restoredSrv);CHECK(restoredSrv==nativeSrv);
    ComPtr<ID3D11SamplerState> restoredSampler;context->PSGetSamplers(0,1,&restoredSampler);CHECK(restoredSampler==nativeSampler);
    context->ClearState();
    std::cout<<"RigidPropGpu: WARP two-eye state restoration; explicit albedo UV, wrap/clamp, color space, mip sampling and transactional rejection passed; no game/headset proof\n";
}
