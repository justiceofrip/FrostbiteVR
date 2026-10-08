#include "Bc2OpticDrawState.h"
#include <d3d11.h>
namespace fvr::bc2 {namespace {
template<class T>std::uint64_t Id(T* p){return reinterpret_cast<std::uintptr_t>(p);}
OpticTextureIdentity Texture(ID3D11View* view){OpticTextureIdentity out;out.view=Id(view);if(!view)return out;
    ID3D11Resource* resource=nullptr;view->GetResource(&resource);if(!resource)return out;out.resource=Id(resource);
    D3D11_RESOURCE_DIMENSION dimension{};resource->GetType(&dimension);out.dimension=unsigned(dimension);
    if(dimension==D3D11_RESOURCE_DIMENSION_TEXTURE2D){ID3D11Texture2D* texture=nullptr;
        if(SUCCEEDED(resource->QueryInterface(__uuidof(ID3D11Texture2D),reinterpret_cast<void**>(&texture)))){
            D3D11_TEXTURE2D_DESC d{};texture->GetDesc(&d);out.width=d.Width;out.height=d.Height;out.format=d.Format;
            out.mips=d.MipLevels;out.arraySize=d.ArraySize;out.samples=d.SampleDesc.Count;texture->Release();}}
    resource->Release();return out;
}
}
OpticDrawState CaptureOpticDrawState(ID3D11DeviceContext* c)noexcept {OpticDrawState out;if(!c)return out;out.context=Id(c);
    ID3D11VertexShader* vs=nullptr;ID3D11PixelShader* ps=nullptr;c->VSGetShader(&vs,nullptr,nullptr);c->PSGetShader(&ps,nullptr,nullptr);
    out.vertexShader=Id(vs);out.pixelShader=Id(ps);if(vs)vs->Release();if(ps)ps->Release();
    ID3D11RenderTargetView* targets[4]{};ID3D11DepthStencilView* depth=nullptr;c->OMGetRenderTargets(4,targets,&depth);
    for(unsigned i=0;i<4;++i){out.targets[i]=Texture(targets[i]);if(targets[i])targets[i]->Release();}out.depth=Texture(depth);if(depth)depth->Release();
    ID3D11ShaderResourceView* resources[8]{};c->PSGetShaderResources(0,8,resources);
    for(unsigned i=0;i<8;++i){out.pixelResources[i]=Texture(resources[i]);if(resources[i])resources[i]->Release();}
    UINT viewCount=0,scissorCount=0;c->RSGetViewports(&viewCount,nullptr);c->RSGetScissorRects(&scissorCount,nullptr);
    out.viewportCount=viewCount;out.scissorCount=scissorCount;
    D3D11_VIEWPORT viewports[4]{};D3D11_RECT scissors[4]{};UINT vc=4,sc=4;c->RSGetViewports(&vc,viewports);c->RSGetScissorRects(&sc,scissors);
    for(unsigned i=0;i<vc&&i<4;++i){const auto& v=viewports[i];out.viewports[i]={v.TopLeftX,v.TopLeftY,v.Width,v.Height,v.MinDepth,v.MaxDepth};}
    for(unsigned i=0;i<sc&&i<4;++i){const auto& v=scissors[i];out.scissors[i]={v.left,v.top,v.right,v.bottom};}
    ID3D11BlendState* blend=nullptr;c->OMGetBlendState(&blend,out.blendFactor.data(),&out.sampleMask);out.blendState=Id(blend);if(blend)blend->Release();
    ID3D11DepthStencilState* state=nullptr;c->OMGetDepthStencilState(&state,&out.stencilReference);out.depthState=Id(state);if(state)state->Release();
    D3D11_PRIMITIVE_TOPOLOGY topology{};c->IAGetPrimitiveTopology(&topology);out.topology=topology;
    out.complete=viewCount<=4&&scissorCount<=4;return out;
}
}
