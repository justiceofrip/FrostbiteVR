#include "fvr/graphics/D3D11RigidPropRenderer.h"
#include <Windows.h>
#include <d3d11_1.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <algorithm>
#include <cassert>
#include <cstring>
#include <vector>
namespace fvr::graphics {
using Microsoft::WRL::ComPtr;
namespace {
constexpr char Shader[]=R"(
cbuffer Prop : register(b0) {row_major float4x4 clip; float4 tint;};
struct V {float3 position:POSITION;float3 normal:NORMAL;};
struct P {float4 position:SV_POSITION;float shade:TEXCOORD0;};
P vs(V v){P p;p.position=mul(float4(v.position,1),clip);p.shade=.55+.45*abs(v.normal.y);return p;}
float4 ps(P p):SV_TARGET{return float4(tint.rgb*p.shade,tint.a);}
)";
struct Constants {math::Matrix4 clip;std::array<float,4> color;};
static_assert(sizeof(Constants)==80&&sizeof(RigidPropVertex)==24);
bool Color(const std::array<float,4>& color){for(auto x:color)if(!std::isfinite(x)||x<0||x>1)return false;return color[3]==1;}
bool Mesh(const RigidPropMesh& mesh){
    if(!mesh.sourceVertexSkinHash||!mesh.sourcePositionHash||mesh.vertices.empty()||mesh.vertices.size()%3)return false;
    for(const auto& v:mesh.vertices){
        for(float x:{v.x,v.y,v.z})if(!std::isfinite(x)||std::abs(x)>10)return false;
        for(float x:{v.nx,v.ny,v.nz})if(!std::isfinite(x)||std::abs(x)>1.001f)return false;
    }return true;
}
struct RestoredState {
    ID3D11DeviceContext1* context;ComPtr<ID3DDeviceContextState> previous;
    RestoredState(ID3D11DeviceContext1* c,ID3DDeviceContextState* next):context(c){context->SwapDeviceContextState(next,&previous);}
    ~RestoredState(){
        // Drop our bindings to borrowed scene targets before parking the private
        // state, so it cannot retain an old world's RTV/DSV across checkpoints.
        context->ClearState();context->SwapDeviceContextState(previous.Get(),nullptr);
    }
};
bool TargetTexture(ID3D11View* view,ID3D11Device* device,unsigned w,unsigned h,DXGI_SAMPLE_DESC& samples){
    if(!view)return false;ComPtr<ID3D11Device> actual;view->GetDevice(&actual);if(actual.Get()!=device)return false;
    ComPtr<ID3D11Resource> resource;view->GetResource(&resource);ComPtr<ID3D11Texture2D> texture;
    if(FAILED(resource.As(&texture)))return false;D3D11_TEXTURE2D_DESC desc{};texture->GetDesc(&desc);
    if(desc.Width!=w||desc.Height!=h||desc.ArraySize!=1||desc.MipLevels!=1)return false;samples=desc.SampleDesc;return true;
}
}
struct D3D11RigidPropRenderer::Impl {
    struct Section {ComPtr<ID3D11Buffer> vertices;unsigned count=0;std::array<float,4> color;};
    DWORD thread=GetCurrentThreadId();RigidPropFrameGate gate{true};RigidPropGeometryKey key{};
    ComPtr<ID3D11Device> device;ComPtr<ID3D11Device1> device1;ComPtr<ID3D11DeviceContext1> context;
    ComPtr<ID3DDeviceContextState> privateState;ComPtr<ID3D11VertexShader> vs;ComPtr<ID3D11PixelShader> ps;
    ComPtr<ID3D11InputLayout> layout;ComPtr<ID3D11Buffer> constants;
    ComPtr<ID3D11RasterizerState> raster;std::array<ComPtr<ID3D11DepthStencilState>,4> depth;ComPtr<ID3D11BlendState> blend;
    std::vector<Section> sections;
    bool Thread()const noexcept{return thread==GetCurrentThreadId();}
};
D3D11RigidPropRenderer::D3D11RigidPropRenderer(bool enabled)noexcept:enabled_(enabled){}
D3D11RigidPropRenderer::~D3D11RigidPropRenderer(){assert(!impl_||impl_->Thread());}
bool D3D11RigidPropRenderer::Initialize(ID3D11DeviceContext* context){
    if(!enabled_||impl_||!context||context->GetType()!=D3D11_DEVICE_CONTEXT_IMMEDIATE)return false;
    auto p=std::make_unique<Impl>();context->GetDevice(&p->device);
    if(FAILED(p->device.As(&p->device1))||FAILED(context->QueryInterface(IID_PPV_ARGS(&p->context))))return false;
    const auto level=p->device->GetFeatureLevel();D3D_FEATURE_LEVEL chosen{};
    const UINT flags=(p->device->GetCreationFlags()&D3D11_CREATE_DEVICE_SINGLETHREADED)?D3D11_1_CREATE_DEVICE_CONTEXT_STATE_SINGLETHREADED:0;
    if(FAILED(p->device1->CreateDeviceContextState(flags,&level,1,D3D11_SDK_VERSION,__uuidof(ID3D11Device),&chosen,&p->privateState))||chosen!=level)return false;
    ComPtr<ID3DBlob> vs,ps,errors;
    if(FAILED(D3DCompile(Shader,sizeof(Shader)-1,nullptr,nullptr,nullptr,"vs","vs_4_0",D3DCOMPILE_ENABLE_STRICTNESS,0,&vs,&errors)))return false;
    errors.Reset();if(FAILED(D3DCompile(Shader,sizeof(Shader)-1,nullptr,nullptr,nullptr,"ps","ps_4_0",D3DCOMPILE_ENABLE_STRICTNESS,0,&ps,&errors)))return false;
    if(FAILED(p->device->CreateVertexShader(vs->GetBufferPointer(),vs->GetBufferSize(),nullptr,&p->vs))||
        FAILED(p->device->CreatePixelShader(ps->GetBufferPointer(),ps->GetBufferSize(),nullptr,&p->ps)))return false;
    const D3D11_INPUT_ELEMENT_DESC elements[]={{"POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0},
        {"NORMAL",0,DXGI_FORMAT_R32G32B32_FLOAT,0,12,D3D11_INPUT_PER_VERTEX_DATA,0}};
    if(FAILED(p->device->CreateInputLayout(elements,2,vs->GetBufferPointer(),vs->GetBufferSize(),&p->layout)))return false;
    D3D11_BUFFER_DESC cb{};cb.ByteWidth=sizeof(Constants);cb.BindFlags=D3D11_BIND_CONSTANT_BUFFER;cb.Usage=D3D11_USAGE_DYNAMIC;cb.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;
    if(FAILED(p->device->CreateBuffer(&cb,nullptr,&p->constants)))return false;
    D3D11_RASTERIZER_DESC rs{};rs.FillMode=D3D11_FILL_SOLID;rs.CullMode=D3D11_CULL_NONE;rs.DepthClipEnable=TRUE;rs.MultisampleEnable=TRUE;
    if(FAILED(p->device->CreateRasterizerState(&rs,&p->raster)))return false;
    D3D11_DEPTH_STENCIL_DESC ds{};ds.DepthEnable=TRUE;ds.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ZERO;ds.DepthFunc=D3D11_COMPARISON_LESS_EQUAL;
    if(FAILED(p->device->CreateDepthStencilState(&ds,&p->depth[0])))return false;
    ds.DepthFunc=D3D11_COMPARISON_GREATER_EQUAL;if(FAILED(p->device->CreateDepthStencilState(&ds,&p->depth[1])))return false;
    ds.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ALL;
    ds.DepthFunc=D3D11_COMPARISON_LESS_EQUAL;if(FAILED(p->device->CreateDepthStencilState(&ds,&p->depth[2])))return false;
    ds.DepthFunc=D3D11_COMPARISON_GREATER_EQUAL;if(FAILED(p->device->CreateDepthStencilState(&ds,&p->depth[3])))return false;
    D3D11_BLEND_DESC bs{};bs.RenderTarget[0].RenderTargetWriteMask=D3D11_COLOR_WRITE_ENABLE_ALL;
    if(FAILED(p->device->CreateBlendState(&bs,&p->blend)))return false;
    impl_=std::move(p);return true;
}
bool D3D11RigidPropRenderer::Upload(RigidPropGeometryKey key,std::span<const RigidPropSectionUpload> sections){
    if(!impl_||!impl_->Thread()||!ValidRigidPropKey(key)||sections.empty()||sections.size()>8)return false;
    std::size_t total=0;for(const auto& s:sections){total+=s.mesh.vertices.size();if(total>32768*3||!Color(s.color)||!Mesh(s.mesh))return false;}
    std::vector<Impl::Section> next;next.reserve(sections.size());
    for(const auto& s:sections){Impl::Section owned{};owned.count=static_cast<unsigned>(s.mesh.vertices.size());owned.color=s.color;
        D3D11_BUFFER_DESC desc{};desc.ByteWidth=owned.count*sizeof(RigidPropVertex);desc.Usage=D3D11_USAGE_IMMUTABLE;desc.BindFlags=D3D11_BIND_VERTEX_BUFFER;
        D3D11_SUBRESOURCE_DATA data{};data.pSysMem=s.mesh.vertices.data();
        if(FAILED(impl_->device->CreateBuffer(&desc,&data,&owned.vertices)))return false;next.push_back(std::move(owned));}
    impl_->sections=std::move(next);impl_->key=key;return true;
}
bool D3D11RigidPropRenderer::Draw(RigidPropGeometryKey key,const RigidPropEye& eye,const RigidPropEye& current,
    std::int64_t now,const math::Matrix4& world,const math::Matrix4& view,const math::Matrix4& projection,const RigidPropTarget& target){
    if(!impl_||!impl_->Thread()||key!=impl_->key||impl_->sections.empty()||!target.width||!target.height||
        target.width>16384||target.height>16384||!target.color||!target.depth)return false;
    const auto clip=RigidPropClipTransform(world,view,projection);if(!clip)return false;
    DXGI_SAMPLE_DESC color{},depth{};
    if(!TargetTexture(target.color,impl_->device.Get(),target.width,target.height,color)||
        !TargetTexture(target.depth,impl_->device.Get(),target.width,target.height,depth)||color.Count!=depth.Count||color.Quality!=depth.Quality)return false;
    D3D11_RENDER_TARGET_VIEW_DESC rd{};target.color->GetDesc(&rd);D3D11_DEPTH_STENCIL_VIEW_DESC dd{};target.depth->GetDesc(&dd);
    if((rd.ViewDimension!=D3D11_RTV_DIMENSION_TEXTURE2D&&rd.ViewDimension!=D3D11_RTV_DIMENSION_TEXTURE2DMS)||
        (dd.ViewDimension!=D3D11_DSV_DIMENSION_TEXTURE2D&&dd.ViewDimension!=D3D11_DSV_DIMENSION_TEXTURE2DMS)||
        !impl_->gate.Admit(eye,current,now))return false;
    auto* c=impl_->context.Get();RestoredState restore(c,impl_->privateState.Get());
    c->ClearState(); // This is only our swapped-in state, never the native state.
    c->IASetInputLayout(impl_->layout.Get());c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    c->VSSetShader(impl_->vs.Get(),nullptr,0);c->PSSetShader(impl_->ps.Get(),nullptr,0);
    auto* cb=impl_->constants.Get();c->VSSetConstantBuffers(0,1,&cb);c->PSSetConstantBuffers(0,1,&cb);
    c->RSSetState(impl_->raster.Get());const D3D11_VIEWPORT vp{0,0,float(target.width),float(target.height),0,1};c->RSSetViewports(1,&vp);
    c->OMSetRenderTargets(1,&target.color,target.depth);c->OMSetDepthStencilState(impl_->depth[(target.reversedDepth?1:0)+(target.writeOwnedDepth?2:0)].Get(),0);c->OMSetBlendState(impl_->blend.Get(),nullptr,0xffffffffu);
    for(const auto& s:impl_->sections){D3D11_MAPPED_SUBRESOURCE mapped{};
        if(FAILED(c->Map(cb,0,D3D11_MAP_WRITE_DISCARD,0,&mapped)))return false;
        const Constants data{*clip,s.color};std::memcpy(mapped.pData,&data,sizeof(data));c->Unmap(cb,0);
        auto* vb=s.vertices.Get();const UINT stride=sizeof(RigidPropVertex),offset=0;c->IASetVertexBuffers(0,1,&vb,&stride,&offset);c->Draw(s.count,0);
    }
    return true;
}
} // namespace fvr::graphics
