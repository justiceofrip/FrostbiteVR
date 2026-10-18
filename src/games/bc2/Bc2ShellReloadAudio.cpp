#include "Bc2ShellReloadAudio.h"
#include "Bc2ShellReloadAudioProfile.h"
#include "fvr/interaction/MechanismAudio.h"
#include <Windows.h>
#include <mmsystem.h>
#include <mmddk.h>
#include <mmdeviceapi.h>
#include <audiopolicy.h>
#include <wrl/client.h>
#include <bcrypt.h>
#include <algorithm>
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

namespace fvr::bc2::shellReloadAudio {
namespace {
using namespace interaction;
constexpr unsigned WaveFrames=29173;
std::atomic<bool> enabled=false,replacing=false,ready=false,quit=false,unsafe=false;
std::atomic<unsigned> inFlight=0,starts=0,stops=0,suppressed=0,accepted=0,queued=0,played=0,completed=0,
    interrupted=0,stale=0,failures=0,routeChecks=0,routeFailures=0,cleanupFailures=0,rowsLost=0;
std::atomic<std::int64_t> routeVerifiedNs=0;
std::atomic<std::shared_ptr<const Source>> current;
ShellReloadAudioSuppression<> suppression;
std::atomic_flag suppressionGate=ATOMIC_FLAG_INIT;
std::vector<char> wave;
std::mutex queueMutex;std::condition_variable changed;std::thread worker;
ShellReloadAudioLedger<> ledger;
struct Pending {ShellReloadAudioEvent event{};std::uint64_t receipt=0;};
std::array<Pending,8> events{};unsigned head=0,count=0;
enum class Disposition:unsigned { Observed,NotReady,StockPassed,QueueFull,Queued,Playing,Completed,Interrupted,Stale,Failed };
struct Receipt {std::uint64_t id=0;ShellReloadAudioEvent event{};Disposition disposition=Disposition::Observed;
    std::int64_t admitted=0,written=0,finished=0;unsigned error=0;};
std::array<Receipt,64> receipts{};std::uint64_t receiptCount=0;std::atomic_flag receiptGate=ATOMIC_FLAG_INIT;
struct NativeRow {std::int64_t now=0;unsigned receiver=0,owner=0,source=0,kind=0,firstPerson=0,ownerFlags=0;
    std::uint64_t cycle=0;bool start=false,local=false,fresh=false,suppressed=false;};
std::array<NativeRow,128> nativeRows{};std::uint64_t nativeCount=0;std::atomic_flag nativeGate=ATOMIC_FLAG_INIT;
std::int64_t Now()noexcept {
    LARGE_INTEGER q{},f{};QueryPerformanceCounter(&q);QueryPerformanceFrequency(&f);
    return f.QuadPart>0?std::int64_t((static_cast<long double>(q.QuadPart)*1000000000.L)/f.QuadPart):0;
}
bool Read(std::uintptr_t at,void* out,std::size_t size)noexcept {
    if(at<0x10000||size>4096||at+size>0x100000000ull)return false;
    SIZE_T got=0;return ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(at),out,size,&got)&&got==size;
}
unsigned U32(std::uintptr_t at)noexcept {unsigned value=0;Read(at,&value,4);return value;}
bool Fresh(const Source& s,std::int64_t now)noexcept {
    return s.spas&&s.manual&&s.focused&&s.tracked&&ShellReloadAudioIdentityValid(s.identity)&&
        s.soldier>=0x10000&&s.weapon>=0x10000&&s.weak>=0x10000&&U32(s.weak)==s.soldier+4&&
        s.observedNs>0&&s.observedNs<=now&&s.deadlineNs>now&&s.deadlineNs-s.observedNs<=200000000;
}
void RecordNative(const NativeRow& row)noexcept {
    if(nativeGate.test_and_set(std::memory_order_acquire)){++rowsLost;return;}
    const auto serial=nativeCount++;nativeRows[serial<64?serial:64+(serial-64)%64]=row;
    nativeGate.clear(std::memory_order_release);
}
std::uint64_t Record(Receipt row)noexcept {
    if(receiptGate.test_and_set(std::memory_order_acquire)){++rowsLost;return 0;}
    row.id=++receiptCount;receipts[(row.id-1)%receipts.size()]=row;
    receiptGate.clear(std::memory_order_release);return row.id;
}
void Update(std::uint64_t id,Disposition disposition,unsigned error=0)noexcept {
    if(!id)return;
    if(receiptGate.test_and_set(std::memory_order_acquire)){++rowsLost;return;}
    auto& r=receipts[(id-1)%receipts.size()];
    if(r.id==id){r.disposition=disposition;r.error=error;if(disposition==Disposition::Playing)r.written=Now();else r.finished=Now();}
    receiptGate.clear(std::memory_order_release);
}
enum class SuppressionStatus { Denied,Allowed,Busy };
SuppressionStatus Suppressed(const ShellReloadAudioIdentity& id)noexcept {
    if(suppressionGate.test_and_set(std::memory_order_acquire))return SuppressionStatus::Busy;
    const bool allowed=suppression.Allows(id);suppressionGate.clear(std::memory_order_release);
    return allowed?SuppressionStatus::Allowed:SuppressionStatus::Denied;
}
void Disable()noexcept {ready=false;routeVerifiedNs=0;}
bool LoadWave(const std::filesystem::path& path){
    if(!path.is_absolute()||path.root_name()!=L"G:")return false;
    std::ifstream f(path,std::ios::binary|std::ios::ate);if(!f||f.tellg()!=std::streamoff(44+WaveFrames*2))return false;
    std::vector<char> bytes(44+WaveFrames*2);f.seekg(0);if(!f.read(bytes.data(),bytes.size()))return false;
    std::array<unsigned char,32> digest{};
    if(BCryptHash(BCRYPT_SHA256_ALG_HANDLE,nullptr,0,reinterpret_cast<PUCHAR>(bytes.data()),ULONG(bytes.size()),digest.data(),ULONG(digest.size()))<0)return false;
    constexpr std::string_view expected="3ec56ca5e0e7fd245d2a7cab3b7140cf45106fc1db4a555b654ffb680f0a7b15",digits="0123456789abcdef";
    for(unsigned n=0;n<digest.size();++n)if(digits[digest[n]>>4]!=expected[n*2]||digits[digest[n]&15]!=expected[n*2+1])return false;
    const auto u16=[&](unsigned p){unsigned short v=0;std::memcpy(&v,bytes.data()+p,2);return v;};
    const auto u32=[&](unsigned p){unsigned v=0;std::memcpy(&v,bytes.data()+p,4);return v;};
    if(std::memcmp(bytes.data(),"RIFF",4)||std::memcmp(bytes.data()+8,"WAVEfmt ",8)||std::memcmp(bytes.data()+36,"data",4)||
       u32(4)!=bytes.size()-8||u32(16)!=16||u16(20)!=1||u16(22)!=1||u32(24)!=48000||u32(28)!=96000||
       u16(32)!=2||u16(34)!=16||u32(40)!=WaveFrames*2)return false;
    wave.assign(bytes.begin()+44,bytes.end());return true;
}
// Separate reload device/queue, using the same current-game endpoint rule as
// mechanism audio. No default mapper and no change to the accepted pump path.
struct Route {UINT device=UINT_MAX;std::wstring endpoint;explicit operator bool()const noexcept{return device!=UINT_MAX;}};
Route GameRoute(){
    using Microsoft::WRL::ComPtr;
    ComPtr<IMMDeviceEnumerator> enumerator;ComPtr<IMMDeviceCollection> devices;
    if(FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator),nullptr,CLSCTX_ALL,IID_PPV_ARGS(&enumerator)))||
       FAILED(enumerator->EnumAudioEndpoints(eRender,DEVICE_STATE_ACTIVE,&devices)))return {};
    UINT size=0;if(FAILED(devices->GetCount(&size))||size>128)return {};
    std::wstring endpoint;
    for(UINT n=0;n<size;++n){
        ComPtr<IMMDevice> device;ComPtr<IAudioSessionManager2> manager;ComPtr<IAudioSessionEnumerator> sessions;
        if(FAILED(devices->Item(n,&device))||FAILED(device->Activate(__uuidof(IAudioSessionManager2),CLSCTX_ALL,nullptr,
           reinterpret_cast<void**>(manager.GetAddressOf())))||FAILED(manager->GetSessionEnumerator(&sessions)))continue;
        int count=0;if(FAILED(sessions->GetCount(&count))||count<0||count>4096)continue;
        bool found=false;
        for(int j=0;j<count;++j){ComPtr<IAudioSessionControl> control;ComPtr<IAudioSessionControl2> process;DWORD pid=0;AudioSessionState state{};
            if(SUCCEEDED(sessions->GetSession(j,&control))&&SUCCEEDED(control.As(&process))&&SUCCEEDED(process->GetProcessId(&pid))&&
               pid==GetCurrentProcessId()&&SUCCEEDED(control->GetState(&state))&&state!=AudioSessionStateExpired){found=true;break;}}
        if(!found)continue;LPWSTR id=nullptr;if(FAILED(device->GetId(&id))||!id)continue;
        const std::wstring candidate(id);CoTaskMemFree(id);if(!endpoint.empty()&&endpoint!=candidate)return {};endpoint=candidate;
    }
    if(endpoint.empty())return {};
    for(UINT n=0;n<waveOutGetNumDevs()&&n<128;++n){
        const auto device=reinterpret_cast<HWAVEOUT>(static_cast<UINT_PTR>(n));ULONG bytes=0;
        if(waveOutMessage(device,DRV_QUERYFUNCTIONINSTANCEIDSIZE,reinterpret_cast<DWORD_PTR>(&bytes),0)!=MMSYSERR_NOERROR||
           bytes<sizeof(wchar_t)||bytes>8192||bytes%sizeof(wchar_t))continue;
        std::vector<wchar_t> id(bytes/sizeof(wchar_t),L'\0');
        if(waveOutMessage(device,DRV_QUERYFUNCTIONINSTANCEID,reinterpret_cast<DWORD_PTR>(id.data()),bytes)==MMSYSERR_NOERROR&&
           id.back()==L'\0'&&_wcsicmp(id.data(),endpoint.c_str())==0)return {n,endpoint};
    }
    return {};
}
struct Buffer {HWAVEOUT device=nullptr;WAVEHDR header{};std::array<char,WaveFrames*2> pcm{};
    MechanismAudioBufferLease lease;ShellReloadAudioIdentity identity{};std::uint64_t receipt=0;};
