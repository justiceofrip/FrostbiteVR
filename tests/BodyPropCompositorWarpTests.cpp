// Explicit software D3D11 test. Never starts a game, SteamVR or a headset.
#include "fvr/graphics/D3D11BodyPropCompositor.h"
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
    auto catalog=std::make_shared<BodyPropCatalog>();catalog->push_back({{1,2,3},{Triangle(1,{1,0,0,1}),Triangle(2,{0,1,0,1})}});
    D3D11BodyPropCompositor compositor;CHECK(compositor.Initialize(context.Get(),64,64,28,catalog));
    std::array<ComPtr<ID3D11Texture2D>,2> images;std::array<ComPtr<ID3D11RenderTargetView>,2> rt;
    D3D11_TEXTURE2D_DESC desc{};desc.Width=desc.Height=64;desc.MipLevels=desc.ArraySize=desc.SampleDesc.Count=1;
    desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;desc.Usage=D3D11_USAGE_DEFAULT;desc.BindFlags=D3D11_BIND_RENDER_TARGET;
    const float blue[]={0,0,1,1};for(unsigned e=0;e<2;++e){CHECK(SUCCEEDED(device->CreateTexture2D(&desc,nullptr,&images[e])));
        CHECK(SUCCEEDED(device->CreateRenderTargetView(images[e].Get(),nullptr,&rt[e])));context->ClearRenderTargetView(rt[e].Get(),blue);}
    PairTicket ticket;ticket.frameId=1;ticket.spaceGeneration=2;ticket.trackingGeneration=3;
    BodyPropFrame frame;frame.frameId=1;frame.spaceGeneration=2;frame.trackingGeneration=3;
    for(auto& e:frame.eyes){e.count=1;e.view=Identity();e.projection=*math::MakeLhProjectionFromFovTangents({-1,1,1,-1},.05f,100);
        e.instances[0]={{1,2,3},Identity(),1000000000,1100000000,1,2,2};}
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
    std::cout<<"BodyPropCompositor WARP: stereo pixels, self-depth, atomic expiry passed; scene depth absent\n";
}
