#include "fvr/graphics/D3D11StereoComfort.h"
#include <d3d11_1.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <cassert>
namespace fvr::graphics {
using Microsoft::WRL::ComPtr;
namespace {
constexpr char Shader[]=R"(
Texture2D<float4> first:register(t0),second:register(t1);
float4 vs(uint n:SV_VertexID):SV_Position {
    float2 p[3]={float2(-1,-1),float2(-1,3),float2(3,-1)};return float4(p[n],0,1);
}
float4 classify():SV_Target {
    uint width,height;first.GetDimensions(width,height);
    bool leftBlack=true,leftWhite=true,rightBlack=true,rightWhite=true;
    // Strict conjunction across all samples avoids changing ordinary stereo
    // with dark occluders, bright sky or only partially different imagery.
    [loop]for(uint y=0;y<16;++y)[loop]for(uint x=0;x<16;++x){
        int3 p=int3(min(uint2(width-1,height-1),(uint2(x,y)*2+1)*uint2(width,height)/32),0);
        float3 a=first.Load(p).rgb,b=second.Load(p).rgb;
        leftBlack=leftBlack&&all(a<=0.005);leftWhite=leftWhite&&all(a>=0.9);
        rightBlack=rightBlack&&all(b<=0.005);rightWhite=rightWhite&&all(b>=0.9);
    }
    return (leftBlack&&rightWhite)||(leftWhite&&rightBlack)?1:0;
}
float4 present(float4 p:SV_Position):SV_Target {
    // Exactly the same GPU decision for each eye, including the first frame.
    return second.Load(int3(0,0,0)).r>0.5?float4(0,0,0,1):first.Load(int3(int2(p.xy),0));
}
)";
bool Color(DXGI_FORMAT f){return f==DXGI_FORMAT_R8G8B8A8_UNORM||f==DXGI_FORMAT_R8G8B8A8_UNORM_SRGB||
    f==DXGI_FORMAT_B8G8R8A8_UNORM||f==DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;}
bool Shape(const D3D11_TEXTURE2D_DESC& d){return d.Width&&d.Height&&d.Width<=16384&&d.Height<=16384&&
    d.ArraySize==1&&d.MipLevels==1&&d.SampleDesc.Count==1&&!d.SampleDesc.Quality;}
struct StateScope {
    ID3D11DeviceContext1* context;ComPtr<ID3DDeviceContextState> previous;
    StateScope(ID3D11DeviceContext1* c,ID3DDeviceContextState* s):context(c){c->SwapDeviceContextState(s,&previous);c->ClearState();}
    ~StateScope(){context->ClearState();context->SwapDeviceContextState(previous.Get(),nullptr);}
};
}
struct D3D11StereoComfort::Impl {
    DWORD thread=GetCurrentThreadId();ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext1> context;
    ComPtr<ID3DDeviceContextState> state;ComPtr<ID3D11VertexShader> vs;ComPtr<ID3D11PixelShader> classify,present;
    ComPtr<ID3D11RasterizerState> raster;ComPtr<ID3D11Texture2D> flag;
    ComPtr<ID3D11RenderTargetView> flagTarget;ComPtr<ID3D11ShaderResourceView> flagView;
    std::array<ComPtr<ID3D11Texture2D>,2> sources;
    std::array<ComPtr<ID3D11ShaderResourceView>,2> views;
    D3D11_TEXTURE2D_DESC shape{};bool prepared=false;
    void Bind(ID3D11PixelShader* ps,unsigned width,unsigned height){
        context->VSSetShader(vs.Get(),nullptr,0);context->PSSetShader(ps,nullptr,0);
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);context->RSSetState(raster.Get());
        const D3D11_VIEWPORT vp{0,0,float(width),float(height),0,1};context->RSSetViewports(1,&vp);
    }
};
D3D11StereoComfort::D3D11StereoComfort()=default;
D3D11StereoComfort::~D3D11StereoComfort(){assert(!impl_||impl_->thread==GetCurrentThreadId());}
bool D3D11StereoComfort::Initialize(ID3D11DeviceContext* c){
    if(impl_||!c||c->GetType()!=D3D11_DEVICE_CONTEXT_IMMEDIATE)return false;
    auto p=std::make_unique<Impl>();c->GetDevice(&p->device);ComPtr<ID3D11Device1> device;
    if(p->device->GetFeatureLevel()<D3D_FEATURE_LEVEL_11_0||FAILED(p->device.As(&device))||FAILED(c->QueryInterface(IID_PPV_ARGS(&p->context))))return false;
    const auto level=p->device->GetFeatureLevel();D3D_FEATURE_LEVEL chosen{};
    const UINT flags=(p->device->GetCreationFlags()&D3D11_CREATE_DEVICE_SINGLETHREADED)?D3D11_1_CREATE_DEVICE_CONTEXT_STATE_SINGLETHREADED:0;
    if(FAILED(device->CreateDeviceContextState(flags,&level,1,D3D11_SDK_VERSION,__uuidof(ID3D11Device),&chosen,&p->state))||chosen!=level)return false;
    ComPtr<ID3DBlob> code,error;
    if(FAILED(D3DCompile(Shader,sizeof(Shader)-1,nullptr,nullptr,nullptr,"vs","vs_5_0",D3DCOMPILE_ENABLE_STRICTNESS,0,&code,&error))||
       FAILED(p->device->CreateVertexShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,&p->vs)))return false;
    for(unsigned i=0;i<2;++i){code.Reset();error.Reset();
        if(FAILED(D3DCompile(Shader,sizeof(Shader)-1,nullptr,nullptr,nullptr,i?"present":"classify","ps_5_0",D3DCOMPILE_ENABLE_STRICTNESS,0,&code,&error))||
           FAILED(p->device->CreatePixelShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,i?&p->present:&p->classify)))return false;
    }
    D3D11_RASTERIZER_DESC rs{};rs.FillMode=D3D11_FILL_SOLID;rs.CullMode=D3D11_CULL_NONE;rs.DepthClipEnable=TRUE;
    if(FAILED(p->device->CreateRasterizerState(&rs,&p->raster)))return false;
    D3D11_TEXTURE2D_DESC d{};d.Width=d.Height=d.MipLevels=d.ArraySize=d.SampleDesc.Count=1;d.Format=DXGI_FORMAT_R8_UNORM;
    d.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
    if(FAILED(p->device->CreateTexture2D(&d,nullptr,&p->flag))||FAILED(p->device->CreateRenderTargetView(p->flag.Get(),nullptr,&p->flagTarget))||
       FAILED(p->device->CreateShaderResourceView(p->flag.Get(),nullptr,&p->flagView)))return false;
    impl_=std::move(p);return true;
}
bool D3D11StereoComfort::Prepare(const std::array<TextureSlice,2>& input){
    if(!impl_||impl_->thread!=GetCurrentThreadId())return false;
    auto& p=*impl_;p.prepared=false;D3D11_TEXTURE2D_DESC base{};
    for(unsigned eye=0;eye<2;++eye){const auto& s=input[eye];if(!s.texture||s.subresource)return false;
        ComPtr<ID3D11Device> device;s.texture->GetDevice(&device);D3D11_TEXTURE2D_DESC d{};s.texture->GetDesc(&d);
        if(device!=p.device||!Shape(d)||!Color(d.Format)||!(d.BindFlags&D3D11_BIND_SHADER_RESOURCE))return false;
        if(!eye)base=d;else if(d.Width!=base.Width||d.Height!=base.Height||d.Format!=base.Format||s.texture==input[0].texture)return false;
    }
    for(unsigned eye=0;eye<2;++eye)if(p.sources[eye].Get()!=input[eye].texture){
        p.sources[eye]=input[eye].texture;p.views[eye].Reset();
        if(FAILED(p.device->CreateShaderResourceView(input[eye].texture,nullptr,&p.views[eye])))return false;
    }
    // A failed view creation cannot become valid just because the pointer repeats.
    if(!p.views[0]||!p.views[1])return false;
    StateScope restore(p.context.Get(),p.state.Get());p.Bind(p.classify.Get(),1,1);
    ID3D11ShaderResourceView* views[]{p.views[0].Get(),p.views[1].Get()};p.context->PSSetShaderResources(0,2,views);
    auto* target=p.flagTarget.Get();p.context->OMSetRenderTargets(1,&target,nullptr);p.context->Draw(3,0);
    p.shape=base;p.prepared=true;return true;
}
bool D3D11StereoComfort::Present(const std::array<ID3D11RenderTargetView*,2>& output){
    if(!impl_||impl_->thread!=GetCurrentThreadId())return false;
    auto& p=*impl_;const bool prepared=p.prepared;p.prepared=false;if(!prepared)return false;
    std::array<ComPtr<ID3D11Resource>,2> resources;
    for(unsigned eye=0;eye<2;++eye){auto* target=output[eye];if(!target)return false;
        ComPtr<ID3D11Device> device;target->GetDevice(&device);if(device!=p.device)return false;
        target->GetResource(&resources[eye]);for(const auto& source:p.sources)if(resources[eye].Get()==source.Get())return false;
        ComPtr<ID3D11Texture2D> texture;if(FAILED(resources[eye].As(&texture)))return false;
        D3D11_TEXTURE2D_DESC d{};texture->GetDesc(&d);D3D11_RENDER_TARGET_VIEW_DESC v{};target->GetDesc(&v);
        if(!Shape(d)||d.Width!=p.shape.Width||d.Height!=p.shape.Height||!CopyCompatibleFormats(d.Format,p.shape.Format)||v.Format!=p.shape.Format||
           v.ViewDimension!=D3D11_RTV_DIMENSION_TEXTURE2D||v.Texture2D.MipSlice)return false;
    }
    if(resources[0]==resources[1])return false;
    StateScope restore(p.context.Get(),p.state.Get());p.Bind(p.present.Get(),p.shape.Width,p.shape.Height);
    for(unsigned eye=0;eye<2;++eye){ID3D11ShaderResourceView* views[]{p.views[eye].Get(),p.flagView.Get()};
        p.context->PSSetShaderResources(0,2,views);p.context->OMSetRenderTargets(1,&output[eye],nullptr);p.context->Draw(3,0);}
    return true;
}
}
