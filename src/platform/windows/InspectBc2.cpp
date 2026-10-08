#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <tlhelp32.h>
#include "fvr/engine/PeImage.h"
#include "Bc2Profile.h"
#include "fvr/engine/ModuleApi.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
namespace fs=std::filesystem;
namespace {
std::string Utf8(const std::wstring& s){if(s.empty())return {};const int n=WideCharToMultiByte(CP_UTF8,0,s.data(),int(s.size()),nullptr,0,nullptr,nullptr);std::string out(n,'\0');WideCharToMultiByte(CP_UTF8,0,s.data(),int(s.size()),out.data(),n,nullptr,nullptr);return out;}
std::string Json(const std::string& s){std::string out="\"";const char* hex="0123456789abcdef";for(unsigned char c:s){if(c=='"'||c=='\\'){out+='\\';out+=char(c);}else if(c<32){out+="\\u00";out+=hex[c>>4];out+=hex[c&15];}else out+=char(c);}return out+'"';}
fs::path ExecutableFolder(){std::wstring p(32768,L'\0');auto n=GetModuleFileNameW(nullptr,p.data(),DWORD(p.size()));if(!n||n>=p.size())throw std::runtime_error("Cannot locate inspector");p.resize(n);return fs::path(p).parent_path();}
void PrintModule(){
    const auto path=ExecutableFolder()/L"BC2Adapter.dll";
    const auto module=LoadLibraryExW(path.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32);
    if(!module)throw std::runtime_error("Cannot load local BC2Adapter.dll");
    const auto entry=reinterpret_cast<FvrGetModuleInfoFn>(GetProcAddress(module,"FvrGetModuleInfo"));FvrModuleInfo info{};
    const bool ok=entry&&entry(FVR_MODULE_ABI,sizeof(info),&info)&&info.abi==FVR_MODULE_ABI&&info.size==sizeof(info)&&info.pointerBits==sizeof(void*)*8;
    if(!ok){FreeLibrary(module);throw std::runtime_error("Adapter ABI mismatch");}
    info.id[63]=0;info.status[255]=0;
    std::cout<<"  \"adapter\": {\"id\":"<<Json(info.id)<<",\"abi\":"<<info.abi<<",\"pointer_bits\":"<<info.pointerBits<<",\"native_capabilities\":"<<info.capabilities<<",\"status\":"<<Json(info.status)<<"},\n";
    FreeLibrary(module);
}
void PrintProcesses(const fs::path& expected){
    std::cout<<"  \"running_processes\": [";bool first=true;
    HANDLE snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);if(snapshot==INVALID_HANDLE_VALUE){std::cout<<"],\n";return;}
    PROCESSENTRY32W p{};p.dwSize=sizeof(p);
    if(Process32FirstW(snapshot,&p))do{
        if(_wcsicmp(p.szExeFile,L"BFBC2Game.exe"))continue;
        HANDLE process=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,p.th32ProcessID);if(!process)continue;
        wchar_t path[32768]{};DWORD length=32768;const bool queried=QueryFullProcessImageNameW(process,0,path,&length)!=0;CloseHandle(process);
        if(!queried||_wcsicmp(fs::path(path).lexically_normal().c_str(),expected.lexically_normal().c_str()))continue;
        if(!first)std::cout<<',';first=false;std::cout<<"{\"pid\":"<<p.th32ProcessID<<",\"loaded_graphics_modules\":[";
        HANDLE modules=CreateToolhelp32Snapshot(TH32CS_SNAPMODULE|TH32CS_SNAPMODULE32,p.th32ProcessID);bool firstModule=true;
        if(modules!=INVALID_HANDLE_VALUE){MODULEENTRY32W m{};m.dwSize=sizeof(m);if(Module32FirstW(modules,&m))do{
            std::wstring name=m.szModule;std::transform(name.begin(),name.end(),name.begin(),[](wchar_t c){return wchar_t(towlower(c));});
            if(name==L"d3d9.dll"||name==L"d3d10.dll"||name==L"d3d10_1.dll"||name==L"d3d11.dll"||name==L"dxgi.dll"){
                if(!firstModule)std::cout<<',';firstModule=false;std::cout<<Json(Utf8(name));}
        }while(Module32NextW(modules,&m));CloseHandle(modules);}
        std::cout<<"],\"module_enumeration_succeeded\":"<<(modules!=INVALID_HANDLE_VALUE?"true":"false")<<'}';
    }while(Process32NextW(snapshot,&p));CloseHandle(snapshot);std::cout<<"],\n";
}
}
int wmain(int argc,wchar_t** argv){
    try{
        fs::path root;
        if(argc==3&&std::wstring(argv[1])==L"--game")root=argv[2];
        else{std::cerr<<"Usage: BC2Inspect.exe [--game installation-folder]\n";return 1;}
        root=fs::absolute(root).lexically_normal();const auto game=root/L"BFBC2Game.exe";
        std::cout<<"{\n  \"schema\":1,\n  \"game_folder\":"<<Json(Utf8(root.wstring()))<<",\n";PrintModule();PrintProcesses(game);
        std::cout<<"  \"renderer_selected\":null,\n  \"renderer_note\":\"Imports and loaded graphics DLLs are discovery evidence, not proof of the active renderer.\",\n";
        if(!fs::is_regular_file(game)){std::cout<<"  \"state\":\"waiting_for_install\",\n  \"message\":\"BFBC2Game.exe is not present. No game files modified.\"\n}\n";return 2;}
        const auto size=fs::file_size(game);const auto stamp=fs::last_write_time(game);
        if(size<64||size>512ull*1024*1024)throw std::runtime_error("Executable size invalid or still installing");
        std::ifstream stream(game,std::ios::binary);std::vector<std::byte> bytes(static_cast<std::size_t>(size));
        if(!stream.read(reinterpret_cast<char*>(bytes.data()),static_cast<std::streamsize>(bytes.size())))throw std::runtime_error("Executable is unavailable or still installing");
        if(fs::file_size(game)!=size||fs::last_write_time(game)!=stamp)throw std::runtime_error("Executable changed during inspection; retry after installation");
        const auto result=fvr::engine::InspectPe(bytes);
        if(!result.valid){std::cout<<"  \"state\":\"invalid_or_incomplete_image\",\n  \"message\":"<<Json(result.error)<<"\n}\n";return 3;}
        const auto& pe=result.image;
        std::cout<<"  \"state\":\"native_profile_unverified\",\n  \"machine\":"<<pe.machine<<",\n  \"image_size\":"<<pe.imageSize<<",\n  \"pe_timestamp\":"<<pe.timestamp<<",\n  \"large_address_aware\":"<<(pe.largeAddressAware?"true":"false")<<",\n  \"imports\":[";
        bool first=true;for(const auto& name:pe.imports){if(!first)std::cout<<',';first=false;std::cout<<Json(name);}std::cout<<"],\n  \"sections\":[";
        first=true;for(const auto& section:pe.sections){if(!first)std::cout<<',';first=false;std::cout<<"{\"name\":"<<Json(section.name)<<",\"rva\":"<<section.rva<<",\"bytes\":"<<section.rawSize<<",\"executable\":"<<((section.flags&0x20000000)?"true":"false")<<'}';}
        std::cout<<"],\n  \"render_path_candidates\":";
        const auto renderPath=fvr::bc2::DiscoverRenderPath(bytes,pe);
        if(renderPath){const auto& r=*renderPath;
            std::cout<<"{\"subsystem_vtable_rva\":"<<r.subsystemVtable<<",\"subsystem_draw_rva\":"<<r.subsystemDraw
                <<",\"world_renderer_vtable_rva\":"<<r.worldRendererVtable<<",\"world_render_rva\":"<<r.worldRender
                <<",\"prepare_view_rva\":"<<r.prepareView<<",\"draw_view_rva\":"<<r.drawView<<",\"update_view_cache_rva\":"<<r.updateViewCache
                <<",\"render_only_verified\":false}";
        }else std::cout<<"null";
        std::cout<<",\n  \"headset_tested\":false,\n  \"game_files_modified\":false\n}\n";return 0;
    }catch(const std::exception& e){std::cerr<<"BC2 inspection failed: "<<e.what()<<'\n';return 1;}
}