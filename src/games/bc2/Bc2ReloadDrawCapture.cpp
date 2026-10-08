#include "Bc2ReloadDrawCapture.h"
#include "Bc2ReloadDrawGeometry.h"
#include "Bc2ReticleDrawObservation.h"
#ifdef FVR_BC2_DRAW_CATALOG_HEADER
#include FVR_BC2_DRAW_CATALOG_HEADER
#endif
#include <Windows.h>
#include <d3d11.h>
#include <wrl/client.h>
#include <algorithm>
#include <cstring>
#include <iomanip>
#include <limits>
#include <vector>
namespace fvr::bc2 {
using Microsoft::WRL::ComPtr;
namespace {
std::uint64_t Id(const void* p)noexcept{return reinterpret_cast<std::uintptr_t>(p);}
struct Binding {
    std::uint64_t source=0,hash=0;unsigned bytes=0,usage=0,copiedBytes=0;
    long result=0;bool captured=false,complete=false,rangeRejected=false;
    unsigned packedMatches=0,opticMatches=0;std::array<unsigned,4> packedOffsets{},opticOffsets{};
};
struct SrvBinding {
    std::uint64_t view=0,resource=0;unsigned dimension=0,format=0,firstElement=0,elements=0;
};
struct Draw {
    ReloadDrawFrameEvidence frame{};ReloadReticleDrawCurrent reticleCurrent{};ReticleDrawObservation reticle{};
    unsigned ordinal=0,count=0,start=0,indexFormat=0,indexOffset=0,stride=0,vertexOffset=0;
    std::int32_t base=0;unsigned topology=0;std::uint64_t layout=0,vs=0,ps=0,target=0,depth=0;
    std::array<std::uint64_t,8> vertexBuffers{};std::array<unsigned,8> vertexStrides{},vertexOffsets{};
    std::array<Binding,16> buffers{}; // VB0, selected IB range, VS CB0..13.
    std::array<SrvBinding,128> srvs{};
    // Diagnostic candidate location observed in exact SPAS shell draws. Capture
    // independently of producer lookup so cross-eye equality can be audited.
    // This is NOT shader layout verification or section/render authority.
    std::array<std::byte,48> shellSlot{};bool shellSlotCaptured=false;
    WeaponDrawMatch match{};
    ReloadDrawGeometry geometry{};bool complete=false,abandoned=false,geometryValid=false,producerAssociation=false;
};
struct Staging {ComPtr<ID3D11Buffer> buffer;std::vector<std::byte> bytes;bool pending=false;};
struct Pending {unsigned record=0;bool used=false;std::array<Staging,16> stages{};};
void BindingJson(std::ostream& out,const Binding& b){
    out<<"{\"source\":"<<b.source<<",\"bytes\":"<<b.bytes<<",\"usage\":"<<b.usage<<",\"copied_bytes\":"<<b.copiedBytes
       <<",\"captured\":"<<(b.captured?"true":"false")<<",\"complete\":"<<(b.complete?"true":"false")
       <<",\"range_rejected\":"<<(b.rangeRejected?"true":"false")<<",\"result\":"<<b.result<<",\"fnv1a64\":\""<<std::hex<<b.hash<<std::dec
       <<"\",\"packed_shell_matches\":"<<b.packedMatches<<",\"packed_shell_offsets\":[";
    for(unsigned i=0;i<std::min(b.packedMatches,4u);++i){if(i)out<<',';out<<b.packedOffsets[i];}
    out<<"],\"packed_optic_matches\":"<<b.opticMatches<<",\"packed_optic_offsets\":[";
    for(unsigned i=0;i<std::min(b.opticMatches,4u);++i){if(i)out<<',';out<<b.opticOffsets[i];}out<<"]}";
}
}
struct Bc2ReloadDrawCapture::Impl {
    bool enabled=false,active=false,stopped=false;std::int64_t begin=0,deadline=0,nextSample=0;
    std::atomic<DWORD> thread=0;ComPtr<ID3D11DeviceContext> context;
    std::uint32_t sampledFrame=0;std::uint64_t sampledWorld=0,sampledRequest=0;bool sampled=false;
    ReloadDrawFrameEvidence frame{};unsigned ordinal=0,perFrame=0,count=0;
    unsigned samples=0,candidates=0,layoutRejected=0,recordOverflow=0,pendingOverflow=0,frameOverflow=0,errors=0,pendingAbandoned=0;
    std::atomic<unsigned> wrongThread=0;
    std::vector<WeaponDrawSection> catalog;
    std::array<Draw,MaxRecords> records{};std::array<Pending,MaxPending> pending{};
    bool Owns(ID3D11DeviceContext* incoming)noexcept{return thread.load(std::memory_order_seq_cst)==GetCurrentThreadId()&&context.Get()==incoming;}
    void Stage(ID3D11Device* device,ID3D11DeviceContext* ctx,ID3D11Buffer* source,
        unsigned offset,unsigned length,unsigned cap,Binding& info,Staging& stage){
        if(!source)return;D3D11_BUFFER_DESC desc{};source->GetDesc(&desc);
        info.source=Id(source);info.bytes=desc.ByteWidth;info.usage=desc.Usage;
        if(!length||length>cap||offset>desc.ByteWidth||length>desc.ByteWidth-offset){info.rangeRejected=true;return;}
        auto stagingDesc=desc;stagingDesc.ByteWidth=length;stagingDesc.Usage=D3D11_USAGE_STAGING;
        stagingDesc.BindFlags=0;stagingDesc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;stagingDesc.MiscFlags=0;stagingDesc.StructureByteStride=0;
        info.result=device->CreateBuffer(&stagingDesc,nullptr,&stage.buffer);if(FAILED(info.result)){++errors;return;}
        stage.bytes.resize(length);info.copiedBytes=length;
        D3D11_BOX box{offset,0,0,offset+length,1,1};
        ctx->CopySubresourceRegion(stage.buffer.Get(),0,0,0,0,source,0,&box);
        stage.pending=true;info.captured=true;
    }
};
Bc2ReloadDrawCapture::Admission::Admission(Bc2ReloadDrawCapture& value)noexcept:owner(value){
    if(!owner.accepting_.load(std::memory_order_seq_cst))return;
    owner.callbacks_.fetch_add(1,std::memory_order_seq_cst);
    if(owner.accepting_.load(std::memory_order_seq_cst))entered=true;
    else owner.callbacks_.fetch_sub(1,std::memory_order_seq_cst);
}
Bc2ReloadDrawCapture::Admission::~Admission(){if(entered)owner.callbacks_.fetch_sub(1,std::memory_order_seq_cst);}
Bc2ReloadDrawCapture::Bc2ReloadDrawCapture()=default;
Bc2ReloadDrawCapture::~Bc2ReloadDrawCapture()=default;
bool Bc2ReloadDrawCapture::SetReticleResolver(ReloadReticleDrawResolver resolver)noexcept {
    if(impl_||accepting_.load(std::memory_order_seq_cst)||callbacks_.load(std::memory_order_seq_cst))return false;
    reticleResolver_=resolver;return true;
}
bool Bc2ReloadDrawCapture::Enable(bool explicitDiagnostic,std::int64_t nowNs)noexcept {
    try {
#ifdef FVR_BC2_DRAW_CATALOG_HEADER
        return Enable(explicitDiagnostic,nowNs,GeneratedWeaponDrawCatalog());
#else
        return Enable(explicitDiagnostic,nowNs,LegacyReloadDrawCatalog());
#endif
    }catch(...){return false;}
}
bool Bc2ReloadDrawCapture::Enable(bool explicitDiagnostic,std::int64_t nowNs,std::span<const WeaponDrawSection> catalog)noexcept {
    if(!explicitDiagnostic||impl_||nowNs<=0||nowNs>std::numeric_limits<std::int64_t>::max()-WindowNs||!WeaponDrawCatalogValid(catalog))return false;
    try{auto next=std::make_unique<Impl>();next->catalog.assign(catalog.begin(),catalog.end());
        next->enabled=true;next->begin=nowNs;next->deadline=nowNs+WindowNs;impl_=std::move(next);
        accepting_.store(true,std::memory_order_seq_cst);return true;}
    catch(...){return false;}
}
void Bc2ReloadDrawCapture::BeginEye(ID3D11DeviceContext* context,const ReloadDrawFrameEvidence& frame)noexcept {
    Admission admitted(*this);if(!admitted.entered)return;auto& s=*impl_;
    DWORD empty=0;const auto caller=GetCurrentThreadId();s.thread.compare_exchange_strong(empty,caller,std::memory_order_seq_cst);
    if(s.thread.load(std::memory_order_seq_cst)!=caller){++s.wrongThread;return;}
    s.active=false;
    if(!s.enabled||s.stopped||!context||frame.eye>1||!frame.nativeFrame||!frame.world||!frame.request||!frame.view||frame.nowNs<s.begin||frame.nowNs>=s.deadline)return;
    if(!s.context)s.context=context;
    if(!s.Owns(context)){++s.wrongThread;return;}
    Poll(context);
    const bool same=s.sampled&&s.sampledFrame==frame.nativeFrame&&s.sampledWorld==frame.world&&s.sampledRequest==frame.request;
    if(!same){if(frame.nowNs<s.nextSample)return;s.sampled=true;s.sampledFrame=frame.nativeFrame;s.sampledWorld=frame.world;s.sampledRequest=frame.request;
        s.nextSample=frame.nowNs+SamplePeriodNs;s.perFrame=0;++s.samples;}
    s.frame=frame;s.ordinal=0;s.active=true;
}
void Bc2ReloadDrawCapture::ObserveIndexed(ID3D11DeviceContext* context,unsigned count,unsigned start,std::int32_t base)noexcept {
    Admission admitted(*this);if(!admitted.entered)return;auto& s=*impl_;
    if(s.thread.load(std::memory_order_seq_cst)!=GetCurrentThreadId()){++s.wrongThread;return;}
    if(!s.enabled||!s.active||s.stopped)return;
    if(!s.Owns(context)){++s.wrongThread;return;}const auto ordinal=++s.ordinal;
    if(!WeaponDrawCountCandidate(s.catalog,count))return;++s.candidates;
    // World geometry frequently shares these small index counts. Reject layouts
    // our fingerprint cannot interpret before spending a record/readback slot;
    // otherwise early terrain draws can exhaust the entire evidence budget.
    ComPtr<ID3D11Buffer> candidateVertex;UINT candidateStride=0,candidateOffset=0;
    context->IAGetVertexBuffers(0,1,&candidateVertex,&candidateStride,&candidateOffset);
    D3D11_PRIMITIVE_TOPOLOGY candidateTopology{};context->IAGetPrimitiveTopology(&candidateTopology);
    if(!candidateVertex||candidateTopology!=D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST||
       !WeaponDrawLayoutCandidate(s.catalog,count,candidateStride)){++s.layoutRejected;return;}
    if(s.perFrame>=MaxPerFrame){++s.frameOverflow;return;}++s.perFrame;
    if(s.count>=MaxRecords){++s.recordOverflow;return;}
    auto slot=std::find_if(s.pending.begin(),s.pending.end(),[](const auto& p){return !p.used;});
    if(slot==s.pending.end()){++s.pendingOverflow;return;}
    const auto record=s.count++;auto& d=s.records[record];d.frame=s.frame;d.ordinal=ordinal;d.count=count;d.start=start;d.base=base;
    slot->record=record;slot->used=true;
    if(count==12&&candidateStride==68&&reticleResolver_)d.reticleCurrent=reticleResolver_(d.frame);
    try{
        ComPtr<ID3D11Device> device;context->GetDevice(&device);
        ComPtr<ID3D11InputLayout> layout;context->IAGetInputLayout(&layout);d.layout=Id(layout.Get());
        D3D11_PRIMITIVE_TOPOLOGY topology{};context->IAGetPrimitiveTopology(&topology);d.topology=topology;
        std::array<ID3D11Buffer*,8> rawVertices{};context->IAGetVertexBuffers(0,8,rawVertices.data(),d.vertexStrides.data(),d.vertexOffsets.data());
        std::array<ComPtr<ID3D11Buffer>,8> vertices;for(unsigned i=0;i<8;++i){vertices[i].Attach(rawVertices[i]);d.vertexBuffers[i]=Id(rawVertices[i]);}
        d.stride=d.vertexStrides[0];d.vertexOffset=d.vertexOffsets[0];
        ComPtr<ID3D11Buffer> indices;DXGI_FORMAT format=DXGI_FORMAT_UNKNOWN;context->IAGetIndexBuffer(&indices,&format,&d.indexOffset);d.indexFormat=format;
        ComPtr<ID3D11VertexShader> vs;ComPtr<ID3D11PixelShader> ps;context->VSGetShader(&vs,nullptr,nullptr);context->PSGetShader(&ps,nullptr,nullptr);d.vs=Id(vs.Get());d.ps=Id(ps.Get());
        ComPtr<ID3D11RenderTargetView> target;ComPtr<ID3D11DepthStencilView> depth;context->OMGetRenderTargets(1,&target,&depth);d.target=Id(target.Get());d.depth=Id(depth.Get());
        const unsigned indexBytes=format==DXGI_FORMAT_R16_UINT?2u:format==DXGI_FORMAT_R32_UINT?4u:0u;
        const auto first=std::uint64_t(d.indexOffset)+std::uint64_t(start)*indexBytes;
        if(topology==D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST&&WeaponDrawLayoutCandidate(s.catalog,count,d.stride)&&indexBytes&&first<=UINT32_MAX){
            if(vertices[0]){D3D11_BUFFER_DESC desc{};vertices[0]->GetDesc(&desc);s.Stage(device.Get(),context,vertices[0].Get(),0,desc.ByteWidth,2*1024*1024,d.buffers[0],slot->stages[0]);}
            s.Stage(device.Get(),context,indices.Get(),unsigned(first),count*indexBytes,400000,d.buffers[1],slot->stages[1]);
        }else{d.buffers[0].rangeRejected=d.buffers[1].rangeRejected=true;d.buffers[0].source=Id(vertices[0].Get());d.buffers[1].source=Id(indices.Get());}
        std::array<ID3D11Buffer*,14> rawConstants{};context->VSGetConstantBuffers(0,14,rawConstants.data());
        std::array<ComPtr<ID3D11Buffer>,14> constants;for(unsigned i=0;i<14;++i)constants[i].Attach(rawConstants[i]);
        unsigned budget=128*1024;
        for(unsigned i=0;i<14;++i)if(constants[i]){D3D11_BUFFER_DESC desc{};constants[i]->GetDesc(&desc);
            s.Stage(device.Get(),context,constants[i].Get(),0,desc.ByteWidth,std::min(budget,65536u),d.buffers[i+2],slot->stages[i+2]);
            if(d.buffers[i+2].captured)budget-=d.buffers[i+2].copiedBytes;}
        std::array<ID3D11ShaderResourceView*,128> rawSrvs{};context->VSGetShaderResources(0,128,rawSrvs.data());
        std::array<ComPtr<ID3D11ShaderResourceView>,128> srvs;for(unsigned i=0;i<128;++i)srvs[i].Attach(rawSrvs[i]);
        for(unsigned i=0;i<128;++i)if(srvs[i]){auto& b=d.srvs[i];D3D11_SHADER_RESOURCE_VIEW_DESC desc{};srvs[i]->GetDesc(&desc);ComPtr<ID3D11Resource> resource;srvs[i]->GetResource(&resource);
            b.view=Id(srvs[i].Get());b.resource=Id(resource.Get());b.dimension=desc.ViewDimension;b.format=desc.Format;
            if(desc.ViewDimension==D3D11_SRV_DIMENSION_BUFFER){b.firstElement=desc.Buffer.FirstElement;b.elements=desc.Buffer.NumElements;}
            else if(desc.ViewDimension==D3D11_SRV_DIMENSION_BUFFEREX){b.firstElement=desc.BufferEx.FirstElement;b.elements=desc.BufferEx.NumElements;}}
    }catch(...){++s.errors;d.abandoned=true;for(auto& stage:slot->stages)stage={};slot->used=false;}
}
void Bc2ReloadDrawCapture::EndEye()noexcept{
    Admission admitted(*this);if(!admitted.entered)return;auto& s=*impl_;
    if(s.thread.load(std::memory_order_seq_cst)!=GetCurrentThreadId()){++s.wrongThread;return;}s.active=false;
}
void Bc2ReloadDrawCapture::Poll(ID3D11DeviceContext* context)noexcept {
    Admission admitted(*this);if(!admitted.entered)return;auto& s=*impl_;
    if(s.thread.load(std::memory_order_seq_cst)!=GetCurrentThreadId()){++s.wrongThread;return;}
    if(s.stopped||!context)return;if(!s.Owns(context)){++s.wrongThread;return;}
    for(auto& pending:s.pending){if(!pending.used)continue;auto& d=s.records[pending.record];bool done=true;
        for(unsigned i=0;i<pending.stages.size();++i){auto& stage=pending.stages[i];auto& b=d.buffers[i];if(!stage.pending)continue;
            D3D11_MAPPED_SUBRESOURCE mapped{};const auto hr=context->Map(stage.buffer.Get(),0,D3D11_MAP_READ,D3D11_MAP_FLAG_DO_NOT_WAIT,&mapped);
            if(hr==DXGI_ERROR_WAS_STILL_DRAWING){done=false;continue;}b.result=hr;
            if(SUCCEEDED(hr)){
                std::memcpy(stage.bytes.data(),mapped.pData,stage.bytes.size());context->Unmap(stage.buffer.Get(),0);b.complete=true;b.hash=ReloadDrawHash(stage.bytes);
                if(i==2&&stage.bytes.size()>=272+d.shellSlot.size()){
                    std::memcpy(d.shellSlot.data(),stage.bytes.data()+272,d.shellSlot.size());d.shellSlotCaptured=true;}
                if(i>=2&&d.frame.producer.packedShellValid){const auto& pattern=d.frame.producer.packedShell;
                    for(std::size_t at=0;at+pattern.size()<=stage.bytes.size();at+=4)if(!std::memcmp(stage.bytes.data()+at,pattern.data(),pattern.size())){
                        if(b.packedMatches<b.packedOffsets.size())b.packedOffsets[b.packedMatches]=unsigned(at);++b.packedMatches;}}
                if(i>=2&&d.frame.producer.packedOpticValid){const auto& pattern=d.frame.producer.packedOptic;
                    for(std::size_t at=0;at+pattern.size()<=stage.bytes.size();at+=4)if(!std::memcmp(stage.bytes.data()+at,pattern.data(),pattern.size())){
                        if(b.opticMatches<b.opticOffsets.size())b.opticOffsets[b.opticMatches]=unsigned(at);++b.opticMatches;}}
            }else ++s.errors;
            stage.buffer.Reset();stage.pending=false;
        }
        if(!done)continue;
        if(d.buffers[0].complete&&d.buffers[1].complete){
            d.match=MatchWeaponDrawSections(s.catalog,d.count,d.stride,{pending.stages[0].bytes,pending.stages[1].bytes,
                d.indexFormat==DXGI_FORMAT_R16_UINT?2u:4u,d.vertexOffset,d.base});
            if(d.match.fingerprintValid){d.geometry.vertexSkinHash=d.match.fingerprint.skin;d.geometry.positionHash=d.match.fingerprint.positions;d.geometryValid=true;}
            if(d.match.status==WeaponDrawMatchStatus::Unique)d.geometry.section=s.catalog[d.match.index].legacyReloadSection;
        }
        const auto& p=d.frame.producer;
        d.producerAssociation=d.geometry.section&&p.exactRequestAssociation&&p.selectedMeshIdentityVerified&&p.request==d.frame.request&&p.nativeFrame==d.frame.nativeFrame&&
            p.observedNs>0&&p.observedNs<=d.frame.nowNs&&p.deadlineNs>d.frame.nowNs&&p.deadlineNs-p.observedNs<=250000000&&
            p.actor&&p.weak&&p.weapon&&p.rigPose&&p.selectedMeshes1p&&p.rigFingerprint==0xa7f219a1426216abull;
        if(d.count==12&&d.stride==68){
            unsigned matches=0;bool constantsComplete=true;
            for(unsigned i=2;i<d.buffers.size();++i){const auto& cb=d.buffers[i];matches+=cb.opticMatches;
                if(cb.source&&(!cb.captured||!cb.complete||cb.rangeRejected))constantsComplete=false;}
            d.reticle=ObserveReticleAperture(d.frame,d.reticleCurrent,d.geometryValid&&d.geometry.section==3,constantsComplete,matches);
        }
        for(auto& stage:pending.stages)stage={};pending.used=false;d.complete=true;
    }
}
bool Bc2ReloadDrawCapture::Stop()noexcept{
    accepting_.store(false,std::memory_order_seq_cst);
    if(callbacks_.load(std::memory_order_seq_cst))return false;
    if(!impl_)return true;auto& s=*impl_;s.enabled=false;s.active=false;s.stopped=true;
    for(auto& p:s.pending)if(p.used){s.records[p.record].abandoned=true;++s.pendingAbandoned;for(auto& stage:p.stages)stage={};p.used=false;}
    s.context.Reset();return true;
}
void Bc2ReloadDrawCapture::Report(std::ostream& out)const {
    out<<"{\"schema\":\"fvr.bc2.reload_draw_capture\",\"version\":1,\"read_only\":true,\"requested\":"<<(impl_?"true":"false");
    if(!impl_){out<<'}';return;}
    // A closed admission gate alone is insufficient: Stop must have drained all
    // callbacks before any mutable evidence or counters can be inspected.
    if(accepting_.load(std::memory_order_seq_cst)||callbacks_.load(std::memory_order_seq_cst)){
        out<<",\"drained\":false,\"draws\":[]}";return;}
    const auto& s=*impl_;if(!s.stopped){out<<",\"drained\":false,\"draws\":[]}";return;}
    out<<",\"stopped\":"<<(s.stopped?"true":"false")<<",\"begin_ns\":"<<s.begin<<",\"deadline_ns\":"<<s.deadline<<",\"sample_period_ns\":"<<SamplePeriodNs
       <<",\"sampled_frames\":"<<s.samples<<",\"candidates\":"<<s.candidates<<",\"record_overflow\":"<<s.recordOverflow<<",\"pending_overflow\":"<<s.pendingOverflow
       <<",\"layout_rejected\":"<<s.layoutRejected<<",\"frame_overflow\":"<<s.frameOverflow<<",\"wrong_thread\":"<<s.wrongThread.load()<<",\"errors\":"<<s.errors<<",\"pending_abandoned\":"<<s.pendingAbandoned
       <<",\"limits\":{\"records\":"<<MaxRecords<<",\"pending\":"<<MaxPending<<",\"per_frame\":"<<MaxPerFrame<<",\"vertex_bytes_per_draw\":2097152,\"vs_constant_bytes_per_draw\":131072,\"index_bytes_per_draw\":400000},\"catalog\":[";
    for(unsigned i=0;i<s.catalog.size();++i){if(i)out<<',';const auto& item=s.catalog[i];
        out<<"{\"index\":"<<i<<",\"resource\":"<<std::quoted(item.resource)<<",\"variant\":"<<std::quoted(item.variant)
           <<",\"section\":"<<std::quoted(item.section)<<",\"lod\":"<<item.lod<<",\"section_index\":"<<item.sectionIndex<<'}';}
    out<<"],\"catalog_native_association_verified\":false,\"draws\":[";
    if(s.stopped)for(unsigned n=0;n<s.count;++n){if(n)out<<',';const auto& d=s.records[n];const auto& f=d.frame;const auto& p=f.producer;
        out<<"{\"qpc_ns\":"<<f.nowNs<<",\"tick_ms\":"<<f.tickMs<<",\"frame\":"<<f.nativeFrame<<",\"eye\":"<<f.eye<<",\"world\":"<<f.world<<",\"request\":"<<f.request<<",\"view\":"<<f.view
           <<",\"draw_ordinal\":"<<d.ordinal<<",\"count\":"<<d.count<<",\"start_index\":"<<d.start<<",\"base_vertex\":"<<d.base<<",\"index_format\":"<<d.indexFormat<<",\"index_offset\":"<<d.indexOffset
           <<",\"topology\":"<<d.topology<<",\"input_layout\":"<<d.layout<<",\"vs\":"<<d.vs<<",\"ps\":"<<d.ps<<",\"target\":"<<d.target<<",\"depth\":"<<d.depth
           <<",\"complete\":"<<(d.complete?"true":"false")<<",\"abandoned\":"<<(d.abandoned?"true":"false")<<",\"geometry_valid\":"<<(d.geometryValid?"true":"false")
           <<",\"geometry_section\":"<<d.geometry.section<<",\"shell_section\":"<<(d.geometry.section<3?d.geometry.section:0)<<",\"optic_reticle\":"<<(d.geometry.section==3?"true":"false")<<",\"vertex_skin_fnv1a64\":\""<<std::hex<<d.geometry.vertexSkinHash<<"\",\"position_fnv1a64\":\""<<d.geometry.positionHash<<std::dec<<'"'
           <<",\"catalog_match_status\":"<<unsigned(d.match.status)<<",\"catalog_matches\":"<<d.match.matches<<",\"catalog_unique_index\":"<<d.match.index
           <<",\"catalog_matching_indices\":[";
        for(unsigned i=0;i<std::min(d.match.matches,unsigned(d.match.matchingIndices.size()));++i){if(i)out<<',';out<<d.match.matchingIndices[i];}
        out<<"],\"catalog_matches_truncated\":"<<(d.match.matches>d.match.matchingIndices.size()?"true":"false")
           <<",\"catalog_match_native_association_verified\":false"
           <<",\"producer_association_verified\":"<<(d.producerAssociation?"true":"false")<<",\"producer\":{\"request\":"<<p.request<<",\"native_frame\":"<<p.nativeFrame<<",\"rig_pose\":"<<p.rigPose
           <<",\"selected_meshes_1p\":"<<p.selectedMeshes1p<<",\"rig_fingerprint\":"<<p.rigFingerprint<<",\"actor\":"<<p.actor<<",\"weak\":"<<p.weak<<",\"weapon\":"<<p.weapon
           <<",\"owner_generation\":"<<p.ownerGeneration<<",\"input_generation\":"<<p.inputGeneration<<",\"space\":"<<p.space<<",\"observed_ns\":"<<p.observedNs<<",\"deadline_ns\":"<<p.deadlineNs
           <<",\"exact_request_association\":"<<(p.exactRequestAssociation?"true":"false")<<",\"selected_mesh_identity_verified\":"<<(p.selectedMeshIdentityVerified?"true":"false")
           <<",\"shell_hidden\":"<<(p.shellHidden?"true":"false")<<",\"packed_shell_valid\":"<<(p.packedShellValid?"true":"false")<<",\"packed_optic_valid\":"<<(p.packedOpticValid?"true":"false")<<",\"hold_cycle\":"<<p.holdCycle<<",\"hold_begin_ns\":"<<p.holdBeginNs<<",\"hold_deadline_ns\":"<<p.holdDeadlineNs<<"},\"vertex_bindings\":[";
        for(unsigned i=0;i<8;++i){if(i)out<<',';out<<"{\"slot\":"<<i<<",\"buffer\":"<<d.vertexBuffers[i]<<",\"stride\":"<<d.vertexStrides[i]<<",\"offset\":"<<d.vertexOffsets[i]<<'}';}
        out<<"],\"buffers\":[";for(unsigned i=0;i<16;++i){if(i)out<<',';BindingJson(out,d.buffers[i]);}
        out<<"],\"reticle_aperture_observation\":{\"missing_bits\":"<<d.reticle.missing
           <<",\"evaluated\":"<<(d.reticle.evaluated?"true":"false")<<",\"visibility\":"<<unsigned(d.reticle.visibility)
           <<",\"hidden_candidate\":"<<(d.reticle.hiddenCandidate?"true":"false")<<",\"draw_suppressed\":false,\"gpu_instance_association_verified\":false"
           <<",\"draw_ns\":"<<d.reticleCurrent.nowNs<<",\"physical_equipment_generation\":"<<p.physicalEquipmentGeneration
           <<",\"current_equipment_generation\":"<<d.reticleCurrent.physicalEquipmentGeneration
           <<",\"eye_world_valid\":"<<(d.reticleCurrent.eyeWorldValid?"true":"false")
           <<",\"eye_world\":["<<d.reticleCurrent.eyeCanonicalLh.x<<','<<d.reticleCurrent.eyeCanonicalLh.y<<','<<d.reticleCurrent.eyeCanonicalLh.z
           <<"],\"eye_bind\":["<<d.reticle.eyeBind.x<<','<<d.reticle.eyeBind.y<<','<<d.reticle.eyeBind.z
           <<"],\"selected_source_sequence\":"<<(p.opticSelected?p.opticSelected->sequence:0)
           <<",\"selected_source_deadline_ns\":"<<(p.opticSelected?p.opticSelected->deadlineNs:0)
           <<",\"selected_current_sequence\":"<<(d.reticleCurrent.selected?d.reticleCurrent.selected->sequence:0)
           <<",\"packed_optic_bytes_hex\":\"";
        if(p.packedOpticValid){constexpr char hex[]="0123456789abcdef";for(const auto value:p.packedOptic){const auto byteValue=std::to_integer<unsigned>(value);out<<hex[byteValue>>4]<<hex[byteValue&15];}}
        out<<"\"},\"shell_slot_observation\":{\"captured\":"<<(d.shellSlotCaptured?"true":"false")<<",\"constant_buffer_slot\":0,\"byte_offset\":272,\"byte_count\":48,\"shader_consumption_verified\":false,\"bytes_hex\":\"";
        if(d.shellSlotCaptured){constexpr char hex[]="0123456789abcdef";for(const auto value:d.shellSlot){const auto byteValue=std::to_integer<unsigned>(value);out<<hex[byteValue>>4]<<hex[byteValue&15];}}
        out<<"\"},\"vs_srvs\":[";bool first=true;for(unsigned i=0;i<128;++i)if(d.srvs[i].view){if(!first)out<<',';first=false;const auto& b=d.srvs[i];out<<"{\"slot\":"<<i<<",\"view\":"<<b.view<<",\"resource\":"<<b.resource<<",\"dimension\":"<<b.dimension<<",\"format\":"<<b.format<<",\"first_element\":"<<b.firstElement<<",\"elements\":"<<b.elements<<'}';}out<<"]}";
    }
    out<<"]}";
}
}
