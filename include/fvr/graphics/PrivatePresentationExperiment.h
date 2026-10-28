#pragma once
#include "D3D11RenderQueueLimit.h"
#include "FrameBridgePresentService.h"
#include <array>
#include <atomic>
#include <cstddef>
#include <cwchar>

namespace fvr::graphics {
enum class PresentationExperimentFailure:unsigned {
    None,Token,Events,Owner,OtherLimiter,Device,Read,QueueChanged,Apply,Restore,Clock,Poll,Ack
};
struct PresentationExperimentRow {
    unsigned phase=0,queueValue=0;
    std::int64_t beginQpc=0;
    bool afterEnabled=false,applied=false,acknowledged=false;
    PresentationExperimentFailure failure=PresentationExperimentFailure::None;
};
struct PresentationExperimentSnapshot {
    bool configured=false,enabled=false,eventsReady=false,ownerPinned=false,complete=false,closed=false,restored=false;
    unsigned phase=0,originalQueue=0,observedQueue=0,polls=0,noEvents=0,requests=0,acks=0,rows=0,ownerMismatch=0;
    std::int64_t qpcFrequency=0;
    FrameBridgePresentIdentity identity{};
    PresentationExperimentFailure failure=PresentationExperimentFailure::None;
    std::array<PresentationExperimentRow,5> phases{};
};
// Configure is completed on the setup worker before callback publication.
// Tick/Close are graphics-owner operations. Only published phase/failure may be
// read by another callback; full evidence is read after callbacks have drained.
template<class Backend> class BasicPrivatePresentationExperiment {
    Backend& backend_;
    struct QueueBackend {
        using Device=typename Backend::Device;
        Backend& backend;unsigned lastRead=0;
        unsigned Thread()const noexcept{return backend.Thread();}
        HRESULT Open(Device device)noexcept{return backend.Open(device);}
        HRESULT Read(unsigned& value)noexcept {const auto hr=backend.Read(value);if(SUCCEEDED(hr))lastRead=value;return hr;}
        HRESULT Write(unsigned value)noexcept {
            // BasicRenderQueueLimit reads its original immediately before the
            // setter. A changed original cannot authorize the experiment's C.
            if(value==1&&lastRead!=3)return E_UNEXPECTED;
            return backend.Write(value);
        }
        void Release()noexcept{backend.Release();}
    } queueBackend_;
    BasicRenderQueueLimit<QueueBackend> limiter_;
    PresentationExperimentSnapshot state_{};
    std::atomic<unsigned> phase_{0},failure_{0},ownerMismatch_{0},ownerThread_{0};
    std::atomic<bool> closed_{false};
    void Fail(PresentationExperimentFailure value)noexcept {
        unsigned empty=0;failure_.compare_exchange_strong(empty,unsigned(value),std::memory_order_release);
    }
    bool Failed()const noexcept{return failure_.load(std::memory_order_acquire)!=0;}
    bool Owner(typename Backend::Device device,const FrameBridgePresentIdentity& identity)noexcept {
        if(backend_.Thread()!=state_.identity.thread||identity!=state_.identity||
           Backend::Address(device)!=state_.identity.device){
            ownerMismatch_.fetch_add(1,std::memory_order_relaxed);Fail(PresentationExperimentFailure::Owner);return false;
        }
        return true;
    }
    bool OpenRead(typename Backend::Device device,unsigned& value)noexcept {
        if(FAILED(backend_.Open(device))){Fail(PresentationExperimentFailure::Device);return false;}
        if(FAILED(backend_.Read(value))){Fail(PresentationExperimentFailure::Read);return false;}
        state_.observedQueue=value;return true;
    }
    static bool Eligible(const FrameBridgePresentIdentity& expected,const FrameBridgePresentBoundary& b)noexcept {
        return expected.Valid()&&b.current==expected&&b.presentedSwapchain==expected.swapchain&&
            b.enabled&&b.lifetimeHeld&&b.bridgeOwnerCurrent&&b.connected&&!b.fatal&&!b.test&&
            !b.drawingNativeView&&b.nativeStateRestored;
    }
    void RowFailure(PresentationExperimentRow& row,PresentationExperimentFailure failure)noexcept {
        row.failure=failure;Fail(failure);
    }
public:
    explicit BasicPrivatePresentationExperiment(Backend& backend)noexcept:backend_(backend),queueBackend_{backend},limiter_(queueBackend_){}
    void Configure(const wchar_t* token,std::size_t length,bool present)noexcept {
        if(state_.configured)return;
        state_.configured=true;state_.enabled=present;if(!present)return;
        bool valid=token&&length==32,nonzero=false;
        if(valid)for(unsigned n=0;n<32;++n){const auto c=token[n];valid&=(c>=L'0'&&c<=L'9')||(c>=L'a'&&c<=L'f');nonzero|=c!=L'0';}
        if(!valid||!nonzero){Fail(PresentationExperimentFailure::Token);return;}
        state_.eventsReady=backend_.OpenEvents(token);
        if(!state_.eventsReady)Fail(PresentationExperimentFailure::Events);
    }
    bool Enabled()const noexcept{return state_.enabled;}
    unsigned Phase()const noexcept{return phase_.load(std::memory_order_acquire);}
    bool AfterEnabled()const noexcept {
        if(!Enabled())return true;
        const auto phase=Phase();return !closed_.load(std::memory_order_acquire)&&!Failed()&&(phase==2||phase==3);
    }
    void Tick(typename Backend::Device device,const FrameBridgePresentIdentity& expected,
              const FrameBridgePresentBoundary& boundary,RenderQueueMode otherLimiter)noexcept {
        if(!Enabled()||Failed())return;
        const auto thread=backend_.Thread(),pinned=ownerThread_.load(std::memory_order_acquire);
        if(thread!=expected.thread||(pinned&&thread!=pinned)){
            ownerMismatch_.fetch_add(1,std::memory_order_relaxed);Fail(PresentationExperimentFailure::Owner);return;
        }
        if(!pinned){
            if(!Eligible(expected,boundary)||Backend::Address(device)!=expected.device)return;
            unsigned absent=0;
            if(!ownerThread_.compare_exchange_strong(absent,thread,std::memory_order_acq_rel)&&absent!=thread){
                Fail(PresentationExperimentFailure::Owner);return;
            }
        }
        if(state_.closed)return;
        if(state_.ownerPinned&&!Owner(device,expected))return;
        if(state_.ownerPinned&&boundary.current!=state_.identity){Fail(PresentationExperimentFailure::Owner);return;}
        if(!Eligible(expected,boundary))return;
        if(backend_.Thread()!=expected.thread||Backend::Address(device)!=expected.device){Fail(PresentationExperimentFailure::Owner);return;}
        if(otherLimiter==RenderQueueMode::LimitOne){Fail(PresentationExperimentFailure::OtherLimiter);return;}
        if(!state_.ownerPinned){
            state_.identity=expected;state_.ownerPinned=true;
            state_.qpcFrequency=backend_.Frequency();
            if(state_.qpcFrequency<=0){Fail(PresentationExperimentFailure::Clock);return;}
            if(!OpenRead(device,state_.originalQueue))return;
            if(state_.originalQueue!=3){Fail(PresentationExperimentFailure::QueueChanged);return;}
        }
        ++state_.polls;const auto event=backend_.PollRequest();
        if(event==0){++state_.noEvents;return;}
        if(event<0){Fail(PresentationExperimentFailure::Poll);return;}
        ++state_.requests;
        const auto next=Phase()+1;
        if(next>5||state_.rows>=state_.phases.size())return;
        auto& row=state_.phases[state_.rows++];row.phase=next;row.afterEnabled=next==2||next==3;
        unsigned current=0;
        if(!OpenRead(device,current)){row.failure=PresentationExperimentFailure(failure_.load());return;}
        if(current!=(Phase()==3?1u:3u)){RowFailure(row,PresentationExperimentFailure::QueueChanged);return;}
        if(next==3){
            limiter_.Configure(device,RenderQueueMode::LimitOne);
            const auto& q=limiter_.Snapshot();state_.observedQueue=q.observed;
            if(!q.available||!q.applied||q.original!=3||q.observed!=1||q.ownerThread!=expected.thread){RowFailure(row,PresentationExperimentFailure::Apply);return;}
        }else if(next==4){
            limiter_.Close();const auto& q=limiter_.Snapshot();state_.observedQueue=q.observed;
            if(!q.closed||!q.restored||q.changedExternally||q.observed!=3){RowFailure(row,PresentationExperimentFailure::Restore);return;}
            state_.restored=true;
        }
        const auto begin=backend_.Qpc();
        if(begin<=0||(state_.rows>1&&begin<=state_.phases[state_.rows-2].beginQpc)){RowFailure(row,PresentationExperimentFailure::Clock);return;}
        row.beginQpc=begin;row.queueValue=state_.observedQueue;row.applied=true;
        if(next==5){backend_.Release();state_.complete=true;state_.restored=true;}
        row.acknowledged=backend_.Acknowledge();
        if(row.acknowledged)++state_.acks;else RowFailure(row,PresentationExperimentFailure::Ack);
        if(next==5){backend_.CloseEvents();state_.closed=true;closed_.store(true,std::memory_order_release);}
        phase_.store(next,std::memory_order_release);
    }
    void Close(typename Backend::Device device,const FrameBridgePresentIdentity& identity)noexcept {
        if(!Enabled())return;
        const auto pinned=ownerThread_.load(std::memory_order_acquire);
        if(pinned&&backend_.Thread()!=pinned){ownerMismatch_.fetch_add(1,std::memory_order_relaxed);Fail(PresentationExperimentFailure::Owner);return;}
        if(state_.closed)return;
        if(state_.ownerPinned&&!Owner(device,identity))return;
        const auto& before=limiter_.Snapshot();
        if(before.attempted&&!before.closed){
            limiter_.Close();const auto& q=limiter_.Snapshot();state_.observedQueue=q.observed;
            if(!q.closed){Fail(PresentationExperimentFailure::Restore);return;}
            if(!q.restored||q.changedExternally){Fail(PresentationExperimentFailure::QueueChanged);state_.restored=false;}
        }
        if(state_.ownerPinned){
            unsigned current=0;
            const bool read=OpenRead(device,current);
            state_.restored=read&&(!state_.originalQueue||current==state_.originalQueue);
            if(read&&!state_.restored)Fail(PresentationExperimentFailure::QueueChanged);
        }else state_.restored=true; // No device was admitted and no native write occurred.
        backend_.Release();backend_.CloseEvents();state_.closed=true;closed_.store(true,std::memory_order_release);
    }
    PresentationExperimentSnapshot Snapshot()const noexcept {
        auto result=state_;result.phase=Phase();result.failure=PresentationExperimentFailure(failure_.load(std::memory_order_acquire));
        result.ownerMismatch=ownerMismatch_.load(std::memory_order_acquire);return result;
    }
    const RenderQueueSnapshot& QueueSnapshot()const noexcept{return limiter_.Snapshot();}
};

struct PresentationExperimentBackend {
    using Device=ID3D11Device*;
    Microsoft::WRL::ComPtr<IDXGIDevice1> dxgi;
    Device device=nullptr;HANDLE request=nullptr,ack=nullptr;
    static std::uintptr_t Address(Device value)noexcept{return reinterpret_cast<std::uintptr_t>(value);}
    unsigned Thread()const noexcept{return GetCurrentThreadId();}
    std::int64_t Frequency()const noexcept{LARGE_INTEGER v{};return QueryPerformanceFrequency(&v)?v.QuadPart:0;}
    std::int64_t Qpc()const noexcept{LARGE_INTEGER v{};return QueryPerformanceCounter(&v)?v.QuadPart:0;}
    bool OpenEvents(const wchar_t* token)noexcept {
        wchar_t name[96]{};swprintf_s(name,L"Local\\FVR.Perf436.%.32ls.request",token);
        request=OpenEventW(SYNCHRONIZE,FALSE,name);
        swprintf_s(name,L"Local\\FVR.Perf436.%.32ls.ack",token);ack=OpenEventW(EVENT_MODIFY_STATE,FALSE,name);
        if(request&&ack)return true;CloseEvents();return false;
    }
    int PollRequest()noexcept {const auto value=WaitForSingleObject(request,0);return value==WAIT_OBJECT_0?1:value==WAIT_TIMEOUT?0:-1;}
    bool Acknowledge()noexcept{return SetEvent(ack)!=FALSE;}
    void CloseEvents()noexcept{if(request)CloseHandle(request);if(ack)CloseHandle(ack);request=ack=nullptr;}
    HRESULT Open(Device value)noexcept {
        if(dxgi)return value==device?S_OK:E_INVALIDARG;
        if(!value)return E_POINTER;
        const auto hr=value->QueryInterface(IID_PPV_ARGS(&dxgi));if(SUCCEEDED(hr))device=value;return hr;
    }
    HRESULT Read(unsigned& value)noexcept{return dxgi?dxgi->GetMaximumFrameLatency(&value):E_POINTER;}
    HRESULT Write(unsigned value)noexcept{return dxgi?dxgi->SetMaximumFrameLatency(value):E_POINTER;}
    void Release()noexcept{dxgi.Reset();device=nullptr;}
};
class PrivatePresentationExperiment {
    PresentationExperimentBackend backend_;
    BasicPrivatePresentationExperiment<PresentationExperimentBackend> policy_{backend_};
    struct PreserveError {DWORD value=GetLastError();~PreserveError(){SetLastError(value);}};
public:
    void ConfigureFromEnvironment()noexcept {
        PreserveError error;wchar_t token[34]{};
        const auto length=GetEnvironmentVariableW(L"FVR_PRIVATE_PRESENT_EXPERIMENT436",token,34);
        policy_.Configure(token,length>32?33:length,length!=0);
    }
    bool Enabled()const noexcept{return policy_.Enabled();}
    unsigned Phase()const noexcept{return policy_.Phase();}
    bool AfterEnabled()const noexcept{return policy_.AfterEnabled();}
    void Tick(ID3D11Device* device,const FrameBridgePresentIdentity& expected,
              const FrameBridgePresentBoundary& boundary,RenderQueueMode otherLimiter)noexcept {
        if(!Enabled())return;PreserveError error;policy_.Tick(device,expected,boundary,otherLimiter);
    }
    void Close(ID3D11Device* device,const FrameBridgePresentIdentity& identity)noexcept {
        if(!Enabled())return;PreserveError error;policy_.Close(device,identity);
    }
    void Write(std::ostream& out,bool callbacksDrained=true)const {
        out<<"{\"schema\":\"fvr.private-presentation-experiment436.v1\",\"available\":"<<(callbacksDrained?"true":"false");
        if(!callbacksDrained){out<<'}';return;}
        const auto s=policy_.Snapshot();const auto b=[](bool value){return value?"true":"false";};
        out<<",\"configured\":"<<b(s.configured)<<",\"enabled\":"<<b(s.enabled)<<",\"events_ready\":"<<b(s.eventsReady)
           <<",\"owner_pinned\":"<<b(s.ownerPinned)<<",\"phase\":"<<s.phase<<",\"complete\":"<<b(s.complete)
           <<",\"closed\":"<<b(s.closed)<<",\"restored\":"<<b(s.restored)<<",\"failed\":"<<b(s.failure!=PresentationExperimentFailure::None)
           <<",\"failure\":"<<unsigned(s.failure)<<",\"qpc_frequency\":"<<s.qpcFrequency<<",\"original_queue\":"<<s.originalQueue
           <<",\"observed_queue\":"<<s.observedQueue<<",\"polls\":"<<s.polls<<",\"no_events\":"<<s.noEvents
           <<",\"requests\":"<<s.requests<<",\"acks\":"<<s.acks<<",\"owner_mismatch\":"<<s.ownerMismatch
           <<",\"identity\":{\"renderer\":"<<s.identity.renderer<<",\"renderer_vtable\":"<<s.identity.rendererVtable
           <<",\"device\":"<<s.identity.device<<",\"context\":"<<s.identity.context<<",\"swapchain\":"<<s.identity.swapchain
           <<",\"thread\":"<<s.identity.thread<<"},\"rows\":[";
        for(unsigned n=0;n<s.rows;++n){const auto& r=s.phases[n];if(n)out<<',';
            out<<"{\"phase\":"<<r.phase<<",\"begin_qpc\":"<<r.beginQpc<<",\"queue_value\":"<<r.queueValue
               <<",\"after_enabled\":"<<b(r.afterEnabled)<<",\"applied\":"<<b(r.applied)
               <<",\"acknowledged\":"<<b(r.acknowledged)<<",\"failure\":"<<unsigned(r.failure)<<'}';}
        out<<"],\"queue_limiter\":";WriteRenderQueueSnapshot(out,policy_.QueueSnapshot());out<<'}';
    }
};
}
