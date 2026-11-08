#include "Bc2CampaignProvisioningRuntime.h"
#include "Bc2OriginalFiringObserver.h"
#include "Bc2ReloadFlowRuntime.h"
#include "Bc2Gameplay.h"
#include <atomic>
#include <chrono>
#include <mutex>
#include <fstream>
#if defined(_WIN32) && defined(_M_IX86)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <MinHook.h>
#include <intrin.h>
#include <bcrypt.h>
#pragma comment(lib,"bcrypt.lib")
namespace fvr::bc2::campaignProvisioning {
namespace {
using Seat=void(__thiscall*)(void*,unsigned,void*);
Seat originalSeat=nullptr;
Service service;ProcessIdentity identity{};ReloadStateMemory memory{};
std::recursive_mutex gate;
std::atomic<unsigned> firingDepth=0,seatCalls=0;
std::atomic<bool> enabled=false;
bool installed=false,started=false;std::uint64_t seatSequence=0,serverSequence=0;
thread_local unsigned seatDepth=0;
std::array<unsigned char,5> seatPrefix{};
bool OwnerCurrent(void*,const policy::Owner& owner,std::int64_t now){
    const auto source=reloadFlowRuntime::ReadCampaignProvisioningSource(now);
    return source&&ProvisionOwner(*source,identity.session)==owner;
}
OriginalFiringToken Enter(void*,const OriginalFiringUpdate& call)noexcept {
    firingDepth.fetch_add(1,std::memory_order_acq_rel);
    if(enabled.load(std::memory_order_acquire)){
        std::unique_lock lock(gate,std::try_to_lock);
        if(lock.owns_lock()){
            const auto now=Now();const auto source=reloadFlowRuntime::ReadCampaignProvisioningSource(now);
            if(source)service.ObserveServer(*source,call.firing,GetCurrentThreadId(),++serverSequence,now);
        }
    }
    OriginalFiringToken token{};token.words[0]=1;return token;
}
void Leave(void*,const OriginalFiringUpdate&,const OriginalFiringToken& token)noexcept {
    if(token.words[0])firingDepth.fetch_sub(1,std::memory_order_acq_rel);
}
void CallSeatOriginal(void* self,unsigned seat,void* context){
    __try {originalSeat(self,seat,context);}
    __finally {--seatDepth;seatCalls.fetch_sub(1,std::memory_order_acq_rel);}
}
void __fastcall SeatHook(void* self,void*,unsigned seat,void* context){
    const auto caller=unsigned(_ReturnAddress());seatCalls.fetch_add(1,std::memory_order_acq_rel);++seatDepth;
    if(enabled.load(std::memory_order_acquire)){
        std::unique_lock lock(gate,std::try_to_lock);
        if(lock.owns_lock()){
            const auto now=Now();const auto source=reloadFlowRuntime::ReadCampaignProvisioningSource(now);
            if(source&&source->links.soldier==unsigned(self)){
                policy::Boundary boundary{ProvisionOwner(*source,identity.session),++seatSequence,now,
                    source->client.observedNs,GetCurrentThreadId(),caller,seat,firingDepth.load(std::memory_order_acquire),seatDepth,seatPrefix};
                gameplay::CampaignProvisioningSeatEntry(service,*source,boundary);
            }
        }
    }
    // Exactly one original invocation on every ordinary path, even refusal.
    // SEH finally preserves depth on native unwind; it does not catch or retry.
    CallSeatOriginal(self,seat,context);
}
}
std::int64_t Now()noexcept {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
std::optional<ProcessIdentity> Identify(std::span<const std::byte> bytes)noexcept {
    if(bytes.empty()||bytes.size()>MAXDWORD)return {};
    ProcessIdentity id{};id.pid=GetCurrentProcessId();FILETIME creation{},exit{},kernel{},user{};
    if(!GetProcessTimes(GetCurrentProcess(),&creation,&exit,&kernel,&user))return {};
    id.creation=(std::uint64_t(creation.dwHighDateTime)<<32)|creation.dwLowDateTime;id.session=id.creation;
    const auto length=GetModuleFileNameW(nullptr,reinterpret_cast<wchar_t*>(id.executable.data()),DWORD(id.executable.size()));
    if(!length||length>=id.executable.size())return {};
    BCRYPT_ALG_HANDLE algorithm=nullptr;
    if(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0)return {};
    const auto result=BCryptHash(algorithm,nullptr,0,reinterpret_cast<PUCHAR>(const_cast<std::byte*>(bytes.data())),
        ULONG(bytes.size()),id.image.data(),ULONG(id.image.size()));
    BCryptCloseAlgorithmProvider(algorithm,0);return result<0?std::nullopt:std::optional(id);
}
bool Install(const ProcessIdentity& process,const ReloadStateMemory& reader,bool explicitRequest)noexcept {
    if(installed||started||!explicitRequest||process.pid!=GetCurrentProcessId()||!reader.read||!reader.type)return false;
    FILETIME creation{},exit{},kernel{},user{};
    if(!GetProcessTimes(GetCurrentProcess(),&creation,&exit,&kernel,&user)||
       ((std::uint64_t(creation.dwHighDateTime)<<32)|creation.dwLowDateTime)!=process.creation)return false;
    const policy::Memory pm{reader.context,reader.read,reader.type};
    if(!policy::ValidateOriginalProvisioningCode(pm))return false;
    memory=reader;identity=process;
    const LifetimeHost life{nullptr,
        [](void*,const ReloadServerSnapshot& source,std::uint64_t nonce)noexcept{return gameplay::ExcludeCampaignProvisioning(source,nonce);},
        [](void*,const ReloadServerSnapshot& source,unsigned item,unsigned data,std::uint64_t nonce,std::int64_t now)noexcept{
            return gameplay::RetireCampaignProvisioningItem(source,item,data,nonce,now);},
        [](void*,const ReloadServerSnapshot& source,std::uint64_t nonce)noexcept{return gameplay::ResumeCampaignProvisioning(source,nonce);}};
    if(!service.Configure(process,pm,{nullptr,OwnerCurrent,policy::InvokeOriginalAssignment},life,true))return false;
    if(MH_CreateHook(reinterpret_cast<void*>(0x6b0830),SeatHook,reinterpret_cast<void**>(&originalSeat))!=MH_OK)return false;
    const OriginalFiringObserver observer{&service,Enter,Leave,nullptr,Leave};
    if(!reloadFlowRuntime::RegisterOriginalFiringObserver(observer)){
        // Before Start: only our newly created, disabled seat hook is removed.
        MH_RemoveHook(reinterpret_cast<void*>(0x6b0830));return false;
    }
    if(!gameplay::EnableCampaignProvisioning())return false;
    installed=true;return true;
}
bool Start()noexcept {
    if(!installed||started)return false;started=true;
    if(!memory.read(memory.context,0x6b0830,seatPrefix.data(),seatPrefix.size())||seatPrefix[0]!=0xe9)return false;
    std::int32_t displacement=0;std::memcpy(&displacement,seatPrefix.data()+1,4);
    if(std::uint32_t(0x6b0835+displacement)!=std::uint32_t(SeatHook))return false;
    enabled.store(true,std::memory_order_release);return true;
}
bool Stop()noexcept {
    enabled.store(false,std::memory_order_release);
    if(!installed)return true;
    const auto disabled=MH_DisableHook(reinterpret_cast<void*>(0x6b0830));
    if(disabled!=MH_OK&&disabled!=MH_ERROR_DISABLED)return false;
    const auto until=GetTickCount64()+2000;
    while(seatCalls.load(std::memory_order_acquire)&&GetTickCount64()<until)Sleep(1);
    if(seatCalls.load(std::memory_order_acquire))return false;
    std::lock_guard lock(gate);service.Stop();
    // NativeProbe's global disable/drain owns hooks. The observer/trampoline,
    // service, and any quarantine remain process-retained, never reinstalled.
    return true;
}
bool Submit(const Request& request)noexcept {
    if(!enabled.load(std::memory_order_acquire))return false;
    std::lock_guard lock(gate);return service.Submit(request,Now());
}
std::optional<Request> SourceRequest()noexcept {
    if(!enabled.load(std::memory_order_acquire))return {};
    std::lock_guard lock(gate);const auto source=reloadFlowRuntime::ReadCampaignProvisioningSource(Now());if(!source)return {};
    Request request{};request.bytes=sizeof(Request);request.process=identity;
    request.client=source->client.owner;request.source=ProvisionOwner(*source,identity.session);
    request.sourceSequence=source->client.sequence;request.sourceObservedNs=source->client.observedNs;return request;
}
Receipt Snapshot()noexcept {std::lock_guard lock(gate);service.Expire(Now());return service.Read();}
void ServiceTransport(const std::filesystem::path& directory)noexcept {
    if(!enabled.load(std::memory_order_acquire))return;
    // Worker cadence only. Fixed-size complete messages, no native process RPC.
    const auto read=[&](const auto& path,auto& value){
        const auto file=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
        if(file==INVALID_HANDLE_VALUE)return false;BY_HANDLE_FILE_INFORMATION info{};DWORD got=0;
        const bool okay=GetFileInformationByHandle(file,&info)&&!(info.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT))&&
            !info.nFileSizeHigh&&info.nFileSizeLow==sizeof(value)&&ReadFile(file,&value,sizeof(value),&got,nullptr)&&got==sizeof(value);
        CloseHandle(file);return okay;
    };
    const auto write=[&](const auto& path,const auto& value){
        auto temporary=path;temporary+=L".pending";
        const auto file=CreateFileW(temporary.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(file==INVALID_HANDLE_VALUE)return;DWORD written=0;
        bool okay=WriteFile(file,&value,sizeof(value),&written,nullptr)&&written==sizeof(value);
        okay=CloseHandle(file)&&okay;
        const auto attributes=GetFileAttributesW(path.c_str());
        if(attributes!=INVALID_FILE_ATTRIBUTES&&(attributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT)))okay=false;
        if(okay)MoveFileExW(temporary.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING);
        // Failed publication never retries a native command or changes nonce.
    };
    Request request{};if(read(directory/L"campaign-provision-request.bin",request))(void)Submit(request);
    if(const auto source=SourceRequest())write(directory/L"campaign-provision-source.bin",*source);
    write(directory/L"campaign-provision-receipt.bin",ToWire(Snapshot()));
}
}
#else
namespace fvr::bc2::campaignProvisioning {
std::optional<ProcessIdentity> Identify(std::span<const std::byte>)noexcept{return {};}
bool Install(const ProcessIdentity&,const ReloadStateMemory&,bool)noexcept{return false;}
bool Start()noexcept{return false;}bool Stop()noexcept{return true;}
bool Submit(const Request&)noexcept{return false;}std::optional<Request> SourceRequest()noexcept{return {};}
Receipt Snapshot()noexcept{return {};}std::int64_t Now()noexcept{return 0;}
void ServiceTransport(const std::filesystem::path&)noexcept{}
}
#endif
