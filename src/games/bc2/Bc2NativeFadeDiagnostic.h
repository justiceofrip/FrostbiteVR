#pragma once
// Opt-in bounded observation. No native calls, field writes or GPU commands.
#include <Windows.h>
#include <d3d11.h>
#include <wrl/client.h>
#include <array>
#include <atomic>
#include <cstdint>
#include <charconv>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <ostream>
#include <string>

namespace fvr::bc2::fadeDiagnostic {
using Read = bool(*)(std::uintptr_t,void*,std::size_t) noexcept;
enum class Phase : unsigned { WorldBefore, PrepareBefore, PrepareAfter, DrawBefore,
    DrawAfter, CaptureSource, WorldAfter };
inline const char* Name(Phase p) noexcept {
    switch(p){case Phase::WorldBefore:return "world_before";
    case Phase::PrepareBefore:return "prepare_before";
    case Phase::PrepareAfter:return "prepare_after";
    case Phase::DrawBefore:return "draw_before";
    case Phase::DrawAfter:return "draw_after";
    case Phase::CaptureSource:return "capture_source";
    case Phase::WorldAfter:return "world_after";}return "invalid";
}
// Reject x86 address arithmetic wrapping before any caller-provided memory reader.
inline bool ReadField(Read read,std::uintptr_t base,unsigned offset,void* out,std::size_t bytes) noexcept {
    if(!read||!base||base>UINT32_MAX||offset>UINT32_MAX-base)return false;
    const auto address=base+offset;
    return bytes&&bytes-1<=UINT32_MAX-address&&read(address,out,bytes);
}
enum class ContextStatus : unsigned { Ready, BootstrapUnavailable, DifferentThread, DifferentContext };
inline ContextStatus CheckContext(bool ready,DWORD owner,DWORD current,
    std::uintptr_t expected,std::uintptr_t actual) noexcept {
    if(!ready||!owner||!expected)return ContextStatus::BootstrapUnavailable;
    if(owner!=current)return ContextStatus::DifferentThread;
    return actual==expected?ContextStatus::Ready:ContextStatus::DifferentContext;
}
struct Key {
    std::uintptr_t world=0,request=0,view=0;
    std::array<std::uintptr_t,2> candidates{};
    unsigned candidateCount=0;bool candidatesAreStereoPair=false;
    DWORD graphicsThread=0;ContextStatus contextStatus=ContextStatus::BootstrapUnavailable;
    std::uint64_t pairRequest=0,tracking=0,space=0,leaseFrame=0;
};
struct View {
    std::uintptr_t address=0;unsigned vtable=0,owner=0;
    bool identity=false,scaleRead=false,effectRead=false;
    std::array<unsigned,3> scaleBits{};std::array<unsigned,4> effectBits{};
};
struct Target {
    bool contextRead=false,hasRtv=false,hasTexture=false;
    std::uintptr_t context=0,rtv=0,resource=0,texture=0,captureSource=0;
    D3D11_TEXTURE2D_DESC shape{};D3D11_RENDER_TARGET_VIEW_DESC view{};
};
struct Row {
    std::atomic<bool> ready=false;
    std::uint64_t tick=0,qpc=0;DWORD thread=0;Phase phase{};
    Key key{};unsigned worldFrame=0,requestState=0;bool worldRead=false,requestRead=false;
    std::array<View,2> views{};Target target{};
};
class Observer {
public:
    // 391 observed ~942 brackets/sec. 32768 retains a twelve-second window
    // with >2.8x that average; overflow still invalidates complete coverage.
    static constexpr unsigned Capacity=32768;
    static constexpr unsigned MaxDurationMs=12000;
    static bool VerifyGetters(std::uintptr_t vtable,Read read) noexcept {
        unsigned scale=0,effect=0;
        if(!ReadField(read,vtable,0x58,&scale,4)||!ReadField(read,vtable,0xd8,&effect,4))return false;
        // Observed x86 getter bodies, validated before using their read-only field ranges.
        constexpr unsigned char a[]={0x8d,0x81,0x30,0x15,0,0,0xc3};
        constexpr unsigned char b[]={0x8b,0x44,0x24,4,0xd9,0x81,0x50,0x16,0,0,0xd9,0x18,
            0xd9,0x81,0x54,0x16,0,0,0xd9,0x58,4,0xd9,0x81,0x58,0x16,0,0,0xd9,0x58,8,
            0xd9,0x81,0x5c,0x16,0,0,0xd9,0x58,0x0c,0xc2,4,0};
        std::array<unsigned char,sizeof(a)> actualA{};
        std::array<unsigned char,sizeof(b)> actualB{};
        return ReadField(read,scale,0,actualA.data(),actualA.size())&&ReadField(read,effect,0,actualB.data(),actualB.size())&&
            !std::memcmp(a,actualA.data(),sizeof(a))&&!std::memcmp(b,actualB.data(),sizeof(b));
    }
    // Configure once, before hooks. Absolute system tick lets the receiver share this window.
    bool Configure(std::uint64_t start,unsigned duration,unsigned limit,std::uintptr_t vtable,Read read) {
        if(rows_||!start||!duration||duration>MaxDurationMs||!limit||limit>Capacity||
            start>UINT64_MAX-duration||!VerifyGetters(vtable,read))return false;
        rows_=std::make_unique<Row[]>(limit);start_=start;duration_=duration;limit_=limit;
        vtable_=vtable;read_=read;return true;
    }
    bool ConfigureFile(const std::filesystem::path& file,std::uintptr_t vtable,Read read) {
        if(!std::filesystem::exists(file))return true; // Absent means disabled.
        if(!std::filesystem::is_regular_file(file)||std::filesystem::file_size(file)>128)return false;
        std::ifstream input(file);std::array<std::uint64_t,3> values{};std::string token;
        for(auto& value:values){
            if(!(input>>token))return false;
            const auto parsed=std::from_chars(token.data(),token.data()+token.size(),value);
            if(parsed.ec!=std::errc{}||parsed.ptr!=token.data()+token.size())return false;
        }
        if(input>>token)return false;
        const auto now=GetTickCount64(),start=values[0];
        if(start<now||start-now>60000||values[1]>MaxDurationMs||values[2]>Capacity)return false;
        return Configure(start,unsigned(values[1]),unsigned(values[2]),vtable,read);
    }
    bool Due(std::uint64_t tick) const noexcept {
        return rows_&&tick>=start_&&tick-start_<duration_;
    }
    bool Enabled() const noexcept {return bool(rows_);}
    // One immutable slot per call. Overflow is reported; no wrapping/stale reuse or hook-side I/O.
    void Sample(Phase phase,const Key& key,ID3D11DeviceContext* context=nullptr,
        ID3D11RenderTargetView* selected=nullptr,ID3D11Texture2D* capture=nullptr) noexcept {
        const auto tick=GetTickCount64();if(!Due(tick))return;
        SampleAt(tick,phase,key,context,selected,capture);
    }
    void SampleAt(std::uint64_t tick,Phase phase,const Key& key,ID3D11DeviceContext* context=nullptr,
        ID3D11RenderTargetView* selected=nullptr,ID3D11Texture2D* capture=nullptr) noexcept {
        if(!Due(tick))return;
        // Saturate at limit+1: overflow is sticky, with no wrap or stale-slot reuse.
        unsigned index=allocated_.load(std::memory_order_relaxed);
        do {if(index>limit_)return;}
        while(!allocated_.compare_exchange_weak(index,index+1,std::memory_order_relaxed));
        if(index==limit_)return;
        auto& r=rows_[index];r.tick=tick;r.phase=phase;r.key=key;r.thread=GetCurrentThreadId();
        LARGE_INTEGER q{};QueryPerformanceCounter(&q);r.qpc=q.QuadPart;
        r.worldRead=ReadField(read_,key.world,0x88,&r.worldFrame,4);
        r.requestRead=ReadField(read_,key.request,0xc4,&r.requestState,4);
        for(unsigned eye=0;eye<2;++eye){
            auto& v=r.views[eye];v.address=key.candidates[eye];if(!v.address)continue;
            v.identity=ReadField(read_,v.address,0,&v.vtable,4)&&v.vtable==vtable_&&
                ReadField(read_,v.address,0x70,&v.owner,4)&&v.owner==key.request;
            if(!v.identity)continue;
            v.scaleRead=ReadField(read_,v.address,0x1530,v.scaleBits.data(),sizeof(v.scaleBits));
            v.effectRead=ReadField(read_,v.address,0x1650,v.effectBits.data(),sizeof(v.effectBits));
            unsigned table=0,owner=0;
            v.identity=ReadField(read_,v.address,0,&table,4)&&table==v.vtable&&
                ReadField(read_,v.address,0x70,&owner,4)&&owner==v.owner;
            if(!v.identity)v.scaleRead=v.effectRead=false;
        }
        // Context is supplied only on the existing verified rendering thread.
        // Get/description calls only: no Map, readback, draw, dispatch, copy or Flush.
        if(context&&key.contextStatus==ContextStatus::Ready){
            auto& t=r.target;t.contextRead=true;t.context=reinterpret_cast<std::uintptr_t>(context);
            Microsoft::WRL::ComPtr<ID3D11RenderTargetView> rtv;
            if(selected)rtv=selected;else context->OMGetRenderTargets(1,&rtv,nullptr);
            t.captureSource=reinterpret_cast<std::uintptr_t>(capture);
            if(rtv){
                t.hasRtv=true;t.rtv=reinterpret_cast<std::uintptr_t>(rtv.Get());rtv->GetDesc(&t.view);
                Microsoft::WRL::ComPtr<ID3D11Resource> resource;rtv->GetResource(&resource);
                t.resource=reinterpret_cast<std::uintptr_t>(resource.Get());
                Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
                if(SUCCEEDED(resource.As(&texture))){
                    t.hasTexture=true;t.texture=reinterpret_cast<std::uintptr_t>(texture.Get());
                    texture->GetDesc(&t.shape);
                }
            }
        }
        r.ready.store(true,std::memory_order_release);
    }
    void Write(std::ostream& out) const {
        LARGE_INTEGER f{};QueryPerformanceFrequency(&f);
        out<<"{\"schema\":\"fvr.bc2.native_fade_observation.v1\",\"enabled\":"<<(Enabled()?"true":"false")
           <<",\"start_tick_ms\":"<<start_<<",\"duration_ms\":"<<duration_<<",\"limit\":"<<limit_
           <<",\"attempted_lower_bound\":"<<allocated_.load()<<",\"overflow\":"<<(allocated_.load()>limit_?"true":"false")
           <<",\"allocated_bytes\":"<<std::size_t(limit_)*sizeof(Row)
           <<",\"qpc_frequency\":"<<f.QuadPart
           <<",\"callback_timing\":\"brackets_only_no_callback_hooks\",\"state_semantics\":\"raw_verified_getter_fields_not_proven_fade\"}\n";
        for(unsigned i=0;i<limit_;++i){
            const auto& r=rows_[i];if(!r.ready.load(std::memory_order_acquire))continue;
            const auto& t=r.target;const auto& d=t.shape;
            out<<"{\"index\":"<<i<<",\"tick_ms\":"<<r.tick<<",\"qpc\":"<<r.qpc<<",\"thread\":"<<r.thread
               <<",\"graphics_thread\":"<<r.key.graphicsThread<<",\"context_status\":"<<unsigned(r.key.contextStatus)
               <<",\"phase\":\""<<Name(r.phase)<<"\",\"world\":"<<r.key.world<<",\"request\":"<<r.key.request
               <<",\"view\":"<<r.key.view<<",\"world_frame\":"<<r.worldFrame<<",\"world_read\":"<<r.worldRead
               <<",\"request_state\":"<<r.requestState<<",\"request_read\":"<<r.requestRead
               <<",\"pair_request\":"<<r.key.pairRequest<<",\"tracking\":"<<r.key.tracking<<",\"space\":"<<r.key.space
               <<",\"lease_frame\":"<<r.key.leaseFrame<<",\"candidate_count\":"<<r.key.candidateCount
               <<",\"candidates_are_stereo_pair\":"<<r.key.candidatesAreStereoPair<<",\"views\":[";
            for(unsigned eye=0;eye<2;++eye){const auto& v=r.views[eye];if(eye)out<<',';
                out<<"{\"address\":"<<v.address<<",\"vtable\":"<<v.vtable<<",\"owner\":"<<v.owner
                   <<",\"identity\":"<<v.identity<<",\"scale_read\":"<<v.scaleRead<<",\"effect_read\":"<<v.effectRead
                   <<",\"scale_bits\":[";for(unsigned n=0;n<3;++n){if(n)out<<',';out<<v.scaleBits[n];}
                out<<"],\"effect_bits\":[";for(unsigned n=0;n<4;++n){if(n)out<<',';out<<v.effectBits[n];}out<<"]}";}
            out<<"],\"om\":{\"read\":"<<t.contextRead<<",\"context\":"<<t.context<<",\"has_rtv\":"<<t.hasRtv
               <<",\"rtv\":"<<t.rtv<<",\"resource\":"<<t.resource<<",\"has_texture\":"<<t.hasTexture
               <<",\"texture\":"<<t.texture<<",\"capture_source\":"<<t.captureSource
               <<",\"width\":"<<d.Width<<",\"height\":"<<d.Height<<",\"format\":"<<d.Format
               <<",\"mips\":"<<d.MipLevels<<",\"array_size\":"<<d.ArraySize<<",\"samples\":"<<d.SampleDesc.Count
               <<",\"sample_quality\":"<<d.SampleDesc.Quality<<",\"usage\":"<<d.Usage<<",\"bind\":"<<d.BindFlags
               <<",\"cpu_access\":"<<d.CPUAccessFlags<<",\"misc\":"<<d.MiscFlags
               <<",\"rtv_format\":"<<t.view.Format<<",\"rtv_dimension\":"<<t.view.ViewDimension<<",\"rtv_descriptor_words\":[";
            std::array<unsigned,sizeof(D3D11_RENDER_TARGET_VIEW_DESC)/4> words{};
            std::memcpy(words.data(),&t.view,sizeof(t.view));
            for(unsigned n=0;n<words.size();++n){if(n)out<<',';out<<words[n];}
            out<<"]}}\n";
        }
    }
private:
    std::unique_ptr<Row[]> rows_;std::atomic<unsigned> allocated_=0;
    std::uint64_t start_=0;unsigned duration_=0,limit_=0;std::uintptr_t vtable_=0;Read read_=nullptr;
};
} // namespace fvr::bc2::fadeDiagnostic
