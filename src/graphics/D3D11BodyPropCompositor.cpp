#include "fvr/graphics/D3D11BodyPropCompositor.h"
#include <Windows.h>
#include <d3d11.h>
#include <wrl/client.h>
#include <algorithm>
#include <cassert>
namespace fvr::graphics {
using Microsoft::WRL::ComPtr;
struct D3D11BodyPropCompositor::Impl {
    DWORD thread=GetCurrentThreadId();unsigned width=0,height=0,format=0;
    ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;
    std::array<ComPtr<ID3D11Texture2D>,2> color,depth;
    std::array<ComPtr<ID3D11RenderTargetView>,2> targets;
    std::array<ComPtr<ID3D11DepthStencilView>,2> depths;
    std::array<std::unique_ptr<D3D11RigidPropRenderer>,MaxBodyProps> renderers;
    std::array<RigidPropGeometryKey,MaxBodyProps> uploaded{};
    std::shared_ptr<const BodyPropCatalog> catalog;
    BodyPropComposeStats stats{};
};
D3D11BodyPropCompositor::D3D11BodyPropCompositor():impl_(std::make_unique<Impl>()){}
D3D11BodyPropCompositor::~D3D11BodyPropCompositor(){assert(impl_->thread==GetCurrentThreadId());}
const BodyPropComposeStats& D3D11BodyPropCompositor::Statistics()const noexcept{return impl_->stats;}
bool D3D11BodyPropCompositor::Initialize(ID3D11DeviceContext* context,unsigned width,unsigned height,unsigned format,
    std::shared_ptr<const BodyPropCatalog> catalog){
    auto& p=*impl_;if(p.thread!=GetCurrentThreadId()||p.context||!context||context->GetType()!=D3D11_DEVICE_CONTEXT_IMMEDIATE||
        !width||!height||width>16384||height>16384||!CopyCompatibleFormats(format,format)||!catalog||catalog->empty()||catalog->size()>16)return false;
    for(std::size_t n=0;n<catalog->size();++n){const auto& a=(*catalog)[n];if(!ValidRigidPropKey(a.key)||a.sections.empty())return false;
        for(std::size_t j=0;j<n;++j)if((*catalog)[j].key==a.key)return false;}
    context->GetDevice(&p.device);p.context=context;p.width=width;p.height=height;p.format=format;p.catalog=std::move(catalog);
    D3D11_TEXTURE2D_DESC d{};d.Width=width;d.Height=height;d.MipLevels=d.ArraySize=d.SampleDesc.Count=1;
    d.Usage=D3D11_USAGE_DEFAULT;d.Format=DXGI_FORMAT(format);d.BindFlags=D3D11_BIND_RENDER_TARGET;
    for(unsigned eye=0;eye<2;++eye){if(FAILED(p.device->CreateTexture2D(&d,nullptr,&p.color[eye]))||
        FAILED(p.device->CreateRenderTargetView(p.color[eye].Get(),nullptr,&p.targets[eye])))return false;}
    d.Format=DXGI_FORMAT_D32_FLOAT;d.BindFlags=D3D11_BIND_DEPTH_STENCIL;
    for(unsigned eye=0;eye<2;++eye){if(FAILED(p.device->CreateTexture2D(&d,nullptr,&p.depth[eye]))||
        FAILED(p.device->CreateDepthStencilView(p.depth[eye].Get(),nullptr,&p.depths[eye])))return false;}
    for(auto& renderer:p.renderers){renderer=std::make_unique<D3D11RigidPropRenderer>(true);if(!renderer->Initialize(context))return false;}
    return true;
}
bool D3D11BodyPropCompositor::Compose(const BodyPropFrame& frame,const PairTicket& ticket,
    const std::array<ID3D11Texture2D*,2>& originals,std::int64_t (*clockNs)()noexcept)noexcept {
    auto& p=*impl_;++p.stats.frames;
    if(p.thread!=GetCurrentThreadId()||!p.catalog||!p.context||!clockNs||!BodyPropFrameMatches(frame,ticket)){++p.stats.invalid;return false;}
    try {
        for(auto* original:originals){if(!original){++p.stats.invalid;return false;}D3D11_TEXTURE2D_DESC d{};original->GetDesc(&d);
            ComPtr<ID3D11Device> device;original->GetDevice(&device);
            if(device.Get()!=p.device.Get()||d.Width!=p.width||d.Height!=p.height||d.ArraySize!=1||d.MipLevels!=1||d.SampleDesc.Count!=1||
                !CopyCompatibleFormats(d.Format,p.format)){++p.stats.invalid;return false;}}
        std::array<bool,MaxBodyProps> admitted{};unsigned count=0;auto now=clockNs();
        // A slot is either present in BOTH eyes or absent in both. An equipment
        // transition between native eyes cannot create a one-eye body object.
        for(unsigned slot=0;slot<MaxBodyProps;++slot){
            if(slot>=frame.eyes[0].count||slot>=frame.eyes[1].count)continue;
            const auto& a=frame.eyes[0].instances[slot];const auto& b=frame.eyes[1].instances[slot];
            if(!SameBodyPropSource(a,b)){++p.stats.invalid;continue;}
            if(!BodyPropFresh(a,now)||!BodyPropFresh(b,now)){++p.stats.expired;continue;}
            const BodyPropAsset* asset=nullptr;for(const auto& candidate:*p.catalog)if(candidate.key==a.geometry){asset=&candidate;break;}
            if(!asset){++p.stats.missing;continue;}
            if(p.uploaded[slot]!=asset->key){if(!p.renderers[slot]->Upload(asset->key,asset->sections)){++p.stats.failures;return false;}p.uploaded[slot]=asset->key;}
            admitted[slot]=true;++count;
        }
        if(!count)return false;
        for(unsigned eye=0;eye<2;++eye){
            const auto& e=frame.eyes[eye];p.context->CopyResource(p.color[eye].Get(),originals[eye]);
            // Native canonical LH projection: positive m32 is reverse-Z,
            // negative m32 is ordinary near->0,far->1 perspective depth.
            const bool reversed=e.projection.values[3][2]>0;
            p.context->ClearDepthStencilView(p.depths[eye].Get(),D3D11_CLEAR_DEPTH,reversed?0.f:1.f,0);
            for(unsigned slot=0;slot<MaxBodyProps;++slot)if(admitted[slot]){
                const auto& instance=e.instances[slot];now=clockNs();if(!BodyPropFresh(instance,now)){++p.stats.expired;return false;}
                // Host-local frame gate IDs, not dereferenceable native objects.
                const RigidPropEye lease{1,frame.trackingGeneration,std::uint64_t(eye)+1,frame.frameId,
                    std::uint64_t(slot)+1,instance.actorGeneration,instance.geometry.asset,instance.equipmentGeneration,
                    frame.spaceGeneration,frame.trackingGeneration,eye,instance.observedNs,instance.deadlineNs};
                if(!p.renderers[slot]->Draw(instance.geometry,lease,lease,now,instance.world,e.view,e.projection,
                    {p.targets[eye].Get(),p.depths[eye].Get(),p.width,p.height,reversed,true})){++p.stats.failures;return false;}
            }
        }
        now=clockNs();for(unsigned eye=0;eye<2;++eye)for(unsigned slot=0;slot<MaxBodyProps;++slot)
            if(admitted[slot]&&!BodyPropFresh(frame.eyes[eye].instances[slot],now)){++p.stats.expired;return false;}
        for(unsigned eye=0;eye<2;++eye)p.context->CopyResource(originals[eye],p.color[eye].Get());
        ++p.stats.pairs;p.stats.instances+=count*2;return true;
    }catch(...){++p.stats.failures;return false;}
}
} // namespace fvr::graphics
