#include "Bc2MechanismAudio.h"
#include "Bc2AudioCachePath.h"
#include "Bc2ShellReloadAudio.h"
#include <Windows.h>
#include <mmsystem.h>
#include <mmddk.h>
#include <mmdeviceapi.h>
#include <audiopolicy.h>
#include <wrl/client.h>
#include <bcrypt.h>
#include <algorithm>
#include <MinHook.h>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <fstream>
#include <memory>
#include <mutex>
#include <ostream>
#include <thread>
#include <vector>
#include <string_view>

namespace fvr::bc2::mechanismAudio {
namespace {
using namespace interaction;
using Native=void(__thiscall*)(void*,const void*,unsigned);
Native startOriginal=nullptr,stopOriginal=nullptr;
std::array<void*,2> hooks{};
std::atomic<bool> enabled=false,replacing=false,playerReady=false,workerQuit=false;
std::atomic<unsigned> inFlight=0,starts=0,stops=0,suppressed=0,rowsLost=0,queued=0,played=0,playFailed=0,stale=0;
std::atomic<unsigned> physicalStages=0,cueRowsLost=0;
std::atomic<std::int64_t> routeVerifiedNs=0,lastRouteVerifiedNs=0;
std::atomic<unsigned> routeChecks=0,routeFailures=0,cleanupFailures=0,lastCleanupError=0,suppressionDenied=0;
std::atomic<bool> routeFailed=false;
MechanismAudioSuppressionLedger<> suppressionLedger;std::atomic_flag suppressionGate=ATOMIC_FLAG_INIT;
MechanismAudioStockPassLatch stockPass;
void WakeAudioPlayer()noexcept;
bool RecordSuppression(const MechanismAudioIdentity& id)noexcept {
    if(suppressionGate.test_and_set(std::memory_order_acquire))return false;
    const bool accepted=suppressionLedger.Record(id);suppressionGate.clear(std::memory_order_release);return accepted;
}
bool WasSuppressed(const MechanismAudioIdentity& id)noexcept {
    if(suppressionGate.test_and_set(std::memory_order_acquire))return false;
    const bool found=suppressionLedger.Contains(id);suppressionGate.clear(std::memory_order_release);return found;
}
std::atomic<unsigned> outputDevice=UINT_MAX;std::atomic<bool> endpointMatched=false;
std::atomic<std::shared_ptr<const Source>> current;
std::int64_t Now() noexcept {
    LARGE_INTEGER count{},frequency{};QueryPerformanceCounter(&count);QueryPerformanceFrequency(&frequency);
    return frequency.QuadPart>0?std::int64_t((static_cast<long double>(count.QuadPart)*1000000000.L)/frequency.QuadPart):0;
}
bool Read(std::uintptr_t at,void* out,std::size_t size) noexcept {
    if(at<0x10000||size==0||size>4096||at+size>0x100000000ull)return false;
    SIZE_T got=0;return ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(at),out,size,&got)&&got==size;
}
unsigned U32(std::uintptr_t at) noexcept {unsigned out=0;Read(at,&out,4);return out;}
bool Fresh(const Source& s,std::int64_t now) noexcept {
    return s.soldier>=0x10000&&s.weak>=0x10000&&s.weapon>=0x10000&&s.identity.actor&&s.identity.actorGeneration&&
        s.identity.item&&s.identity.itemGeneration&&s.manual&&s.focused&&s.tracked&&s.observedNs>0&&
        s.observedNs<=now&&now<s.deadlineNs&&s.deadlineNs-s.observedNs<=200000000&&
        U32(s.weak)==s.soldier+4;
}
constexpr std::array<unsigned char,16> spasGuid{0x1b,0xf4,0x9c,0x2d,0xba,0x3a,0x22,0x4a,0x82,0xa8,0x64,0x05,0x1c,0x18,0x04,0x32};
constexpr std::array<unsigned char,16> m95Guid{0x5e,0xf2,0x55,0x5f,0xb4,0xd4,0x20,0x4a,0x9f,0x14,0xca,0x15,0x2a,0x96,0x10,0xfa};
Cue Match(const std::array<unsigned char,16>& guid) noexcept {return guid==spasGuid?Cue::Spas:guid==m95Guid?Cue::M95:Cue::None;}
struct Row {std::int64_t ns=0;unsigned thread=0,self=0,owner=0,guidAddress=0,unused=0;std::array<unsigned char,16> guid{};
    unsigned sourceSoldier=0,sourceWeapon=0,cue=0;std::uint64_t cycle=0,shot=0;unsigned char ownerFlags=0,callbackFirstPerson=0;
    bool start=false,readable=false,local=false,fresh=false,suppressed=false;};
std::array<Row,256> rows{};unsigned rowCount=0,knownRows=0,otherRows=0;std::atomic_flag rowGate=ATOMIC_FLAG_INIT;
void Record(const Row& row) noexcept {
    if(rowGate.test_and_set(std::memory_order_acquire)){++rowsLost;return;}
    // Preserve the first96 and last96 pump/bolt rows independently from other
    // local sounds. Fire/reload chatter cannot exhaust the relevant journal.
    unsigned slot=0;
    if(row.cue){const auto n=knownRows++;slot=n<96?n:96+(n-96)%96;}
    else {const auto n=otherRows++;slot=192+(n<32?n:32+(n-32)%32);}
    rows[slot]=row;rowCount=std::max(rowCount,slot+1);
    rowGate.clear(std::memory_order_release);
}
void Dispatch(bool start,void* self,const void* guid,unsigned unused) noexcept {
    ++inFlight;struct Exit{~Exit(){--inFlight;}} exit;
    const auto original=start?startOriginal:stopOriginal;
    if(!enabled.load(std::memory_order_acquire)){original(self,guid,unused);return;}
    if(start)++starts;else ++stops;
    Row row;row.ns=Now();row.thread=GetCurrentThreadId();row.self=unsigned(reinterpret_cast<std::uintptr_t>(self));
    row.guidAddress=unsigned(reinterpret_cast<std::uintptr_t>(guid));row.unused=unused;row.start=start;
    if(!Read(row.guidAddress,row.guid.data(),row.guid.size())){original(self,guid,unused);return;}
    if(shellReloadAudio::Dispatch(start,row.self,row.guid))return;
    const auto cue=Match(row.guid);row.cue=unsigned(cue);
    // All unrelated animation sounds pass through without owner/source reads.
    if(cue==Cue::None){original(self,guid,unused);return;}
    row.owner=U32(row.self+4);
    row.readable=Read(row.self+0x50,&row.callbackFirstPerson,1)&&
        Read(row.owner+0x475,&row.ownerFlags,1);
    const auto source=current.load(std::memory_order_acquire);
    if(source){row.sourceSoldier=source->soldier;row.sourceWeapon=source->weapon;row.cycle=source->identity.cycle;row.shot=source->identity.shot;
        row.local=row.readable&&row.owner==source->soldier;row.fresh=Fresh(*source,row.ns);}
    // Never suppress NPC audio, third-person callbacks, fire/reload sounds,
    // unknown animation identities or a gun without a live manual owner.
    // M95's graph also emits casing impacts: it remains observation-only.
    const bool intended=start&&source&&row.local&&row.callbackFirstPerson==1&&(row.ownerFlags&1)&&
        cue==Cue::Spas&&source->cue==Cue::Spas&&replacing.load(std::memory_order_acquire);
    const bool eligible=intended&&row.fresh&&stockPass.AllowsReplacement()&&playerReady.load(std::memory_order_acquire)&&
        MechanismAudioRouteFresh(routeVerifiedNs.load(std::memory_order_acquire),row.ns);
    // Unknown debt or a busy receipt ledger passes through to stock. Later
    // physical stages cannot infer that this native start was suppressed.
    row.suppressed=eligible&&RecordSuppression(source->identity);
    if(eligible&&!row.suppressed)++suppressionDenied;
    stockPass.ObserveStart(intended,row.suppressed);
    if(!stockPass.AllowsReplacement()){playerReady=false;routeVerifiedNs=0;WakeAudioPlayer();}
    if(row.local||cue!=Cue::None)Record(row);
    if(row.suppressed){++suppressed;return;}
    original(self,guid,unused);
}
void __fastcall Start(void* self,void*,const void* guid,unsigned unused){Dispatch(true,self,guid,unused);}
void __fastcall End(void* self,void*,const void* guid,unsigned unused){Dispatch(false,self,guid,unused);}

struct CueWave {std::vector<char> pcm;};
std::array<CueWave,2> waves;
std::mutex queueMutex;std::condition_variable queueChanged;std::thread worker;
void WakeAudioPlayer()noexcept {queueChanged.notify_one();}
enum class CueDisposition:unsigned { ObservedOnly,NotReady,QueueFull,Queued,Playing,Completed,Interrupted,Stale,Failed,StockStartNotSuppressed };
enum class CueStop:unsigned { None,Completed,MissingSource,ExpiredOrUntracked,ItemLifetimeChanged,CycleSuperseded,NextCue,Shutdown,Failure,RouteChanged,StockStartPassed };
struct CueReceipt {
    std::uint64_t receipt=0;MechanismAudioEvent event{};
    std::int64_t admittedNs=0,queuedNs=0,writeBeginNs=0,writeEndNs=0,finishedNs=0;
    unsigned waveResult=0;CueDisposition disposition=CueDisposition::ObservedOnly;CueStop stop=CueStop::None;
};
std::array<CueReceipt,64> cueRows{};std::uint64_t cueRowsTotal=0;std::atomic_flag cueGate=ATOMIC_FLAG_INIT;
std::uint64_t RecordCue(CueReceipt row)noexcept {
    if(cueGate.test_and_set(std::memory_order_acquire)){++cueRowsLost;return 0;}
    row.receipt=++cueRowsTotal;cueRows[(row.receipt-1)%cueRows.size()]=row;
    cueGate.clear(std::memory_order_release);return row.receipt;
}
void UpdateCue(std::uint64_t receipt,CueDisposition disposition,std::int64_t begin=0,std::int64_t end=0,unsigned result=0,CueStop stop=CueStop::None)noexcept {
    if(!receipt)return;
    if(cueGate.test_and_set(std::memory_order_acquire)){++cueRowsLost;return;}
    auto& row=cueRows[(receipt-1)%cueRows.size()];
    if(row.receipt==receipt){row.disposition=disposition;row.waveResult=result;row.stop=stop;
        if(begin)row.writeBeginNs=begin;if(end)row.writeEndNs=end;
        if(disposition!=CueDisposition::Playing)row.finishedNs=Now();}
    cueGate.clear(std::memory_order_release);
}
struct QueuedEvent {MechanismAudioEvent event{};std::uint64_t receipt=0;};
std::array<QueuedEvent,16> events{};unsigned eventHead=0,eventCount=0;
MechanismAudioLedger<> ledger;
void DiscardQueued(CueStop reason)noexcept {
    std::lock_guard lock(queueMutex);
    while(eventCount){const auto& event=events[eventHead];UpdateCue(event.receipt,CueDisposition::Stale,0,0,0,reason);
        ++stale;eventHead=(eventHead+1)%events.size();--eventCount;}
}
bool HashMatches(const std::vector<char>& data,std::string_view expected) noexcept {
    if(expected.size()!=64||data.size()>1024*1024)return false;
    std::array<unsigned char,32> hash{};
    if(BCryptHash(BCRYPT_SHA256_ALG_HANDLE,nullptr,0,reinterpret_cast<PUCHAR>(const_cast<char*>(data.data())),
        ULONG(data.size()),hash.data(),ULONG(hash.size()))<0)return false;
    constexpr char digits[]="0123456789abcdef";
    for(unsigned n=0;n<hash.size();++n)if(digits[hash[n]>>4]!=expected[n*2]||digits[hash[n]&15]!=expected[n*2+1])return false;
    return true;
}
bool CacheManifest(const std::filesystem::path& directory){
    std::ifstream stream(directory/L"mechanism-audio.json",std::ios::binary|std::ios::ate);if(!stream)return false;
    const auto size=stream.tellg();if(size<1||size>4096)return false;
    std::vector<char> data(std::size_t(size),char{});stream.seekg(0);if(!stream.read(data.data(),size))return false;
    return HashMatches(data,"9505a703347eb5b7a2e64f54f1972a7a5d727b45dbe1a6756e7b43e4de7ecded");
}
bool LoadWave(const std::filesystem::path& path,std::size_t frames,std::string_view expected,CueWave& result){
    if(!LocalAudioCacheFile(path))return false;
    std::ifstream stream(path,std::ios::binary|std::ios::ate);if(!stream)return false;
    const auto size=stream.tellg();if(size!=std::streamoff(44+frames*2))return false;
    std::vector<char> data(std::size_t(size),char{});stream.seekg(0);if(!stream.read(data.data(),size))return false;
    if(!HashMatches(data,expected))return false;
    const auto u16=[&](std::size_t p){unsigned short v=0;std::memcpy(&v,data.data()+p,2);return v;};
    const auto u32=[&](std::size_t p){unsigned v=0;std::memcpy(&v,data.data()+p,4);return v;};
    if(std::memcmp(data.data(),"RIFF",4)||std::memcmp(data.data()+8,"WAVEfmt ",8)||std::memcmp(data.data()+36,"data",4)||
       u32(4)!=data.size()-8||u32(16)!=16||u16(20)!=1||u16(22)!=1||u32(24)!=48000||u32(28)!=96000||
       u16(32)!=2||u16(34)!=16||u32(40)!=frames*2)return false;
    result.pcm.assign(data.begin()+44,data.end());return true;
}
// Resolve the endpoint that already hosts THIS game's audio session. A default
// mapper may route to a different device after Steam Link reconnects; refuse it.
struct OutputRoute {UINT device=UINT_MAX;std::wstring endpoint;explicit operator bool()const noexcept{return device!=UINT_MAX&&!endpoint.empty();}};
OutputRoute GameOutputDevice() {
    using Microsoft::WRL::ComPtr;
    ComPtr<IMMDeviceEnumerator> enumerator;ComPtr<IMMDeviceCollection> devices;
    if(FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator),nullptr,CLSCTX_ALL,IID_PPV_ARGS(&enumerator)))||
       FAILED(enumerator->EnumAudioEndpoints(eRender,DEVICE_STATE_ACTIVE,&devices)))return {};
    UINT count=0;if(FAILED(devices->GetCount(&count))||count>128)return {};
    std::wstring endpoint;
    for(UINT n=0;n<count;++n){
        ComPtr<IMMDevice> device;ComPtr<IAudioSessionManager2> manager;ComPtr<IAudioSessionEnumerator> sessions;
        if(FAILED(devices->Item(n,&device))||FAILED(device->Activate(__uuidof(IAudioSessionManager2),CLSCTX_ALL,nullptr,
            reinterpret_cast<void**>(manager.GetAddressOf())))||FAILED(manager->GetSessionEnumerator(&sessions)))continue;
        int size=0;if(FAILED(sessions->GetCount(&size))||size<0||size>4096)continue;
        bool found=false;
        for(int j=0;j<size;++j){ComPtr<IAudioSessionControl> control;ComPtr<IAudioSessionControl2> process;DWORD pid=0;AudioSessionState state{};
            if(SUCCEEDED(sessions->GetSession(j,&control))&&SUCCEEDED(control.As(&process))&&
               SUCCEEDED(process->GetProcessId(&pid))&&pid==GetCurrentProcessId()&&
               SUCCEEDED(control->GetState(&state))&&state!=AudioSessionStateExpired){found=true;break;}}
        if(!found)continue;
        LPWSTR id=nullptr;if(FAILED(device->GetId(&id))||!id)continue;
        const std::wstring candidate(id);CoTaskMemFree(id);
        if(!endpoint.empty()&&endpoint!=candidate)return {};
        endpoint=candidate;
    }
    if(endpoint.empty())return {};
    const auto waveCount=waveOutGetNumDevs();
    for(UINT n=0;n<waveCount&&n<128;++n){
        const auto device=reinterpret_cast<HWAVEOUT>(static_cast<UINT_PTR>(n));ULONG size=0;
        if(waveOutMessage(device,DRV_QUERYFUNCTIONINSTANCEIDSIZE,reinterpret_cast<DWORD_PTR>(&size),0)!=MMSYSERR_NOERROR||
           size<sizeof(wchar_t)||size>8192||size%sizeof(wchar_t))continue;
        std::vector<wchar_t> id(size/sizeof(wchar_t),L'\0');
        if(waveOutMessage(device,DRV_QUERYFUNCTIONINSTANCEID,reinterpret_cast<DWORD_PTR>(id.data()),size)==MMSYSERR_NOERROR&&
           id.back()==L'\0'&&_wcsicmp(id.data(),endpoint.c_str())==0)return {n,endpoint};
    }
    return {};
}
struct DriverBuffer {
    HWAVEOUT device=nullptr;WAVEHDR header{};std::array<char,11344*2> pcm{};
    MechanismAudioBufferLease lease;MechanismAudioIdentity identity{};std::uint64_t receipt=0;
};
// At most one device/buffer can be retained. Install refuses a later worker
// while it exists; callback mode is NULL, so no driver callback enters our DLL.
std::atomic<DriverBuffer*> retainedDriver=nullptr;
void DisableAudio()noexcept {playerReady.store(false,std::memory_order_release);routeVerifiedNs.store(0,std::memory_order_release);}
void CleanupFailed(DriverBuffer& state,unsigned code)noexcept {
    DisableAudio();++cleanupFailures;++playFailed;lastCleanupError=code;
    UpdateCue(state.receipt,CueDisposition::Failed,0,0,code,CueStop::Failure);
}
bool ReleaseBuffer(DriverBuffer& state,CueStop reason)noexcept {
    if(state.lease.Retained())return false;
    if(state.lease.CanPrepare())return true;
    const bool completed=(state.header.dwFlags&WHDR_DONE)!=0;
    if(state.lease.Phase()==MechanismAudioBufferPhase::Queued){
        const auto result=completed?MMSYSERR_NOERROR:waveOutReset(state.device);
        if(!state.lease.Returned(result==MMSYSERR_NOERROR)){CleanupFailed(state,result);return false;}
    }
    if(!state.lease.CanUnprepare())return false;
    const auto result=waveOutUnprepareHeader(state.device,&state.header,sizeof(state.header));
    if(!state.lease.Unprepared(result==MMSYSERR_NOERROR)){CleanupFailed(state,result);return false;}
    UpdateCue(state.receipt,completed?CueDisposition::Completed:CueDisposition::Interrupted,0,0,0,completed?CueStop::Completed:reason);
    state.header={};state.identity={};state.receipt=0;return true;
}
void CloseOrRetain(DriverBuffer* state)noexcept {
    if(!state)return;DisableAudio();
    if(!state->device){delete state;return;}
    if(!ReleaseBuffer(*state,workerQuit.load()?CueStop::Shutdown:CueStop::Failure)){
        retainedDriver.store(state,std::memory_order_release);return;
    }
    const auto result=waveOutClose(state->device);
    if(!state->lease.Closed(result==MMSYSERR_NOERROR)){
        CleanupFailed(*state,result);retainedDriver.store(state,std::memory_order_release);return;
    }
    delete state;
}
void PlayerBody() {
    const auto com=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    if(FAILED(com)){++playFailed;return;}
    struct ComExit{~ComExit(){CoUninitialize();}} comExit;
    const auto selected=GameOutputDevice();outputDevice=selected.device;++routeChecks;
    if(!selected){++playFailed;++routeFailures;routeFailed=true;return;}
    std::unique_ptr<DriverBuffer,decltype(&CloseOrRetain)> storage(new DriverBuffer,&CloseOrRetain);
    auto& state=*storage;auto& active=state.header;
    WAVEFORMATEX format{WAVE_FORMAT_PCM,1,48000,96000,2,16,0};
    if(waveOutOpen(&state.device,selected.device,&format,0,0,CALLBACK_NULL)!=MMSYSERR_NOERROR){++playFailed;state.device=nullptr;return;}
    const auto confirmed=GameOutputDevice();++routeChecks;
    if(!confirmed||confirmed.device!=selected.device||_wcsicmp(confirmed.endpoint.c_str(),selected.endpoint.c_str())!=0){
        ++routeFailures;routeFailed=true;return;
    }
    endpointMatched=true;
    MechanismAudioRoutePolicy route;route.Open(Now());
    if(!stockPass.AllowsReplacement())return;
    routeVerifiedNs=route.VerifiedNs();lastRouteVerifiedNs=route.VerifiedNs();
    playerReady.store(true,std::memory_order_release);
    if(!stockPass.AllowsReplacement()){DisableAudio();return;}
    while(!workerQuit.load(std::memory_order_acquire)){
        if(!stockPass.AllowsReplacement()){DisableAudio();ReleaseBuffer(state,CueStop::StockStartPassed);break;}
        QueuedEvent next{};bool have=false;
        {std::unique_lock lock(queueMutex);queueChanged.wait_for(lock,std::chrono::milliseconds(10),[]{return eventCount||workerQuit.load()||!stockPass.AllowsReplacement();});
            if(eventCount){next=events[eventHead];eventHead=(eventHead+1)%events.size();--eventCount;have=true;}}
        auto now=Now();
        if(!stockPass.AllowsReplacement()){DisableAudio();
            if(have){++stale;UpdateCue(next.receipt,CueDisposition::Stale,0,0,0,CueStop::StockStartPassed);}
            ReleaseBuffer(state,CueStop::StockStartPassed);break;}
        if(route.CheckDue(now)){
            const auto observed=GameOutputDevice();++routeChecks;now=Now();
            const bool same=observed&&observed.device==selected.device&&_wcsicmp(observed.endpoint.c_str(),selected.endpoint.c_str())==0;
            if(!route.Validate(now,same)){
                DisableAudio();routeFailed=true;++routeFailures;
                ReleaseBuffer(state,CueStop::RouteChanged);break;
            }
            routeVerifiedNs.store(route.VerifiedNs(),std::memory_order_release);lastRouteVerifiedNs=route.VerifiedNs();
        }
        if(!route.Fresh(now)){DisableAudio();routeFailed=true;++routeFailures;ReleaseBuffer(state,CueStop::RouteChanged);break;}
        const auto source=current.load(std::memory_order_acquire);
        const bool usable=source&&Fresh(*source,now)&&source->cue==Cue::Spas;
        if(!state.lease.CanPrepare()){
            CueStop reason=CueStop::None;
            if(active.dwFlags&WHDR_DONE)reason=CueStop::Completed;
            else if(!source)reason=CueStop::MissingSource;
            else if(!usable)reason=CueStop::ExpiredOrUntracked;
            else if(source->identity.actor!=state.identity.actor||source->identity.actorGeneration!=state.identity.actorGeneration||
                    source->identity.item!=state.identity.item||source->identity.itemGeneration!=state.identity.itemGeneration)reason=CueStop::ItemLifetimeChanged;
            else if(!MechanismAudioSourceCurrent(source->identity,state.identity))reason=CueStop::CycleSuperseded;
            if(reason!=CueStop::None&&!ReleaseBuffer(state,reason))break;
        }
        if(!have)continue;
        const auto& event=next.event;
        if(!stockPass.AllowsReplacement()||!usable||!MechanismAudioSourceCurrent(source->identity,event.identity)||event.deadlineNs<=now||event.observedNs>now){++stale;UpdateCue(next.receipt,CueDisposition::Stale);continue;}
        if(!ReleaseBuffer(state,CueStop::NextCue))break;
        const auto n=unsigned(event.stage)-1;if(n>=waves.size()||waves[n].pcm.size()>state.pcm.size()){++playFailed;UpdateCue(next.receipt,CueDisposition::Failed);break;}
        std::copy(waves[n].pcm.begin(),waves[n].pcm.end(),state.pcm.begin());
        active.lpData=state.pcm.data();active.dwBufferLength=DWORD(waves[n].pcm.size());
        const auto prepare=waveOutPrepareHeader(state.device,&active,sizeof(active));
        if(!state.lease.Prepared(prepare==MMSYSERR_NOERROR)){++playFailed;DisableAudio();UpdateCue(next.receipt,CueDisposition::Failed,0,0,prepare);break;}
        state.identity=event.identity;state.receipt=next.receipt;
        const auto writeBegin=Now();const auto write=waveOutWrite(state.device,&active,sizeof(active));const auto writeEnd=Now();
        state.lease.Submitted(write==MMSYSERR_NOERROR);
        UpdateCue(next.receipt,write==MMSYSERR_NOERROR?CueDisposition::Playing:CueDisposition::Failed,writeBegin,writeEnd,write);
        if(write!=MMSYSERR_NOERROR){++playFailed;DisableAudio();state.receipt=0;break;}
        ++played;
    }
    DisableAudio();
}
void Player() noexcept {
    try{PlayerBody();}catch(...){DisableAudio();++playFailed;}
    DiscardQueued(!stockPass.AllowsReplacement()?CueStop::StockStartPassed:workerQuit.load()?CueStop::Shutdown:CueStop::Failure);
}
}

