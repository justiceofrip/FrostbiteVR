// Explicit software D3D11 test. Never starts a game, SteamVR or a headset.
#include "fvr/graphics/D3D11BodyPropCompositor.h"
#include "fvr/graphics/D3D11SceneDepthPair.h"
#include "Test.h"
#include <Windows.h>
#include <d3d11.h>
#include <wrl/client.h>
#include <iostream>
using namespace fvr;using namespace fvr::graphics;using Microsoft::WRL::ComPtr;
namespace {
std::int64_t clockValue=1000000000;unsigned clockCalls=0,expireAt=0;
std::int64_t Clock()noexcept {++clockCalls;return expireAt&&clockCalls>=expireAt?1100000000:clockValue;}
auto Identity(){math::Matrix4 m{};for(unsigned i=0;i<4;++i)m.values[i][i]=1;return m;}
RigidPropSectionUpload Triangle(float z,std::array<float,4> tint){RigidPropSectionUpload s;s.color=tint;
    s.mesh.sourceVertexSkinHash=1;s.mesh.sourcePositionHash=2;
    s.mesh.vertices={{-.4f,-.4f,z,0,1,0},{0,.4f,z,0,1,0},{.4f,-.4f,z,0,1,0}};return s;}
std::array<unsigned char,4> Pixel(ID3D11Device* d,ID3D11DeviceContext* c,ID3D11Texture2D* t){
    D3D11_TEXTURE2D_DESC desc{};t->GetDesc(&desc);desc.Usage=D3D11_USAGE_STAGING;desc.BindFlags=0;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> read;if(FAILED(d->CreateTexture2D(&desc,nullptr,&read)))return {};c->CopyResource(read.Get(),t);
    D3D11_MAPPED_SUBRESOURCE mapped{};if(FAILED(c->Map(read.Get(),0,D3D11_MAP_READ,0,&mapped)))return {};
    const auto* p=static_cast<unsigned char*>(mapped.pData)+(desc.Height/2)*mapped.RowPitch+(desc.Width/2)*4;
    std::array<unsigned char,4> out{p[0],p[1],p[2],p[3]};c->Unmap(read.Get(),0);return out;
}
}
int main(){ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;D3D_FEATURE_LEVEL selected{};
    const D3D_FEATURE_LEVEL level=D3D_FEATURE_LEVEL_11_0;
    CHECK(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,&level,1,D3D11_SDK_VERSION,&device,&selected,&context)));
    auto catalog=std::make_shared<BodyPropCatalog>();
    for(unsigned n=0;n<MaxBodyPropAssets;++n)catalog->push_back({{n+1,2,3},{Triangle(1,{1,0,0,1}),Triangle(2,{0,1,0,1})}});
    // Keep the last asset's front section visibly red through an actual albedo
    // sample. The same depth/eye/expiry checks now exercise textured geometry.
    auto texture=std::make_shared<PropAlbedoTexture>();texture->width=texture->height=1;texture->mips={{{255,0,0,255}}};
    auto& front=catalog->back().sections[0];front.color={1,1,1,1};front.albedo=texture;front.uv.assign(3,{0,0});
    {auto overflow=std::make_shared<BodyPropCatalog>(*catalog);overflow->push_back({{MaxBodyPropAssets+1,2,3},catalog->front().sections});
        D3D11BodyPropCompositor rejected;CHECK(!rejected.Initialize(context.Get(),64,64,28,overflow));}
    {auto duplicate=std::make_shared<BodyPropCatalog>(*catalog);duplicate->back().key=duplicate->front().key;
        D3D11BodyPropCompositor rejected;CHECK(!rejected.Initialize(context.Get(),64,64,28,duplicate));}
    D3D11BodyPropCompositor compositor;CHECK(compositor.Initialize(context.Get(),64,64,28,catalog));
    std::array<ComPtr<ID3D11Texture2D>,2> images;std::array<ComPtr<ID3D11RenderTargetView>,2> rt;
    D3D11_TEXTURE2D_DESC desc{};desc.Width=desc.Height=64;desc.MipLevels=desc.ArraySize=desc.SampleDesc.Count=1;
    desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;desc.Usage=D3D11_USAGE_DEFAULT;desc.BindFlags=D3D11_BIND_RENDER_TARGET;
    const float blue[]={0,0,1,1};for(unsigned e=0;e<2;++e){CHECK(SUCCEEDED(device->CreateTexture2D(&desc,nullptr,&images[e])));
        CHECK(SUCCEEDED(device->CreateRenderTargetView(images[e].Get(),nullptr,&rt[e])));context->ClearRenderTargetView(rt[e].Get(),blue);}
    PairTicket ticket;ticket.frameId=1;ticket.spaceGeneration=2;ticket.trackingGeneration=3;
    BodyPropFrame frame;frame.frameId=1;frame.spaceGeneration=2;frame.trackingGeneration=3;
    for(auto& e:frame.eyes){e.count=1;e.view=Identity();e.projection=*math::MakeLhProjectionFromFovTangents({-1,1,1,-1},.05f,100);
        e.instances[0]={{MaxBodyPropAssets,2,3},Identity(),1000000000,1100000000,1,2,2};}
    CHECK(compositor.Compose(frame,ticket,{images[0].Get(),images[1].Get()},Clock));
    for(auto& i:images){const auto p=Pixel(device.Get(),context.Get(),i.Get());CHECK(p[0]>200&&p[1]<5&&p[2]<5);}
    // Front red geometry must occlude back green geometry even when the back
    // section is submitted later. ZERO depth writes would produce green here.
    CHECK(compositor.Statistics().pairs==1&&compositor.Statistics().instances==2);
    for(auto& r:rt)context->ClearRenderTargetView(r.Get(),blue);
    ++frame.frameId;++ticket.frameId;++frame.trackingGeneration;++ticket.trackingGeneration;
    clockCalls=0;expireAt=3;CHECK(!compositor.Compose(frame,ticket,{images[0].Get(),images[1].Get()},Clock));
    for(auto& i:images){const auto p=Pixel(device.Get(),context.Get(),i.Get());CHECK(p[0]<5&&p[1]<5&&p[2]>250);}
    CHECK(compositor.Statistics().pairs==1&&compositor.Statistics().expired==1);
    // Mid-pair expiry never partially decorates either original eye image.
    // Connected compositor path: actual rasterized prop depth must intersect
    // world AND first-person depth independently in both eyes.
    expireAt=0;clockCalls=0;
    std::array<ComPtr<ID3D11Texture2D>,4> scene;
    std::array<ComPtr<ID3D11ShaderResourceView>,4> views;
    std::array<ComPtr<ID3D11DepthStencilView>,4> dsv;
    desc.Format=DXGI_FORMAT_R32_TYPELESS;desc.BindFlags=D3D11_BIND_DEPTH_STENCIL|D3D11_BIND_SHADER_RESOURCE;
    D3D11_DEPTH_STENCIL_VIEW_DESC ds{};ds.Format=DXGI_FORMAT_D32_FLOAT;ds.ViewDimension=D3D11_DSV_DIMENSION_TEXTURE2D;
    D3D11_SHADER_RESOURCE_VIEW_DESC sv{};sv.Format=DXGI_FORMAT_R32_FLOAT;sv.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2D;sv.Texture2D.MipLevels=1;
    SceneDepthViews layers;
    for(unsigned i=0;i<4;++i){CHECK(SUCCEEDED(device->CreateTexture2D(&desc,nullptr,&scene[i])));
        CHECK(SUCCEEDED(device->CreateDepthStencilView(scene[i].Get(),&ds,&dsv[i])));
        CHECK(SUCCEEDED(device->CreateShaderResourceView(scene[i].Get(),&sv,&views[i])));layers.eyes[i/2][i%2]=views[i].Get();}
    for(unsigned mode=0;mode<3;++mode){
        for(auto& r:rt)context->ClearRenderTargetView(r.Get(),blue);
        for(unsigned i=0;i<4;++i){
            // Mode0: all visible. Mode1: world hides left, hand hides right.
            // Mode2: left visible, right hand hides; catch eye/source exchange.
            const bool occluded=mode==1?(i==0||i==3):mode==2&&i==3;
            context->ClearDepthStencilView(dsv[i].Get(),D3D11_CLEAR_DEPTH,occluded?.1f:1.f,0);}
        ++frame.frameId;++ticket.frameId;
        CHECK(compositor.Compose(frame,ticket,{images[0].Get(),images[1].Get()},Clock,&layers));
        for(unsigned e=0;e<2;++e){const auto p=Pixel(device.Get(),context.Get(),images[e].Get());
            const bool hidden=mode==1||(mode==2&&e==1);
            CHECK(hidden?(p[2]>250&&p[0]<5):(p[0]>200&&p[2]<5));}
    }
    CHECK(compositor.Statistics().sceneDepthPairs==3);
    for(auto& r:rt)context->ClearRenderTargetView(r.Get(),blue);
    layers.eyes[1][1]=nullptr;++frame.frameId;++ticket.frameId;
    CHECK(!compositor.Compose(frame,ticket,{images[0].Get(),images[1].Get()},Clock,&layers));
    // A bad right-eye depth never publishes only the already-rendered left.
    for(auto& i:images){const auto p=Pixel(device.Get(),context.Get(),i.Get());CHECK(p[0]<5&&p[2]>250);}
    CHECK(compositor.Statistics().sceneDepthFailures==1);
    std::cout<<"BodyPropCompositor WARP: self-depth, expiry, both native layers, distinct eyes and atomic depth failure passed\n";
}
