#include "Bc2MenuStartupRuntime.h"
#include "Bc2MenuStartup.h"
#include <Windows.h>
#include <atomic>
#include <cstring>
#include <ostream>
#include <string>
#include <vector>

namespace fvr::bc2::menu::startup {
namespace {
using Update=void(__thiscall*)(void*,unsigned,unsigned);
MenuStartupBinding binding{};unsigned imageBase=0,missingMask=0;
std::wstring eaPath,dolbyPath;
HANDLE worker=nullptr;
std::atomic<bool> prepared=false,enabled=false,slotOwned=false,restored=true,drained=true;
std::atomic<bool> protectionDebt=false;DWORD originalProtection=0;
std::atomic<unsigned> active=0,updates=0,attempted=0,calls=0,faults=0,ownerChanges=0,expired=0,uiThread=0;
std::atomic_flag inspecting=ATOMIC_FLAG_INIT;
std::uint64_t deadline=0;unsigned admittedManager=0,admittedOwner=0;
bool Copy(unsigned at,void* out,std::size_t n)noexcept {
    if(at<0x10000||!out||!n||n>65536||n>UINT32_MAX-at)return false;
    __try{std::memcpy(out,reinterpret_cast<const void*>(at),n);return true;}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
const MenuMemory memory{nullptr,[](void*,unsigned at,void* out,std::size_t n){return Copy(at,out,n);}};
bool MissingFile(const std::wstring& path)noexcept {
    if(path.empty())return false;
    if(GetFileAttributesW(path.c_str())!=INVALID_FILE_ATTRIBUTES)return false;
    const auto error=GetLastError();return error==ERROR_FILE_NOT_FOUND||error==ERROR_PATH_NOT_FOUND;
}
bool Same(std::span<const std::byte> bytes,const engine::PeImage& pe,unsigned rva,unsigned n){
    for(const auto& s:pe.sections)if(rva>=s.rva&&rva-s.rva<=s.rawSize&&n<=s.rawSize-(rva-s.rva)){
        const auto at=std::size_t(s.rawOffset)+rva-s.rva;if(at>bytes.size()||n>bytes.size()-at)return false;
        std::vector<std::byte> live(n);return Copy(imageBase+rva,live.data(),n)&&!std::memcmp(live.data(),bytes.data()+at,n);
    }return false;
}
bool Cleanup(unsigned owner)noexcept {
    // Native void __thiscall(MainMenuUI*), no stack arguments. This is the
    // exact stop/release path called by native skipMovie and EOF handling.
    __try{reinterpret_cast<void(__thiscall*)(void*)>(imageBase+binding.cleanup)(reinterpret_cast<void*>(owner));return true;}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
void Inspect(unsigned manager)noexcept {
    if(manager!=admittedManager){++ownerChanges;enabled=false;return;}
    const auto s=ReadMenuStartup(memory,binding,imageBase,manager);
    if(!s){++faults;enabled=false;return;}
    if(admittedOwner&&admittedOwner!=s->owner){++ownerChanges;enabled=false;return;}
    admittedOwner=s->owner;
    // Exhausted sequence is only a reason to stop this bounded helper. It is
    // never published as evidence that the menu has drawn or is interactive.
    if(s->enabled&&s->current[0]==0&&s->index>s->count){enabled=false;return;}
    if(!s->enabled)return; // Early attachment waits within the same deadline.
    const auto movie=ChooseMissingStartupMovie(true,s->enabled,s->index,s->count,
        s->current.data(),s->previous.data(),missingMask,attempted.load());
    if(movie==MenuStartupMovie::None)return;
    const auto repeated=ReadMenuStartup(memory,binding,imageBase,manager);
    if(!repeated||*repeated!=*s||!MissingFile(movie==MenuStartupMovie::EA?eaPath:dolbyPath)){
        ++faults;enabled=false;return;
    }
    if(!enabled.load(std::memory_order_acquire))return;
    if(GetTickCount64()>=deadline){++expired;enabled=false;return;}
    if(active.load()!=1){++faults;enabled=false;return;}
    // Record before calling: a fault never permits replaying the same cleanup.
    attempted.fetch_or(unsigned(movie));++calls;
    if(!Cleanup(s->owner)){++faults;enabled=false;return;}
    if((attempted.load()&missingMask)==missingMask)enabled=false;
}
void __fastcall UpdateHook(void* self,void*,unsigned a,unsigned b){
    struct Active{Active(){++active;}~Active(){--active;}} callback;
    reinterpret_cast<Update>(imageBase+binding.update)(self,a,b);++updates;
    // Deliberately AFTER the entire native UI update, matching the actual-game
    // probe. The existing mouse Pump is nested inside that update and is not a
    // proven safe location for releasing the movie currently being processed.
    if(!enabled.load(std::memory_order_acquire))return;
    if(GetTickCount64()>=deadline){++expired;enabled=false;return;}
    if(inspecting.test_and_set(std::memory_order_acquire)){++faults;enabled=false;return;}
    struct Guard{~Guard(){inspecting.clear(std::memory_order_release);}} guard;
    unsigned zero=0;const auto thread=GetCurrentThreadId();uiThread.compare_exchange_strong(zero,thread);
    if(uiThread.load()!=thread){++faults;enabled=false;return;}
    Inspect(unsigned(reinterpret_cast<std::uintptr_t>(self)));
}
bool Swap(unsigned expected,unsigned desired,bool& changed)noexcept {
    changed=false;DWORD old=0;
    auto* slot=reinterpret_cast<volatile LONG*>(imageBase+binding.updateSlot);
    if(!VirtualProtect(const_cast<LONG*>(slot),4,PAGE_READWRITE,&old))return false;
    // Preserve the first protection until it is actually restored. If a prior
    // restore failed, VirtualProtect reports RW, which is NOT the original.
    if(!protectionDebt.exchange(true))originalProtection=old;
    const auto prior=InterlockedCompareExchange(slot,LONG(desired),LONG(expected));changed=unsigned(prior)==expected;
    DWORD ignored=0;const bool protection=VirtualProtect(const_cast<LONG*>(slot),4,originalProtection,&ignored)!=0;
    if(protection)protectionDebt=false;
    return changed&&protection;
}
bool Restore()noexcept {
    bool okay=true;
    if(slotOwned.load()){
        bool changed=false;okay=Swap(unsigned(reinterpret_cast<std::uintptr_t>(&UpdateHook)),imageBase+binding.update,changed);
        if(changed)slotOwned=false;
    }
    if(protectionDebt.load()){
        DWORD ignored=0;
        if(VirtualProtect(reinterpret_cast<void*>(imageBase+binding.updateSlot),4,originalProtection,&ignored))protectionDebt=false;
        else okay=false;
    }
    restored=!slotOwned.load()&&!protectionDebt.load();
    if(!okay)++faults;return restored.load();
}
bool Drain()noexcept {
    const auto end=GetTickCount64()+2000;
    while(active.load()&&GetTickCount64()<end)Sleep(1);
    drained=!active.load();return drained.load();
}
DWORD WINAPI Watch(void*)noexcept {
    while(enabled.load(std::memory_order_acquire)){
        if(GetTickCount64()>=deadline){++expired;enabled=false;break;}Sleep(5);
    }
    Restore();
    Drain();return 0;
}
}
bool Prepare(std::span<const std::byte> bytes,const engine::PeImage& pe,std::uintptr_t image){
    if(prepared.load()||worker||image!=0x400000)return false;
    const auto found=DiscoverMenuStartup(bytes,pe);if(!found)return false;
    imageBase=unsigned(image);binding=*found;
    for(const auto& s:MenuStartupCodeSpans())if(!Same(bytes,pe,s.rva,s.size))return false;
    unsigned slot=0;if(!Copy(imageBase+binding.updateSlot,&slot,4)||slot!=imageBase+binding.update)return false;
    wchar_t exe[32768]{};const auto length=GetModuleFileNameW(nullptr,exe,unsigned(std::size(exe)));
    if(!length||length>=std::size(exe))return false;
    std::wstring folder(exe,length);const auto slash=folder.find_last_of(L"\\/");if(slash==std::wstring::npos)return false;
    folder.resize(slash+1);eaPath=folder+L"Output\\win32\\movies\\ea_logo_hd.res";
    dolbyPath=folder+L"Output\\win32\\movies\\dolbydigital.res";
    missingMask=(MissingFile(eaPath)?1u:0u)|(MissingFile(dolbyPath)?2u:0u);
    prepared=true;return true;
}
void Start()noexcept {
    if(!prepared.load()||!missingMask||worker||slotOwned.load())return;
    // Attaching during gameplay is not permission to touch the next main menu.
    unsigned manager=0;if(!Copy(imageBase+binding.managerGlobal,&manager,4))return;
    const auto initial=ReadMenuStartup(memory,binding,imageBase,manager);if(!initial)return;
    admittedManager=initial->manager;admittedOwner=initial->owner;
    deadline=GetTickCount64()+30000;enabled=true;restored=false;drained=false;
    bool changed=false;const bool okay=Swap(imageBase+binding.update,unsigned(reinterpret_cast<std::uintptr_t>(&UpdateHook)),changed);
    slotOwned=changed;
    if(!okay){++faults;enabled=false;Restore();drained=!active.load();return;}
    worker=CreateThread(nullptr,0,Watch,nullptr,0,nullptr);
    if(!worker){++faults;enabled=false;Restore();drained=!active.load();}
}
bool Stop()noexcept {
    enabled=false;
    if(worker){
        if(WaitForSingleObject(worker,2500)!=WAIT_OBJECT_0)return false;
        CloseHandle(worker);worker=nullptr;
    }
    Restore();Drain();
    return !slotOwned.load()&&!protectionDebt.load()&&restored.load()&&drained.load()&&!active.load();
}
void Report(std::ostream& out){
    out<<"{\"prepared\":"<<prepared.load()<<",\"missing_mask\":"<<missingMask
       <<",\"attempted_mask\":"<<attempted.load()<<",\"cleanup_calls\":"<<calls.load()<<",\"updates\":"<<updates.load()
       <<",\"faults\":"<<faults.load()<<",\"owner_changes\":"<<ownerChanges.load()<<",\"timeouts\":"<<expired.load()
       <<",\"slot_restored\":"<<restored.load()<<",\"protection_debt\":"<<protectionDebt.load()<<",\"callbacks_drained\":"<<drained.load()
       <<",\"visible_menu_verified\":false}";
}
}
