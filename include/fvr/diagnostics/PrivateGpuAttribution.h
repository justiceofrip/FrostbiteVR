#pragma once
#include "fvr/diagnostics/PrivateCpuAttribution.h"
#include <d3d11.h>
#include <wrl/client.h>
#include <array>
#include <atomic>
#include <cstdint>
#include <ostream>
#include <type_traits>

// Native-context diagnostic only. Configure on the proven graphics owner;
// ordinary callbacks cannot activate it. No waits, Flush, query recycling,
// frame-channel decisions, native memory writes or per-callback allocation.
namespace fvr::diagnostics::privateGpu {
constexpr unsigned Capacity=96,PairLimit=8,OrdinaryLimit=16,PairMarkers=9;
enum class Kind:unsigned {OrdinaryDraw,TrackedDraw,DepthCopy,EyeCapture,ExportCopy};
enum class Phase:unsigned {Off,Armed,Recording,Draining,Finished,Failed};
enum class Result:unsigned {Pending,Valid,Abnormal,Incomplete,Timeout,Disjoint,ClockError,QueryError};
enum class ActivationMode:unsigned {FirstEight,PhaseCPlus3s,Invalid};
enum class ActivationFailure:unsigned {None,InvalidOption,MissingPrerequisite,PhaseDeparted,Clock,NotReached};
inline ActivationMode ParseActivationMode(const wchar_t* value,unsigned length,bool present)noexcept {
    if(!present||(value&&length==1&&value[0]==L'0'))return ActivationMode::FirstEight;
    return value&&length==4&&value[0]==L'3'&&value[1]==L'0'&&value[2]==L'0'&&value[3]==L'0'
        ?ActivationMode::PhaseCPlus3s:ActivationMode::Invalid;
}
using Identity=privateCpu::Identity;
struct Key {
    std::uint64_t owner=0,frame=0,request=0,channel=0,deviceEpoch=0,tracking=0,space=0;
    std::int64_t predictedNs=0;
    std::uint64_t resourceEpoch=0;
    bool operator==(const Key&)const=default;
};
struct Token {unsigned slot=UINT32_MAX;std::uint64_t serial=0;Identity owner{};};
static_assert(std::is_trivially_destructible_v<Token>); // Safe in a native __finally leaf.
struct Row {
    Kind kind{};Key key{};unsigned eye=0,stage=0;Result result=Result::Pending;
    std::uint64_t serial=0,beginQpc=0,endQpc=0,observedQpc=0,gpuBegin=0,gpuEnd=0;
    bool active=false,ended=false;HRESULT error=S_OK;
};
struct Snapshot {
    bool enabled=false,available=false,callbacksDrained=false,epochEnded=false,disjoint=false;
    Phase phase=Phase::Off;Identity owner{};
    std::uint64_t device=0,context=0,qpcFrequency=0,gpuFrequency=0,beginQpc=0,
        admissionDeadline=0,drainDeadline=0,epochEndQpc=0;
    unsigned count=0,pairs=0,ordinary=0,validTrackedPairs=0,createdQueries=0;
    unsigned ownerMismatch=0,contextMismatch=0,deviceMismatch=0,invalidKey=0,
        duplicate=0,capped=0,clockErrors=0,queryErrors=0;
    HRESULT error=S_OK;std::array<Row,Capacity> rows{};
    ActivationMode activationMode=ActivationMode::FirstEight;
    ActivationFailure activationFailure=ActivationFailure::None;
    std::uint64_t phase3ObservedQpc=0,activationNotBeforeQpc=0;
};
inline const char* Name(ActivationMode mode)noexcept {
    switch(mode){case ActivationMode::FirstEight:return "first_eight";
    case ActivationMode::PhaseCPlus3s:return "phase_c_plus_3s";default:return "invalid";}
}
inline const char* Name(ActivationFailure failure)noexcept {
    switch(failure){case ActivationFailure::None:return "none";case ActivationFailure::InvalidOption:return "invalid_option";
    case ActivationFailure::MissingPrerequisite:return "missing_prerequisite";case ActivationFailure::PhaseDeparted:return "phase_departed";
    case ActivationFailure::Clock:return "clock_error";case ActivationFailure::NotReached:return "activation_not_reached";}return "invalid";
}
inline const char* Name(Kind kind)noexcept {
    switch(kind){case Kind::OrdinaryDraw:return "ordinary_draw";case Kind::TrackedDraw:return "tracked_draw";
    case Kind::DepthCopy:return "depth_copy";case Kind::EyeCapture:return "eye_capture";case Kind::ExportCopy:return "export_copy";}return "invalid";
}
inline const char* Name(Result result)noexcept {
    switch(result){case Result::Pending:return "pending";case Result::Valid:return "valid";case Result::Abnormal:return "abnormal";
    case Result::Incomplete:return "incomplete";case Result::Timeout:return "timeout";case Result::Disjoint:return "disjoint";
    case Result::ClockError:return "clock_error";case Result::QueryError:return "query_error";}return "invalid";
}
struct PreserveError {DWORD saved=GetLastError();~PreserveError(){SetLastError(saved);}};
inline ActivationMode ActivationModeFromEnvironment()noexcept {
    PreserveError preserve;wchar_t value[6]{};SetLastError(ERROR_SUCCESS);
    const auto length=GetEnvironmentVariableW(L"FVR_PRIVATE_GPU_PHASE_C_DELAY_MS",value,6);
    return ParseActivationMode(value,length,length!=0||GetLastError()!=ERROR_ENVVAR_NOT_FOUND);
}

// Fake backends exercise the same admission, query ordering and lifetime code.
// Only the backend below touches D3D11; all of its calls stay on the owner.
template<class Backend> class BasicRecorder {
public:
    using Device=typename Backend::Device;using Context=typename Backend::Context;
private:
    struct Pair {Key key{};unsigned mask=0;};
    Backend& backend_;std::atomic<Phase> phase_{Phase::Off};bool configured_=false,enabled_=false;
    Device device_{};Context context_{};Identity owner_{};
    std::uint64_t frequency_=0,start_=0,deadline_=0,drainDeadline_=0,lastQpc_=0,epochEnd_=0,gpuFrequency_=0;
    unsigned count_=0,pairs_=0,ordinary_=0,active_=0,created_=0;
    bool epochEnded_=false,epochRead_=false,disjoint_=false;
    HRESULT error_=S_OK;
    // Written once by setup before callbacks; subsequent gate state is owner-only.
    bool activationConfigured_=false;
    ActivationMode activationMode_=ActivationMode::FirstEight;
    ActivationFailure activationFailure_=ActivationFailure::None;
    std::uint64_t phase3ObservedQpc_=0,activationNotBeforeQpc_=0;
    std::array<Pair,PairLimit> pairsData_{};std::array<Row,Capacity> rows_{};
    std::atomic<unsigned> ownerMismatch_{0},contextMismatch_{0},deviceMismatch_{0},invalidKey_{0},
        duplicate_{0},capped_{0},clockErrors_{0},queryErrors_{0};
    bool Owner()noexcept {
        if(backend_.Current()==owner_)return true;
        ownerMismatch_.fetch_add(1,std::memory_order_relaxed);return false;
    }
    bool Tick(std::uint64_t& tick)noexcept {
        tick=backend_.Tick();if(!tick||tick<lastQpc_){++clockErrors_;return false;}
        lastQpc_=tick;return true;
    }
    static bool Core(const Key& key)noexcept{return key.frame&&key.tracking&&key.space&&key.predictedNs>0;}
    static bool Full(const Key& key)noexcept{return Core(key)&&key.owner&&key.request&&key.channel&&key.deviceEpoch;}
    static bool SameCore(const Key& a,const Key& b)noexcept {
        return a.frame==b.frame&&a.tracking==b.tracking&&a.space==b.space&&a.predictedNs==b.predictedNs;
    }
    static bool Compatible(const Key& a,const Key& b)noexcept {
        const auto same=[](auto x,auto y){return !x||!y||x==y;};
        return SameCore(a,b)&&same(a.owner,b.owner)&&same(a.request,b.request)&&same(a.channel,b.channel)&&
            same(a.deviceEpoch,b.deviceEpoch)&&same(a.resourceEpoch,b.resourceEpoch);
    }
    static int Marker(Kind kind,unsigned eye,unsigned stage)noexcept {
        if(eye>1)return -1;
        switch(kind){case Kind::TrackedDraw:return stage?-1:int(eye);
        case Kind::DepthCopy:return stage>1?-1:int(2+eye*2+stage);
        case Kind::EyeCapture:return stage?-1:int(6+eye);
        case Kind::ExportCopy:return eye||stage?-1:8;
        default:return -1;}
    }
    void Finish(Result pending)noexcept {
        // Even timeout/clock/query failure during a nested interval must close
        // our one disjoint epoch before releasing its query. This does not wait
        // for GPU completion, or permit an old token to issue another timestamp.
        CloseEpoch(lastQpc_);
        for(unsigned i=0;i<count_;++i)if(rows_[i].result==Result::Pending){rows_[i].result=pending;rows_[i].active=false;}
        active_=0;backend_.Reset();phase_.store(Phase::Finished,std::memory_order_release);
    }
    void CloseEpoch(std::uint64_t tick)noexcept {
        if(epochEnded_||!start_)return;
        backend_.EndEpoch();epochEnded_=true;epochEnd_=tick;phase_.store(Phase::Draining,std::memory_order_release);
    }
    bool ObserveDeadline(std::uint64_t now)noexcept {
        if(now<deadline_)return true;
        if(!active_)CloseEpoch(now);
        return false;
    }
    void QueryFailure(HRESULT hr)noexcept {
        error_=hr;++queryErrors_;
        Finish(Result::QueryError);
    }
public:
    explicit BasicRecorder(Backend& backend)noexcept:backend_(backend){}
    BasicRecorder(const BasicRecorder&)=delete;BasicRecorder& operator=(const BasicRecorder&)=delete;
    void ConfigureActivation(ActivationMode mode,bool prerequisites)noexcept {
        // Setup only, before Configure/any callbacks. Never rearm or weaken a gate.
        if(activationConfigured_||configured_)return;activationConfigured_=true;activationMode_=mode;
        if(mode==ActivationMode::Invalid)activationFailure_=ActivationFailure::InvalidOption;
        else if(mode==ActivationMode::PhaseCPlus3s&&!prerequisites)activationFailure_=ActivationFailure::MissingPrerequisite;
    }
    bool Configure(Device device,Context context)noexcept {
        PreserveError preserve;
        // Setup is single-owner, before Begin/Poll callbacks become active.
        if(configured_){
            if(!enabled_)return false;
            if(!Owner())return false;
            if(device!=device_){++deviceMismatch_;return false;}
            if(context!=context_){++contextMismatch_;return false;}
            const auto phase=phase_.load(std::memory_order_acquire);
            return phase==Phase::Armed||phase==Phase::Recording||phase==Phase::Draining;
        }
        configured_=true;if(!backend_.OptedIn())return false;
        enabled_=true;owner_=backend_.Current();device_=device;context_=context;frequency_=backend_.Frequency();
        if(activationFailure_!=ActivationFailure::None){error_=E_INVALIDARG;phase_=Phase::Failed;return false;}
        if(!owner_||!frequency_||frequency_>UINT64_MAX/4||!device||!context){
            error_=E_INVALIDARG;phase_=Phase::Failed;return false;}
        const auto hr=backend_.Configure(device,context,created_);
        if(FAILED(hr)){error_=hr;++queryErrors_;backend_.Reset();phase_=Phase::Failed;return false;}
        phase_.store(Phase::Armed,std::memory_order_release);return true;
    }
    void ObserveActivationPhase(unsigned presentationPhase,bool verified)noexcept {
        if(activationMode_!=ActivationMode::PhaseCPlus3s||phase_.load(std::memory_order_acquire)!=Phase::Armed)return;
        PreserveError preserve;if(!Owner())return; // Before any mutable latch read/write.
        if(activationFailure_!=ActivationFailure::None)return;
        if(presentationPhase>3||(phase3ObservedQpc_&&presentationPhase!=3)){
            activationFailure_=ActivationFailure::PhaseDeparted;return;}
        if(presentationPhase!=3)return;
        if(!verified){activationFailure_=ActivationFailure::MissingPrerequisite;return;}
        if(phase3ObservedQpc_)return;
        std::uint64_t now=0;
        if(!Tick(now)||now>UINT64_MAX-frequency_*3){activationFailure_=ActivationFailure::Clock;return;}
        phase3ObservedQpc_=now;activationNotBeforeQpc_=now+frequency_*3;
    }
    bool Matches(Device device,Context context)const noexcept {
        const auto phase=phase_.load(std::memory_order_acquire);
        return phase!=Phase::Off&&phase!=Phase::Failed&&device==device_&&context==context_;
    }
    Token Begin(Kind kind,const Key& key,unsigned eye=0,unsigned stage=0,Context expectedContext={})noexcept {
        auto phase=phase_.load(std::memory_order_acquire);
        if(phase!=Phase::Armed&&phase!=Phase::Recording)return {};
        PreserveError preserve;
        if(!Owner())return {};
        if(expectedContext&&expectedContext!=context_){++contextMismatch_;return {};}
        const int marker=Marker(kind,eye,stage);
        if(kind==Kind::OrdinaryDraw){if(eye||stage||!key.owner||!key.frame){++invalidKey_;return {};}}
        else if(marker<0||(kind==Kind::ExportCopy?(!Core(key)||!key.resourceEpoch):!Full(key))){++invalidKey_;return {};}
        if(phase==Phase::Armed){
            if(kind!=Kind::TrackedDraw||eye||!Full(key)||!backend_.AllowOwner())return {};
            if(activationMode_==ActivationMode::PhaseCPlus3s&&
               (activationFailure_!=ActivationFailure::None||!activationNotBeforeQpc_))return {};
            std::uint64_t now=0;if(!Tick(now)){
                if(activationMode_==ActivationMode::PhaseCPlus3s)activationFailure_=ActivationFailure::Clock;return {};}
            if(activationMode_==ActivationMode::PhaseCPlus3s&&now<activationNotBeforeQpc_)return {};
            if(now>UINT64_MAX-frequency_*4){++clockErrors_;
                if(activationMode_==ActivationMode::PhaseCPlus3s)activationFailure_=ActivationFailure::Clock;return {};}
            start_=now;deadline_=now+frequency_*2;drainDeadline_=now+frequency_*4;
            backend_.BeginEpoch();phase_.store(Phase::Recording,std::memory_order_release);
        }
        std::uint64_t now=0;if(!Tick(now))return {};
        if(!ObserveDeadline(now))return {};
        if(count_==Capacity){++capped_;return {};}
        if(kind==Kind::OrdinaryDraw){
            if(ordinary_==OrdinaryLimit){++capped_;return {};}
            for(unsigned i=0;i<count_;++i)if(rows_[i].kind==kind&&rows_[i].key.owner==key.owner&&rows_[i].key.frame==key.frame){++duplicate_;return {};}
            ++ordinary_;
        }else{
            unsigned pair=pairs_;
            for(unsigned i=0;i<pairs_;++i)if(pairsData_[i].key.frame==key.frame){pair=i;break;}
            if(pair==pairs_){
                if(pairs_==PairLimit){++capped_;return {};}
                if(kind!=Kind::TrackedDraw||eye||!Full(key)){++invalidKey_;return {};}
                pairsData_[pairs_++].key=key;
            }else if(!Compatible(pairsData_[pair].key,key)){++invalidKey_;return {};}
            auto& admitted=pairsData_[pair];const auto bit=1u<<unsigned(marker);
            if(admitted.mask&bit){++duplicate_;return {};}
            admitted.mask|=bit;
            if(key.resourceEpoch)admitted.key.resourceEpoch=key.resourceEpoch;
        }
        const unsigned index=count_++;auto& row=rows_[index];
        row.kind=kind;row.key=key;row.eye=eye;row.stage=stage;row.serial=std::uint64_t(index)+1;
        row.beginQpc=now;row.active=true;++active_;backend_.Stamp(index,false);
        return {index,row.serial,owner_};
    }
    void End(Token token,bool normal=true)noexcept {
        if(token.slot==UINT32_MAX)return;
        const auto phase=phase_.load(std::memory_order_acquire);
        if(phase!=Phase::Recording&&phase!=Phase::Draining)return;
        PreserveError preserve;
        if(!Owner())return;
        if(token.owner!=owner_||token.slot>=count_||rows_[token.slot].serial!=token.serial){++invalidKey_;return;}
        auto& row=rows_[token.slot];if(!row.active){++duplicate_;return;}
        row.active=false;--active_;std::uint64_t now=0;
        if(!Tick(now)){row.result=Result::ClockError;return;}row.endQpc=now;
        if(!normal){row.result=Result::Abnormal;return;}
        backend_.Stamp(token.slot,true);row.ended=true;
        if(now>=deadline_&&!active_)CloseEpoch(now);
    }
    void Poll()noexcept {
        const auto phase=phase_.load(std::memory_order_acquire);
        if(phase!=Phase::Recording&&phase!=Phase::Draining)return;
        PreserveError preserve;
        if(!Owner())return;
        std::uint64_t now=0;if(!Tick(now)){Finish(Result::ClockError);return;}
        if(phase==Phase::Recording)ObserveDeadline(now);
        if(now>=drainDeadline_){Finish(Result::Timeout);return;}
        if(!epochEnded_)return;
        if(!epochRead_){D3D11_QUERY_DATA_TIMESTAMP_DISJOINT data{};const auto hr=backend_.ReadEpoch(data);
            if(hr==S_FALSE)return;if(hr!=S_OK){QueryFailure(FAILED(hr)?hr:E_UNEXPECTED);return;}
            epochRead_=true;disjoint_=data.Disjoint!=FALSE;gpuFrequency_=data.Frequency;
            if(disjoint_){Finish(Result::Disjoint);return;}
            if(!gpuFrequency_){++clockErrors_;Finish(Result::ClockError);return;}
        }
        // At most 193 GetData calls per service point; no retry of Busy queries
        // within a call, and no query issuance while collecting results.
        bool pending=false;
        for(unsigned i=0;i<count_;++i){auto& row=rows_[i];if(row.result!=Result::Pending)continue;
            if(!row.ended){pending=true;continue;}
            std::uint64_t begin=0,end=0;
            const auto a=backend_.ReadStamp(i,false,begin),b=backend_.ReadStamp(i,true,end);
            if((a!=S_OK&&a!=S_FALSE)||(b!=S_OK&&b!=S_FALSE)){
                row.error=(a!=S_OK&&a!=S_FALSE)?a:b;row.result=Result::QueryError;++queryErrors_;continue;}
            if(a==S_FALSE||b==S_FALSE){pending=true;continue;}
            row.gpuBegin=begin;row.gpuEnd=end;row.observedQpc=now;
            row.result=end>=begin?Result::Valid:Result::ClockError;
            if(row.result==Result::ClockError)++clockErrors_;
        }
        if(!pending)Finish(Result::Incomplete);
    }
    void Stop()noexcept {
        const auto phase=phase_.load(std::memory_order_acquire);
        if(phase==Phase::Off||phase==Phase::Failed||phase==Phase::Finished)return;
        PreserveError preserve;
        if(!Owner())return;
        // Existing graphics cleanup only, after native callbacks have drained.
        if(activationMode_==ActivationMode::PhaseCPlus3s&&!start_&&activationFailure_==ActivationFailure::None)
            activationFailure_=ActivationFailure::NotReached;
        // Close once, perform one ordinary nonblocking read, then label gaps.
        for(unsigned i=0;i<count_;++i)if(rows_[i].active){rows_[i].active=false;rows_[i].result=Result::Incomplete;}
        active_=0;std::uint64_t now=0;
        if(start_&&Tick(now))CloseEpoch(now);
        if(start_)Poll();
        if(phase_.load(std::memory_order_acquire)!=Phase::Finished)Finish(Result::Incomplete);
    }
    bool Report(Snapshot& out,bool callbacksDrained)const noexcept {
        out={};out.callbacksDrained=callbacksDrained;if(!callbacksDrained)return false;
        out.enabled=enabled_;out.phase=phase_.load(std::memory_order_acquire);out.owner=owner_;
        out.device=Backend::Address(device_);out.context=Backend::Address(context_);out.qpcFrequency=frequency_;
        out.gpuFrequency=gpuFrequency_;out.beginQpc=start_;out.admissionDeadline=deadline_;out.drainDeadline=drainDeadline_;
        out.activationMode=activationMode_;out.activationFailure=activationFailure_;
        out.phase3ObservedQpc=phase3ObservedQpc_;out.activationNotBeforeQpc=activationNotBeforeQpc_;
        out.epochEnded=epochEnded_;out.epochEndQpc=epochEnd_;out.disjoint=disjoint_;out.count=count_;
        out.pairs=pairs_;out.ordinary=ordinary_;out.createdQueries=created_;out.error=error_;
        out.ownerMismatch=ownerMismatch_.load();out.contextMismatch=contextMismatch_.load();out.deviceMismatch=deviceMismatch_.load();
        out.invalidKey=invalidKey_.load();out.duplicate=duplicate_.load();out.capped=capped_.load();
        out.clockErrors=clockErrors_.load();out.queryErrors=queryErrors_.load();out.rows=rows_;
        bool anyValid=false;for(unsigned i=0;i<count_;++i)anyValid|=rows_[i].result==Result::Valid;
        out.available=out.phase==Phase::Finished&&epochRead_&&!disjoint_&&gpuFrequency_!=0&&anyValid;
        for(unsigned p=0;p<pairs_;++p){unsigned validMask=0;
            for(unsigned i=0;i<count_;++i)if(rows_[i].kind!=Kind::OrdinaryDraw&&rows_[i].result==Result::Valid&&
                Compatible(pairsData_[p].key,rows_[i].key)){
                const auto marker=Marker(rows_[i].kind,rows_[i].eye,rows_[i].stage);
                if(marker>=0)validMask|=1u<<unsigned(marker);
            }
            if(validMask==(1u<<PairMarkers)-1)++out.validTrackedPairs;
        }
        return true;
    }
    void WriteJson(std::ostream& out,bool callbacksDrained)const {
        Snapshot s;Report(s,callbacksDrained);
        out<<"{\"schema\":\"fvr.private-gpu-attribution.v1\",\"callbacks_drained\":"<<(s.callbacksDrained?"true":"false")
            <<",\"enabled\":"<<(s.enabled?"true":"false")<<",\"available\":"<<(s.available?"true":"false")
            <<",\"phase\":"<<unsigned(s.phase)<<",\"owner_thread\":"<<s.owner.thread<<",\"owner_creation_filetime\":"<<s.owner.creation
            <<",\"device\":"<<s.device<<",\"context\":"<<s.context<<",\"qpc_frequency\":"<<s.qpcFrequency
            <<",\"gpu_frequency\":"<<s.gpuFrequency<<",\"begin_qpc\":"<<s.beginQpc<<",\"admission_deadline_qpc\":"<<s.admissionDeadline
            <<",\"activation_mode\":\""<<Name(s.activationMode)<<"\",\"activation_failure\":\""<<Name(s.activationFailure)
            <<"\",\"activation_delay_ms\":"<<(s.activationMode==ActivationMode::PhaseCPlus3s?3000:0)
            <<",\"phase3_observed_qpc\":"<<s.phase3ObservedQpc<<",\"activation_not_before_qpc\":"<<s.activationNotBeforeQpc
            <<",\"actual_activation_qpc\":"<<s.beginQpc
            <<",\"drain_deadline_qpc\":"<<s.drainDeadline<<",\"epoch_end_qpc\":"<<s.epochEndQpc
            <<",\"epoch_ended\":"<<(s.epochEnded?"true":"false")<<",\"disjoint\":"<<(s.disjoint?"true":"false")
            <<",\"duration_kind\":\"same-context GPU timestamp interval; no CPU-clock calibration\",\"nested_intervals\":true"
            <<",\"interval_capacity\":"<<Capacity<<",\"count\":"<<s.count<<",\"pairs\":"<<s.pairs<<",\"ordinary\":"<<s.ordinary
            <<",\"valid_tracked_pairs\":"<<s.validTrackedPairs<<",\"created_queries\":"<<s.createdQueries<<",\"owner_mismatch\":"<<s.ownerMismatch<<",\"context_mismatch\":"<<s.contextMismatch
            <<",\"device_mismatch\":"<<s.deviceMismatch<<",\"invalid_key\":"<<s.invalidKey<<",\"duplicate\":"<<s.duplicate
            <<",\"capped\":"<<s.capped<<",\"clock_errors\":"<<s.clockErrors<<",\"query_errors\":"<<s.queryErrors
            <<",\"error\":"<<std::uint32_t(s.error)<<",\"rows\":[";
        for(unsigned i=0;i<s.count;++i){const auto& r=s.rows[i];const auto& k=r.key;if(i)out<<',';
            out<<"{\"kind\":\""<<Name(r.kind)<<"\",\"result\":\""<<Name(r.result)<<"\",\"eye\":"<<r.eye<<",\"stage\":"<<r.stage
                <<",\"key\":{\"owner\":"<<k.owner<<",\"frame\":"<<k.frame<<",\"request\":"<<k.request<<",\"channel\":"<<k.channel
                <<",\"device_epoch\":"<<k.deviceEpoch<<",\"tracking\":"<<k.tracking<<",\"space\":"<<k.space
                <<",\"predicted_ns\":"<<k.predictedNs<<",\"resource_epoch\":"<<k.resourceEpoch<<'}'
                <<",\"begin_qpc\":"<<r.beginQpc<<",\"end_qpc\":"<<r.endQpc<<",\"observed_qpc\":"<<r.observedQpc
                <<",\"gpu_begin\":"<<r.gpuBegin<<",\"gpu_end\":"<<r.gpuEnd<<",\"active\":"<<(r.active?"true":"false")
                <<",\"ended\":"<<(r.ended?"true":"false")<<",\"error\":"<<std::uint32_t(r.error)<<'}';
        }out<<"]}";
    }
};

class NativeBackend {
    using Query=Microsoft::WRL::ComPtr<ID3D11Query>;
    Microsoft::WRL::ComPtr<ID3D11Device> device_;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context_;
    Query disjoint_;std::array<std::array<Query,2>,Capacity> stamps_{};
public:
    using Device=ID3D11Device*;using Context=ID3D11DeviceContext*;
    static bool OptedIn()noexcept {
        wchar_t value[3]{};return GetEnvironmentVariableW(L"FVR_PRIVATE_GPU_TIMING436",value,3)==1&&value[0]==L'8';
    }
    static Identity Current()noexcept{return privateCpu::NativeClock::Current();}
    static bool AllowOwner()noexcept{return privateCpu::recorder.AllowOwner(privateCpu::Stage::MainOrdinary);}
    static std::uint64_t Frequency()noexcept{LARGE_INTEGER value{};return QueryPerformanceFrequency(&value)&&value.QuadPart>0?std::uint64_t(value.QuadPart):0;}
    static std::uint64_t Tick()noexcept{LARGE_INTEGER value{};return QueryPerformanceCounter(&value)&&value.QuadPart>0?std::uint64_t(value.QuadPart):0;}
    template<class T> static std::uint64_t Address(T* pointer)noexcept{return reinterpret_cast<std::uintptr_t>(pointer);}
    HRESULT Configure(Device device,Context context,unsigned& created)noexcept {
        Microsoft::WRL::ComPtr<ID3D11Device> actual;context->GetDevice(&actual);
        if(actual.Get()!=device||context->GetType()!=D3D11_DEVICE_CONTEXT_IMMEDIATE)return E_INVALIDARG;
        Microsoft::WRL::ComPtr<ID3D11DeviceContext> immediate;device->GetImmediateContext(&immediate);
        if(immediate.Get()!=context)return E_INVALIDARG;
        device_=device;context_=context;D3D11_QUERY_DESC desc{D3D11_QUERY_TIMESTAMP_DISJOINT,0};
        auto hr=device_->CreateQuery(&desc,&disjoint_);if(FAILED(hr))return hr;++created;
        desc.Query=D3D11_QUERY_TIMESTAMP;
        for(auto& pair:stamps_)for(auto& query:pair){hr=device_->CreateQuery(&desc,&query);if(FAILED(hr))return hr;++created;}
        return S_OK;
    }
    void BeginEpoch()noexcept{context_->Begin(disjoint_.Get());}
    void EndEpoch()noexcept{context_->End(disjoint_.Get());}
    void Stamp(unsigned slot,bool end)noexcept{context_->End(stamps_[slot][end?1:0].Get());}
    HRESULT ReadEpoch(D3D11_QUERY_DATA_TIMESTAMP_DISJOINT& out)noexcept{return context_->GetData(disjoint_.Get(),&out,sizeof(out),D3D11_ASYNC_GETDATA_DONOTFLUSH);}
    HRESULT ReadStamp(unsigned slot,bool end,std::uint64_t& out)noexcept{return context_->GetData(stamps_[slot][end?1:0].Get(),&out,sizeof(out),D3D11_ASYNC_GETDATA_DONOTFLUSH);}
    void Reset()noexcept{for(auto& pair:stamps_)for(auto& query:pair)query.Reset();disjoint_.Reset();context_.Reset();device_.Reset();}
};
inline NativeBackend nativeBackend;
inline BasicRecorder<NativeBackend> recorder{nativeBackend};
}
