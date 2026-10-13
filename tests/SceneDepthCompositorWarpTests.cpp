// Explicit software renderer check; no game, headset or hardware GPU required.
#include "fvr/graphics/D3D11SceneDepthCompositor.h"
#include "Test.h"
#include <Windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <array>
#include <cstring>
#include <future>
#include <iostream>
#include <limits>
#include <vector>
using namespace fvr::graphics;using Microsoft::WRL::ComPtr;
namespace {
struct Color {ComPtr<ID3D11Texture2D> texture;ComPtr<ID3D11RenderTargetView> target;ComPtr<ID3D11ShaderResourceView> view;};
struct Depth {ComPtr<ID3D11Texture2D> texture;ComPtr<ID3D11DepthStencilView> target;ComPtr<ID3D11ShaderResourceView> view;};
bool MakeColor(ID3D11Device* d,Color& out,unsigned size=32,DXGI_FORMAT format=DXGI_FORMAT_R8G8B8A8_UNORM){
    D3D11_TEXTURE2D_DESC desc{};desc.Width=desc.Height=size;desc.ArraySize=desc.MipLevels=desc.SampleDesc.Count=1;
    desc.Format=format;desc.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
    return SUCCEEDED(d->CreateTexture2D(&desc,nullptr,&out.texture))&&SUCCEEDED(d->CreateRenderTargetView(out.texture.Get(),nullptr,&out.target))&&SUCCEEDED(d->CreateShaderResourceView(out.texture.Get(),nullptr,&out.view));
}
bool MakeDepth(ID3D11Device* d,unsigned samples,Depth& out,bool packed=false){
    D3D11_TEXTURE2D_DESC desc{};desc.Width=desc.Height=32;desc.ArraySize=desc.MipLevels=1;desc.SampleDesc.Count=samples;
    desc.Format=packed?DXGI_FORMAT_R24G8_TYPELESS:DXGI_FORMAT_R32_TYPELESS;desc.BindFlags=D3D11_BIND_DEPTH_STENCIL|D3D11_BIND_SHADER_RESOURCE;
    if(FAILED(d->CreateTexture2D(&desc,nullptr,&out.texture)))return false;
    D3D11_DEPTH_STENCIL_VIEW_DESC ds{};ds.Format=packed?DXGI_FORMAT_D24_UNORM_S8_UINT:DXGI_FORMAT_D32_FLOAT;ds.ViewDimension=samples==1?D3D11_DSV_DIMENSION_TEXTURE2D:D3D11_DSV_DIMENSION_TEXTURE2DMS;
    D3D11_SHADER_RESOURCE_VIEW_DESC sr{};sr.Format=packed?DXGI_FORMAT_R24_UNORM_X8_TYPELESS:DXGI_FORMAT_R32_FLOAT;sr.ViewDimension=samples==1?D3D11_SRV_DIMENSION_TEXTURE2D:D3D11_SRV_DIMENSION_TEXTURE2DMS;if(samples==1)sr.Texture2D.MipLevels=1;
    return SUCCEEDED(d->CreateDepthStencilView(out.texture.Get(),&ds,&out.target))&&SUCCEEDED(d->CreateShaderResourceView(out.texture.Get(),&sr,&out.view));
}
std::array<unsigned char,4> Pixel(ID3D11Device* d,ID3D11DeviceContext* c,const Color& target){
    D3D11_TEXTURE2D_DESC desc{};target.texture->GetDesc(&desc);desc.Usage=D3D11_USAGE_STAGING;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;desc.BindFlags=0;
    ComPtr<ID3D11Texture2D> copy;if(FAILED(d->CreateTexture2D(&desc,nullptr,&copy)))return {};c->CopyResource(copy.Get(),target.texture.Get());
    D3D11_MAPPED_SUBRESOURCE map{};if(FAILED(c->Map(copy.Get(),0,D3D11_MAP_READ,0,&map)))return {};
    const auto* bytes=static_cast<unsigned char*>(map.pData)+16*map.RowPitch+16*4;std::array<unsigned char,4> value{bytes[0],bytes[1],bytes[2],bytes[3]};c->Unmap(copy.Get(),0);return value;
}
bool IsCoverage(const std::array<unsigned char,4>& p,unsigned visible,unsigned samples){
    const float red=255.f*visible/samples;return std::abs(float(p[0])-red)<=1&&p[1]==0&&std::abs(float(p[2])-(255.f-red))<=1&&p[3]==255;
}
struct Seeder {
    ComPtr<ID3D11VertexShader> vs;ComPtr<ID3D11PixelShader> ps;ComPtr<ID3D11Buffer> cb;
    ComPtr<ID3D11DepthStencilState> state;ComPtr<ID3D11RasterizerState> raster;
    bool Init(ID3D11Device* d){
        constexpr char code[]=R"(
        cbuffer Seed:register(b0){uint mask;uint3 unused;};
        float4 vs(uint n:SV_VertexID):SV_Position{float2 p[3]={float2(-1,-1),float2(-1,3),float2(3,-1)};return float4(p[n],0,1);}
        float ps(float4 p:SV_Position,uint si:SV_SampleIndex):SV_Depth{return (mask&(1u<<si))?.25:.75;})";
        ComPtr<ID3DBlob> v,p,error;if(FAILED(D3DCompile(code,sizeof(code)-1,nullptr,nullptr,nullptr,"vs","vs_5_0",0,0,&v,&error))||FAILED(D3DCompile(code,sizeof(code)-1,nullptr,nullptr,nullptr,"ps","ps_5_0",0,0,&p,&error)))return false;
        if(FAILED(d->CreateVertexShader(v->GetBufferPointer(),v->GetBufferSize(),nullptr,&vs))||FAILED(d->CreatePixelShader(p->GetBufferPointer(),p->GetBufferSize(),nullptr,&ps)))return false;
        D3D11_BUFFER_DESC b{};b.ByteWidth=16;b.Usage=D3D11_USAGE_DEFAULT;b.BindFlags=D3D11_BIND_CONSTANT_BUFFER;if(FAILED(d->CreateBuffer(&b,nullptr,&cb)))return false;
        D3D11_DEPTH_STENCIL_DESC s{};s.DepthEnable=TRUE;s.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ALL;s.DepthFunc=D3D11_COMPARISON_ALWAYS;if(FAILED(d->CreateDepthStencilState(&s,&state)))return false;
        D3D11_RASTERIZER_DESC rs{};rs.FillMode=D3D11_FILL_SOLID;rs.CullMode=D3D11_CULL_NONE;rs.DepthClipEnable=rs.MultisampleEnable=TRUE;return SUCCEEDED(d->CreateRasterizerState(&rs,&raster));
    }
    void Draw(ID3D11DeviceContext* c,const Depth& d,unsigned mask){
        c->ClearState();const std::array<unsigned,4> value{mask,0,0,0};c->UpdateSubresource(cb.Get(),0,nullptr,value.data(),0,0);
        auto* b=cb.Get();c->PSSetConstantBuffers(0,1,&b);c->VSSetShader(vs.Get(),nullptr,0);c->PSSetShader(ps.Get(),nullptr,0);
        c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);c->OMSetRenderTargets(0,nullptr,d.target.Get());c->OMSetDepthStencilState(state.Get(),0);
        c->RSSetState(raster.Get());const D3D11_VIEWPORT vp{0,0,32,32,0,1};c->RSSetViewports(1,&vp);c->Draw(3,0);c->ClearState();
    }
};
}
int main(){
    ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;D3D_FEATURE_LEVEL level{};const auto wanted=D3D_FEATURE_LEVEL_11_0;
    CHECK(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,&wanted,1,D3D11_SDK_VERSION,&device,&level,&context)));
    D3D11SceneDepthCompositor compositor;CHECK(!compositor.Initialize(nullptr));CHECK(compositor.Initialize(context.Get()));CHECK(!compositor.Initialize(context.Get()));
    Color original,props,output,wrongSize;CHECK(MakeColor(device.Get(),original)&&MakeColor(device.Get(),props)&&MakeColor(device.Get(),output)&&MakeColor(device.Get(),wrongSize,16));
    const float blue[]{0,0,1,1},red[]{1,0,0,1},green[]{0,1,0,1};context->ClearRenderTargetView(original.target.Get(),blue);context->ClearRenderTargetView(props.target.Get(),red);
    Depth prop;CHECK(MakeDepth(device.Get(),1,prop));context->ClearDepthStencilView(prop.target.Get(),D3D11_CLEAR_DEPTH,.5f,0);
    Seeder seeder;CHECK(seeder.Init(device.Get()));unsigned cases=0;
    for(bool packed:{false,true})for(unsigned samples:{1u,2u,4u,8u}){Depth world,hand;CHECK(MakeDepth(device.Get(),samples,world,packed)&&MakeDepth(device.Get(),samples,hand,packed));
        const auto all=(1u<<samples)-1;
        for(bool reverse:{false,true})for(const auto masks:{std::array<unsigned,2>{0,0},{all,0},{0,all},{all,all},{all&0x55,all&0xaa},{all&0x55,all&0x55}}){
            seeder.Draw(context.Get(),world,masks[0]);seeder.Draw(context.Get(),hand,masks[1]);
            auto* prior=original.target.Get();auto* conflict=output.view.Get();context->OMSetRenderTargets(1,&prior,nullptr);context->PSSetShaderResources(7,1,&conflict);
            const D3D11_VIEWPORT vp{2,3,17,19,.1f,.9f};context->RSSetViewports(1,&vp);context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINELIST);
            SceneDepthComposition request{original.view.Get(),props.view.Get(),prop.view.Get(),world.view.Get(),hand.view.Get(),output.target.Get(),reverse};CHECK(compositor.Compose(request));
            unsigned visible=0;for(unsigned s=0;s<samples;++s)visible+=reverse?((masks[0]&(1u<<s))&&(masks[1]&(1u<<s))):(!(masks[0]&(1u<<s))&&!(masks[1]&(1u<<s)));
            CHECK(IsCoverage(Pixel(device.Get(),context.Get(),output),visible,samples));
            ComPtr<ID3D11RenderTargetView> rt;ComPtr<ID3D11ShaderResourceView> sr;context->OMGetRenderTargets(1,&rt,nullptr);context->PSGetShaderResources(7,1,&sr);CHECK(rt==original.target&&sr==output.view);
            D3D11_VIEWPORT restored{};UINT count=1;context->RSGetViewports(&count,&restored);CHECK(count==1&&std::memcmp(&vp,&restored,sizeof(vp))==0);
            D3D11_PRIMITIVE_TOPOLOGY topology{};context->IAGetPrimitiveTopology(&topology);CHECK(topology==D3D11_PRIMITIVE_TOPOLOGY_LINELIST);
            // Invalid resources must leave output pixels and caller state alone.
            context->ClearRenderTargetView(output.target.Get(),green);auto bad=request;bad.output=original.target.Get();CHECK(!compositor.Compose(bad));
            bad=request;bad.firstPersonDepth=nullptr;CHECK(!compositor.Compose(bad));bad=request;bad.originalColor=wrongSize.view.Get();CHECK(!compositor.Compose(bad));
            bad=request;bad.propDepth=props.view.Get();CHECK(!compositor.Compose(bad));
            if(samples>1){bad=request;bad.firstPersonDepth=prop.view.Get();CHECK(!compositor.Compose(bad));}
            CHECK(!std::async(std::launch::async,[&]{return compositor.Compose(request);}).get());
            CHECK((Pixel(device.Get(),context.Get(),output)==std::array<unsigned char,4>{0,255,0,255}));
            context->ClearState();++cases;
        }
    }
    // Blending decoded linear colors and writing through an sRGB RTV must encode
    // once, not average already encoded color bytes or double encode them.
    for(auto format:{DXGI_FORMAT_R8G8B8A8_UNORM,DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,DXGI_FORMAT_B8G8R8A8_UNORM,DXGI_FORMAT_B8G8R8A8_UNORM_SRGB}){
        Color a,b,out;Depth world,hand;CHECK(MakeColor(device.Get(),a,32,format)&&MakeColor(device.Get(),b,32,format)&&MakeColor(device.Get(),out,32,format));
        CHECK(MakeDepth(device.Get(),2,world)&&MakeDepth(device.Get(),2,hand));seeder.Draw(context.Get(),world,1);seeder.Draw(context.Get(),hand,0);
        context->ClearRenderTargetView(a.target.Get(),blue);context->ClearRenderTargetView(b.target.Get(),red);
        CHECK(compositor.Compose({a.view.Get(),b.view.Get(),prop.view.Get(),world.view.Get(),hand.view.Get(),out.target.Get()}));
        const auto color=Pixel(device.Get(),context.Get(),out);const int expected=(format==DXGI_FORMAT_R8G8B8A8_UNORM_SRGB||format==DXGI_FORMAT_B8G8R8A8_UNORM_SRGB)?188:128;
        CHECK(std::abs(int(color[0])-expected)<=1&&color[1]==0&&std::abs(int(color[2])-expected)<=1&&color[3]==255);
    }
    std::cout<<"SceneDepthCompositor WARP: "<<cases<<" D24/D32 world/hand, forward/reverse and sample-coverage cases; four color formats, state restoration and invalid-resource checks passed. Live transport not tested.\n";
}