std::atomic<Buffer*> retained=nullptr;
bool Release(Buffer& b)noexcept {
    if(b.lease.Retained())return false;if(b.lease.CanPrepare())return true;
    const bool done=(b.header.dwFlags&WHDR_DONE)!=0;
    if(b.lease.Phase()==MechanismAudioBufferPhase::Queued){
        const auto result=done?MMSYSERR_NOERROR:waveOutReset(b.device);
        if(!b.lease.Returned(result==MMSYSERR_NOERROR)){++cleanupFailures;Update(b.receipt,Disposition::Failed,result);Disable();return false;}}
    if(!b.lease.CanUnprepare())return false;
    const auto result=waveOutUnprepareHeader(b.device,&b.header,sizeof(b.header));
    if(!b.lease.Unprepared(result==MMSYSERR_NOERROR)){++cleanupFailures;Update(b.receipt,Disposition::Failed,result);Disable();return false;}
    if(b.receipt){if(done)++completed;else ++interrupted;Update(b.receipt,done?Disposition::Completed:Disposition::Interrupted);}
    b.header={};b.identity={};b.receipt=0;return true;
}
void CloseOrRetain(Buffer* b)noexcept {
    if(!b)return;Disable();if(!b->device){delete b;return;}
    if(!Release(*b)){retained.store(b);return;}
    const auto result=waveOutClose(b->device);
    if(!b->lease.Closed(result==MMSYSERR_NOERROR)){++cleanupFailures;retained.store(b);return;}delete b;
}
void PlayerBody(){
    if(FAILED(CoInitializeEx(nullptr,COINIT_MULTITHREADED))){++failures;return;}
    struct ComExit{~ComExit(){CoUninitialize();}} comExit;
    const auto route=GameRoute();++routeChecks;if(!route){++routeFailures;return;}
    std::unique_ptr<Buffer,decltype(&CloseOrRetain)> storage(new Buffer,&CloseOrRetain);auto& b=*storage;
    WAVEFORMATEX format{WAVE_FORMAT_PCM,1,48000,96000,2,16,0};
    if(waveOutOpen(&b.device,route.device,&format,0,0,CALLBACK_NULL)!=MMSYSERR_NOERROR){++failures;b.device=nullptr;return;}
    MechanismAudioRoutePolicy policy;policy.Open(Now());
    const auto sameRoute=[&]{const auto r=GameRoute();++routeChecks;return r&&r.device==route.device&&_wcsicmp(r.endpoint.c_str(),route.endpoint.c_str())==0;};
    if(!sameRoute()){++routeFailures;return;}
    routeVerifiedNs=policy.VerifiedNs();ready=true;
    while(!quit.load()&&!unsafe.load()){
        Pending next{};bool have=false;
        {std::unique_lock lock(queueMutex);changed.wait_for(lock,std::chrono::milliseconds(10),[]{return count||quit.load()||unsafe.load();});
            if(count){next=events[head];head=(head+1)%events.size();--count;have=true;}}
        auto now=Now();
        if(policy.CheckDue(now)){const bool same=sameRoute();now=Now();if(!policy.Validate(now,same)){++routeFailures;break;}
            routeVerifiedNs=policy.VerifiedNs();}
        if(!policy.Fresh(now)){++routeFailures;break;}
        const auto s=current.load();const bool fresh=s&&Fresh(*s,now);
        if(!b.lease.CanPrepare()&&((b.header.dwFlags&WHDR_DONE)||!fresh||s->identity!=b.identity||Suppressed(b.identity)==SuppressionStatus::Denied))
            if(!Release(b))break;
        if(!have)continue;
        const auto permission=Suppressed(next.event.identity);
        if(quit.load()||unsafe.load()||!fresh||s->identity!=next.event.identity||permission==SuppressionStatus::Denied||
           next.event.observedNs>now||next.event.deadlineNs<=now){++stale;Update(next.receipt,Disposition::Stale);continue;}
        if(permission==SuppressionStatus::Busy){
            // A callback owning the ledger is not revocation. Keep the exact
            // original cue/deadline; neither replay nor renew it on contention.
            {std::lock_guard lock(queueMutex);if(count<events.size()){
                head=(head+unsigned(events.size())-1)%events.size();events[head]=next;++count;
            }else {++stale;Update(next.receipt,Disposition::QueueFull);}}
            std::this_thread::sleep_for(std::chrono::milliseconds(1));continue;
        }
        if(!Release(b))break;
        std::copy(wave.begin(),wave.end(),b.pcm.begin());b.header.lpData=b.pcm.data();b.header.dwBufferLength=DWORD(wave.size());
        const auto prepare=waveOutPrepareHeader(b.device,&b.header,sizeof(b.header));
        if(!b.lease.Prepared(prepare==MMSYSERR_NOERROR)){++failures;Update(next.receipt,Disposition::Failed,prepare);break;}
        b.identity=next.event.identity;b.receipt=next.receipt;
        const auto write=waveOutWrite(b.device,&b.header,sizeof(b.header));b.lease.Submitted(write==MMSYSERR_NOERROR);
        Update(next.receipt,write==MMSYSERR_NOERROR?Disposition::Playing:Disposition::Failed,write);
        if(write!=MMSYSERR_NOERROR){++failures;b.receipt=0;break;}++played;
    }
}
void Player()noexcept {
    try{PlayerBody();}catch(...){++failures;}Disable();
    std::lock_guard lock(queueMutex);while(count){++stale;Update(events[head].receipt,Disposition::Stale);head=(head+1)%events.size();--count;}
}
}
bool Install(const Config& config){
    if(enabled.load()||worker.joinable()||retained.load()||inFlight.load())return false;
    if(config.replace&&!LoadWave(config.insertionWave))return false;
    suppression={};ledger={};head=count=0;receipts={};receiptCount=0;nativeRows={};nativeCount=0;
    starts=stops=suppressed=accepted=queued=played=completed=interrupted=stale=failures=routeChecks=routeFailures=cleanupFailures=rowsLost=0;
    ready=false;unsafe=false;quit=false;routeVerifiedNs=0;current.store({});replacing=config.replace;
    enabled=true;if(config.replace)worker=std::thread(Player);return true;
}
bool Dispatch(bool start,std::uintptr_t receiver,const std::array<unsigned char,16>& guid)noexcept {
    if(!enabled.load())return false;++inFlight;struct Exit{~Exit(){--inFlight;}} exit;
    const auto kind=ShellReloadSoundFor("SPAS12_sp",guid);if(kind==ShellReloadSound::Unrelated)return false;
    if(start)++starts;else ++stops;
    NativeRow row;row.now=Now();row.receiver=unsigned(receiver);row.owner=U32(receiver+4);row.kind=unsigned(kind);row.start=start;
    unsigned char first=0,flags=0;const bool read=Read(receiver+0x50,&first,1)&&Read(row.owner+0x475,&flags,1);
    row.firstPerson=first;row.ownerFlags=flags;
    const auto s=current.load();
    if(s){row.source=s->soldier;row.cycle=s->identity.cycle;row.local=read&&row.owner==s->soldier;row.fresh=Fresh(*s,row.now);}
    const bool intended=s&&s->spas&&ShellReloadLocalStart(start,kind,row.local,first,flags,s->manual)&&replacing.load();
    if(intended){
        const bool prepared=row.fresh&&ready.load()&&!unsafe.load()&&MechanismAudioRouteFresh(routeVerifiedNs.load(),row.now);
        if(suppressionGate.test_and_set(std::memory_order_acquire)){unsafe=true;Disable();changed.notify_one();}
        else {
            // The finishing/cocking graph cannot create insertion authority.
            // It is silenced only for a loop this exact manual cycle replaced.
            row.suppressed=ShellReloadSuppressStart(suppression,kind,s->identity,prepared);
            suppressionGate.clear(std::memory_order_release);
        }
    }
    RecordNative(row);if(row.suppressed)++suppressed;return row.suppressed;
}
void Publish(const Source& s){current.store(std::make_shared<const Source>(s));changed.notify_one();}
bool Emit(const ShellReloadAudioEvent& event){
    if(!enabled.load())return false;const auto now=Now();const auto s=current.load();
    if(!s||s->identity!=event.identity||!Fresh(*s,now))return false;
    std::unique_lock lock(queueMutex,std::try_to_lock);if(!lock.owns_lock())return false;
    if(ledger.Accept(event,now)!=ShellReloadAudioAdmission::Accepted)return false;++accepted;
    Receipt r;r.event=event;r.admitted=now;
    if(!replacing.load()){Record(r);return false;}
    if(!ready.load()||unsafe.load()||!MechanismAudioRouteFresh(routeVerifiedNs.load(),now)){r.disposition=Disposition::NotReady;Record(r);return false;}
    if(Suppressed(event.identity)==SuppressionStatus::Denied){r.disposition=Disposition::StockPassed;Record(r);return false;}
    if(count==events.size()){r.disposition=Disposition::QueueFull;Record(r);return false;}
    r.disposition=Disposition::Queued;const auto id=Record(r);
    events[(head+count)%events.size()]={event,id};++count;++queued;changed.notify_one();return true;
}
void RetireActor(std::uint64_t actor,std::uint64_t generation){
    std::lock_guard lock(queueMutex);ledger.RetireActor(actor,generation);
    while(suppressionGate.test_and_set(std::memory_order_acquire))YieldProcessor();
    suppression.RetireActor(actor,generation);suppressionGate.clear(std::memory_order_release);
}
void Stop()noexcept {enabled=false;replacing=false;Disable();current.store({});quit=true;changed.notify_all();}
bool Drain()noexcept {
    Stop();const auto before=GetTickCount64();while(inFlight.load()&&GetTickCount64()-before<3000)Sleep(1);
    if(inFlight.load())return false;if(worker.joinable())worker.join();return retained.load()==nullptr;
}
void Report(std::ostream& out){
    out<<"{\"enabled\":"<<enabled.load()<<",\"replacement\":"<<replacing.load()<<",\"ready\":"<<ready.load()
       <<",\"native_starts\":"<<starts.load()<<",\"native_stops\":"<<stops.load()<<",\"suppressed\":"<<suppressed.load()
       <<",\"applied_shells\":"<<accepted.load()<<",\"queued\":"<<queued.load()<<",\"played\":"<<played.load()
       <<",\"completed\":"<<completed.load()<<",\"interrupted\":"<<interrupted.load()<<",\"stale\":"<<stale.load()
       <<",\"failures\":"<<failures.load()<<",\"route_checks\":"<<routeChecks.load()<<",\"route_failures\":"<<routeFailures.load()
       <<",\"cleanup_failures\":"<<cleanupFailures.load()<<",\"unsafe\":"<<unsafe.load()<<",\"rows_lost\":"<<rowsLost.load()
       <<",\"retained_driver_bytes\":"<<(retained.load()?sizeof(Buffer):0)<<",\"native_rows\":[";
    while(nativeGate.test_and_set(std::memory_order_acquire))YieldProcessor();bool comma=false;
    for(const auto& r:nativeRows){if(!r.now)continue;if(comma)out<<',';comma=true;
        out<<"{\"ns\":"<<r.now<<",\"owner\":"<<r.owner<<",\"source\":"<<r.source<<",\"kind\":"<<r.kind
           <<",\"first_person\":"<<r.firstPerson<<",\"owner_flags\":"<<r.ownerFlags<<",\"cycle\":"<<r.cycle
           <<",\"start\":"<<r.start<<",\"local\":"<<r.local<<",\"fresh\":"<<r.fresh<<",\"suppressed\":"<<r.suppressed<<'}';}
    const auto total=nativeCount;nativeGate.clear(std::memory_order_release);out<<"],\"native_rows_total\":"<<total<<",\"cues\":[";
    while(receiptGate.test_and_set(std::memory_order_acquire))YieldProcessor();comma=false;
    for(const auto& r:receipts){if(!r.id)continue;if(comma)out<<',';comma=true;const auto& e=r.event;const auto& i=e.identity;
        out<<"{\"id\":"<<r.id<<",\"actor\":"<<i.actor<<",\"actor_generation\":"<<i.actorGeneration<<",\"item\":"<<i.item
           <<",\"item_generation\":"<<i.itemGeneration<<",\"cycle\":"<<i.cycle<<",\"request\":"<<e.request
           <<",\"native_event\":"<<e.nativeEvent<<",\"observed_ns\":"<<e.observedNs<<",\"admitted_ns\":"<<r.admitted
           <<",\"written_ns\":"<<r.written<<",\"finished_ns\":"<<r.finished<<",\"disposition\":"<<unsigned(r.disposition)<<",\"error\":"<<r.error<<'}';}
    const auto receiptsTotal=receiptCount;receiptGate.clear(std::memory_order_release);out<<"],\"cue_rows_total\":"<<receiptsTotal<<'}';
}
}
