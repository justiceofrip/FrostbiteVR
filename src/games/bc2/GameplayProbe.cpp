#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <MinHook.h>
#include "Bc2Profile.h"
#include "NativeProbeConfig.h"
#include <array>
#include <atomic>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <vector>
namespace {
using UpdateFn=void(__thiscall*)(void*,float);
using GatherFn=void(__thiscall*)(void*,void*);
UpdateFn originalUpdate=nullptr;GatherFn originalGather=nullptr;
fvr::bc2::GameplayCandidates profile;std::uintptr_t base=0;
std::atomic<bool> sampling=false;std::atomic<unsigned> active=0,calls=0,localCalls=0,gathers=0,mismatches=0;
std::array<void*,2> entries{};LONG started=0;
bool Read(std::uintptr_t address,void* out,std::size_t bytes)noexcept {SIZE_T n=0;return address>=0x10000&&address<=UINT32_MAX-bytes&&ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(address),out,bytes,&n)&&n==bytes;}
unsigned U32(std::uintptr_t address)noexcept {unsigned out=0;Read(address,&out,4);return out;}
struct Owner {unsigned manager=0,player=0,soldier=0,controlled=0,entry=0,router=0,cache=0;};
bool Resolve(Owner& o)noexcept {
    o.manager=U32(base+profile.contextObject+8);if(U32(o.manager)!=base+profile.managerVtable)return false;
    o.player=U32(o.manager+profile.localPlayerOffset);if(!o.player)return false;
    unsigned char flags=0;if(!Read(o.player+0xccd,&flags,1)||!(flags&8))return false;
    const auto weak=U32(o.player+profile.soldierWeakOffset),target=U32(weak);if(target<4)return false;o.soldier=target-4;
    if(U32(o.soldier+0x220)!=o.player)return false;
    o.controlled=U32(o.player+0xc60);const auto slot=U32(o.player+0xc64);
    const auto begin=U32(o.controlled+0x7c),end=U32(o.controlled+0x80);if(!begin||end<begin||end-begin>256||(end-begin)%4||slot>=(end-begin)/4)return false;
    o.entry=U32(begin+slot*4);o.router=U32(o.entry+0x198);o.cache=U32(o.player+profile.inputCacheOffset);
    return o.cache&&U32(o.router)==base+profile.inputRouterVtable&&U32(o.manager+profile.localPlayerOffset)==o.player;
}
struct Record {Owner owner{};unsigned thread=0;float dt=0;std::array<unsigned,42> input{};};
std::array<Record,256> records;std::atomic<unsigned> count=0;
struct Scope {Owner owner{};float dt=0;};thread_local Scope* current=nullptr;
struct Active {Active(){++active;}~Active(){--active;}};
void __fastcall GatherHook(void* self,void*,void* input){
    Active entered;originalGather(self,input);
    if(!sampling.load(std::memory_order_acquire)||!current)return;
    const auto& o=current->owner;
    if(reinterpret_cast<unsigned>(self)!=o.router||reinterpret_cast<unsigned>(input)!=o.cache){++mismatches;return;}
    ++gathers;const auto index=count.fetch_add(1);
    if(index<records.size()){auto& record=records[index];record.owner=o;record.thread=GetCurrentThreadId();record.dt=current->dt;Read(o.cache,record.input.data(),sizeof(record.input));}
}
void __fastcall UpdateHook(void* self,void*,float dt){
    Active entered;++calls;Scope scope{};
    const bool local=sampling.load(std::memory_order_acquire)&&!current&&Resolve(scope.owner)&&scope.owner.player==reinterpret_cast<unsigned>(self);
    if(local){++localCalls;scope.dt=dt;current=&scope;}
    originalUpdate(self,dt);
    if(local)current=nullptr;
}
void Require(bool okay,const char* message){if(!okay)throw std::runtime_error(message);}
std::size_t Offset(const fvr::engine::PeImage& pe,unsigned rva,unsigned n){for(const auto& s:pe.sections)if(rva>=s.rva&&rva-s.rva<=s.rawSize&&n<=s.rawSize-(rva-s.rva))return s.rawOffset+rva-s.rva;throw std::runtime_error("Unbacked gameplay profile address");}
bool Disable(){sampling.store(false,std::memory_order_release);bool okay=true;for(auto entry:entries)if(entry){const auto result=MH_DisableHook(entry);okay&=result==MH_OK||result==MH_ERROR_DISABLED;}return okay;}
}
extern "C" DWORD WINAPI FvrRunNativeProbe(void* request){
    fvr::bc2::NativeProbeConfig config{};if(!Read(reinterpret_cast<std::uintptr_t>(request),&config,sizeof(config))||config.magic!=0x32504246||config.bytes!=sizeof(config)||config.flags||config.durationMs<1000||config.durationMs>60000||config.reportPath[511])return 10;
    if(InterlockedCompareExchange(&started,1,0))return 11;
    std::ofstream report;bool disabled=true;
    try {
        const std::filesystem::path output(config.reportPath);Require(output.is_absolute()&&!std::filesystem::exists(output),"New absolute report required");report.open(output);Require(bool(report),"Create report");
        wchar_t filename[32768]{};Require(GetModuleFileNameW(nullptr,filename,32768)>0,"Game filename");const std::filesystem::path game(filename);
        Require(!_wcsicmp(game.filename().c_str(),L"BFBC2Game.exe"),"Unexpected game");
        const auto size=std::filesystem::file_size(game);Require(size>64&&size<512ull*1024*1024,"Game image bounds");std::vector<std::byte> bytes(std::size_t(size),std::byte{});
        std::ifstream file(game,std::ios::binary);Require(bool(file.read(reinterpret_cast<char*>(bytes.data()),bytes.size())),"Read image");const auto pe=fvr::engine::InspectPe(bytes);Require(pe.valid,"Inspect PE");
        const auto found=fvr::bc2::DiscoverGameplay(bytes,pe.image);Require(bool(found),"Gameplay code relationships unverified");profile=*found;base=reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
        for(const auto [rva,n]:{std::pair{profile.playerInputUpdate,0x1ebu},std::pair{profile.inputGather,0x232u},std::pair{profile.contextGetter,0x79u},std::pair{profile.soldierGetter,23u},std::pair{profile.inputRouterVtable,20u}}){
            std::vector<std::byte> live(n);Require(Read(base+rva,live.data(),n)&&!std::memcmp(live.data(),bytes.data()+Offset(pe.image,rva,n),n),"Live gameplay code changed");}
        Owner owner{};Require(Resolve(owner),"Local player/entry ownership unverified");
        Require(MH_Initialize()==MH_OK,"Initialize gameplay hooks");entries={reinterpret_cast<void*>(base+profile.playerInputUpdate),reinterpret_cast<void*>(base+profile.inputGather)};
        Require(MH_CreateHook(entries[0],UpdateHook,reinterpret_cast<void**>(&originalUpdate))==MH_OK,"Create update observer");
        Require(MH_CreateHook(entries[1],GatherHook,reinterpret_cast<void**>(&originalGather))==MH_OK,"Create input observer");
        for(auto entry:entries)Require(MH_QueueEnableHook(entry)==MH_OK,"Queue observer");Require(MH_ApplyQueued()==MH_OK,"Enable observers");disabled=false;sampling.store(true,std::memory_order_release);
        Sleep(config.durationMs);disabled=Disable();
        const auto until=GetTickCount64()+2000;while(active.load()&&GetTickCount64()<until)Sleep(1);Require(active.load()==0,"Gameplay callback still active");
        const auto complete=(std::min)(count.load(),unsigned(records.size()));
        report<<std::boolalpha<<"{\"state\":\"observed\",\"hooks_disabled\":"<<disabled<<",\"input_memory_written\":false,\"update_calls\":"<<calls.load()<<",\"local_calls\":"<<localCalls.load()<<",\"gathers\":"<<gathers.load()<<",\"mismatches\":"<<mismatches.load()<<",\"records\":[";
        for(unsigned i=0;i<complete;++i){const auto& r=records[i];if(i)report<<',';report<<"{\"player\":"<<r.owner.player<<",\"soldier\":"<<r.owner.soldier<<",\"controlled\":"<<r.owner.controlled<<",\"entry\":"<<r.owner.entry<<",\"router\":"<<r.owner.router<<",\"cache\":"<<r.owner.cache<<",\"thread\":"<<r.thread<<",\"dt\":"<<r.dt<<",\"input\":[";
            for(unsigned j=0;j<r.input.size();++j){if(j)report<<',';report<<r.input[j];}report<<"]}";}
        report<<"]}\n";return disabled&&localCalls.load()&&gathers.load()&&!mismatches.load()?0:12;
    }catch(const std::exception& error){disabled=Disable();if(report)report<<"{\"state\":\"failed\",\"error\":\""<<error.what()<<"\",\"hooks_disabled\":"<<(disabled?"true":"false")<<"}\n";return 20;}
}