bool Install(std::uintptr_t image,const Config& config){
    if(enabled.load()||hooks[0]||hooks[1]||worker.joinable()||retainedDriver.load()||image!=0x400000)return false;
    // Exact entry bytes of thiscall(receiver, animationGuid*, unused), ret8.
    constexpr std::array<unsigned char,20> prologue{0x56,0x8b,0xf1,0x8b,0x46,0x04,0x8a,0x88,0x75,0x04,0,0,0x80,0xe1,0x01,0x38,0x4e,0x50,0x75,0x1a};
    auto stopPrologue=prologue;stopPrologue.back()=0x15;
    std::array<unsigned char,20> code{};
    if(!Read(image+0x481650,code.data(),code.size())||code!=prologue||
       !Read(image+0x481690,code.data(),code.size())||code!=stopPrologue)return false;
    if(inFlight.load())return false;
    rows={};rowCount=knownRows=otherRows=0;starts=stops=suppressed=rowsLost=queued=played=playFailed=stale=0;
    cueRows={};cueRowsTotal=0;physicalStages=cueRowsLost=0;
    suppressionLedger={};stockPass.ResetSession();suppressionDenied=0;routeVerifiedNs=lastRouteVerifiedNs=0;
    routeChecks=routeFailures=cleanupFailures=lastCleanupError=0;routeFailed=false;
    playerReady=false;endpointMatched=false;outputDevice=UINT_MAX;eventHead=eventCount=0;ledger={};
    if(config.replaceSpas&&(config.rearWave.parent_path()!=config.closureWave.parent_path()||
       !CacheManifest(config.rearWave.parent_path())||
       !LoadWave(config.rearWave,10080,"ca559f5bc46f67ba845ba40ae8008c2edcc0e572545ab4e4f3878b205b80b0da",waves[0])||
       !LoadWave(config.closureWave,11344,"e06db8a1d0ed46d2d49a73a4c59d5f606d30c75a3a7f4e939cf4aabbf47a14ea",waves[1])))return false;
    const auto init=MH_Initialize();if(init!=MH_OK&&init!=MH_ERROR_ALREADY_INITIALIZED)return false;
    hooks={reinterpret_cast<void*>(image+0x481650),reinterpret_cast<void*>(image+0x481690)};
    if(MH_CreateHook(hooks[0],reinterpret_cast<void*>(&Start),reinterpret_cast<void**>(&startOriginal))!=MH_OK){hooks={};return false;}
    if(MH_CreateHook(hooks[1],reinterpret_cast<void*>(&End),reinterpret_cast<void**>(&stopOriginal))!=MH_OK){MH_RemoveHook(hooks[0]);hooks={};return false;}
    replacing=config.replaceSpas;workerQuit=false;
    if(config.replaceSpas)worker=std::thread(Player);
    enabled.store(true,std::memory_order_release);
    if(MH_EnableHook(hooks[0])!=MH_OK||MH_EnableHook(hooks[1])!=MH_OK){Stop();DrainAndRemove();return false;}
    return true;
}
void Publish(const Source& source){current.store(std::make_shared<const Source>(source),std::memory_order_release);queueChanged.notify_one();}
bool Emit(const interaction::MechanismAudioEvent& event){
    if(!enabled.load())return false;
    const auto source=current.load();const auto now=Now();
    if(!source||source->cue==Cue::None||source->identity!=event.identity||!Fresh(*source,now))return false;
    std::unique_lock lock(queueMutex,std::try_to_lock);if(!lock.owns_lock())return false;
    if(ledger.Accept(event,now)!=MechanismAudioAdmission::Accepted)return false;
    ++physicalStages;CueReceipt row;row.event=event;row.admittedNs=now;
    // Observe accepted stages even with replacement disabled. The same ledger
    // deduplicates regrips/recovery, without changing any gameplay decision.
    if(!replacing.load()||source->cue!=Cue::Spas){RecordCue(row);return false;}
    if(!stockPass.AllowsReplacement()){row.disposition=CueDisposition::StockStartNotSuppressed;RecordCue(row);return false;}
    if(!playerReady.load()||!MechanismAudioRouteFresh(routeVerifiedNs.load(),now)){row.disposition=CueDisposition::NotReady;RecordCue(row);return false;}
    if(!WasSuppressed(event.identity)){row.disposition=CueDisposition::StockStartNotSuppressed;RecordCue(row);return false;}
    if(eventCount==events.size()){row.disposition=CueDisposition::QueueFull;RecordCue(row);return false;}
    row.disposition=CueDisposition::Queued;row.queuedNs=Now();const auto receipt=RecordCue(row);
    events[(eventHead+eventCount)%events.size()]={event,receipt};++eventCount;++queued;queueChanged.notify_one();return true;
}
void RetireActor(std::uint64_t actor,std::uint64_t generation){
    std::lock_guard lock(queueMutex);ledger.RetireActor(actor,generation);
    while(suppressionGate.test_and_set(std::memory_order_acquire))YieldProcessor();
    suppressionLedger.RetireActor(actor,generation);suppressionGate.clear(std::memory_order_release);
}
bool Stop() noexcept {
    enabled.store(false,std::memory_order_release);replacing=false;DisableAudio();current.store({});workerQuit=true;queueChanged.notify_all();
    bool ok=true;for(auto hook:hooks)if(hook){const auto result=MH_DisableHook(hook);ok&=result==MH_OK||result==MH_ERROR_DISABLED;}return ok;
}
bool DrainAndRemove(unsigned timeoutMs) noexcept {
    Stop();const auto start=GetTickCount64();while(inFlight.load()&&GetTickCount64()-start<timeoutMs)Sleep(1);
    if(inFlight.load())return false;
    if(worker.joinable())worker.join();bool ok=true;
    for(auto& hook:hooks)if(hook){const auto status=MH_RemoveHook(hook);if(status==MH_OK||status==MH_ERROR_NOT_CREATED)hook=nullptr;else ok=false;}
    return ok&&!retainedDriver.load(std::memory_order_acquire);
}
void Report(std::ostream& out){
    std::array<CueReceipt,64> cues{};std::uint64_t cueTotal=0;
    while(cueGate.test_and_set(std::memory_order_acquire))YieldProcessor();
    cues=cueRows;cueTotal=cueRowsTotal;cueGate.clear(std::memory_order_release);
    out<<"{\"enabled\":"<<enabled.load()<<",\"replacement\":"<<replacing.load()<<",\"player_ready\":"<<playerReady.load()
       <<",\"starts\":"<<starts.load()<<",\"stops\":"<<stops.load()<<",\"suppressed\":"<<suppressed.load()
       <<",\"queued\":"<<queued.load()<<",\"played\":"<<played.load()<<",\"play_failed\":"<<playFailed.load()<<",\"stale\":"<<stale.load()
       <<",\"output_device\":"<<outputDevice.load()<<",\"game_endpoint_matched\":"<<endpointMatched.load()
       <<",\"route_checks\":"<<routeChecks.load()<<",\"route_failures\":"<<routeFailures.load()<<",\"route_failed\":"<<routeFailed.load()
       <<",\"last_route_verified_ns\":"<<lastRouteVerifiedNs.load()<<",\"cleanup_failures\":"<<cleanupFailures.load()
       <<",\"last_cleanup_error\":"<<lastCleanupError.load()<<",\"retained_driver_storage_bytes\":"<<(retainedDriver.load()?sizeof(DriverBuffer):0)
       <<",\"suppression_receipt_denied\":"<<suppressionDenied.load()
       <<",\"stock_pass_veto\":"<<!stockPass.AllowsReplacement()
       <<",\"known_rows_total\":"<<knownRows<<",\"other_local_rows_total\":"<<otherRows
       <<",\"rows_lost\":"<<rowsLost.load()<<",\"rows\":[";
    while(rowGate.test_and_set(std::memory_order_acquire))YieldProcessor();
    bool comma=false;for(unsigned n=0;n<rowCount;++n){const auto& r=rows[n];if(!r.ns)continue;if(comma)out<<',';comma=true;
        out<<"{\"ns\":"<<r.ns<<",\"thread\":"<<r.thread<<",\"self\":"<<r.self<<",\"owner\":"<<r.owner
           <<",\"start\":"<<r.start<<",\"guid_address\":"<<r.guidAddress<<",\"unused\":"<<r.unused<<",\"guid\":\"";
        constexpr char hex[]="0123456789abcdef";for(auto c:r.guid)out<<hex[c>>4]<<hex[c&15];
        out<<"\",\"cue\":"<<r.cue<<",\"callback_first_person\":"<<unsigned(r.callbackFirstPerson)<<",\"owner_flags\":"<<unsigned(r.ownerFlags)
           <<",\"source_soldier\":"<<r.sourceSoldier<<",\"source_weapon\":"<<r.sourceWeapon<<",\"cycle\":"<<r.cycle<<",\"shot\":"<<r.shot
           <<",\"readable\":"<<r.readable<<",\"local\":"<<r.local<<",\"fresh\":"<<r.fresh<<",\"suppressed\":"<<r.suppressed<<'}';}
    rowGate.clear(std::memory_order_release);
    out<<"],\"physical_stages\":"<<physicalStages.load()<<",\"cue_rows_total\":"<<cueTotal
       <<",\"cue_rows_lost\":"<<cueRowsLost.load()<<",\"cues\":[";
    comma=false;for(const auto& r:cues){if(!r.receipt)continue;if(comma)out<<',';comma=true;const auto& id=r.event.identity;
        out<<"{\"receipt\":"<<r.receipt<<",\"actor\":"<<id.actor<<",\"actor_generation\":"<<id.actorGeneration
           <<",\"item\":"<<id.item<<",\"item_generation\":"<<id.itemGeneration<<",\"cycle\":"<<id.cycle<<",\"shot\":"<<id.shot
           <<",\"stage\":"<<unsigned(r.event.stage)<<",\"sequence\":"<<r.event.sequence
           <<",\"observed_ns\":"<<r.event.observedNs<<",\"deadline_ns\":"<<r.event.deadlineNs<<",\"admitted_ns\":"<<r.admittedNs
           <<",\"queued_ns\":"<<r.queuedNs<<",\"write_begin_ns\":"<<r.writeBeginNs<<",\"write_end_ns\":"<<r.writeEndNs
           <<",\"finished_ns\":"<<r.finishedNs<<",\"wave_result\":"<<r.waveResult<<",\"disposition\":"<<unsigned(r.disposition)
           <<",\"stop_reason\":"<<unsigned(r.stop)<<'}';}
    out<<"]}";
}
}
