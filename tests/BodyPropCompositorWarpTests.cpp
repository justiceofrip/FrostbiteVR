// Explicit software D3D11 test. Never starts a game, SteamVR or a headset.
#include "fvr/graphics/D3D11BodyPropCompositor.h"
#include "fvr/graphics/D3D11SceneDepthPair.h"
#include "fvr/graphics/VehicleReticle.h"
#include "Test.h"
#include <Windows.h>
#include <d3d11.h>
#include <wrl/client.h>
#include <iostream>
#include <optional>
using namespace fvr;using namespace fvr::graphics;using Microsoft::WRL::ComPtr;
namespace {
std::int64_t clockValue=1000000000;unsigned clockCalls=0,expireAt=0;
std::int64_t Clock()noexcept {++clockCalls;return expireAt&&clockCalls>=expireAt?1100000000:clockValue;}
auto Identity(){math::Matrix4 m{};for(unsigned i=0;i<4;++i)m.values[i][i]=1;return m;}
RigidPropSectionUpload Triangle(float z,std::array<float,4> tint){RigidPropSectionUpload s;s.color=tint;
    s.mesh.sourceVertexSkinHash=1;s.mesh.sourcePositionHash=2;
    s.mesh.vertices={{-.4f,-.4f,z,0,1,0},{0,.4f,z,0,1,0},{.4f,-.4f,z,0,1,0}};return s;}
std::array<unsigned char,4> Pixel(ID3D11Device* d,ID3D11DeviceContext* c,ID3D11Texture2D* t,int x=-1,int y=-1){
    D3D11_TEXTURE2D_DESC desc{};t->GetDesc(&desc);desc.Usage=D3D11_USAGE_STAGING;desc.BindFlags=0;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> read;if(FAILED(d->CreateTexture2D(&desc,nullptr,&read)))return {};c->CopyResource(read.Get(),t);
    D3D11_MAPPED_SUBRESOURCE mapped{};if(FAILED(c->Map(read.Get(),0,D3D11_MAP_READ,0,&mapped)))return {};
    const auto* p=static_cast<unsigned char*>(mapped.pData)+(y<0?desc.Height/2:unsigned(y))*mapped.RowPitch+(x<0?desc.Width/2:unsigned(x))*4;
    std::array<unsigned char,4> out{p[0],p[1],p[2],p[3]};c->Unmap(read.Get(),0);return out;
}
std::optional<unsigned> WhitePixels(ID3D11DeviceContext* c,ID3D11Texture2D* source,ID3D11Texture2D* read,
    unsigned left,unsigned top,unsigned size){
    const D3D11_BOX region{left,top,0,left+size,top+size,1};
    c->CopySubresourceRegion(read,0,0,0,0,source,0,&region);
    D3D11_MAPPED_SUBRESOURCE mapped{};if(FAILED(c->Map(read,0,D3D11_MAP_READ,0,&mapped)))return {};
    unsigned white=0;
    for(unsigned y=0;y<size;++y){const auto* p=static_cast<const unsigned char*>(mapped.pData)+y*mapped.RowPitch;
        for(unsigned x=0;x<size;++x,p+=4)if(p[0]>250&&p[1]>250&&p[2]>250&&p[3]>250)++white;}
    c->Unmap(read,0);return white;
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
    // Aiming glyph is visible even when the native scene is nearer than its
    // display plane; it must not disappear behind terrain like body equipment.
    layers.eyes[1][1]=views[3].Get();
    for(auto& depth:dsv)context->ClearDepthStencilView(depth.Get(),D3D11_CLEAR_DEPTH,.1f,0);
    for(auto& r:rt)context->ClearRenderTargetView(r.Get(),blue);
    for(auto& e:frame.eyes){e.instances[0].geometry=VehicleReticleGeometry;
        e.instances[0].equipmentGeneration=BodyPropWireEpoch(BodyPropSourceKind::VehicleReticle,1);
        e.instances[0].world=Identity();e.instances[0].world.values[3][2]=2;}
    ++frame.frameId;++ticket.frameId;
    CHECK(compositor.Compose(frame,ticket,{images[0].Get(),images[1].Get()},Clock,&layers));
    for(auto& i:images){const auto p=Pixel(device.Get(),context.Get(),i.Get(),40,32);CHECK(p[0]>200&&p[1]>200&&p[2]>200);
        const auto gap=Pixel(device.Get(),context.Get(),i.Get());CHECK(gap[0]<5&&gap[2]>250);}
    // Foot->vehicle can lose the first-person depth pass while retaining a
    // proven aiming ray. Exercise the actual shared missing-depth admission,
    // including another body prop that would paint the center red if admitted.
    const auto reticle=frame.eyes[0].instances[0];
    const BodyPropInstance body{{MaxBodyPropAssets,2,3},Identity(),1000000000,1100000000,1,
        BodyPropWireEpoch(BodyPropSourceKind::Ammo,1),2};
    for(auto& e:frame.eyes){e.count=2;e.instances[1]=body;}
    for(auto& r:rt)context->ClearRenderTargetView(r.Get(),blue);
    const auto depthPairs=compositor.Statistics().sceneDepthPairs;
    ++frame.frameId;++ticket.frameId;
    CHECK(compositor.Compose(frame,ticket,{images[0].Get(),images[1].Get()},Clock,nullptr,BodyPropMissingDepthPolicy::VehicleReticleOnly));
    CHECK(compositor.Statistics().overlayOnlyPairs==1&&compositor.Statistics().depthOmittedInstances==1&&compositor.Statistics().sceneDepthPairs==depthPairs);
    for(auto& i:images){const auto glyph=Pixel(device.Get(),context.Get(),i.Get(),40,32);CHECK(glyph[0]>200&&glyph[1]>200&&glyph[2]>200);
        const auto middle=Pixel(device.Get(),context.Get(),i.Get());CHECK(middle[0]<5&&middle[2]>250);}
    // A missing/stale/unequal/wrong-namespace/out-of-view reticle cannot use
    // the exception, and cannot expose a body prop or decorate just one eye.
    for(unsigned failure=0;failure<7;++failure){
        for(auto& e:frame.eyes){e.count=1;e.instances[0]=reticle;e.view=Identity();}
        if(failure==0)for(auto& e:frame.eyes)e.instances[0]=body;
        if(failure==1)frame.eyes[1].count=0;
        if(failure==2)frame.eyes[1].instances[0].world.values[3][0]=.01f;
        if(failure==3){frame.eyes[1].instances[0].deadlineNs=1000000001;clockValue=1000000002;}
        if(failure==4)for(auto& e:frame.eyes)e.instances[0].equipmentGeneration=2;
        if(failure==5)frame.eyes[1].view.values[3][0]=10;
        if(failure==6){clockCalls=0;expireAt=3;}
        for(auto& r:rt)context->ClearRenderTargetView(r.Get(),blue);
        ++frame.frameId;++ticket.frameId;
        CHECK(!compositor.Compose(frame,ticket,{images[0].Get(),images[1].Get()},Clock,nullptr,BodyPropMissingDepthPolicy::VehicleReticleOnly));
        for(auto& i:images){const auto middle=Pixel(device.Get(),context.Get(),i.Get());const auto glyph=Pixel(device.Get(),context.Get(),i.Get(),40,32);
            CHECK(middle[0]<5&&middle[2]>250&&glyph[0]<5&&glyph[2]>250);}
        clockValue=1000000000;expireAt=0;clockCalls=0;
    }
    // Restored exact depth re-enables ordinary geometry through the same API.
    // Its native world occlusion still applies, while the glyph remains visible.
    for(auto& e:frame.eyes){e.count=2;e.instances[0]=reticle;e.instances[1]=body;e.view=Identity();}
    for(unsigned hidden=0;hidden<2;++hidden){
        for(auto& d:dsv)context->ClearDepthStencilView(d.Get(),D3D11_CLEAR_DEPTH,hidden?.1f:1.f,0);
        for(auto& r:rt)context->ClearRenderTargetView(r.Get(),blue);
        ++frame.frameId;++ticket.frameId;
        CHECK(compositor.Compose(frame,ticket,{images[0].Get(),images[1].Get()},Clock,&layers,BodyPropMissingDepthPolicy::VehicleReticleOnly));
        for(auto& i:images){const auto middle=Pixel(device.Get(),context.Get(),i.Get());const auto glyph=Pixel(device.Get(),context.Get(),i.Get(),40,32);
            CHECK(hidden?(middle[0]<5&&middle[2]>250):(middle[0]>200&&middle[2]<5));CHECK(glyph[0]>200&&glyph[1]>200);}
    }
    // Synthetic full-resolution sight line at the observed display scale. The
    // old .10 half-stroke covers no pixel centers at the initial phase, despite
    // a successful draw. Sweep quarter-pixel phases and require visible area in
    // each eye through the production compositor, not a chosen fixed pixel.
    {
        constexpr unsigned FullWidth=1920,FullHeight=1080,RoiSize=16;
        D3D11BodyPropCompositor fullCompositor;
        CHECK(fullCompositor.Initialize(context.Get(),FullWidth,FullHeight,28,catalog));
        D3D11_TEXTURE2D_DESC fullDesc{};fullDesc.Width=FullWidth;fullDesc.Height=FullHeight;
        fullDesc.MipLevels=fullDesc.ArraySize=fullDesc.SampleDesc.Count=1;
        fullDesc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;fullDesc.Usage=D3D11_USAGE_DEFAULT;fullDesc.BindFlags=D3D11_BIND_RENDER_TARGET;
        std::array<ComPtr<ID3D11Texture2D>,2> fullColor;std::array<ComPtr<ID3D11RenderTargetView>,2> fullTargets;
        for(unsigned eye=0;eye<2;++eye){CHECK(SUCCEEDED(device->CreateTexture2D(&fullDesc,nullptr,&fullColor[eye])));
            CHECK(SUCCEEDED(device->CreateRenderTargetView(fullColor[eye].Get(),nullptr,&fullTargets[eye])));}
        fullDesc.Width=fullDesc.Height=RoiSize;fullDesc.Usage=D3D11_USAGE_STAGING;
        fullDesc.BindFlags=0;fullDesc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        ComPtr<ID3D11Texture2D> fullRead;CHECK(SUCCEEDED(device->CreateTexture2D(&fullDesc,nullptr,&fullRead)));
        const auto fullProjection=math::MakeLhProjectionFromFovTangents({-1,1,.8f,-.8f},.1f,2000);
        CHECK(fullProjection);
        auto world=Identity();for(unsigned n=0;n<3;++n)world.values[n][n]=.175f;
        // 64 mm IPD, 54 m depth; a 2 cm vertical offset gives a quarter-pixel
        // center phase. These are generated matrices, not native game captures.
        world.values[3]={-.032f,-.02f,54,1};
        PairTicket fullTicket;fullTicket.spaceGeneration=2;fullTicket.trackingGeneration=3;
        BodyPropFrame fullFrame;fullFrame.spaceGeneration=2;fullFrame.trackingGeneration=3;
        clockValue=1000000000;expireAt=0;clockCalls=0;
        for(unsigned phaseY=0;phaseY<4;++phaseY)for(unsigned phaseX=0;phaseX<4;++phaseX){
            ++fullTicket.frameId;fullFrame.frameId=fullTicket.frameId;
            for(unsigned eye=0;eye<2;++eye){auto& e=fullFrame.eyes[eye];e.count=1;e.view=Identity();
                e.view.values[3][0]=eye?-.032f:.032f;e.projection=*fullProjection;
                e.projection.values[2][0]+=2*(float(phaseX)*.25f)/float(FullWidth);
                e.projection.values[2][1]-=2*(float(phaseY)*.25f)/float(FullHeight);
                e.instances[0]={VehicleReticleGeometry,world,1000000000,1100000000,1,
                    BodyPropWireEpoch(BodyPropSourceKind::VehicleReticle,1),2};
                context->ClearRenderTargetView(fullTargets[eye].Get(),blue);}
            CHECK(fullCompositor.Compose(fullFrame,fullTicket,{fullColor[0].Get(),fullColor[1].Get()},Clock,
                nullptr,BodyPropMissingDepthPolicy::VehicleReticleOnly));
            for(auto& image:fullColor){const auto white=WhitePixels(context.Get(),image.Get(),fullRead.Get(),
                    FullWidth/2-RoiSize/2,FullHeight/2-RoiSize/2,RoiSize);
                CHECK(white&&*white>0);}
        }
        CHECK(fullCompositor.Statistics().overlayOnlyPairs==16&&fullCompositor.Statistics().instances==32);
    }
    std::cout<<"BodyPropCompositor WARP: self-depth, expiry, both native layers, distinct eyes, atomic failures, depth-loss reticle and full-resolution subpixel coverage passed\n";
}
