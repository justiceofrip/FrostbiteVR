#include "fvr/graphics/D3D11SceneDrawWitness.h"
#include <Windows.h>
#include <d3d11.h>
#include <wrl/client.h>
#include <array>
#include <algorithm>
#include <cstring>
#include <fstream>
#include <sstream>
#include <vector>

namespace fvr::graphics {
using Microsoft::WRL::ComPtr;
namespace {
std::uint64_t Id(const void* p)noexcept{return reinterpret_cast<std::uintptr_t>(p);}
struct Buffer {
    std::uint64_t id=0;unsigned size=0,offset=0;long result=0;
    bool queued=false,ready=false,omitted=false;
    ComPtr<ID3D11Buffer> staging;
};
struct Draw {
    SceneWitnessDrawArguments args{};unsigned ordinal=0;
    std::array<std::uint64_t,5> shaders{};
    std::uint64_t layout=0,index=0,target=0,depth=0,depthState=0;
    std::array<std::uint64_t,8> vertices{};
    std::array<unsigned,8> strides{},offsets{};
    unsigned indexFormat=0,indexOffset=0,topology=0,stencilRef=0;
    unsigned targetWidth=0,targetHeight=0,targetSamples=0,depthWidth=0,depthHeight=0,depthSamples=0;
    D3D11_VIEWPORT viewport{};unsigned viewportCount=0;
    // Full binding identities for VS/PS constants. Other-stage shader identities
    // are retained, but their constant payloads are explicitly not captured.
    std::array<Buffer,28> constants{};
};
struct Color {
    ComPtr<ID3D11Texture2D> staging;
    std::vector<unsigned char> bytes;
    unsigned format=0;std::uint64_t source=0;long result=0;
    bool queued=false,ready=false;
};
struct Eye {
    std::array<Draw,SceneWitnessDrawsPerEye> draws{};
    unsigned count=0,observed=0,overflow=0,skipped=0,bufferBytes=0,bufferCopies=0;
    bool began=false,ended=false;
    std::array<std::byte,0x460> expectedCamera{};bool expectedCameraValid=false;
    Color color;
};
struct Pair {SceneWitnessKey key{};std::array<Eye,2> eyes{};bool failed=false;};
void KeyJson(std::ostream& o,const SceneWitnessKey& k){
    o<<"{\"world\":"<<k.world<<",\"request\":"<<k.request<<",\"native_frame\":"<<k.nativeFrame
     <<",\"device_epoch\":"<<k.deviceEpoch<<",\"channel\":"<<k.channel<<",\"tracking\":"<<k.tracking
     <<",\"space\":"<<k.space<<",\"predicted_ns\":"<<k.predictedNs<<",\"source_slot\":"<<k.sourceSlot
     <<",\"native_owner\":"<<k.nativeOwner<<",\"native_request\":"<<k.nativeRequest
     <<",\"main_view\":"<<k.mainView<<",\"right_view\":"<<k.rightView<<",\"width\":"<<k.width<<",\"height\":"<<k.height<<'}';
}
template<class T>void Values(std::ostream& o,const T& values){o<<'[';bool first=true;for(auto v:values){if(!first)o<<',';first=false;o<<v;}o<<']';}
void TargetSize(ID3D11View* view,unsigned& width,unsigned& height,unsigned& samples){
    if(!view)return;ComPtr<ID3D11Resource> r;view->GetResource(&r);ComPtr<ID3D11Texture2D> t;
    if(SUCCEEDED(r.As(&t))){D3D11_TEXTURE2D_DESC d{};t->GetDesc(&d);width=d.Width;height=d.Height;samples=d.SampleDesc.Count;}
}
}
struct D3D11SceneDrawWitness::Impl {
    std::atomic<DWORD> thread=0;std::atomic<unsigned> wrongOwner=0;
    ID3D11DeviceContext* context=nullptr; // exact owner; never used from writer
    bool stopped=false;unsigned bufferUsed=0,bufferCopies=0,omitted=0,errors=0,polls=0;
    SceneWitnessPairGate gate;
    std::array<Pair,SceneWitnessPairs> pairs{};
    std::vector<unsigned char> bufferBytes;
    Impl():bufferBytes(SceneWitnessBufferBudget){}
    bool Owns(ID3D11DeviceContext* c)noexcept {
        return c&&thread.load(std::memory_order_acquire)==GetCurrentThreadId()&&context==c;
    }
    void Stage(ID3D11DeviceContext* c,ID3D11Buffer* source,Buffer& b,Eye& eye){
        if(!source)return;D3D11_BUFFER_DESC d{};source->GetDesc(&d);b.id=Id(source);b.size=d.ByteWidth;
        if(!b.size||b.size>65536||eye.bufferCopies>=SceneWitnessEyeBufferCopies||
            b.size>SceneWitnessEyeBufferBudget-eye.bufferBytes||b.size>bufferBytes.size()-bufferUsed){b.omitted=true;++omitted;return;}
        b.offset=bufferUsed;bufferUsed+=b.size;++bufferCopies;eye.bufferBytes+=b.size;++eye.bufferCopies;
        D3D11_BUFFER_DESC staging{};staging.ByteWidth=b.size;staging.Usage=D3D11_USAGE_STAGING;staging.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        ComPtr<ID3D11Device> device;c->GetDevice(&device);b.result=device->CreateBuffer(&staging,nullptr,&b.staging);
        if(FAILED(b.result)){++errors;return;}c->CopyResource(b.staging.Get(),source);b.queued=true;
    }
    void PollOwned(ID3D11DeviceContext* c){
        // Budget per callback, including not-ready maps; never repeatedly spin
        // on a single resource. Subsequent native callbacks continue the work.
        unsigned attempts=0;const unsigned total=SceneWitnessPairs*2*SceneWitnessDrawsPerEye*28;
        unsigned visited=0;
        for(;visited<1024&&attempts<64;++visited){const auto at=(polls+visited)%total;
            auto& b=pairs[at/(2*SceneWitnessDrawsPerEye*28)].eyes[(at/(SceneWitnessDrawsPerEye*28))%2].draws[(at/28)%SceneWitnessDrawsPerEye].constants[at%28];
            if(!b.staging)continue;++attempts;D3D11_MAPPED_SUBRESOURCE m{};
            const auto hr=c->Map(b.staging.Get(),0,D3D11_MAP_READ,D3D11_MAP_FLAG_DO_NOT_WAIT,&m);
            if(hr==DXGI_ERROR_WAS_STILL_DRAWING)continue;b.result=hr;
            if(SUCCEEDED(hr)){std::memcpy(bufferBytes.data()+b.offset,m.pData,b.size);c->Unmap(b.staging.Get(),0);b.ready=true;}else ++errors;
            b.staging.Reset();
        }
        polls=(polls+visited)%total;
        for(auto& p:pairs)for(auto& e:p.eyes){auto& color=e.color;if(!color.staging)continue;D3D11_MAPPED_SUBRESOURCE m{};
            auto hr=c->Map(color.staging.Get(),0,D3D11_MAP_READ,D3D11_MAP_FLAG_DO_NOT_WAIT,&m);
            if(hr==DXGI_ERROR_WAS_STILL_DRAWING)continue;color.result=hr;
            if(SUCCEEDED(hr)){
                const auto row=std::size_t(p.key.width)*4;
                if(m.RowPitch<row){color.result=E_INVALIDARG;++errors;}
                else {for(unsigned y=0;y<p.key.height;++y)std::memcpy(color.bytes.data()+y*row,static_cast<const unsigned char*>(m.pData)+std::size_t(y)*m.RowPitch,row);color.ready=true;}
                c->Unmap(color.staging.Get(),0);
            }else ++errors;color.staging.Reset();
        }
    }
};
D3D11SceneDrawWitness::D3D11SceneDrawWitness()=default;
D3D11SceneDrawWitness::~D3D11SceneDrawWitness()=default;
D3D11SceneDrawWitness::Admission::Admission(D3D11SceneDrawWitness& o)noexcept:owner(o){
    if(!owner.accepting_.load(std::memory_order_seq_cst))return;
    owner.callbacks_.fetch_add(1,std::memory_order_seq_cst);
    if(owner.accepting_.load(std::memory_order_seq_cst))entered=true;else owner.callbacks_.fetch_sub(1,std::memory_order_seq_cst);
}
D3D11SceneDrawWitness::Admission::~Admission(){if(entered)owner.callbacks_.fetch_sub(1,std::memory_order_seq_cst);}
bool D3D11SceneDrawWitness::Enable(bool enabled)noexcept {
    if(!enabled||impl_)return false;
    try{impl_=std::make_unique<Impl>();accepting_.store(true,std::memory_order_seq_cst);return true;}catch(...){return false;}
}
bool D3D11SceneDrawWitness::Request(unsigned seq,unsigned firstDrawOrdinal)noexcept {
    if(!accepting_.load(std::memory_order_seq_cst)||!seq||seq>SceneWitnessPairs||firstDrawOrdinal>SceneWitnessMaxFirstOrdinal)return false;
    unsigned previous=seq-1;
    if(requested_.load(std::memory_order_acquire)!=previous)return false;
    if(previous&&completed_.load(std::memory_order_acquire)<previous&&failed_.load(std::memory_order_acquire)<previous)return false;
    if(previous&&firstDrawOrdinal_.load(std::memory_order_acquire)!=firstDrawOrdinal)return false;
    if(!previous)firstDrawOrdinal_.store(firstDrawOrdinal,std::memory_order_release);
    return requested_.compare_exchange_strong(previous,seq,std::memory_order_release);
}
void D3D11SceneDrawWitness::BeginEye(ID3D11DeviceContext* c,const SceneWitnessKey& key,unsigned eye,std::uint64_t view,std::span<const std::byte> expectedCamera)noexcept {
    Admission a(*this);if(!a.entered)return;auto& s=*impl_;
    DWORD zero=0;if(!c)return;
    if(s.thread.compare_exchange_strong(zero,GetCurrentThreadId(),std::memory_order_acq_rel))s.context=c;
    if(!s.Owns(c)){++s.wrongOwner;return;}if(c->GetType()!=D3D11_DEVICE_CONTEXT_IMMEDIATE)return;
    try{s.PollOwned(c);const auto seq=requested_.load(std::memory_order_acquire);
        if(!seq||seq<=completed_.load(std::memory_order_acquire)||seq<=failed_.load(std::memory_order_acquire))return;
        // Completed draw admission can wait for asynchronous private readback
        // across later native frames. Their identity must not fail this pair.
        if(seq==s.gate.Sequence()&&s.gate.Complete())return;
        if(!s.gate.Begin(seq,key,eye,view)){
            if(s.gate.Sequence()==seq&&(s.gate.Failed()||key!=s.gate.Key())){s.pairs[seq-1].failed=true;s.gate.Abandon();failed_.store(seq,std::memory_order_release);}return;
        }
        auto& pair=s.pairs[seq-1];pair.key=key;pair.eyes[eye].began=true;
        if(expectedCamera.size()==pair.eyes[eye].expectedCamera.size()){
            std::copy(expectedCamera.begin(),expectedCamera.end(),pair.eyes[eye].expectedCamera.begin());pair.eyes[eye].expectedCameraValid=true;
        }
    }catch(...){++s.errors;s.gate.Abandon();failed_.store(requested_.load(),std::memory_order_release);}
}
void D3D11SceneDrawWitness::Observe(ID3D11DeviceContext* c,const SceneWitnessDrawArguments& args)noexcept {
    Admission a(*this);if(!a.entered)return;auto& s=*impl_;if(!s.Owns(c)){++s.wrongOwner;return;}if(!s.gate.Active())return;
    auto& eye=s.pairs[s.gate.Sequence()-1].eyes[s.gate.Eye()];const auto ordinal=eye.observed++;
    if(ordinal<firstDrawOrdinal_.load(std::memory_order_acquire)){++eye.skipped;return;}
    if(eye.count==eye.draws.size()){++eye.overflow;return;}auto& d=eye.draws[eye.count++];d.args=args;d.ordinal=ordinal;
    try{
        ComPtr<ID3D11VertexShader> vs;ComPtr<ID3D11PixelShader> ps;ComPtr<ID3D11GeometryShader> gs;ComPtr<ID3D11HullShader> hs;ComPtr<ID3D11DomainShader> ds;
        c->VSGetShader(&vs,nullptr,nullptr);c->PSGetShader(&ps,nullptr,nullptr);c->GSGetShader(&gs,nullptr,nullptr);c->HSGetShader(&hs,nullptr,nullptr);c->DSGetShader(&ds,nullptr,nullptr);
        d.shaders={Id(vs.Get()),Id(ps.Get()),Id(gs.Get()),Id(hs.Get()),Id(ds.Get())};
        ComPtr<ID3D11InputLayout> layout;c->IAGetInputLayout(&layout);d.layout=Id(layout.Get());
        ComPtr<ID3D11Buffer> ib;DXGI_FORMAT format{};c->IAGetIndexBuffer(&ib,&format,&d.indexOffset);d.index=Id(ib.Get());d.indexFormat=format;
        D3D11_PRIMITIVE_TOPOLOGY topology{};c->IAGetPrimitiveTopology(&topology);d.topology=topology;
        ID3D11Buffer* vb[8]{};c->IAGetVertexBuffers(0,8,vb,d.strides.data(),d.offsets.data());for(unsigned i=0;i<8;++i){d.vertices[i]=Id(vb[i]);if(vb[i])vb[i]->Release();}
        ComPtr<ID3D11RenderTargetView> rt;ComPtr<ID3D11DepthStencilView> depth;c->OMGetRenderTargets(1,&rt,&depth);d.target=Id(rt.Get());d.depth=Id(depth.Get());
        TargetSize(rt.Get(),d.targetWidth,d.targetHeight,d.targetSamples);TargetSize(depth.Get(),d.depthWidth,d.depthHeight,d.depthSamples);
        ComPtr<ID3D11DepthStencilState> state;c->OMGetDepthStencilState(&state,&d.stencilRef);d.depthState=Id(state.Get());
        std::array<D3D11_VIEWPORT,D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE> viewports{};d.viewportCount=unsigned(viewports.size());c->RSGetViewports(&d.viewportCount,viewports.data());if(d.viewportCount)d.viewport=viewports[0];
        ID3D11Buffer* cb[28]{};c->VSGetConstantBuffers(0,14,cb);c->PSGetConstantBuffers(0,14,cb+14);
        // All references returned by Get are released even if allocation fails.
        std::array<ComPtr<ID3D11Buffer>,28> owned;for(unsigned i=0;i<28;++i)owned[i].Attach(cb[i]);
        for(unsigned i=0;i<28;++i)s.Stage(c,owned[i].Get(),d.constants[i],eye);
    }catch(...){++s.errors;}
}
void D3D11SceneDrawWitness::CaptureColor(ID3D11DeviceContext* c,ID3D11Texture2D* texture,const SceneWitnessKey& key,unsigned eye)noexcept {
    Admission a(*this);if(!a.entered)return;auto& s=*impl_;if(!s.Owns(c)){++s.wrongOwner;return;}
    const auto seq=s.gate.Sequence();if(!seq||eye>1||key!=s.gate.Key()||s.gate.Failed())return;
    auto& e=s.pairs[seq-1].eyes[eye];if(!e.began||!e.ended||e.color.queued||!texture)return;
    auto& color=e.color;color.source=Id(texture);D3D11_TEXTURE2D_DESC desc{};texture->GetDesc(&desc);color.format=desc.Format;
    ComPtr<ID3D11Device> actual,expected;texture->GetDevice(&actual);c->GetDevice(&expected);
    if(actual.Get()!=expected.Get()||desc.Width!=key.width||desc.Height!=key.height||desc.ArraySize!=1||desc.MipLevels!=1||desc.SampleDesc.Count!=1||
       (desc.Format!=DXGI_FORMAT_R8G8B8A8_UNORM&&desc.Format!=DXGI_FORMAT_R8G8B8A8_UNORM_SRGB&&desc.Format!=DXGI_FORMAT_B8G8R8A8_UNORM&&desc.Format!=DXGI_FORMAT_B8G8R8A8_UNORM_SRGB)){
        s.pairs[seq-1].failed=true;failed_.store(seq,std::memory_order_release);return;
    }
    try{color.bytes.resize(std::size_t(key.width)*key.height*4);desc.Usage=D3D11_USAGE_STAGING;desc.BindFlags=0;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;desc.MiscFlags=0;
        color.result=expected->CreateTexture2D(&desc,nullptr,&color.staging);if(FAILED(color.result)){++s.errors;s.pairs[seq-1].failed=true;failed_.store(seq,std::memory_order_release);return;}
        c->CopyResource(color.staging.Get(),texture);color.queued=true;
    }catch(...){++s.errors;s.pairs[seq-1].failed=true;failed_.store(seq,std::memory_order_release);}
}
void D3D11SceneDrawWitness::EndEye(ID3D11DeviceContext* c,const SceneWitnessKey& key,unsigned eye)noexcept {
    Admission a(*this);if(!a.entered)return;auto& s=*impl_;if(!s.Owns(c)){++s.wrongOwner;return;}
    if(!s.gate.Active())return;const auto seq=s.gate.Sequence();
    if(s.gate.End(key,eye))s.pairs[seq-1].eyes[eye].ended=true;
    else {s.pairs[seq-1].failed=true;failed_.store(seq,std::memory_order_release);}
}
void D3D11SceneDrawWitness::Poll(ID3D11DeviceContext* c)noexcept {
    Admission a(*this);if(!a.entered)return;auto& s=*impl_;if(!s.Owns(c))return;
    try{s.PollOwned(c);const auto seq=s.gate.Sequence();if(seq&&s.gate.Complete()){
        const auto& p=s.pairs[seq-1];bool ready=!p.failed;
        for(const auto& e:p.eyes){ready=ready&&e.color.ready;for(unsigned n=0;n<e.count;++n)for(const auto& b:e.draws[n].constants)ready=ready&&!b.staging;}
        if(ready)completed_.store(seq,std::memory_order_release);
    }}catch(...){++s.errors;}
}
bool D3D11SceneDrawWitness::Stop()noexcept {
    accepting_.store(false,std::memory_order_seq_cst);if(callbacks_.load(std::memory_order_seq_cst))return false;
    if(!impl_)return true;auto& s=*impl_;s.stopped=true;s.context=nullptr;
    // Do not Release graphics interfaces on this worker: BC2 can create a
    // single-threaded device. Poll already releases completed private copies
    // on the immediate-context owner. Unfinished private staging references
    // remain bounded and quarantined in the game-lifetime module; they hold no
    // native target, request, bridge slot or shared-source retirement lease.
    return true;
}
bool D3D11SceneDrawWitness::Write(const std::filesystem::path& directory)const noexcept {
    if(!impl_)return true;if(accepting_.load(std::memory_order_seq_cst)||callbacks_.load(std::memory_order_seq_cst)||!impl_->stopped)return false;
    try{const auto& s=*impl_;std::ostringstream o;unsigned pendingBuffers=0,pendingColors=0;
        for(const auto& p:s.pairs)for(const auto& e:p.eyes){pendingColors+=bool(e.color.staging);for(const auto& d:e.draws)for(const auto& b:d.constants)pendingBuffers+=bool(b.staging);}
        o<<"{\"schema\":\"fvr.graphics.scene-draw-witness.v1\",\"read_only\":true,\"destruction_association_proven\":false,\"gs_hs_ds_constants_captured\":false,\"constant_subranges_captured\":false,\"instance_payload_captured\":false"
         <<",\"requested\":"<<requested_.load()<<",\"completed\":"<<completed_.load()<<",\"failed\":"<<failed_.load()<<",\"wrong_owner\":"<<s.wrongOwner.load()<<",\"errors\":"<<s.errors
         <<",\"pending_buffers_retained_until_game_exit\":"<<pendingBuffers<<",\"pending_colors_retained_until_game_exit\":"<<pendingColors
         <<",\"buffer_bytes\":"<<s.bufferUsed<<",\"buffer_copies\":"<<s.bufferCopies<<",\"buffer_copy_limit_per_eye\":"<<SceneWitnessEyeBufferCopies
         <<",\"buffer_bytes_limit_per_eye\":"<<SceneWitnessEyeBufferBudget<<",\"omitted_buffers\":"<<s.omitted<<",\"first_draw_ordinal\":"<<firstDrawOrdinal_.load()<<",\"draw_limit_per_eye\":"<<SceneWitnessDrawsPerEye<<",\"pairs\":[";
        for(unsigned p=0;p<requested_.load();++p){if(p)o<<',';const auto& pair=s.pairs[p];o<<"{\"sequence\":"<<p+1<<",\"failed\":"<<pair.failed<<",\"key\":";KeyJson(o,pair.key);o<<",\"eyes\":[";
            for(unsigned e=0;e<2;++e){if(e)o<<',';const auto& eye=pair.eyes[e];o<<"{\"eye\":"<<e<<",\"began\":"<<eye.began<<",\"ended\":"<<eye.ended<<",\"observed_draws\":"<<eye.observed<<",\"overflow\":"<<eye.overflow
                <<",\"skipped_prefix\":"<<eye.skipped<<",\"buffer_bytes\":"<<eye.bufferBytes<<",\"buffer_copies\":"<<eye.bufferCopies
                <<",\"expected_camera_valid\":"<<eye.expectedCameraValid<<",\"expected_camera_hex\":\"";
                constexpr char hex[]="0123456789abcdef";if(eye.expectedCameraValid)for(auto b:eye.expectedCamera){auto v=std::to_integer<unsigned>(b);o<<hex[v>>4]<<hex[v&15];}
                o<<'"'
                <<",\"color\":{\"source\":"<<eye.color.source<<",\"queued\":"<<eye.color.queued<<",\"ready\":"<<eye.color.ready<<",\"format\":"<<eye.color.format<<",\"result\":"<<eye.color.result<<"},\"draws\":[";
                for(unsigned n=0;n<eye.count;++n){if(n)o<<',';const auto& d=eye.draws[n];const auto& a=d.args;
                    o<<"{\"ordinal\":"<<d.ordinal<<",\"kind\":"<<unsigned(a.kind)<<",\"count\":"<<a.count<<",\"instances\":"<<a.instances<<",\"start\":"<<a.start<<",\"base\":"<<a.base<<",\"first_instance\":"<<a.firstInstance<<",\"argument_buffer\":"<<a.argumentBuffer<<",\"argument_offset\":"<<a.argumentOffset
                     <<",\"shaders\":";Values(o,d.shaders);o<<",\"layout\":"<<d.layout<<",\"index\":"<<d.index<<",\"index_format\":"<<d.indexFormat<<",\"index_offset\":"<<d.indexOffset<<",\"vertices\":";Values(o,d.vertices);o<<",\"strides\":";Values(o,d.strides);o<<",\"offsets\":";Values(o,d.offsets);
                    o<<",\"topology\":"<<d.topology<<",\"target\":"<<d.target<<",\"depth\":"<<d.depth<<",\"depth_state\":"<<d.depthState<<",\"stencil_ref\":"<<d.stencilRef
                     <<",\"target_size\":["<<d.targetWidth<<','<<d.targetHeight<<','<<d.targetSamples<<"],\"depth_size\":["<<d.depthWidth<<','<<d.depthHeight<<','<<d.depthSamples
                     <<"],\"viewport_count\":"<<d.viewportCount<<",\"viewport\":["<<d.viewport.TopLeftX<<','<<d.viewport.TopLeftY<<','<<d.viewport.Width<<','<<d.viewport.Height<<','<<d.viewport.MinDepth<<','<<d.viewport.MaxDepth<<"],\"constants\":[";
                    for(unsigned b=0;b<28;++b){if(b)o<<',';const auto& cb=d.constants[b];o<<"{\"id\":"<<cb.id<<",\"size\":"<<cb.size<<",\"offset\":"<<cb.offset<<",\"queued\":"<<cb.queued<<",\"ready\":"<<cb.ready<<",\"omitted\":"<<cb.omitted<<",\"result\":"<<cb.result<<'}';}o<<"]}";
                }o<<"]}";
            }o<<"]}";
        }o<<"]}\n";const auto text=o.str();if(text.size()>SceneWitnessMetadataBudget)return false;
        std::filesystem::create_directories(directory);
        // Never replace prior evidence, including on a second Write invocation.
        if(std::filesystem::exists(directory/"witness.json")||std::filesystem::exists(directory/"buffers.bin"))return false;
        std::ofstream buffers(directory/"buffers.bin",std::ios::binary);buffers.write(reinterpret_cast<const char*>(s.bufferBytes.data()),s.bufferUsed);buffers.close();if(!buffers)return false;
        for(unsigned p=0;p<requested_.load();++p)for(unsigned e=0;e<2;++e){const auto& color=s.pairs[p].eyes[e].color;if(!color.ready)continue;
            const auto name="pair-"+std::to_string(p+1)+"-eye-"+std::to_string(e)+".raw";if(std::filesystem::exists(directory/name))return false;
            std::ofstream file(directory/name,std::ios::binary);file.write(reinterpret_cast<const char*>(color.bytes.data()),std::streamsize(color.bytes.size()));file.close();if(!file)return false;
        }
        std::ofstream metadata(directory/"witness.json",std::ios::binary);metadata<<text;metadata.close();return bool(metadata);
    }catch(...){return false;}
}
}
