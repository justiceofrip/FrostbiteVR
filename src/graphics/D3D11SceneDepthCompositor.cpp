#include "fvr/graphics/D3D11SceneDepthCompositor.h"
#include <Windows.h>
#include <d3d11_1.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <array>
#include <cassert>
#include <cstring>
namespace fvr::graphics {
using Microsoft::WRL::ComPtr;
namespace {
constexpr char Shader[]=R"(
cbuffer Config:register(b0){uint reversedDepth;uint samples;uint2 padding;};
Texture2D<float4> original:register(t0),prop:register(t1);
Texture2D<float> propDepth:register(t2);
#ifdef MULTISAMPLE
Texture2DMS<float> worldDepth:register(t3),firstPersonDepth:register(t4);
#else
Texture2D<float> worldDepth:register(t3),firstPersonDepth:register(t4);
#endif
float4 vs(uint n:SV_VertexID):SV_Position {
    float2 positions[3]={float2(-1,-1),float2(-1,3),float2(3,-1)};
    return float4(positions[n],0,1);
}
float4 ps(float4 position:SV_Position):SV_Target {
    int2 xy=int2(position.xy);float4 background=original.Load(int3(xy,0));
    float4 foreground=prop.Load(int3(xy,0));if(all(foreground==background))return background;
    float p=propDepth.Load(int3(xy,0));if(!isfinite(p)||p<0||p>1)return background;
    uint covered=0;
    for(uint si=0;si<samples;++si){
#ifdef MULTISAMPLE
        float w=worldDepth.Load(xy,si),h=firstPersonDepth.Load(xy,si);
#else
        float w=worldDepth.Load(int3(xy,0)),h=firstPersonDepth.Load(int3(xy,0));
#endif
        bool valid=isfinite(w)&&isfinite(h)&&w>=0&&w<=1&&h>=0&&h<=1;
        covered+=valid&&(reversedDepth?(p>=w&&p>=h):(p<=w&&p<=h));
    }
    return lerp(background,foreground,float(covered)/samples);
}
)";
struct ViewInfo {ComPtr<ID3D11Resource> resource;D3D11_TEXTURE2D_DESC texture{};D3D11_SHADER_RESOURCE_VIEW_DESC view{};};
bool Input(ID3D11ShaderResourceView* view,ID3D11Device* device,ViewInfo& out){
    if(!view)return false;ComPtr<ID3D11Device> owner;view->GetDevice(&owner);if(owner.Get()!=device)return false;
    view->GetDesc(&out.view);view->GetResource(&out.resource);ComPtr<ID3D11Texture2D> texture;
    if(FAILED(out.resource.As(&texture)))return false;texture->GetDesc(&out.texture);
    const auto& d=out.texture;const auto& v=out.view;
    if(!d.Width||!d.Height||d.Width>16384||d.Height>16384||d.ArraySize!=1||d.MipLevels!=1)return false;
    if(d.SampleDesc.Count==1)return v.ViewDimension==D3D11_SRV_DIMENSION_TEXTURE2D&&v.Texture2D.MostDetailedMip==0&&v.Texture2D.MipLevels==1;
    return v.ViewDimension==D3D11_SRV_DIMENSION_TEXTURE2DMS;
}
bool Color(DXGI_FORMAT f){return f==DXGI_FORMAT_R8G8B8A8_UNORM||f==DXGI_FORMAT_R8G8B8A8_UNORM_SRGB||f==DXGI_FORMAT_B8G8R8A8_UNORM||f==DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;}
bool Depth(DXGI_FORMAT f){return f==DXGI_FORMAT_R24_UNORM_X8_TYPELESS||f==DXGI_FORMAT_R32_FLOAT;}
struct StateScope {
    ID3D11DeviceContext1* context;ComPtr<ID3DDeviceContextState> previous;
    StateScope(ID3D11DeviceContext1* c,ID3DDeviceContextState* state):context(c){context->SwapDeviceContextState(state,&previous);}
    ~StateScope(){context->ClearState();context->SwapDeviceContextState(previous.Get(),nullptr);}
};
}
struct D3D11SceneDepthCompositor::Impl {
    DWORD thread=GetCurrentThreadId();ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext1> context;ComPtr<ID3DDeviceContextState> state;
    ComPtr<ID3D11VertexShader> vs;std::array<ComPtr<ID3D11PixelShader>,2> ps;
    ComPtr<ID3D11Buffer> constants;ComPtr<ID3D11RasterizerState> raster;
};
D3D11SceneDepthCompositor::D3D11SceneDepthCompositor()=default;
D3D11SceneDepthCompositor::~D3D11SceneDepthCompositor(){assert(!impl_||impl_->thread==GetCurrentThreadId());}
bool D3D11SceneDepthCompositor::Initialize(ID3D11DeviceContext* c){
    if(impl_||!c||c->GetType()!=D3D11_DEVICE_CONTEXT_IMMEDIATE)return false;
    auto p=std::make_unique<Impl>();c->GetDevice(&p->device);ComPtr<ID3D11Device1> device;
    if(p->device->GetFeatureLevel()<D3D_FEATURE_LEVEL_11_0||FAILED(p->device.As(&device))||FAILED(c->QueryInterface(IID_PPV_ARGS(&p->context))))return false;
    const auto level=p->device->GetFeatureLevel();D3D_FEATURE_LEVEL chosen{};
    const UINT flags=(p->device->GetCreationFlags()&D3D11_CREATE_DEVICE_SINGLETHREADED)?D3D11_1_CREATE_DEVICE_CONTEXT_STATE_SINGLETHREADED:0;
    if(FAILED(device->CreateDeviceContextState(flags,&level,1,D3D11_SDK_VERSION,__uuidof(ID3D11Device),&chosen,&p->state))||chosen!=level)return false;
    ComPtr<ID3DBlob> vs,error;
    if(FAILED(D3DCompile(Shader,sizeof(Shader)-1,nullptr,nullptr,nullptr,"vs","vs_5_0",D3DCOMPILE_ENABLE_STRICTNESS,0,&vs,&error))||
       FAILED(p->device->CreateVertexShader(vs->GetBufferPointer(),vs->GetBufferSize(),nullptr,&p->vs)))return false;
    const D3D_SHADER_MACRO macros[]={{"MULTISAMPLE","1"},{nullptr,nullptr}};
    for(unsigned n=0;n<2;++n){ComPtr<ID3DBlob> ps;error.Reset();
        if(FAILED(D3DCompile(Shader,sizeof(Shader)-1,nullptr,n?macros:nullptr,nullptr,"ps","ps_5_0",D3DCOMPILE_ENABLE_STRICTNESS,0,&ps,&error))||
           FAILED(p->device->CreatePixelShader(ps->GetBufferPointer(),ps->GetBufferSize(),nullptr,&p->ps[n])))return false;}
    D3D11_BUFFER_DESC cb{};cb.ByteWidth=16;cb.Usage=D3D11_USAGE_DYNAMIC;cb.BindFlags=D3D11_BIND_CONSTANT_BUFFER;cb.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;
    if(FAILED(p->device->CreateBuffer(&cb,nullptr,&p->constants)))return false;
    D3D11_RASTERIZER_DESC rs{};rs.FillMode=D3D11_FILL_SOLID;rs.CullMode=D3D11_CULL_NONE;rs.DepthClipEnable=TRUE;
    if(FAILED(p->device->CreateRasterizerState(&rs,&p->raster)))return false;
    impl_=std::move(p);return true;
}
bool D3D11SceneDepthCompositor::Compose(const SceneDepthComposition& input){
    if(!impl_||impl_->thread!=GetCurrentThreadId()||!input.output)return false;
    auto& p=*impl_;ID3D11ShaderResourceView* views[]={input.originalColor,input.propColor,input.propDepth,input.worldDepth,input.firstPersonDepth};
    std::array<ViewInfo,5> info{};for(unsigned i=0;i<5;++i)if(!Input(views[i],p.device.Get(),info[i]))return false;
    const auto& base=info[0].texture;const auto format=info[0].view.Format;const auto samples=info[3].texture.SampleDesc;
    if(!Color(format)||info[1].view.Format!=format||info[2].view.Format!=DXGI_FORMAT_R32_FLOAT||
       !Depth(info[3].view.Format)||!Depth(info[4].view.Format)||
       !samples.Count||samples.Count>32||(samples.Count&(samples.Count-1)))return false;
    for(unsigned i=0;i<5;++i){const auto& d=info[i].texture;
        if(d.Width!=base.Width||d.Height!=base.Height)return false;
        if(i<3){if(d.SampleDesc.Count!=1)return false;}
        else if(d.SampleDesc.Count!=samples.Count||d.SampleDesc.Quality!=samples.Quality)return false;}
    ComPtr<ID3D11Device> owner;input.output->GetDevice(&owner);if(owner.Get()!=p.device.Get())return false;
    ComPtr<ID3D11Resource> output;input.output->GetResource(&output);
    for(const auto& i:info)if(i.resource==output)return false;
    ComPtr<ID3D11Texture2D> texture;if(FAILED(output.As(&texture)))return false;D3D11_TEXTURE2D_DESC d{};texture->GetDesc(&d);
    D3D11_RENDER_TARGET_VIEW_DESC rv{};input.output->GetDesc(&rv);
    if(d.Width!=base.Width||d.Height!=base.Height||d.ArraySize!=1||d.MipLevels!=1||d.SampleDesc.Count!=1||
       rv.ViewDimension!=D3D11_RTV_DIMENSION_TEXTURE2D||rv.Texture2D.MipSlice||rv.Format!=format)return false;
    StateScope restore(p.context.Get(),p.state.Get());auto* c=p.context.Get();c->ClearState();
    D3D11_MAPPED_SUBRESOURCE map{};if(FAILED(c->Map(p.constants.Get(),0,D3D11_MAP_WRITE_DISCARD,0,&map)))return false;
    const std::array<unsigned,4> values{input.reversedDepth?1u:0u,samples.Count,0,0};std::memcpy(map.pData,values.data(),16);c->Unmap(p.constants.Get(),0);
    auto* cb=p.constants.Get();c->PSSetConstantBuffers(0,1,&cb);c->PSSetShaderResources(0,5,views);
    c->VSSetShader(p.vs.Get(),nullptr,0);c->PSSetShader(p.ps[samples.Count>1?1:0].Get(),nullptr,0);
    c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);c->RSSetState(p.raster.Get());
    const D3D11_VIEWPORT vp{0,0,float(base.Width),float(base.Height),0,1};c->RSSetViewports(1,&vp);
    c->OMSetRenderTargets(1,&input.output,nullptr);c->Draw(3,0);return true;
}
}
