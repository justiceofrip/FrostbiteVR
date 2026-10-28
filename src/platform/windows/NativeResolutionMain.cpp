#include "Bc2ResolutionRequest.h"
#include "Bc2ResolutionBinding.h"
#include "NativeResolutionControllerPolicy.h"
#include <Windows.h>
#include <bcrypt.h>
#include <tlhelp32.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {
namespace fs=std::filesystem;
namespace policy=fvr::bc2::resolutionController;
using fvr::bc2::ResolutionRequest;
void Require(bool okay,const char* text){if(!okay)throw std::runtime_error(std::string(text)+" (Win32="+std::to_string(GetLastError())+")");}
struct Handle {HANDLE h=nullptr;~Handle(){if(h&&h!=INVALID_HANDLE_VALUE)CloseHandle(h);}};
struct Library {HMODULE h=nullptr;~Library(){if(h)FreeLibrary(h);}};
struct Allocation {
    HANDLE process=nullptr;void* pointer=nullptr;bool release=true;
    Allocation(HANDLE p,std::size_t bytes):process(p){pointer=VirtualAllocEx(p,nullptr,bytes,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);Require(pointer!=nullptr,"Allocate resolution request");}
    ~Allocation(){if(pointer&&release)VirtualFreeEx(process,pointer,0,MEM_RELEASE);}
};
void Write(HANDLE process,void* at,const void* source,std::size_t n){SIZE_T written=0;Require(WriteProcessMemory(process,at,source,n,&written)&&written==n,"Write resolution request");}
void Read(HANDLE process,const void* at,void* dest,std::size_t n){SIZE_T read=0;Require(ReadProcessMemory(process,at,dest,n,&read)&&read==n,"Read resolution result");}
std::wstring ProcessPath(HANDLE process){wchar_t text[32768]{};DWORD size=32768;Require(QueryFullProcessImageNameW(process,0,text,&size)!=FALSE,"Query original game path");return fs::path(text).lexically_normal().wstring();}
std::uint64_t Creation(HANDLE process){FILETIME c{},e{},k{},u{};Require(GetProcessTimes(process,&c,&e,&k,&u)!=FALSE,"Query process creation time");return (std::uint64_t(c.dwHighDateTime)<<32)|c.dwLowDateTime;}
bool Alive(HANDLE process){const auto wait=WaitForSingleObject(process,0);Require(wait==WAIT_TIMEOUT||wait==WAIT_OBJECT_0,"Query process lifetime");return wait==WAIT_TIMEOUT;}
bool SamePath(const fs::path& a,const fs::path& b){return !_wcsicmp(a.lexically_normal().c_str(),b.lexically_normal().c_str());}
std::uint64_t Number(const std::wstring& s){std::uint64_t n=0;Require(policy::Unsigned(s,n),"Expected an unsigned decimal integer");return n;}
DWORD Number32(const std::wstring& s){const auto n=Number(s);Require(n<=UINT32_MAX,"Integer exceeds uint32");return DWORD(n);}
std::string Ascii(const std::wstring& text){return {text.begin(),text.end()};}
struct Options {fs::path game,dll,report;std::string operation,token,dllHash;ResolutionRequest request{};};
Options Parse(int argc,wchar_t** argv){
    Require(argc>1&&(argc%2)==1,"Expected resolution --key value arguments");Options o;std::set<std::wstring> seen;
    for(int i=1;i<argc;i+=2){const std::wstring key=argv[i],value=argv[i+1];Require(!value.empty()&&seen.insert(key).second,"Missing or duplicate resolution argument");
        if(key==L"--operation"){Require(value==L"configure"||value==L"release","Unknown resolution operation");o.operation=Ascii(value);}
        else if(key==L"--game")o.game=(fs::absolute(value)/L"BFBC2Game.exe").lexically_normal();
        else if(key==L"--pid")o.request.lease.gamePid=Number32(value);
        else if(key==L"--creation-filetime")o.request.lease.gameCreation=Number(value);
        else if(key==L"--supervisor-pid")o.request.lease.ownerPid=Number32(value);
        else if(key==L"--supervisor-creation-filetime")o.request.lease.ownerCreation=Number(value);
        else if(key==L"--session-token"){Require(policy::Token(value,o.request.lease.token),"Expected a nonzero 128-bit lowercase session token");o.token=Ascii(value);}
        else if(key==L"--dll")o.dll=fs::absolute(value).lexically_normal();
        else if(key==L"--dll-sha256"){Require(policy::Hex(value,64),"Expected lowercase SHA-256 for the bootstrap DLL");o.dllHash=Ascii(value);}
        else if(key==L"--width")o.request.width=Number32(value);
        else if(key==L"--height")o.request.height=Number32(value);
        else if(key==L"--report")o.report=fs::absolute(value).lexically_normal();
        else Require(false,"Unknown resolution argument");
    }
    Require(!o.operation.empty()&&!o.game.empty()&&!o.dll.empty()&&!o.report.empty()&&!o.dllHash.empty()&&!o.token.empty()&&fvr::bc2::ValidResolutionLease(o.request.lease),"Incomplete resolution process/session identity");
    Require(!_wcsicmp(o.dll.filename().c_str(),L"BC2ResolutionBootstrap.dll"),"Expected the canonical resolution bootstrap module name");
    if(o.operation=="configure")Require(fvr::bc2::ValidResolutionSize({float(o.request.width),float(o.request.height)}),"Invalid resolution dimensions");
    else{o.request.width=0;o.request.height=0;}return o;
}
struct Module {std::uint32_t base=0,size=0;fs::path path;};
std::vector<Module> Modules(DWORD pid){
    HANDLE raw=INVALID_HANDLE_VALUE;for(unsigned i=0;i<8;++i){raw=CreateToolhelp32Snapshot(TH32CS_SNAPMODULE,pid);if(raw!=INVALID_HANDLE_VALUE||GetLastError()!=ERROR_BAD_LENGTH)break;}
    Handle snapshot{raw};Require(snapshot.h!=INVALID_HANDLE_VALUE,"Read game modules");
    MODULEENTRY32W item{};item.dwSize=sizeof(item);Require(Module32FirstW(snapshot.h,&item)!=FALSE,"Read first game module");std::vector<Module> out;
    do{Require(out.size()<4096,"Game module list exceeds bound");out.push_back({std::uint32_t(reinterpret_cast<std::uintptr_t>(item.modBaseAddr)),item.modBaseSize,fs::path(item.szExePath).lexically_normal()});}while(Module32NextW(snapshot.h,&item));
    Require(GetLastError()==ERROR_NO_MORE_FILES,"Incomplete game module list");return out;
}
std::optional<Module> Bootstrap(const std::vector<Module>& modules,const fs::path& expected){
    std::optional<Module> found;for(const auto& m:modules)if(!_wcsicmp(m.path.filename().c_str(),expected.filename().c_str())){
        Require(SamePath(m.path,expected)&&!found,"A different or duplicate resolution helper is already loaded");found=m;
    }return found;
}
std::uintptr_t RemoteLoader(const std::vector<Module>& modules,const fs::path& dll){
    const auto function=GetProcAddress(GetModuleHandleW(L"kernel32.dll"),"LoadLibraryW");Require(function!=nullptr,"Locate original loader");
    HMODULE module=nullptr;Require(GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(function),&module)!=FALSE,"Loader owner");
    wchar_t text[32768]{};const auto chars=GetModuleFileNameW(module,text,32768);Require(chars>0&&chars<32768,"Loader owner path");
    const auto rva=reinterpret_cast<std::uintptr_t>(function)-reinterpret_cast<std::uintptr_t>(module);std::uintptr_t found=0;
    for(const auto& m:modules){const auto name=m.path.filename();Require(_wcsicmp(name.c_str(),L"BC2NativeProbe.dll")&&_wcsicmp(name.c_str(),L"BC2GameplayProbe.dll")&&
        _wcsicmp(name.c_str(),L"BC2ResolutionBootstrap.dll")&&_wcsicmp(name.c_str(),dll.filename().c_str()),"Resolution configure requires a fresh game before any VR adapter");
        if(SamePath(m.path,text)&&rva<m.size)found=m.base+rva;
    }Require(found!=0,"Matching x86 loader was not found");return found;
}
std::vector<std::byte> FileBytes(HANDLE file){
    LARGE_INTEGER n{};Require(GetFileSizeEx(file,&n)&&n.QuadPart>=256&&n.QuadPart<=64ll*1024*1024,"Bootstrap image size is outside bounds");
    std::vector<std::byte> bytes(std::size_t(n.QuadPart));DWORD got=0;Require(ReadFile(file,bytes.data(),DWORD(bytes.size()),&got,nullptr)&&got==bytes.size(),"Read pinned bootstrap image");return bytes;
}
std::string Sha256(const std::vector<std::byte>& bytes){
    struct Algorithm{BCRYPT_ALG_HANDLE h=nullptr;~Algorithm(){if(h)BCryptCloseAlgorithmProvider(h,0);}}algorithm;
    struct Hash{BCRYPT_HASH_HANDLE h=nullptr;~Hash(){if(h)BCryptDestroyHash(h);}}hash;
    Require(BCryptOpenAlgorithmProvider(&algorithm.h,BCRYPT_SHA256_ALGORITHM,nullptr,0)>=0,"Open SHA-256 provider");
    Require(BCryptCreateHash(algorithm.h,&hash.h,nullptr,0,nullptr,0,0)>=0&&BCryptHashData(hash.h,reinterpret_cast<PUCHAR>(const_cast<std::byte*>(bytes.data())),ULONG(bytes.size()),0)>=0,"Hash pinned bootstrap image");
    std::array<unsigned char,32> digest{};Require(BCryptFinishHash(hash.h,digest.data(),ULONG(digest.size()),0)>=0,"Finish bootstrap hash");
    constexpr char hex[]="0123456789abcdef";std::string out;for(auto c:digest){out+=hex[c>>4];out+=hex[c&15];}return out;
}
policy::ImagePlan VerifyModule(HANDLE process,const Module& module,const std::vector<std::byte>& bytes){
    auto plan=policy::Plan(bytes,module.base);Require(plan&&plan->imageSize==module.size,"Loaded bootstrap PE identity does not match pinned image");
    auto equal=[&](const char* kind,std::uint32_t rva,const std::vector<std::byte>& expected){std::array<std::byte,65536> actual{};
        for(std::size_t at=0;at<expected.size();){const auto n=std::min(actual.size(),expected.size()-at);Read(process,reinterpret_cast<const void*>(std::uintptr_t(module.base)+rva+at),actual.data(),n);
            const auto different=std::mismatch(actual.begin(),actual.begin()+n,expected.begin()+at);
            if(different.first!=actual.begin()+n){
                const auto index=std::size_t(different.first-actual.begin()),begin=index>4?index-4:0,end=std::min(n,index+12);
                std::ostringstream message;message<<"Loaded bootstrap "<<kind<<" mismatch at RVA 0x"<<std::hex<<(rva+at+index)
                    <<"; expected "<<std::setfill('0')<<std::setw(2)<<std::to_integer<unsigned>(expected[at+index])
                    <<", actual "<<std::setw(2)<<std::to_integer<unsigned>(actual[index])<<"; context RVA 0x"<<(rva+at+begin)<<" expected=";
                for(auto i=begin;i<end;++i)message<<std::setw(2)<<std::to_integer<unsigned>(expected[at+i]);
                message<<" actual=";for(auto i=begin;i<end;++i)message<<std::setw(2)<<std::to_integer<unsigned>(actual[i]);
                // GetLastError belongs to an earlier native API, not a byte
                // comparison. Report only this exact mismatch and context.
                throw std::runtime_error(message.str());
            }at+=n;}
    };equal("headers",0,plan->headers);for(const auto& span:plan->executable)equal("executable",span.rva,span.bytes);return std::move(*plan);
}
DWORD Run(HANDLE process,std::uintptr_t function,Allocation& memory,DWORD timeout,bool& argumentsRetained){
    Handle thread{CreateRemoteThread(process,nullptr,0,reinterpret_cast<LPTHREAD_START_ROUTINE>(function),memory.pointer,0,nullptr)};Require(thread.h!=nullptr,"Start original game resolution helper");
    // A deadline never permits terminating native code or freeing its arguments.
    memory.release=false;argumentsRetained=true;Require(WaitForSingleObject(thread.h,timeout)==WAIT_OBJECT_0,"Resolution helper exceeded its bounded deadline; its arguments remain allocated");
    memory.release=true;argumentsRetained=false;Require(Alive(process),"Original game exited during resolution operation");
    DWORD code=0;Require(GetExitCodeThread(thread.h,&code)!=FALSE,"Read resolution helper exit");return code;
}
std::string Json(const std::string& text){std::string out="\"";for(const auto c:text){if(c=='"'||c=='\\')out+='\\';if(static_cast<unsigned char>(c)>=32)out+=c;}return out+'"';}
void Report(std::ostream& out,const Options& o,const ResolutionRequest& r,const std::string& error,bool moduleRetained,bool argumentsRetained,bool gameExited){
    out<<std::boolalpha<<"{\"schema\":\"bc2vr.native-resolution.v2\",\"operation\":"<<Json(o.operation)<<",\"passed\":"<<(r.passed==1)
       <<",\"pid\":"<<o.request.lease.gamePid<<",\"creation_filetime\":"<<Json(std::to_string(o.request.lease.gameCreation))
       <<",\"supervisor_pid\":"<<o.request.lease.ownerPid<<",\"supervisor_creation_filetime\":"<<Json(std::to_string(o.request.lease.ownerCreation))
       <<",\"session_token\":"<<Json(o.token)<<",\"module_sha256\":"<<Json(o.dllHash)
       <<",\"requested_width\":"<<o.request.width<<",\"requested_height\":"<<o.request.height
       <<",\"before_width\":"<<r.beforeWidth<<",\"before_height\":"<<r.beforeHeight
       <<",\"width\":"<<r.afterWidth<<",\"height\":"<<r.afterHeight<<",\"error_code\":"<<r.error
       <<",\"error\":"<<Json(error)<<",\"applied\":"<<(r.applied==1)
       <<",\"rollback_attempted\":"<<(r.rollbackAttempted==1)<<",\"rollback_confirmed\":"<<(r.rollbackConfirmed==1)
       <<",\"updates\":"<<r.updates<<",\"ui_thread\":"<<r.uiThread
       <<",\"hook_restored\":"<<(r.hookRestored==1)<<",\"callbacks_drained\":"<<(r.callbacksDrained==1)
       <<",\"guard_installed\":"<<(r.guardInstalled==1)<<",\"guard_active\":"<<(r.guardActive==1)
       <<",\"guard_disabled\":"<<(r.guardDisabled==1)<<",\"guard_drained\":"<<(r.guardDrained==1)
       <<",\"guard_released\":"<<(r.guardReleased==1)<<",\"guard_owner_lost\":"<<(r.guardOwnerLost==1)
       <<",\"guard_cleanup_pending\":"<<(r.guardCleanupPending==1)<<",\"guard_never_installed\":"<<(r.guardNeverInstalled==1)
       <<",\"guard_release_reason\":"<<r.guardReleaseReason<<",\"guard_blocked_events\":"<<r.guardBlockedEvents
       <<",\"guard_passed_events\":"<<r.guardPassedEvents<<",\"guard_owner_mismatches\":"<<r.guardOwnerMismatches
       <<",\"game_exited\":"<<gameExited<<",\"module_retained_until_game_exit\":"<<moduleRetained
       <<",\"remote_arguments_retained\":"<<argumentsRetained
       <<",\"settings_file_modified\":null,\"settings_persistence_observed\":false,\"helper_direct_settings_file_writes\":false,\"direct_renderer_writes\":false}\n";
}
void Execute(const Options& o,ResolutionRequest& result,bool& moduleRetained,bool& argumentsRetained,bool& gameExited){
    const bool release=o.operation=="release";const auto& lease=o.request.lease;
    Handle process{OpenProcess(PROCESS_QUERY_INFORMATION|PROCESS_VM_READ|PROCESS_VM_WRITE|PROCESS_VM_OPERATION|PROCESS_CREATE_THREAD|SYNCHRONIZE,FALSE,lease.gamePid)};
    if(!process.h){const auto code=GetLastError();if(release&&code==ERROR_INVALID_PARAMETER){gameExited=true;result.passed=1;return;}Require(false,"Open original game");}
    if(!Alive(process.h)||Creation(process.h)!=lease.gameCreation){Require(release,"Original game lifetime changed");gameExited=true;result.passed=1;return;}
    Require(SamePath(ProcessPath(process.h),o.game),"Original game path changed");
    auto modules=Modules(lease.gamePid);auto module=Bootstrap(modules,o.dll);
    if(release&&!module){result.passed=1;result.guardNeverInstalled=1;result.guardDisabled=1;result.guardDrained=1;return;}
    Handle owner;if(!release){owner.h=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,lease.ownerPid);Require(owner.h&&Alive(owner.h)&&Creation(owner.h)==lease.ownerCreation,"Resolution supervisor is not the original live process");}
    // Holding a read-only, share-read handle prevents replacing the DLL while
    // its hash, local export and loaded executable identity are being checked.
    Handle image{CreateFileW(o.dll.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr)};
    Require(image.h!=INVALID_HANDLE_VALUE,"Open pinned bootstrap image");const auto bytes=FileBytes(image.h);Require(Sha256(bytes)==o.dllHash,"Bootstrap SHA-256 does not match session receipt");
    Library local{LoadLibraryExW(o.dll.c_str(),nullptr,DONT_RESOLVE_DLL_REFERENCES)};Require(local.h!=nullptr,"Inspect resolution helper");
    const auto entry=GetProcAddress(local.h,release?"FvrReleaseResolution":"FvrConfigureResolution");
    const auto rva=entry?reinterpret_cast<std::uintptr_t>(entry)-reinterpret_cast<std::uintptr_t>(local.h):0;Require(rva&&rva<=UINT32_MAX,"Resolution helper export missing");
    if(!release){DWORD binary=0;Require(GetBinaryTypeW(o.game.c_str(),&binary)&&binary==SCS_32BIT_BINARY,"Expected original x86 game");const auto loader=RemoteLoader(modules,o.dll);
        const auto text=o.dll.wstring();Allocation pathMemory(process.h,(text.size()+1)*sizeof(wchar_t));Write(process.h,pathMemory.pointer,text.c_str(),(text.size()+1)*sizeof(wchar_t));
        const auto loaded=Run(process.h,loader,pathMemory,8000,argumentsRetained);Require(loaded!=0,"Resolution helper load failed");moduleRetained=true;
        module=Bootstrap(Modules(lease.gamePid),o.dll);Require(module&&module->base==loaded,"Loaded resolution helper identity changed");
    }else moduleRetained=true;
    const auto plan=VerifyModule(process.h,*module,bytes);Require(plan.ContainsCode(std::uint32_t(rva)),"Resolution export is outside pinned executable bytes");
    Require(Alive(process.h)&&Creation(process.h)==lease.gameCreation&&SamePath(ProcessPath(process.h),o.game),"Original game changed before resolution operation");
    if(!release)Require(Alive(owner.h)&&Creation(owner.h)==lease.ownerCreation,"Resolution supervisor exited before configure");
    Allocation data(process.h,sizeof(o.request));Write(process.h,data.pointer,&o.request,sizeof(o.request));
    const auto code=Run(process.h,std::uintptr_t(module->base)+rva,data,release?6000:20000,argumentsRetained);ResolutionRequest reply{};Read(process.h,data.pointer,&reply,sizeof(reply));
    Require(policy::MatchingReply(o.request,reply,!release),"Resolution reply changed the process/session identity");result=reply;
    Require(code==0&&(release?policy::ReleaseComplete(result):policy::SetupComplete(result)),release?"Resolution guard release was not confirmed":"Native resolution and retained guard were not confirmed; the VR adapter was not attached");
}
}
int wmain(int argc,wchar_t** argv){
    static_assert(sizeof(void*)==4);Options options;ResolutionRequest result{};
    bool reportOwned=false,moduleRetained=false,argumentsRetained=false,gameExited=false;std::string error;
    try{options=Parse(argc,argv);result=options.request;Require(!fs::exists(options.report),"Resolution evidence already exists");reportOwned=true;Execute(options,result,moduleRetained,argumentsRetained,gameExited);}
    catch(const std::exception& e){result.passed=0;error=e.what();}
    Report(std::cout,options,result,error,moduleRetained,argumentsRetained,gameExited);
    if(reportOwned){std::ofstream out(options.report);if(!out){std::cerr<<"Cannot save resolution evidence\n";return 2;}Report(out,options,result,error,moduleRetained,argumentsRetained,gameExited);if(!out)return 2;}
    return result.passed==1?0:1;
}
