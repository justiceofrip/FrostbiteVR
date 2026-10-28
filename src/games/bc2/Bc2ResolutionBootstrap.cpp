#include "Bc2ResolutionBinding.h"
#include "Bc2ResolutionRequest.h"
#include <Windows.h>
#include <dxgi.h>
#include <MinHook.h>
#include <atomic>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <vector>

namespace {
using namespace fvr::bc2;
constexpr unsigned Base=0x400000;
using Update=void(__thiscall*)(void*,unsigned,unsigned);
using Dispatch=void(__thiscall*)(void*,const char*,const char*);
ResolutionBinding binding{};ResolutionSnapshot original{};ResolutionSize target{};
ResolutionRequest result{};
std::atomic<bool> invoked=false,enabled=false,done=false,cancel=false;
std::atomic<unsigned> active=0;
std::atomic_flag inspecting=ATOMIC_FLAG_INIT;
bool slotOwned=false,protectionDebt=false;DWORD protection=0;
std::atomic<bool> cleanupRestored=false,cleanupDrained=false;
bool timedOut=false;
unsigned stable=0;bool attempted=false,rollingBack=false;
std::uint64_t deadline=0,rollbackDeadline=0;
ResolutionLease lease{};unsigned leaseWidth=0,leaseHeight=0;
ResolutionGuardOwner guardOwner{};Dispatch originalDispatch=nullptr;
std::atomic<bool> leasePublished=false,configurationDone=false,releaseRequested=false,guardInstalled=false,guardEnabled=false,ownerLost=false;
std::atomic<bool> configurationDeferred=false;
std::atomic<ResolutionGuardPhase> guardPhase=ResolutionGuardPhase::Inactive;
std::atomic<unsigned> guardCallbacks=0,blockedEvents=0,passedEvents=0,ownerMismatches=0,releaseReason=0;
std::atomic_flag requestGate=ATOMIC_FLAG_INIT,guardCleanupGate=ATOMIC_FLAG_INIT;
// Retain these three handles with the pinned helper for the game lifetime. A
// callback paused before entry may still observe them after a drained release.
HANDLE ownerProcess=nullptr,releaseEvent=nullptr,configurationEvent=nullptr;
bool Copy(unsigned at,void* out,std::size_t n)noexcept {
    if(at<0x10000||!n||n>65536||n>UINT32_MAX-at)return false;
    __try{std::memcpy(out,reinterpret_cast<const void*>(at),n);return true;}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool Store(void* at,const ResolutionRequest& value)noexcept {
    __try{std::memcpy(at,&value,sizeof(value));return true;}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
const MenuMemory memory{nullptr,[](void*,unsigned at,void* out,std::size_t n){return Copy(at,out,n);}};
std::uint64_t Creation(HANDLE process)noexcept {
    FILETIME created{},exited{},kernel{},user{};
    return GetProcessTimes(process,&created,&exited,&kernel,&user)?
        (std::uint64_t(created.dwHighDateTime)<<32)|created.dwLowDateTime:0;
}
bool CurrentGame(const ResolutionLease& value)noexcept {
    return ValidResolutionLease(value)&&value.gamePid==GetCurrentProcessId()&&value.gameCreation==Creation(GetCurrentProcess());
}
void RequestRelease(unsigned reason)noexcept {
    unsigned expected=0;releaseReason.compare_exchange_strong(expected,reason);
    releaseRequested.store(true,std::memory_order_release);
    guardPhase.store(ResolutionGuardPhase::Releasing,std::memory_order_release);
    cancel.store(true,std::memory_order_release);
    if(releaseEvent)SetEvent(releaseEvent);
}
bool ResolutionEvent(const char* event)noexcept {
    constexpr char name[]="changeResolution";std::array<char,sizeof(name)> actual{};
    return Copy(unsigned(reinterpret_cast<std::uintptr_t>(event)),actual.data(),actual.size())&&
        !std::memcmp(actual.data(),name,sizeof(name));
}
void __fastcall DispatchHook(void* self,void*,const char* event,const char* payload){
    const auto incomingError=GetLastError();
    struct Active{Active(){guardCallbacks.fetch_add(1,std::memory_order_acq_rel);}~Active(){guardCallbacks.fetch_sub(1,std::memory_order_release);}} callback;
    bool block=false;
    // Unrelated UI events never scan native ownership, query the GPU, wait on a
    // lock, or access caller payload. The only string read is bounded to17bytes.
    if(!releaseRequested.load(std::memory_order_acquire)&&ResolutionEvent(event)){
        const auto phase=guardPhase.load(std::memory_order_acquire);
        if(phase==ResolutionGuardPhase::Prepared||phase==ResolutionGuardPhase::Active){
            if(!ownerProcess||WaitForSingleObject(ownerProcess,0)!=WAIT_TIMEOUT){
                ownerLost=true;RequestRelease(2);
            }else{
                const auto current=ReadResolutionGuardOwner(memory,binding,Base);
                if(!current||unsigned(reinterpret_cast<std::uintptr_t>(self))!=guardOwner.manager||*current!=guardOwner){
                    ownerMismatches.fetch_add(1,std::memory_order_relaxed);RequestRelease(4);
                }else{
                    // Recheck cancellation after the fresh reads. Menu changes
                    // cannot invalidate this key; renderer/options replacement can.
                    block=!releaseRequested.load(std::memory_order_acquire)&&
                        BlockResolutionEvent("changeResolution",guardPhase.load(std::memory_order_acquire),true,guardOwner,*current);
                }
            }
        }
    }
    if(block)blockedEvents.fetch_add(1,std::memory_order_relaxed);
    else passedEvents.fetch_add(1,std::memory_order_relaxed);
    SetLastError(incomingError);
    if(!block)originalDispatch(self,event,payload);
    const auto outgoingError=GetLastError();
    SetLastError(outgoingError);
}
void DisableGuard()noexcept {
    // The configure worker is the sole hook installer. Never disable while it
    // could still enable the dispatcher or complete a native resize/rollback.
    if(!configurationDone.load(std::memory_order_acquire)||guardCleanupGate.test_and_set(std::memory_order_acquire))return;
    struct Unlock{~Unlock(){guardCleanupGate.clear(std::memory_order_release);}} unlock;
    if(guardEnabled.load(std::memory_order_acquire)){
        const auto status=MH_DisableHook(reinterpret_cast<void*>(Base+binding.dispatcher));
        if(status==MH_OK||status==MH_ERROR_DISABLED)guardEnabled.store(false,std::memory_order_release);
    }
    const auto limit=GetTickCount64()+2000;
    while(guardCallbacks.load(std::memory_order_acquire)&&GetTickCount64()<limit)Sleep(1);
    if(!guardEnabled.load(std::memory_order_acquire)&&!guardCallbacks.load(std::memory_order_acquire))
        guardPhase.store(ResolutionGuardPhase::Released,std::memory_order_release);
    // Never RemoveHook/Uninitialize/FreeLibrary: a thread may be paused before
    // incrementing its callback count. Released callbacks pass through safely.
}
void GuardProof(ResolutionRequest& out)noexcept {
    const auto phase=guardPhase.load(std::memory_order_acquire);
    out.guardInstalled=guardInstalled.load();
    out.guardActive=!releaseRequested.load()&&(phase==ResolutionGuardPhase::Prepared||phase==ResolutionGuardPhase::Active);
    out.guardDisabled=!guardEnabled.load();out.guardDrained=out.guardDisabled&&!guardCallbacks.load();
    out.guardReleased=out.guardInstalled&&configurationDone.load()&&out.guardDrained&&releaseRequested.load();
    out.guardNeverInstalled=!out.guardInstalled&&configurationDone.load()&&out.guardDrained;
    out.guardOwnerLost=ownerLost.load();out.guardBlockedEvents=blockedEvents.load();out.guardPassedEvents=passedEvents.load();
    out.guardOwnerMismatches=ownerMismatches.load();out.guardReleaseReason=releaseReason.load();
    out.guardCleanupPending=releaseRequested.load()&&(!configurationDone.load()||!out.guardDrained||!cleanupRestored.load()||!cleanupDrained.load());
}
void CompleteConfiguration()noexcept {
    configurationDone.store(true,std::memory_order_release);
    if(configurationEvent)SetEvent(configurationEvent);
}
DWORD WINAPI OwnerWatchdog(void*)noexcept {
    const HANDLE events[]{ownerProcess,releaseEvent};
    const auto state=WaitForMultipleObjects(2,events,FALSE,INFINITE);
    if(state!=WAIT_OBJECT_0+1){ownerLost=true;RequestRelease(2);}
    // A hung original engine call cannot authorize cleanup or a fake success.
    if(WaitForSingleObject(configurationEvent,INFINITE)==WAIT_OBJECT_0)DisableGuard();
    return 0;
}
[[noreturn]] void GuardSetupFailure(unsigned code,const char* message){
    result.error=code;throw std::runtime_error(message);
}
void InstallGuard(){
    const auto current=ReadResolutionGuardOwner(memory,binding,Base);
    if(!current||current->manager!=original.manager||current->renderer!=original.renderer||current->options!=original.options||
       current->settings!=original.settings||current->registry!=original.registry)GuardSetupFailure(18,"resolution guard owner");
    guardOwner=*current;
    if(releaseRequested.load()||WaitForSingleObject(ownerProcess,0)!=WAIT_TIMEOUT)GuardSetupFailure(19,"resolution lease ended");
    if(MH_Initialize()!=MH_OK)GuardSetupFailure(20,"resolution guard initialize");
    if(MH_CreateHook(reinterpret_cast<void*>(Base+binding.dispatcher),reinterpret_cast<void*>(&DispatchHook),
        reinterpret_cast<void**>(&originalDispatch))!=MH_OK)GuardSetupFailure(21,"resolution guard create");
    guardPhase.store(ResolutionGuardPhase::Prepared,std::memory_order_release);
    if(releaseRequested.load())GuardSetupFailure(22,"resolution guard cancelled");
    if(MH_EnableHook(reinterpret_cast<void*>(Base+binding.dispatcher))!=MH_OK)GuardSetupFailure(23,"resolution guard enable");
    guardInstalled=true;guardEnabled=true;
    const auto watcher=CreateThread(nullptr,0,&OwnerWatchdog,nullptr,0,nullptr);
    if(!watcher)GuardSetupFailure(24,"resolution owner watcher");
    CloseHandle(watcher);
}
bool Same(std::span<const std::byte> b,const fvr::engine::PeImage& pe,unsigned rva,unsigned n){
    for(const auto& s:pe.sections)if(rva>=s.rva&&rva-s.rva<=s.rawSize&&n<=s.rawSize-(rva-s.rva)){
        const auto at=std::size_t(s.rawOffset)+rva-s.rva;if(at>b.size()||n>b.size()-at)return false;
        std::vector<std::byte> live(n);return Copy(Base+rva,live.data(),n)&&!std::memcmp(live.data(),b.data()+at,n);
    }return false;
}
bool ObservedChain(const ResolutionSnapshot& s,unsigned& width,unsigned& height,bool& windowed)noexcept {
    __try{DXGI_SWAP_CHAIN_DESC desc{};
        if(FAILED(reinterpret_cast<IDXGISwapChain*>(s.swapchain)->GetDesc(&desc))||!desc.OutputWindow||
            !IsWindow(desc.OutputWindow)||desc.SampleDesc.Count!=1)return false;
        DWORD pid=0;GetWindowThreadProcessId(desc.OutputWindow,&pid);if(pid!=GetCurrentProcessId())return false;
        width=desc.BufferDesc.Width;height=desc.BufferDesc.Height;windowed=desc.Windowed!=FALSE;return true;
    }__except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
struct NativeString {const char* begin;const char* end;const char* capacity;unsigned allocator=0;};
bool SetNative(const ResolutionSnapshot& s,ResolutionSize size,bool fullscreen)noexcept {
    // Original typed setters own serialization and registry mutation. Their ABI
    // is __thiscall(registry, const eastl::string*, const value*), ret 8.
    char sizeName[]="DX11DisplaySettings.WindowSize",fullName[]="DX11DisplaySettings.FullScreen";
    const NativeString sizeKey{sizeName,sizeName+sizeof(sizeName)-1,sizeName+sizeof(sizeName)},
        fullKey{fullName,fullName+sizeof(fullName)-1,fullName+sizeof(fullName)};
    const unsigned char full=fullscreen?1:0;
    __try{
        const auto getter=reinterpret_cast<void*(__thiscall*)(void*,const char*,const void*)>(Base+binding.getField);
        void* const registry=reinterpret_cast<void*>(s.registry);
        if(getter(registry,sizeName,nullptr)!=reinterpret_cast<void*>(s.settings+0x0c)||
           getter(registry,fullName,nullptr)!=reinterpret_cast<void*>(s.settings+0x35))return false;
        const auto before=ReadResolutionSnapshot(memory,binding,Base);
        if(!before||!before->SameOwner(s)||before->desired!=s.desired||before->fullscreen!=s.fullscreen||
           before->optionsDesired!=s.optionsDesired||before->optionsFullscreen!=s.optionsFullscreen)return false;
        // Native graphics options are the persistent source copied back into
        // the registry at campaign start and later Options Apply. Synchronize
        // that source through its own setters, not direct field writes. These
        // setters are __thiscall(options, value), ret 4, and mark it dirty for
        // the original frame update. Never call the options-file save routine.
        void* const options=reinterpret_cast<void*>(s.options);
        reinterpret_cast<void(__thiscall*)(void*,bool)>(Base+binding.setOptionsFullscreen)(options,fullscreen);
        reinterpret_cast<void(__thiscall*)(void*,const ResolutionSize*)>(Base+binding.setOptionsSize)(options,&size);
        const auto upstream=ReadResolutionSnapshot(memory,binding,Base);
        if(!upstream||!upstream->SameOwner(s)||upstream->optionsDesired!=size||upstream->optionsFullscreen!=fullscreen)return false;
        reinterpret_cast<void(__thiscall*)(void*,const NativeString*,const unsigned char*)>(Base+binding.setFullscreen)(registry,&fullKey,&full);
        reinterpret_cast<void(__thiscall*)(void*,const NativeString*,const ResolutionSize*)>(Base+binding.setWindowSize)(registry,&sizeKey,&size);
        // Stock Options Apply explicitly notifies renderer virtual slot 0x90
        // after changing display settings. Registry setters alone do not queue
        // its resize. Recheck the same menu/renderer/settings owner immediately
        // before this original __thiscall(renderer), ret, including rollback.
        const auto beforeCommit=ReadResolutionSnapshot(memory,binding,Base);
        if(!beforeCommit||!beforeCommit->SameOwner(s)||!ResolutionSettingsMatch(*beforeCommit,size,fullscreen))return false;
        reinterpret_cast<void(__thiscall*)(void*)>(Base+binding.applySettings)(reinterpret_cast<void*>(s.renderer));
        return true;
    }__except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
void Finish(unsigned error)noexcept {result.error=error;enabled=false;done.store(true,std::memory_order_release);}
void Step(unsigned manager)noexcept {
    ++result.updates;
    const auto thread=GetCurrentThreadId();if(!result.uiThread)result.uiThread=thread;
    if(result.uiThread!=thread||manager!=original.manager||active.load()!=1){Finish(5);return;}
    const auto sample=ReadResolutionSnapshot(memory,binding,Base);
    if(!sample||!sample->SameOwner(original)){Finish(6);return;}
    const auto& s=*sample;result.afterWidth=s.width;result.afterHeight=s.height;
    const auto now=GetTickCount64();
    if(!attempted){
        if(cancel.load()||now>=deadline){Finish(7);return;}
        if(!ResolutionSettingsMatch(s,original.desired,original.fullscreen)){Finish(8);return;}
        // Let an already-pending native options apply finish before mutation.
        if(s.optionsDirty)return;
        attempted=true;
        const bool okay=SetNative(s,target,false);
        const auto after=ReadResolutionSnapshot(memory,binding,Base);
        if(!after||!after->SameOwner(original)){Finish(6);return;}
        result.applied=okay&&ResolutionSettingsMatch(*after,target,false);
        if(!result.applied)cancel=true;
        return;
    }
    if(!rollingBack&&(cancel.load()||now>=deadline)){
        // Revert only values still equal to our attempted values or the exact
        // untouched original. Never overwrite an intervening user's setting.
        if(!CanRestoreResolutionSettings(s,original,target)){Finish(8);return;}
        rollingBack=true;result.rollbackAttempted=1;stable=0;rollbackDeadline=now+3000;
        if(!SetNative(s,original.desired,original.fullscreen)){Finish(9);return;}
        return;
    }
    const auto wanted=rollingBack?original.desired:target;
    const bool wantedFullscreen=rollingBack?original.fullscreen:false;
    if(!ResolutionSettingsMatch(s,wanted,wantedFullscreen)){Finish(8);return;}
    unsigned width=0,height=0;bool windowed=false;
    // Observe the upstream dirty flag drained by the ORIGINAL frame update,
    // then require stable real renderer/swapchain agreement. A registry-only
    // resize is insufficient evidence that campaign entry will retain it.
    if(!s.optionsDirty&&ObservedChain(s,width,height,windowed)&&width==unsigned(wanted.width)&&height==unsigned(wanted.height)&&
       s.width==width&&s.height==height&&windowed!=wantedFullscreen){++stable;}else stable=0;
    if(stable>=3){if(rollingBack){result.rollbackConfirmed=1;Finish(7);}else{result.passed=1;Finish(0);}return;}
    if(rollingBack&&now>=rollbackDeadline)Finish(10);
}
void __fastcall UpdateHook(void* self,void*,unsigned a,unsigned b){
    struct Active{Active(){active.fetch_add(1,std::memory_order_acq_rel);}~Active(){
        if(active.fetch_sub(1,std::memory_order_acq_rel)==1&&configurationDeferred.load(std::memory_order_acquire)){
            cleanupDrained=true;CompleteConfiguration();
        }
    }} callback;
    reinterpret_cast<Update>(Base+binding.menu.update)(self,a,b);
    if(!enabled.load(std::memory_order_acquire))return;
    if(inspecting.test_and_set(std::memory_order_acquire)){cancel=true;return;}
    struct Guard{~Guard(){inspecting.clear(std::memory_order_release);}} guard;
    Step(unsigned(reinterpret_cast<std::uintptr_t>(self)));
}
bool Swap(unsigned expected,unsigned desired)noexcept {
    DWORD old=0;auto* slot=reinterpret_cast<volatile LONG*>(Base+binding.menu.updateSlot);
    if(!VirtualProtect(const_cast<LONG*>(slot),4,PAGE_READWRITE,&old))return false;
    if(!protectionDebt){protection=old;protectionDebt=true;}
    const bool changed=unsigned(InterlockedCompareExchange(slot,LONG(desired),LONG(expected)))==expected;
    if(changed)slotOwned=desired==unsigned(reinterpret_cast<std::uintptr_t>(&UpdateHook));
    DWORD ignored=0;if(VirtualProtect(const_cast<LONG*>(slot),4,protection,&ignored))protectionDebt=false;
    return changed&&!protectionDebt;
}
void Cleanup()noexcept {
    enabled=false;
    if(slotOwned)Swap(unsigned(reinterpret_cast<std::uintptr_t>(&UpdateHook)),Base+binding.menu.update);
    if(protectionDebt){DWORD ignored=0;if(VirtualProtect(reinterpret_cast<void*>(Base+binding.menu.updateSlot),4,protection,&ignored))protectionDebt=false;}
    cleanupRestored=!slotOwned&&!protectionDebt;
    const auto end=GetTickCount64()+2000;while(active.load()&&GetTickCount64()<end)Sleep(1);
    cleanupDrained=!active.load();
}
void Run(){
    if(reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr))!=Base)throw std::runtime_error("image base");
    wchar_t exe[32768]{};const auto n=GetModuleFileNameW(nullptr,exe,unsigned(std::size(exe)));if(!n||n>=std::size(exe))throw std::runtime_error("image path");
    const std::filesystem::path path(exe);const auto size=std::filesystem::file_size(path);
    if(size<4096||size>64*1024*1024)throw std::runtime_error("image size");
    std::ifstream file(path,std::ios::binary);std::vector<std::byte> bytes(std::size_t(size),std::byte{});
    if(!file.read(reinterpret_cast<char*>(bytes.data()),std::streamsize(size)))throw std::runtime_error("image read");
    const auto pe=fvr::engine::InspectPe(bytes);if(!pe.valid)throw std::runtime_error("PE");
    const auto found=DiscoverResolutionBinding(bytes,pe.image);if(!found)throw std::runtime_error("binding");binding=*found;
    for(const auto& span:ResolutionCodeSpans())if(!Same(bytes,pe.image,span.rva,span.size))throw std::runtime_error("live resolution span");
    for(const auto& span:MenuStartupCodeSpans())if(!Same(bytes,pe.image,span.rva,span.size))throw std::runtime_error("live UI span");
    unsigned slot=0;if(!Copy(Base+binding.menu.updateSlot,&slot,4)||slot!=Base+binding.menu.update)throw std::runtime_error("UI slot already owned");
    const auto initial=ReadResolutionSnapshot(memory,binding,Base);if(!initial)throw std::runtime_error("front-end ownership");original=*initial;
    if(!ResolutionSettingsMatch(original,original.desired,original.fullscreen))throw std::runtime_error("upstream display settings differ");
    result.beforeWidth=original.width;result.beforeHeight=original.height;
    result.afterWidth=original.width;result.afterHeight=original.height;
    target={float(result.width),float(result.height)};
    // Persistent event protection is armed before the first original setter.
    // Its lifetime is the exact supervisor lease, not the temporary menu hook.
    InstallGuard();
    // No callbacks, original registry calls or GPU operations from this worker.
    // The exact original UI update boundary performs the operation instead.
    deadline=GetTickCount64()+10000;enabled=true;
    if(!Swap(Base+binding.menu.update,unsigned(reinterpret_cast<std::uintptr_t>(&UpdateHook))))throw std::runtime_error("UI hook");
    while(!done.load(std::memory_order_acquire)&&GetTickCount64()<deadline+4000){if(GetTickCount64()>=deadline)cancel=true;Sleep(5);}
    if(!done.load(std::memory_order_acquire)){enabled=false;timedOut=true;}
}
}
extern "C" __declspec(dllexport) DWORD WINAPI FvrConfigureResolution(void* argument){
    static_assert(sizeof(void*)==4);
    ResolutionRequest input{};
    if(!Copy(unsigned(reinterpret_cast<std::uintptr_t>(argument)),&input,sizeof(input))||
       input.magic!=0x52565246||input.version!=2||!CurrentGame(input.lease)||
       !ValidResolutionSize({float(input.width),float(input.height)}))return 2;
    ResolutionRequest rejected{};rejected.lease=input.lease;rejected.width=input.width;rejected.height=input.height;
    if(requestGate.test_and_set(std::memory_order_acquire)){rejected.error=17;rejected.guardCleanupPending=1;Store(argument,rejected);return 17;}
    if(invoked.load()){
        requestGate.clear(std::memory_order_release);rejected.error=1;Store(argument,rejected);return 1;
    }
    invoked=true;lease=input.lease;leaseWidth=input.width;leaseHeight=input.height;
    result={};result.width=input.width;result.height=input.height;result.lease=lease;
    // The immutable process handle pins identity even if Windows reuses the PID.
    // Publish the lease only after all callback-visible handles are initialized.
    ownerProcess=OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,lease.ownerPid);
    releaseEvent=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    configurationEvent=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    const bool validOwner=ownerProcess&&releaseEvent&&configurationEvent&&Creation(ownerProcess)==lease.ownerCreation&&
        WaitForSingleObject(ownerProcess,0)==WAIT_TIMEOUT;
    leasePublished.store(true,std::memory_order_release);
    requestGate.clear(std::memory_order_release);
    HMODULE pinned=nullptr;
    const bool pin=GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,
        reinterpret_cast<LPCWSTR>(&FvrConfigureResolution),&pinned)!=FALSE;
    bool runException=false;
    if(!validOwner||!pin){runException=true;result.error=13;}
    else try{Run();}catch(...){runException=true;}
    Cleanup();
    if(!cleanupDrained.load()){
        // The UI callback can still own result/original. Publish a separate
        // failure object; do not race its fields or claim resize completion.
        RequestRelease(3);configurationDeferred.store(true,std::memory_order_release);
        // Cover the last callback leaving just before publication. Otherwise
        // that callback's epilogue completes the lease only after Step exits.
        if(!active.load(std::memory_order_acquire)){cleanupDrained=true;CompleteConfiguration();}
        DisableGuard();ResolutionRequest incomplete{};incomplete.lease=lease;
        incomplete.width=leaseWidth;incomplete.height=leaseHeight;incomplete.error=11;
        incomplete.hookRestored=cleanupRestored.load();GuardProof(incomplete);
        Store(argument,incomplete);return 11;
    }
    result.hookRestored=cleanupRestored.load();result.callbacksDrained=cleanupDrained.load();
    if(runException){result.passed=0;if(!result.error)result.error=3;}
    if(!cleanupRestored.load()||!cleanupDrained.load()||timedOut){result.passed=0;result.error=timedOut?12:11;}
    if(!result.passed||releaseRequested.load()||WaitForSingleObject(ownerProcess,0)!=WAIT_TIMEOUT){
        result.passed=0;RequestRelease(3);
    }else{
        auto prepared=ResolutionGuardPhase::Prepared;
        if(!guardPhase.compare_exchange_strong(prepared,ResolutionGuardPhase::Active)){result.passed=0;RequestRelease(3);}
    }
    CompleteConfiguration();
    if(releaseRequested.load())DisableGuard();
    GuardProof(result);
    if(!result.guardActive||result.guardCleanupPending){result.passed=0;if(!result.error)result.error=16;}
    if(!Store(argument,result))return 4;
    return result.passed?0:result.error?result.error:1;
}
extern "C" __declspec(dllexport) DWORD WINAPI FvrReleaseResolution(void* argument){
    ResolutionRequest input{};
    if(!Copy(unsigned(reinterpret_cast<std::uintptr_t>(argument)),&input,sizeof(input))||
       input.magic!=0x52565246||input.version!=2||!CurrentGame(input.lease))return 2;
    ResolutionRequest out{};out.lease=input.lease;
    if(requestGate.test_and_set(std::memory_order_acquire)){out.error=17;out.guardCleanupPending=1;Store(argument,out);return 17;}
    if(invoked.load()){
        if(!leasePublished.load(std::memory_order_acquire)||lease!=input.lease){
            requestGate.clear(std::memory_order_release);out.error=14;Store(argument,out);return 14;
        }
    }else{
        // A release can arrive after LoadLibrary but before configure starts.
        // Tombstone the exact lease so a delayed configure cannot arm afterward.
        invoked=true;lease=input.lease;leaseWidth=input.width;leaseHeight=input.height;
        cleanupRestored=true;cleanupDrained=true;configurationDone=true;
        leasePublished.store(true,std::memory_order_release);
    }
    out.width=leaseWidth;out.height=leaseHeight;
    requestGate.clear(std::memory_order_release);
    RequestRelease(1);
    if(!configurationDone.load(std::memory_order_acquire)&&configurationEvent)WaitForSingleObject(configurationEvent,2000);
    DisableGuard();
    out.hookRestored=cleanupRestored.load();out.callbacksDrained=cleanupDrained.load();GuardProof(out);
    out.passed=!out.guardActive&&!out.guardCleanupPending&&out.hookRestored&&out.callbacksDrained&&
        (out.guardReleased||out.guardNeverInstalled);
    out.error=out.passed?0:16;
    if(!Store(argument,out))return 4;
    return out.error;
}
BOOL WINAPI DllMain(HINSTANCE module,DWORD reason,LPVOID){if(reason==DLL_PROCESS_ATTACH)DisableThreadLibraryCalls(module);return TRUE;}
