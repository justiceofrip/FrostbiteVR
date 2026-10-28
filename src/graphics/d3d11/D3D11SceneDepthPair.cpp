#include "fvr/graphics/D3D11SceneDepthPair.h"
#include "fvr/graphics/SceneDepthImportPolicy.h"
#include <Windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <wrl/client.h>
#include <cassert>
#include <limits>
namespace fvr::graphics {
using Microsoft::WRL::ComPtr;
namespace {
bool Adapter(ID3D11Device* device,std::uint32_t& low,std::int32_t& high){
    ComPtr<IDXGIDevice> dx;ComPtr<IDXGIAdapter> adapter;DXGI_ADAPTER_DESC desc{};
    if(FAILED(device->QueryInterface(IID_PPV_ARGS(&dx)))||FAILED(dx->GetAdapter(&adapter))||FAILED(adapter->GetDesc(&desc)))return false;
    low=desc.AdapterLuid.LowPart;high=desc.AdapterLuid.HighPart;return true;
}
bool Shape(const D3D11_TEXTURE2D_DESC& d){return d.Width&&d.Width<=16384&&d.Height&&d.Height<=16384&&d.Format==DXGI_FORMAT_R24G8_TYPELESS&&
    d.MipLevels==1&&d.ArraySize==1&&d.Usage==D3D11_USAGE_DEFAULT&&!d.CPUAccessFlags&&d.SampleDesc.Count&&d.SampleDesc.Count<=32&&!(d.SampleDesc.Count&(d.SampleDesc.Count-1));}
bool SameShape(const D3D11_TEXTURE2D_DESC& a,const D3D11_TEXTURE2D_DESC& b){return a.Width==b.Width&&a.Height==b.Height&&a.Format==b.Format&&a.SampleDesc.Count==b.SampleDesc.Count&&a.SampleDesc.Quality==b.SampleDesc.Quality;}
bool SameTicket(const PairTicket& a,const PairTicket& b){return std::memcmp(&a,&b,sizeof(a))==0;}

}
struct D3D11SceneDepthProducer::Impl {
    DWORD thread=GetCurrentThreadId();ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;
    std::array<ComPtr<ID3D11Texture2D>,4> images;D3D11_TEXTURE2D_DESC shape{};
    std::array<std::uint64_t,4> handles{};std::uint64_t generation=0,lastTracking=0,lastSpace=0;
    FrameMetadata frame{};SceneDepthFrame sealed{};unsigned count=0;bool capturing=false,pending=false,failed=false;
    void Retire(){for(auto& t:images)t.Reset();handles={};shape={};frame={};sealed={};count=0;lastTracking=lastSpace=0;capturing=pending=failed=false;}
};
D3D11SceneDepthProducer::D3D11SceneDepthProducer()=default;
D3D11SceneDepthProducer::~D3D11SceneDepthProducer(){Close();}
bool D3D11SceneDepthProducer::Initialize(ID3D11Device* device){
    if(impl_||!device)return false;auto next=std::make_unique<Impl>();next->device=device;device->GetImmediateContext(&next->context);impl_=std::move(next);return true;
}
bool D3D11SceneDepthProducer::Begin(const FrameMetadata& f){
    if(!impl_||impl_->thread!=GetCurrentThreadId()||impl_->capturing||impl_->pending||!f.frameId||!f.spaceGeneration||!f.trackingGeneration||f.predictedNs<=0||
       (f.spaceGeneration==impl_->lastSpace&&f.trackingGeneration<=impl_->lastTracking))return false;
    // Native frame serials can restart at a campaign load while the IPC channel
    // stays alive. New tracking requests, not native counters, order captures.
    impl_->frame=f;impl_->lastTracking=f.trackingGeneration;impl_->lastSpace=f.spaceGeneration;impl_->count=0;impl_->capturing=true;impl_->failed=false;return true;
}
bool D3D11SceneDepthProducer::Capture(unsigned eye,unsigned stage,ID3D11Texture2D* source){
    if(!impl_||impl_->thread!=GetCurrentThreadId())return false;auto& p=*impl_;
    if(!p.capturing||p.pending||p.failed)return false;
    const auto reject=[&]{p.failed=true;return false;};
    if(!source||eye>1||stage>1||eye*2+stage!=p.count)return reject();
    D3D11_TEXTURE2D_DESC d{};source->GetDesc(&d);ComPtr<ID3D11Device> owner;source->GetDevice(&owner);
    if(owner.Get()!=p.device.Get()||!Shape(d))return reject();
    if(p.count&&(!p.images[0]||!SameShape(d,p.shape)))return reject();
    if(!p.count&&(!p.images[0]||!SameShape(d,p.shape))){
        if(p.generation==UINT64_MAX)return reject();
        std::array<ComPtr<ID3D11Texture2D>,4> next;std::array<std::uint64_t,4> handles{};
        auto target=d;target.BindFlags=D3D11_BIND_SHADER_RESOURCE;target.MiscFlags=D3D11_RESOURCE_MISC_SHARED;
        for(unsigned i=0;i<4;++i){ComPtr<IDXGIResource> dx;HANDLE handle=nullptr;
            if(FAILED(p.device->CreateTexture2D(&target,nullptr,&next[i]))||FAILED(next[i].As(&dx))||FAILED(dx->GetSharedHandle(&handle))||!handle)return reject();
            handles[i]=reinterpret_cast<std::uintptr_t>(handle);}
        p.images=std::move(next);p.handles=handles;p.shape=d;++p.generation;
    }
    for(const auto& t:p.images)if(source==t.Get())return reject();
    p.context->CopyResource(p.images[p.count].Get(),source);++p.count;return true;
}
bool D3D11SceneDepthProducer::Seal(const TextureDescriptor& descriptor,const PairTicket& ticket,bool reversed,SceneDepthFrame& out){
    out={};if(!impl_||impl_->thread!=GetCurrentThreadId())return false;auto& p=*impl_;
    if(!p.capturing||p.pending||p.failed||p.count!=4||p.frame.frameId!=ticket.frameId||p.frame.spaceGeneration!=ticket.spaceGeneration||
       p.frame.trackingGeneration!=ticket.trackingGeneration||p.frame.predictedNs!=ticket.predictedNs)return false;
    SceneDepthFrame frame;frame.color=ticket;frame.flags=SceneDepthCanonicalClip|(reversed?SceneDepthReversed:0);
    frame.width=p.shape.Width;frame.height=p.shape.Height;frame.format=p.shape.Format;frame.samples=p.shape.SampleDesc.Count;frame.quality=p.shape.SampleDesc.Quality;
    if(!Adapter(p.device.Get(),frame.adapterLow,frame.adapterHigh))return false;
    frame.resourceGeneration=p.generation;for(unsigned i=0;i<4;++i)frame.resources[i/2][i%2]=p.handles[i];
    if(!SceneDepthMatches(frame,descriptor,ticket))return false;p.sealed=frame;p.capturing=false;p.pending=true;out=frame;return true;
}
void D3D11SceneDepthProducer::Acknowledge(const PairTicket& ticket,bool consumed){
    if(!impl_||impl_->thread!=GetCurrentThreadId()||!impl_->pending||!SameTicket(impl_->sealed.color,ticket))return;
    if(!consumed){impl_->Retire();return;}impl_->pending=false;impl_->sealed={};
}
void D3D11SceneDepthProducer::Cancel(){if(impl_&&impl_->thread==GetCurrentThreadId()&&!impl_->pending){impl_->capturing=false;impl_->failed=false;impl_->count=0;impl_->frame={};}}
void D3D11SceneDepthProducer::Retire(){if(impl_&&impl_->thread==GetCurrentThreadId())impl_->Retire();}
void D3D11SceneDepthProducer::Close(){assert(!impl_||impl_->thread==GetCurrentThreadId());impl_.reset();}
struct D3D11SceneDepthConsumer::Impl {
    DWORD thread=GetCurrentThreadId();ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;
    std::array<std::array<ComPtr<ID3D11Texture2D>,4>,SceneDepthImportPolicy::Capacity> sources;
    std::array<ComPtr<ID3D11Texture2D>,4> local;std::array<ComPtr<ID3D11ShaderResourceView>,4> views;
    SceneDepthImportPolicy imports;ImportStats stats;
    SceneDepthFrame copied{};bool haveCopy=false;
};
D3D11SceneDepthConsumer::D3D11SceneDepthConsumer()=default;
D3D11SceneDepthConsumer::~D3D11SceneDepthConsumer(){assert(!impl_||impl_->thread==GetCurrentThreadId());}
bool D3D11SceneDepthConsumer::Initialize(ID3D11Device* device){
    if(impl_||!device)return false;auto next=std::make_unique<Impl>();next->device=device;device->GetImmediateContext(&next->context);impl_=std::move(next);return true;
}
void D3D11SceneDepthConsumer::Reset(){if(!impl_||impl_->thread!=GetCurrentThreadId())return;auto& p=*impl_;
    for(auto& set:p.sources)for(auto& t:set)t.Reset();for(auto& t:p.local)t.Reset();for(auto& v:p.views)v.Reset();
    p.imports.Reset();p.copied={};p.haveCopy=false;}
D3D11SceneDepthConsumer::ImportStats D3D11SceneDepthConsumer::ImportStatistics()const noexcept {
    return impl_?impl_->stats:ImportStats{};
}
bool D3D11SceneDepthConsumer::CopyBeforeColor(const SceneDepthFrame& frame,const TextureDescriptor& descriptor,const PairTicket& ticket,
    unsigned sourceSlot,unsigned sourceSlots){
    if(!impl_||impl_->thread!=GetCurrentThreadId())return false;auto& p=*impl_;
    SceneDepthImportPolicy::Selection selected;
    if(!p.imports.Select(frame,descriptor,ticket,sourceSlot,sourceSlots,selected))return false;
    std::uint32_t low=0;std::int32_t high=0;if(!Adapter(p.device.Get(),low,high)||low!=frame.adapterLow||high!=frame.adapterHigh)return false;
    std::array<ComPtr<ID3D11Texture2D>,4> sources,local;std::array<ComPtr<ID3D11ShaderResourceView>,4> views;
    if(selected.openSources){
        for(unsigned i=0;i<4;++i){const auto id=frame.resources[i/2][i%2];if(id>std::numeric_limits<std::uintptr_t>::max())return false;
            if(FAILED(p.device->OpenSharedResource(reinterpret_cast<HANDLE>(std::uintptr_t(id)),IID_PPV_ARGS(&sources[i]))))return false;
            ++p.stats.sourceOpens;
            D3D11_TEXTURE2D_DESC d{};sources[i]->GetDesc(&d);
            if(!Shape(d)||d.Width!=frame.width||d.Height!=frame.height||d.SampleDesc.Count!=frame.samples||d.SampleDesc.Quality!=frame.quality||
               !(d.MiscFlags&D3D11_RESOURCE_MISC_SHARED)||(d.MiscFlags&(D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX|D3D11_RESOURCE_MISC_SHARED_NTHANDLE)))return false;
            for(unsigned j=0;j<i;++j)if(sources[i]==sources[j])return false;
        }
    }
    if(selected.createLocal){
        for(unsigned i=0;i<4;++i){D3D11_TEXTURE2D_DESC d{};
            auto* source=selected.openSources?sources[i].Get():p.sources[sourceSlot][i].Get();if(!source)return false;source->GetDesc(&d);
            d.MiscFlags=0;d.BindFlags=D3D11_BIND_SHADER_RESOURCE;if(FAILED(p.device->CreateTexture2D(&d,nullptr,&local[i])))return false;
            ++p.stats.localCreates;
            D3D11_SHADER_RESOURCE_VIEW_DESC view{};view.Format=DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
            view.ViewDimension=frame.samples==1?D3D11_SRV_DIMENSION_TEXTURE2D:D3D11_SRV_DIMENSION_TEXTURE2DMS;
            if(frame.samples==1)view.Texture2D.MipLevels=1;
            if(FAILED(p.device->CreateShaderResourceView(local[i].Get(),&view,&views[i])))return false;
        }
    }
    if(!p.imports.Commit(selected))return false;
    if(selected.resetImports)for(auto& set:p.sources)for(auto& t:set)t.Reset();
    if(selected.openSources)p.sources[sourceSlot]=std::move(sources);
    if(selected.createLocal){p.local=std::move(local);p.views=std::move(views);}
    for(unsigned i=0;i<4;++i)p.context->CopyResource(p.local[i].Get(),p.sources[sourceSlot][i].Get());
    p.copied=frame;p.haveCopy=true;return true;
}
bool D3D11SceneDepthConsumer::Views(const PairTicket& ticket,SceneDepthViews& out)const {
    out={};if(!impl_||impl_->thread!=GetCurrentThreadId()||!impl_->haveCopy||!SameTicket(impl_->copied.color,ticket))return false;
    for(unsigned i=0;i<4;++i)out.eyes[i/2][i%2]=impl_->views[i].Get();out.reversed=(impl_->copied.flags&SceneDepthReversed)!=0;return true;
}
}
