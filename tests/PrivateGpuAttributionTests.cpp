#include "Test.h"
#include "fvr/diagnostics/PrivateGpuAttribution.h"
#include <sstream>
#include <string>
#include <iostream>
using namespace fvr::diagnostics::privateGpu;
namespace {
struct Fake {
    using Device=std::uintptr_t;using Context=std::uintptr_t;
    bool opt=true,allowed=true,busyEpoch=false,busyStamp=false,disjoint=false;
    Identity identity{41,700};std::uint64_t now=100,hz=1000,gpu=1000,gpuHz=1000000;
    HRESULT configureResult=S_OK,epochResult=S_OK,stampResult=S_OK;
    unsigned configured=0,beginEpoch=0,endEpoch=0,stamped=0,reads=0,resets=0,reused=0;
    std::array<std::array<std::uint64_t,2>,Capacity> times{};
    std::array<std::array<unsigned,2>,Capacity> issued{};
    bool OptedIn()noexcept{SetLastError(9001);return opt;}
    Identity Current()noexcept{SetLastError(9002);return identity;}
    bool AllowOwner()noexcept{return allowed;}
    std::uint64_t Frequency()noexcept{return hz;}
    std::uint64_t Tick()noexcept{SetLastError(9003);return now;}
    static std::uint64_t Address(std::uintptr_t value)noexcept{return value;}
    HRESULT Configure(Device d,Context c,unsigned& created)noexcept {
        ++configured;if(d!=1||c!=2)return E_INVALIDARG;
        if(FAILED(configureResult))return configureResult;created=Capacity*2+1;return S_OK;
    }
    void BeginEpoch()noexcept{++beginEpoch;}
    void EndEpoch()noexcept{++endEpoch;}
    void Stamp(unsigned slot,bool end)noexcept {
        ++stamped;if(issued[slot][end]++)++reused;times[slot][end]=gpu;gpu+=10;
    }
    HRESULT ReadEpoch(D3D11_QUERY_DATA_TIMESTAMP_DISJOINT& data)noexcept {
        ++reads;if(busyEpoch)return S_FALSE;if(epochResult!=S_OK)return epochResult;
        data={gpuHz,disjoint?TRUE:FALSE};return S_OK;
    }
    HRESULT ReadStamp(unsigned slot,bool end,std::uint64_t& value)noexcept {
        ++reads;if(busyStamp)return S_FALSE;if(stampResult!=S_OK)return stampResult;
        value=times[slot][end];return S_OK;
    }
    void Reset()noexcept{++resets;SetLastError(9004);}
};
using Recorder=BasicRecorder<Fake>;
Key K(unsigned frame=1){return {17,frame,frame+10,2,1,frame+100,3,std::int64_t(frame)+1000,0};}
Token Begin(Recorder& recorder,Kind kind,Key key,unsigned eye=0,unsigned stage=0){return recorder.Begin(kind,key,eye,stage,2);}
bool Active(Token token){return token.slot!=UINT32_MAX;}
void Pair(Recorder& recorder,unsigned frame){
    const auto key=K(frame);auto left=Begin(recorder,Kind::TrackedDraw,key);
    for(unsigned stage=0;stage<2;++stage)recorder.End(Begin(recorder,Kind::DepthCopy,key,0,stage));
    recorder.End(left);recorder.End(Begin(recorder,Kind::EyeCapture,key));
    auto right=Begin(recorder,Kind::TrackedDraw,key,1);
    for(unsigned stage=0;stage<2;++stage)recorder.End(Begin(recorder,Kind::DepthCopy,key,1,stage));
    recorder.End(right);recorder.End(Begin(recorder,Kind::EyeCapture,key,1));
    auto exportKey=key;exportKey.owner=exportKey.request=exportKey.channel=exportKey.deviceEpoch=0;exportKey.resourceEpoch=12;
    recorder.End(Begin(recorder,Kind::ExportCopy,exportKey));
}
struct SavedEnvironment {
    std::wstring prior;bool existed=false;
    SavedEnvironment(){const auto n=GetEnvironmentVariableW(L"FVR_PRIVATE_GPU_TIMING436",nullptr,0);
        if(n){prior.resize(n);const auto used=GetEnvironmentVariableW(L"FVR_PRIVATE_GPU_TIMING436",prior.data(),n);prior.resize(used);existed=true;}}
    ~SavedEnvironment(){SetEnvironmentVariableW(L"FVR_PRIVATE_GPU_TIMING436",existed?prior.c_str():nullptr);}
};
int OptInAndActivation(){
    SavedEnvironment saved;
    for(const auto* value:{L"",L"08",L"8 ",L"88",L"0",L"800000"}){
        CHECK(SetEnvironmentVariableW(L"FVR_PRIVATE_GPU_TIMING436",value));CHECK(!NativeBackend::OptedIn());}
    CHECK(SetEnvironmentVariableW(L"FVR_PRIVATE_GPU_TIMING436",nullptr));CHECK(!NativeBackend::OptedIn());
    CHECK(SetEnvironmentVariableW(L"FVR_PRIVATE_GPU_TIMING436",L"8"));CHECK(NativeBackend::OptedIn());
    Fake off;off.opt=false;Recorder disabled(off);CHECK(!disabled.Configure(1,2));
    CHECK(!Active(Begin(disabled,Kind::TrackedDraw,K())));disabled.Poll();disabled.Stop();
    CHECK(!off.configured&&!off.beginEpoch&&!off.stamped&&!off.reads);
    Fake f;Recorder r(f);SetLastError(1234);CHECK(r.Configure(1,2));CHECK(GetLastError()==1234);
    CHECK(r.Configure(1,2)&&f.configured==1&&!f.beginEpoch&&!f.stamped);
    CHECK(!Active(Begin(r,Kind::OrdinaryDraw,K()))&&!Active(Begin(r,Kind::DepthCopy,K())));
    f.allowed=false;CHECK(!Active(Begin(r,Kind::TrackedDraw,K()))&&!f.beginEpoch);
    f.allowed=true;const auto token=Begin(r,Kind::TrackedDraw,K());CHECK(Active(token)&&f.beginEpoch==1);
    r.End(token);CHECK(GetLastError()==1234);r.Stop();CHECK(GetLastError()==1234);return 0;
}
int DelayedActivation(){
    CHECK(ParseActivationMode(nullptr,0,false)==ActivationMode::FirstEight);
    CHECK(ParseActivationMode(L"0",1,true)==ActivationMode::FirstEight);
    CHECK(ParseActivationMode(L"3000",4,true)==ActivationMode::PhaseCPlus3s);
    for(const auto* value:{L"",L"3",L"03000",L"3000 ",L"3001",L"300000"})
        CHECK(ParseActivationMode(value,unsigned(std::char_traits<wchar_t>::length(value)),true)==ActivationMode::Invalid);
    Fake f;Recorder r(f);r.ConfigureActivation(ActivationMode::PhaseCPlus3s,true);CHECK(r.Configure(1,2));
    // A foreign callback cannot establish or invalidate the owner's phase latch.
    ++f.identity.creation;r.ObserveActivationPhase(3,true);r.ObserveActivationPhase(4,false);--f.identity.creation;
    Snapshot s;CHECK(r.Report(s,true)&&!s.phase3ObservedQpc&&s.activationFailure==ActivationFailure::None&&s.ownerMismatch==2);
    r.ObserveActivationPhase(2,true);CHECK(!Active(Begin(r,Kind::TrackedDraw,K()))&&!f.beginEpoch&&!f.stamped);
    SetLastError(1234);r.ObserveActivationPhase(3,true);CHECK(GetLastError()==1234);
    CHECK(r.Report(s,true)&&s.phase3ObservedQpc==100&&s.activationNotBeforeQpc==3100);
    // Neither repeated phase service nor attempted reconfiguration moves the threshold.
    f.now=500;r.ObserveActivationPhase(3,true);r.ConfigureActivation(ActivationMode::FirstEight,true);
    f.now=3099;CHECK(!Active(Begin(r,Kind::TrackedDraw,K()))&&!f.beginEpoch&&!f.stamped);
    f.now=3100;f.allowed=false;CHECK(!Active(Begin(r,Kind::TrackedDraw,K()))&&!f.beginEpoch);
    f.allowed=true;f.now=3150;Pair(r,1);r.End(Begin(r,Kind::OrdinaryDraw,K(50)));
    CHECK(r.Report(s,true)&&s.beginQpc==3150&&s.admissionDeadline==5150&&s.drainDeadline==7150);
    CHECK(s.activationMode==ActivationMode::PhaseCPlus3s&&s.activationFailure==ActivationFailure::None);
    // Once recording, phase changes must not strand tokens or change existing drain semantics.
    r.ObserveActivationPhase(4,false);f.now=5000;r.Poll();CHECK(!f.endEpoch&&!f.reads);
    f.now=5150;r.Poll();CHECK(r.Report(s,true)&&s.available&&s.validTrackedPairs==1&&s.ordinary==1);
    CHECK(f.beginEpoch==1&&f.endEpoch==1&&!f.reused&&s.createdQueries==193);
    std::ostringstream out;r.WriteJson(out,true);
    CHECK(out.str().find("\"activation_mode\":\"phase_c_plus_3s\"")!=std::string::npos);
    CHECK(out.str().find("\"phase3_observed_qpc\":100")!=std::string::npos);
    CHECK(out.str().find("\"activation_not_before_qpc\":3100")!=std::string::npos);
    CHECK(out.str().find("\"actual_activation_qpc\":3150")!=std::string::npos);
    CHECK(out.str().find("\"activation_failure\":\"none\"")!=std::string::npos);
    f.now=8000;r.ObserveActivationPhase(3,true);CHECK(!Active(Begin(r,Kind::TrackedDraw,K(2)))&&f.beginEpoch==1);
    // Default mode ignores the new phase service, including missing phase proof.
    Fake legacy;Recorder original(legacy);original.ConfigureActivation(ActivationMode::FirstEight,false);
    CHECK(original.Configure(1,2));original.ObserveActivationPhase(4,false);
    const auto token=Begin(original,Kind::TrackedDraw,K());CHECK(Active(token));original.End(token);original.Stop();
    CHECK(original.Report(s,true)&&s.beginQpc==100&&!s.phase3ObservedQpc&&!s.activationNotBeforeQpc);
    return 0;
}
int DelayedActivationFailures(){
    Snapshot s;
    for(const auto mode:{ActivationMode::Invalid,ActivationMode::PhaseCPlus3s}){
        Fake f;Recorder r(f);r.ConfigureActivation(mode,false);CHECK(!r.Configure(1,2));
        CHECK(!Active(Begin(r,Kind::TrackedDraw,K()))&&!f.configured&&!f.beginEpoch&&!f.stamped);
        CHECK(r.Report(s,true)&&s.activationFailure==(mode==ActivationMode::Invalid?
            ActivationFailure::InvalidOption:ActivationFailure::MissingPrerequisite));
    }
    for(unsigned variant=0;variant<4;++variant){
        Fake f;Recorder r(f);r.ConfigureActivation(ActivationMode::PhaseCPlus3s,true);CHECK(r.Configure(1,2));
        if(variant!=0)r.ObserveActivationPhase(3,true);
        r.ObserveActivationPhase(variant==2?2:variant==3?3:4,false);
        r.ObserveActivationPhase(3,true);f.now=4000;
        CHECK(!Active(Begin(r,Kind::TrackedDraw,K()))&&!f.beginEpoch&&!f.stamped);r.Stop();
        CHECK(r.Report(s,true)&&!s.available&&s.activationFailure==(variant==3?
            ActivationFailure::MissingPrerequisite:ActivationFailure::PhaseDeparted));
    }
    {Fake f;Recorder r(f);r.ConfigureActivation(ActivationMode::PhaseCPlus3s,true);CHECK(r.Configure(1,2));
        f.now=UINT64_MAX-2999;r.ObserveActivationPhase(3,true);
        CHECK(!Active(Begin(r,Kind::TrackedDraw,K()))&&!f.beginEpoch&&!f.stamped);r.Stop();
        CHECK(r.Report(s,true)&&s.activationFailure==ActivationFailure::Clock&&!s.activationNotBeforeQpc);}
    {Fake f;Recorder r(f);r.ConfigureActivation(ActivationMode::PhaseCPlus3s,true);CHECK(r.Configure(1,2));
        r.ObserveActivationPhase(3,true);f.now=99;CHECK(!Active(Begin(r,Kind::TrackedDraw,K())));r.Stop();
        CHECK(r.Report(s,true)&&s.activationFailure==ActivationFailure::Clock&&!f.beginEpoch);}
    {Fake f;Recorder r(f);r.ConfigureActivation(ActivationMode::PhaseCPlus3s,true);CHECK(r.Configure(1,2));
        r.Stop();CHECK(r.Report(s,true)&&s.activationFailure==ActivationFailure::NotReached&&!s.beginQpc);
        std::ostringstream out;r.WriteJson(out,true);CHECK(out.str().find("\"activation_failure\":\"activation_not_reached\"")!=std::string::npos);}
    return 0;
}
int ExactOwnersAndKeys(){
    Fake f;Recorder r(f);CHECK(r.Configure(1,2));
    f.identity.creation++;CHECK(!Active(Begin(r,Kind::TrackedDraw,K()))&&!r.Configure(1,2));
    f.identity.creation--;CHECK(!r.Configure(3,2));CHECK(!r.Configure(1,4));
    CHECK(!Active(r.Begin(Kind::TrackedDraw,K(),0,0,4))&&!f.beginEpoch);
    const auto left=Begin(r,Kind::TrackedDraw,K());CHECK(Active(left));
    CHECK(!Active(Begin(r,Kind::TrackedDraw,K()))); // No second query issuance.
    auto bad=K();++bad.tracking;CHECK(!Active(Begin(r,Kind::DepthCopy,bad)));
    bad=K();++bad.request;CHECK(!Active(Begin(r,Kind::DepthCopy,bad)));
    CHECK(!Active(Begin(r,Kind::DepthCopy,K(2)))); // No implicit pair admission.
    auto forged=left;++forged.serial;r.End(forged);CHECK(f.stamped==1);
    r.End(left);r.End(left);CHECK(f.stamped==2&&!f.reused);
    auto partial=K();partial.owner=partial.request=partial.channel=partial.deviceEpoch=0;partial.resourceEpoch=9;
    CHECK(!Active(Begin(r,Kind::DepthCopy,partial)));CHECK(!Active(Begin(r,Kind::EyeCapture,partial)));
    CHECK(!Active(Begin(r,Kind::TrackedDraw,partial,1)));
    const auto exportToken=Begin(r,Kind::ExportCopy,partial);CHECK(Active(exportToken));r.End(exportToken);
    partial.resourceEpoch=10;CHECK(!Active(Begin(r,Kind::EyeCapture,partial)));
    r.Stop();Snapshot s;CHECK(r.Report(s,true));
    CHECK(s.ownerMismatch==2&&s.deviceMismatch==1&&s.contextMismatch==2&&s.invalidKey==8&&s.duplicate==2);
    CHECK(s.count==2&&!f.reused);return 0;
}
int CompleteNestedEpisode(){
    Fake f;Recorder r(f);CHECK(r.Configure(1,2));Pair(r,1);
    auto ordinary=K(50);ordinary.tracking=ordinary.space=0;ordinary.predictedNs=0;
    r.End(Begin(r,Kind::OrdinaryDraw,ordinary));
    CHECK(!f.endEpoch&&!f.reads&&f.stamped==20);
    r.Poll();CHECK(!f.reads);f.now=2100;r.Poll();
    Snapshot s;CHECK(r.Report(s,true)&&s.available&&s.count==10&&s.validTrackedPairs==1);
    CHECK(s.pairs==1&&s.ordinary==1&&s.createdQueries==193&&s.gpuFrequency==1000000);
    CHECK(f.beginEpoch==1&&f.endEpoch==1&&f.resets==1&&!f.reused);
    CHECK(s.rows[0].gpuBegin<s.rows[1].gpuBegin&&s.rows[0].gpuEnd>s.rows[1].gpuEnd);
    for(unsigned i=0;i<s.count;++i)CHECK(s.rows[i].result==Result::Valid&&s.rows[i].observedQpc==2100);
    const auto calls=f.reads;r.Stop();r.Poll();CHECK(f.reads==calls&&f.endEpoch==1);
    std::ostringstream out;r.WriteJson(out,true);CHECK(out.str().find("\"valid_tracked_pairs\":1")!=std::string::npos);
    CHECK(out.str().find("\"nested_intervals\":true")!=std::string::npos);
    CHECK(!r.Report(s,false)&&!s.available&&!s.count);std::ostringstream unavailable;r.WriteJson(unavailable,false);
    CHECK(unavailable.str().find("\"callbacks_drained\":false")!=std::string::npos);return 0;
}
int BusyTimeoutAndStop(){
    Fake f;Recorder r(f);CHECK(r.Configure(1,2));Pair(r,1);f.busyEpoch=true;f.now=2100;r.Poll();
    CHECK(f.endEpoch==1&&f.reads==1);r.Poll();CHECK(f.reads==2&&f.stamped==18);
    f.busyEpoch=false;f.busyStamp=true;r.Poll();CHECK(f.reads==21);
    f.now=4100;r.Poll();Snapshot s;CHECK(r.Report(s,true)&&!s.available);
    CHECK(s.rows[0].result==Result::Timeout&&f.resets==1&&f.reads==21);
    Fake g;Recorder stopped(g);CHECK(stopped.Configure(1,2));const auto token=Begin(stopped,Kind::TrackedDraw,K());
    stopped.Stop();stopped.End(token);CHECK(stopped.Report(s,true)&&s.rows[0].result==Result::Incomplete);
    CHECK(g.stamped==1&&g.endEpoch==1);
    Fake h;Recorder active(h);CHECK(active.Configure(1,2));const auto outer=Begin(active,Kind::TrackedDraw,K());
    const auto inner=Begin(active,Kind::DepthCopy,K());h.now=2100;active.Poll();CHECK(!h.endEpoch&&!h.resets);
    h.now=4100;active.Poll();CHECK(h.endEpoch==1&&h.resets==1&&h.stamped==2&&!h.reads);
    active.End(inner);active.End(outer);active.Stop();CHECK(h.endEpoch==1&&h.stamped==2);
    CHECK(active.Report(s,true)&&s.rows[0].result==Result::Timeout&&s.rows[1].result==Result::Timeout&&!s.available);
    return 0;
}
int RejectedResults(){
    Snapshot s;
    {Fake f;Recorder r(f);CHECK(r.Configure(1,2));Pair(r,1);f.disjoint=true;f.now=2100;r.Poll();
        CHECK(r.Report(s,true)&&s.disjoint&&!s.available&&s.rows[0].result==Result::Disjoint&&!s.validTrackedPairs);}
    {Fake f;Recorder r(f);CHECK(r.Configure(1,2));Pair(r,1);f.gpuHz=0;f.now=2100;r.Poll();
        CHECK(r.Report(s,true)&&!s.available&&s.clockErrors==1&&s.rows[0].result==Result::ClockError);}
    {Fake f;Recorder r(f);CHECK(r.Configure(1,2));Pair(r,1);f.times[0][1]=f.times[0][0]-1;f.now=2100;r.Poll();
        CHECK(r.Report(s,true)&&s.clockErrors==1&&s.rows[0].result==Result::ClockError&&!s.validTrackedPairs);}
    {Fake f;Recorder r(f);CHECK(r.Configure(1,2));auto t=Begin(r,Kind::TrackedDraw,K());r.End(t,false);f.now=2100;r.Poll();
        CHECK(r.Report(s,true)&&s.rows[0].result==Result::Abnormal&&f.stamped==1);}
    {Fake f;Recorder r(f);CHECK(r.Configure(1,2));auto t=Begin(r,Kind::TrackedDraw,K());f.now=99;r.End(t);r.Poll();
        CHECK(r.Report(s,true)&&!s.available&&s.clockErrors>=1&&s.rows[0].result==Result::ClockError);}
    {Fake f;f.configureResult=E_OUTOFMEMORY;Recorder r(f);CHECK(!r.Configure(1,2));
        CHECK(r.Report(s,true)&&s.phase==Phase::Failed&&s.queryErrors==1&&!f.beginEpoch);}
    {Fake f;Recorder r(f);CHECK(r.Configure(1,2));Pair(r,1);f.epochResult=E_FAIL;f.now=2100;r.Poll();
        CHECK(r.Report(s,true)&&s.queryErrors==1&&s.rows[0].result==Result::QueryError&&!s.available);}
    {Fake f;Recorder r(f);CHECK(r.Configure(1,2));Pair(r,1);f.stampResult=E_FAIL;f.now=2100;r.Poll();
        CHECK(r.Report(s,true)&&s.queryErrors==9&&s.rows[0].result==Result::QueryError&&!s.validTrackedPairs);}
    return 0;
}
int FixedBudget(){
    Fake f;Recorder r(f);CHECK(r.Configure(1,2));for(unsigned frame=1;frame<=PairLimit;++frame)Pair(r,frame);
    for(unsigned frame=100;frame<100+OrdinaryLimit;++frame)r.End(Begin(r,Kind::OrdinaryDraw,K(frame)));
    CHECK(!Active(Begin(r,Kind::TrackedDraw,K(9))));CHECK(!Active(Begin(r,Kind::OrdinaryDraw,K(200))));
    CHECK(!Active(Begin(r,Kind::DepthCopy,K(9))));CHECK(!Active(Begin(r,Kind::EyeCapture,K(9))));
    auto exportKey=K(9);exportKey.resourceEpoch=12;CHECK(!Active(Begin(r,Kind::ExportCopy,exportKey)));
    CHECK(!Active(Begin(r,Kind::DepthCopy,K(1),2)));f.now=2100;r.Poll();Snapshot s;
    CHECK(r.Report(s,true)&&s.available&&s.count==88&&s.validTrackedPairs==8&&s.capped==5&&s.invalidKey==1);
    CHECK(f.stamped==176&&!f.reused&&f.beginEpoch==1&&f.endEpoch==1);return 0;
}
// Separate explicit invocation only: real hardware D3D11 queries, not CTest's
// default pure-policy run. This harness supplies activation instead of a game
// actor, and flushes ONLY its own submitted test work after the epoch closes.
struct SmokeBackend:NativeBackend {
    unsigned stamped=0,reused=0;std::array<std::array<bool,2>,Capacity> issued{};
    static bool OptedIn()noexcept{return true;}
    static bool AllowOwner()noexcept{return true;}
    void Stamp(unsigned slot,bool end)noexcept {
        ++stamped;if(issued[slot][end])++reused;issued[slot][end]=true;NativeBackend::Stamp(slot,end);
    }
};
int GpuSmoke(){
    using Microsoft::WRL::ComPtr;
    ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;D3D_FEATURE_LEVEL level{};
    const D3D_FEATURE_LEVEL levels[]{D3D_FEATURE_LEVEL_11_0};
    const auto hr=D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,levels,1,D3D11_SDK_VERSION,&device,&level,&context);
    if(FAILED(hr)){std::cerr<<"GPU smoke unavailable: hardware D3D11CreateDevice HRESULT="<<std::hex<<std::uint32_t(hr)<<'\n';return 2;}
    D3D11_TEXTURE2D_DESC desc{};desc.Width=desc.Height=32;desc.MipLevels=desc.ArraySize=desc.SampleDesc.Count=1;
    desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;desc.Usage=D3D11_USAGE_DEFAULT;
    ComPtr<ID3D11Texture2D> source,target;
    CHECK(SUCCEEDED(device->CreateTexture2D(&desc,nullptr,&source))&&SUCCEEDED(device->CreateTexture2D(&desc,nullptr,&target)));
    SmokeBackend backend;BasicRecorder<SmokeBackend> r(backend);
    if(!r.Configure(device.Get(),context.Get())){Snapshot s;r.Report(s,true);
        std::cerr<<"GPU smoke query setup failed: HRESULT="<<std::hex<<std::uint32_t(s.error)<<'\n';return 2;}
    const auto key=K();
    for(unsigned eye=0;eye<2;++eye){
        const auto draw=r.Begin(Kind::TrackedDraw,key,eye,0,context.Get());CHECK(Active(draw));
        for(unsigned stage=0;stage<2;++stage){const auto depth=r.Begin(Kind::DepthCopy,key,eye,stage,context.Get());CHECK(Active(depth));
            context->CopyResource(target.Get(),source.Get());r.End(depth);}
        r.End(draw);const auto copy=r.Begin(Kind::EyeCapture,key,eye,0,context.Get());CHECK(Active(copy));
        context->CopyResource(source.Get(),target.Get());r.End(copy);
    }
    auto exportKey=key;exportKey.owner=exportKey.request=exportKey.channel=exportKey.deviceEpoch=0;exportKey.resourceEpoch=1;
    const auto exported=r.Begin(Kind::ExportCopy,exportKey,0,0,context.Get());CHECK(Active(exported));
    context->CopyResource(target.Get(),source.Get());r.End(exported);
    Snapshot s;const auto deadline=GetTickCount64()+5000;bool flushed=false;
    do {
        r.Poll();r.Report(s,true);
        if(s.epochEnded&&!flushed){context->Flush();flushed=true;} // Harness submission only.
        if(s.phase==Phase::Finished||s.phase==Phase::Failed)break;
        Sleep(1);
    }while(GetTickCount64()<deadline);
    r.Stop();r.Report(s,true);
    if(!s.available||s.validTrackedPairs!=1||s.disjoint||s.queryErrors||s.clockErrors){
        std::cerr<<"GPU smoke did not produce a complete valid pair: ";r.WriteJson(std::cerr,true);std::cerr<<'\n';return 2;}
    CHECK(s.count==9&&s.createdQueries==193&&s.gpuFrequency>0&&!backend.reused&&backend.stamped==18);
    CHECK(s.rows[0].gpuBegin<=s.rows[1].gpuBegin&&s.rows[0].gpuEnd>=s.rows[1].gpuEnd);
    CHECK(s.rows[4].gpuBegin<=s.rows[5].gpuBegin&&s.rows[4].gpuEnd>=s.rows[5].gpuEnd);
    std::cout<<"{\"gpu_smoke\":true,\"hardware_device\":true,\"intervals\":"<<s.count
        <<",\"valid_pairs\":"<<s.validTrackedPairs<<",\"gpu_frequency\":"<<s.gpuFrequency
        <<",\"query_reuse\":"<<backend.reused<<",\"harness_flush\":"<<(flushed?"true":"false")<<"}\n";
    return 0;
}
}
int main(int argc,char** argv){if(argc==2&&std::string(argv[1])=="--gpu")return GpuSmoke();CHECK(argc==1);
    CHECK(OptInAndActivation()==0);CHECK(DelayedActivation()==0);CHECK(DelayedActivationFailures()==0);
    CHECK(ExactOwnersAndKeys()==0);CHECK(CompleteNestedEpisode()==0);
    CHECK(BusyTimeoutAndStop()==0);CHECK(RejectedResults()==0);CHECK(FixedBudget()==0);return 0;}
