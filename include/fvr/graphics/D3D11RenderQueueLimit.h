#pragma once
#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>
#include <ostream>

namespace fvr::graphics {
// Private A/B experiment. No native view is skipped, no simulation clock or
// render size changes, and no graphics wait/Flush is introduced by this class.
enum class RenderQueueMode:unsigned {Off,Observe,LimitOne};
struct RenderQueueSnapshot {
    RenderQueueMode mode=RenderQueueMode::Off;
    bool attempted=false,available=false,writeAttempted=false,applied=false,
        closed=false,restored=false,changedExternally=false;
    unsigned ownerThread=0,original=0,observed=0,readCalls=0,writeCalls=0,ownerMismatch=0;
    HRESULT lastResult=S_OK;
};
template<class Backend> class BasicRenderQueueLimit {
    Backend& backend_;RenderQueueSnapshot state_{};
    HRESULT Read(unsigned& value)noexcept {++state_.readCalls;return state_.lastResult=backend_.Read(value);}
    HRESULT Write(unsigned value)noexcept {++state_.writeCalls;return state_.lastResult=backend_.Write(value);}
public:
    explicit BasicRenderQueueLimit(Backend& backend)noexcept:backend_(backend){}
    void Configure(typename Backend::Device device,RenderQueueMode mode)noexcept {
        if(state_.attempted||mode==RenderQueueMode::Off)return;
        state_.mode=mode;state_.attempted=true;state_.ownerThread=backend_.Thread();
        if(!device||!state_.ownerThread)return;
        state_.lastResult=backend_.Open(device);if(FAILED(state_.lastResult))return;
        if(FAILED(Read(state_.original))||state_.original<1||state_.original>16){backend_.Release();return;}
        state_.available=true;state_.observed=state_.original;
        if(mode==RenderQueueMode::Observe)return;
        if(state_.original==1){state_.applied=true;return;}
        // Even a failed setter is checked at Close: an unknown result must not
        // discard the original value or permit an unverified inverse write.
        state_.writeAttempted=true;
        const auto written=Write(1);
        const auto read=Read(state_.observed);
        state_.applied=SUCCEEDED(written)&&SUCCEEDED(read)&&state_.observed==1;
        if(FAILED(written))state_.lastResult=written;
    }
    void Close()noexcept {
        if(!state_.attempted||state_.closed)return;
        if(backend_.Thread()!=state_.ownerThread){++state_.ownerMismatch;return;}
        if(state_.available){
            if(FAILED(Read(state_.observed)))return; // Retain exact restoration evidence for owner retry.
            if(state_.writeAttempted&&state_.observed==1){
                if(FAILED(Write(state_.original))||FAILED(Read(state_.observed)))return;
                if(state_.observed!=state_.original)return;
                state_.restored=true;
            }else if(state_.observed==state_.original){state_.restored=true;}
            else {state_.changedExternally=true;} // Never overwrite a third party's new value.
        }
        state_.closed=true;backend_.Release();
    }
    const RenderQueueSnapshot& Snapshot()const noexcept{return state_;}
};
struct DxgiRenderQueueBackend {
    using Device=ID3D11Device*;
    Microsoft::WRL::ComPtr<IDXGIDevice1> dxgi;
    unsigned Thread()const noexcept{return GetCurrentThreadId();}
    HRESULT Open(Device device)noexcept{return device->QueryInterface(IID_PPV_ARGS(&dxgi));}
    HRESULT Read(unsigned& value)noexcept{return dxgi?dxgi->GetMaximumFrameLatency(&value):E_POINTER;}
    HRESULT Write(unsigned value)noexcept{return dxgi?dxgi->SetMaximumFrameLatency(value):E_POINTER;}
    void Release()noexcept{dxgi.Reset();}
};
inline RenderQueueMode PrivateRenderQueueMode()noexcept {
    const auto error=GetLastError();wchar_t value[2]{};
    const auto length=GetEnvironmentVariableW(L"FVR_PRIVATE_RENDER_QUEUE436",value,2);SetLastError(error);
    if(length==1&&value[0]==L'0')return RenderQueueMode::Observe;
    if(length==1&&value[0]==L'1')return RenderQueueMode::LimitOne;
    return RenderQueueMode::Off;
}
class D3D11RenderQueueLimit {
    DxgiRenderQueueBackend backend_;
    BasicRenderQueueLimit<DxgiRenderQueueBackend> policy_{backend_};
public:
    void Configure(ID3D11Device* device)noexcept {policy_.Configure(device,PrivateRenderQueueMode());}
    void Close()noexcept {policy_.Close();}
    const RenderQueueSnapshot& Snapshot()const noexcept{return policy_.Snapshot();}
};
inline void WriteRenderQueueSnapshot(std::ostream& out,const RenderQueueSnapshot& s){
    const auto b=[](bool value){return value?"true":"false";};
    const char* mode=s.mode==RenderQueueMode::LimitOne?"limit_one":s.mode==RenderQueueMode::Observe?"observe":"off";
    out<<"{\"mode\":\""<<mode<<"\",\"attempted\":"<<b(s.attempted)<<",\"available\":"<<b(s.available)
        <<",\"write_attempted\":"<<b(s.writeAttempted)<<",\"applied\":"<<b(s.applied)
        <<",\"closed\":"<<b(s.closed)<<",\"restored\":"<<b(s.restored)
        <<",\"changed_externally\":"<<b(s.changedExternally)<<",\"owner_thread\":"<<s.ownerThread
        <<",\"original\":"<<s.original<<",\"observed\":"<<s.observed<<",\"last_hresult\":"<<s.lastResult
        <<",\"read_calls\":"<<s.readCalls<<",\"write_calls\":"<<s.writeCalls
        <<",\"owner_mismatch\":"<<s.ownerMismatch<<'}';
}
}
