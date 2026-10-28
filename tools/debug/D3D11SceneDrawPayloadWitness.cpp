#include "D3D11SceneDrawPayloadWitness.h"
#include <Windows.h>
#include <d3d11.h>
#include <wrl/client.h>
#include <atomic>
#include <cstring>

namespace fvr::debug {
using Microsoft::WRL::ComPtr;
namespace {
std::uint64_t Id(const void* p)noexcept{return reinterpret_cast<std::uintptr_t>(p);}
unsigned ElementBytes(DXGI_FORMAT f)noexcept {
    switch(f){
    case DXGI_FORMAT_R32_FLOAT:case DXGI_FORMAT_R32_UINT:case DXGI_FORMAT_R32_SINT:return 4;
    case DXGI_FORMAT_R32G32_FLOAT:case DXGI_FORMAT_R32G32_UINT:case DXGI_FORMAT_R32G32_SINT:return 8;
    case DXGI_FORMAT_R32G32B32_FLOAT:case DXGI_FORMAT_R32G32B32_UINT:case DXGI_FORMAT_R32G32B32_SINT:return 12;
    case DXGI_FORMAT_R32G32B32A32_FLOAT:case DXGI_FORMAT_R32G32B32A32_UINT:case DXGI_FORMAT_R32G32B32A32_SINT:return 16;
    case DXGI_FORMAT_R16_FLOAT:case DXGI_FORMAT_R16_UINT:case DXGI_FORMAT_R16_SINT:
    case DXGI_FORMAT_R16_UNORM:case DXGI_FORMAT_R16_SNORM:return 2;
    case DXGI_FORMAT_R16G16_FLOAT:case DXGI_FORMAT_R16G16_UINT:case DXGI_FORMAT_R16G16_SINT:
    case DXGI_FORMAT_R16G16_UNORM:case DXGI_FORMAT_R16G16_SNORM:return 4;
    case DXGI_FORMAT_R16G16B16A16_FLOAT:case DXGI_FORMAT_R16G16B16A16_UINT:case DXGI_FORMAT_R16G16B16A16_SINT:
    case DXGI_FORMAT_R16G16B16A16_UNORM:case DXGI_FORMAT_R16G16B16A16_SNORM:return 8;
    case DXGI_FORMAT_R8_UINT:case DXGI_FORMAT_R8_SINT:case DXGI_FORMAT_R8_UNORM:case DXGI_FORMAT_R8_SNORM:return 1;
    case DXGI_FORMAT_R8G8_UINT:case DXGI_FORMAT_R8G8_SINT:case DXGI_FORMAT_R8G8_UNORM:case DXGI_FORMAT_R8G8_SNORM:return 2;
    case DXGI_FORMAT_R8G8B8A8_UINT:case DXGI_FORMAT_R8G8B8A8_SINT:case DXGI_FORMAT_R8G8B8A8_UNORM:
    case DXGI_FORMAT_R8G8B8A8_SNORM:return 4;
    default:return 0;
    }
}
void GetSrv(ID3D11DeviceContext* c,PayloadStage stage,unsigned slot,ID3D11ShaderResourceView** v)noexcept {
    switch(stage){
    case PayloadStage::Vertex:c->VSGetShaderResources(slot,1,v);break;
    case PayloadStage::Pixel:c->PSGetShaderResources(slot,1,v);break;
    case PayloadStage::Geometry:c->GSGetShaderResources(slot,1,v);break;
    case PayloadStage::Hull:c->HSGetShaderResources(slot,1,v);break;
    case PayloadStage::Domain:c->DSGetShaderResources(slot,1,v);break;
    }
}
}
struct D3D11SceneDrawPayloadWitness::Impl {
    PayloadResult result{};
    std::array<std::array<ComPtr<ID3D11Buffer>,PayloadRangeLimit>,2> staging{};
    std::atomic<DWORD> thread=0;std::atomic<unsigned> wrongThread=0;
    ID3D11DeviceContext* context=nullptr;bool closed=false;
    explicit Impl(const PayloadSelection& p){result.selection=p;}
    bool Own(ID3D11DeviceContext* c)noexcept {
        if(!c)return false;
        DWORD zero=0;
        if(thread.compare_exchange_strong(zero,GetCurrentThreadId(),std::memory_order_acq_rel))context=c;
        if(thread.load(std::memory_order_acquire)!=GetCurrentThreadId()){++wrongThread;return false;}
        if(context!=c){result.identityFailure=true;return false;}
        if(c->GetType()!=D3D11_DEVICE_CONTEXT_IMMEDIATE){result.identityFailure=true;return false;}
        return true;
    }
    bool EyeIdentity(const fvr::graphics::SceneWitnessKey& key,unsigned eye,std::uint64_t view)noexcept {
        if(eye>1||!PayloadKeyValid(key)||view!=(eye?key.rightView:key.mainView)){
            result.identityFailure=true;return false;}
        if(!result.key.world){
            if(eye){result.identityFailure=true;return false;}
            result.key=key;
        }
        if(result.key!=key){result.identityFailure=true;return false;}
        return true;
    }
    void Capture(ID3D11DeviceContext* c,unsigned eye,unsigned n,ID3D11Buffer* indirect)noexcept {
        const auto& request=result.selection.ranges[n];auto& r=result.eyes[eye].records[n];
        r.requested=request;r.payloadOffset=(eye*PayloadRangeLimit+n)*PayloadRangeBytes;
        ComPtr<ID3D11Buffer> source;std::uint64_t base=0,available=0;
        D3D11_BUFFER_DESC desc{};
        if(request.source==PayloadSource::Vertex){
            c->IAGetVertexBuffers(request.slot,1,&source,&r.bindingStride,&r.bindingOffset);
            base=r.bindingOffset;
        }else if(request.source==PayloadSource::Index){
            DXGI_FORMAT f{};c->IAGetIndexBuffer(&source,&f,&r.bindingOffset);
            r.indexFormat=unsigned(f);base=r.bindingOffset;
        }else if(request.source==PayloadSource::Indirect){
            // Caller passes the actual intercepted buffer. Never dereference an
            // arbitrary numeric address from a diagnostic request/config file.
            if(!indirect||Id(indirect)!=result.eyes[eye].actual.argumentBuffer){r.status=PayloadStatus::Absent;return;}
            source=indirect;base=result.eyes[eye].actual.argumentOffset;
        }else{
            ComPtr<ID3D11ShaderResourceView> srv;GetSrv(c,request.stage,request.slot,&srv);
            if(!srv){r.status=PayloadStatus::Absent;return;}
            r.view=Id(srv.Get());D3D11_SHADER_RESOURCE_VIEW_DESC view{};srv->GetDesc(&view);
            static_assert(sizeof(view)<=sizeof(r.srvDescriptorWords));
            r.srvDescriptorBytes=sizeof(view);std::memcpy(r.srvDescriptorWords.data(),&view,sizeof(view));
            r.srvDimension=unsigned(view.ViewDimension);r.srvFormat=unsigned(view.Format);
            ComPtr<ID3D11Resource> resource;srv->GetResource(&resource);r.resource=Id(resource.Get());
            D3D11_RESOURCE_DIMENSION dimension{};resource->GetType(&dimension);r.resourceDimension=unsigned(dimension);
            if(FAILED(resource.As(&source))||(view.ViewDimension!=D3D11_SRV_DIMENSION_BUFFER&&
                view.ViewDimension!=D3D11_SRV_DIMENSION_BUFFEREX)){r.status=PayloadStatus::UnsupportedView;return;}
            source->GetDesc(&desc);
            if(view.ViewDimension==D3D11_SRV_DIMENSION_BUFFER){r.firstElement=view.Buffer.FirstElement;r.elements=view.Buffer.NumElements;}
            else{r.firstElement=view.BufferEx.FirstElement;r.elements=view.BufferEx.NumElements;r.srvFlags=view.BufferEx.Flags;}
            unsigned stride=0;
            if(view.Format==DXGI_FORMAT_UNKNOWN&&(desc.MiscFlags&D3D11_RESOURCE_MISC_BUFFER_STRUCTURED))stride=desc.StructureByteStride;
            else if(view.ViewDimension==D3D11_SRV_DIMENSION_BUFFEREX&&view.Format==DXGI_FORMAT_R32_TYPELESS&&
                view.BufferEx.Flags==D3D11_BUFFEREX_SRV_FLAG_RAW&&(desc.MiscFlags&D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS))stride=4;
            else stride=ElementBytes(view.Format);
            if(!stride){r.status=PayloadStatus::UnsupportedView;return;}
            base=std::uint64_t(r.firstElement)*stride;available=std::uint64_t(r.elements)*stride;
        }
        if(!source){r.status=PayloadStatus::Absent;return;}
        source->GetDesc(&desc);r.resource=Id(source.Get());r.resourceDimension=D3D11_RESOURCE_DIMENSION_BUFFER;
        r.bufferBytes=desc.ByteWidth;r.structureStride=desc.StructureByteStride;r.bindFlags=desc.BindFlags;r.miscFlags=desc.MiscFlags;
        if(request.source!=PayloadSource::ShaderResource)available=base<=desc.ByteWidth?desc.ByteWidth-base:0;
        if(request.source==PayloadSource::Indirect&&!(desc.MiscFlags&D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS)){
            r.status=PayloadStatus::InvalidRange;return;}
        if(!PayloadRangeFits(desc.ByteWidth,base,available,request.offset,request.bytes)){
            r.status=PayloadStatus::InvalidRange;return;}
        // D3D11 predicates also govern CopySubresourceRegion. A skipped copy
        // could still map successfully, yielding undefined staging contents.
        // Keep native state untouched and report this coverage gap explicitly.
        if(result.eyes[eye].predicate){r.status=PayloadStatus::PredicatedNotCopied;return;}
        r.copiedOffset=unsigned(base+request.offset);r.copiedBytes=request.bytes;
        ComPtr<ID3D11Device> device;c->GetDevice(&device);
        D3D11_BUFFER_DESC target{};target.ByteWidth=request.bytes;target.Usage=D3D11_USAGE_STAGING;target.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        r.result=device->CreateBuffer(&target,nullptr,&staging[eye][n]);
        if(FAILED(r.result)){r.status=PayloadStatus::CreateFailed;return;}
        const D3D11_BOX box{r.copiedOffset,0,0,r.copiedOffset+r.copiedBytes,1,1};
        c->CopySubresourceRegion(staging[eye][n].Get(),0,0,0,0,source.Get(),0,&box);
        r.status=PayloadStatus::Pending;
    }
};
D3D11SceneDrawPayloadWitness::D3D11SceneDrawPayloadWitness(const PayloadSelection& plan){
    if(PayloadPlanValid(plan))impl_=std::make_unique<Impl>(plan);
}
D3D11SceneDrawPayloadWitness::~D3D11SceneDrawPayloadWitness(){
    // Caller normally closes on the graphics owner. On misuse, quarantine this
    // one bounded diagnostic rather than release COM resources on a foreign
    // thread of a single-threaded native device. No native resource is retained.
    if(impl_&&!impl_->closed&&impl_->thread.load()&&impl_->thread.load()!=GetCurrentThreadId())(void)impl_.release();
}
bool D3D11SceneDrawPayloadWitness::Valid()const noexcept{return bool(impl_);}
bool D3D11SceneDrawPayloadWitness::Observe(ID3D11DeviceContext* c,const fvr::graphics::SceneWitnessKey& key,
    unsigned eye,std::uint64_t view,unsigned ordinal,const fvr::graphics::SceneWitnessDrawArguments& args,
    ID3D11Buffer* indirect)noexcept {
    if(!impl_||eye>1||ordinal!=impl_->result.selection.ordinal[eye])return false;
    auto& s=*impl_;if(!s.Own(c)||s.closed||!s.EyeIdentity(key,eye,view))return false;
    auto& e=s.result.eyes[eye];
    if(s.result.identityFailure||s.result.shapeFailure||e.captured||e.finished||
        (eye&&(!s.result.eyes[0].finished||!s.result.eyes[0].captured))){s.result.identityFailure=true;return false;}
    e.ordinal=ordinal;e.view=view;e.actual=args;
    if(!PayloadDrawShape(args,s.result.selection.expected[eye])){s.result.shapeFailure=true;return false;}
    e.captured=true;
    {ComPtr<ID3D11InputLayout> layout;c->IAGetInputLayout(&layout);e.inputLayout=Id(layout.Get());
     D3D11_PRIMITIVE_TOPOLOGY topology{};c->IAGetPrimitiveTopology(&topology);e.topology=unsigned(topology);
     ComPtr<ID3D11Predicate> predicate;BOOL value=FALSE;c->GetPredication(&predicate,&value);
     e.predicate=Id(predicate.Get());e.predicateValue=value!=FALSE;}
    {ComPtr<ID3D11VertexShader> v;ComPtr<ID3D11PixelShader> p;ComPtr<ID3D11GeometryShader> g;
     ComPtr<ID3D11HullShader> h;ComPtr<ID3D11DomainShader> d;
     c->VSGetShader(&v,nullptr,nullptr);c->PSGetShader(&p,nullptr,nullptr);c->GSGetShader(&g,nullptr,nullptr);
     c->HSGetShader(&h,nullptr,nullptr);c->DSGetShader(&d,nullptr,nullptr);
     e.shaders={Id(v.Get()),Id(p.Get()),Id(g.Get()),Id(h.Get()),Id(d.Get())};}
    for(unsigned n=0;n<s.result.selection.count;++n)s.Capture(c,eye,n,indirect);
    return true;
}
bool D3D11SceneDrawPayloadWitness::EndEye(ID3D11DeviceContext* c,const fvr::graphics::SceneWitnessKey& key,
    unsigned eye,std::uint64_t view)noexcept {
    if(!impl_)return false;auto& s=*impl_;
    if(!s.Own(c)||s.closed||!s.EyeIdentity(key,eye,view))return false;
    auto& e=s.result.eyes[eye];
    if(e.finished||(eye&&!s.result.eyes[0].finished)){s.result.identityFailure=true;return false;}
    if(!e.captured)s.result.missingDraw=true;
    e.finished=true;return e.captured&&!s.result.identityFailure&&!s.result.shapeFailure;
}
void D3D11SceneDrawPayloadWitness::Poll(ID3D11DeviceContext* c)noexcept {
    if(!impl_)return;auto& s=*impl_;if(!s.Own(c)||s.closed)return;
    // At most sixteen nonblocking maps per callback; no Flush/query/wait loop.
    for(unsigned e=0;e<2;++e)for(unsigned n=0;n<s.result.selection.count;++n){auto& target=s.staging[e][n];if(!target)continue;
        auto& r=s.result.eyes[e].records[n];D3D11_MAPPED_SUBRESOURCE mapped{};
        const auto hr=c->Map(target.Get(),0,D3D11_MAP_READ,D3D11_MAP_FLAG_DO_NOT_WAIT,&mapped);
        if(hr==DXGI_ERROR_WAS_STILL_DRAWING)continue;
        r.result=hr;
        if(SUCCEEDED(hr)){std::memcpy(s.result.bytes.data()+r.payloadOffset,mapped.pData,r.copiedBytes);c->Unmap(target.Get(),0);r.status=PayloadStatus::Ready;}
        else r.status=PayloadStatus::MapFailed;
        target.Reset();
    }
}
bool D3D11SceneDrawPayloadWitness::Snapshot(ID3D11DeviceContext* c,PayloadResult& result)noexcept {
    if(!impl_)return false;auto& s=*impl_;if(!s.Own(c))return false;
    if(!s.closed&&(!s.result.eyes[0].finished||!s.result.eyes[1].finished))return false;
    for(auto& eye:s.staging)for(auto& resource:eye)if(resource)return false;
    s.result.wrongThread=s.wrongThread.load(std::memory_order_relaxed);result=s.result;return true;
}
bool D3D11SceneDrawPayloadWitness::CloseOnGraphicsThread(ID3D11DeviceContext* c)noexcept {
    if(!impl_)return true;auto& s=*impl_;if(!s.Own(c))return false;
    for(unsigned e=0;e<2;++e)for(unsigned n=0;n<s.result.selection.count;++n){
        auto& target=s.staging[e][n];if(target){target.Reset();s.result.eyes[e].records[n].status=PayloadStatus::Cancelled;}}
    s.closed=true;return true;
}
}
