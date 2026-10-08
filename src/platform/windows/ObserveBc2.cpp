#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <tlhelp32.h>
#include "Bc2Profile.h"
#include "fvr/engine/FrostbiteCamera.h"
#include "fvr/interaction/TrackingMath.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <vector>
namespace fs=std::filesystem;
namespace {
struct Handle {HANDLE h=nullptr;~Handle(){if(h&&h!=INVALID_HANDLE_VALUE)CloseHandle(h);}explicit operator bool()const{return h&&h!=INVALID_HANDLE_VALUE;}};
struct Module {std::wstring name;std::uintptr_t base=0;std::size_t size=0;};
std::vector<std::byte> Read(HANDLE p,std::uintptr_t at,std::size_t n){
    if(!at||!n||n>1024*1024||at>UINT32_MAX||n>UINT32_MAX-at)throw std::runtime_error("Unbounded process read rejected");
    std::vector<std::byte> out(n);SIZE_T got=0;if(!ReadProcessMemory(p,reinterpret_cast<const void*>(at),out.data(),n,&got)||got!=n)throw std::runtime_error("Native object changed or became unreadable; retry observation");return out;
}
std::uint32_t U32(std::span<const std::byte> b,std::size_t off=0){std::uint32_t value=0;std::memcpy(&value,b.data()+off,4);return value;}
std::uint32_t Ptr(HANDLE p,std::uintptr_t a){return U32(Read(p,a,4));}
std::size_t Offset(const fvr::engine::PeImage& pe,std::uint32_t rva,std::size_t size){for(const auto& s:pe.sections)if(rva>=s.rva&&rva-s.rva<=s.rawSize&&size<=s.rawSize-(rva-s.rva))return std::size_t(s.rawOffset)+rva-s.rva;throw std::runtime_error("Profile points outside backed image");}
std::vector<Module> Modules(DWORD pid){Handle snap{CreateToolhelp32Snapshot(TH32CS_SNAPMODULE|TH32CS_SNAPMODULE32,pid)};if(!snap)throw std::runtime_error("Cannot enumerate process modules");MODULEENTRY32W m{};m.dwSize=sizeof(m);std::vector<Module> out;if(Module32FirstW(snap.h,&m))do{out.push_back({m.szModule,reinterpret_cast<std::uintptr_t>(m.modBaseAddr),m.modBaseSize});}while(Module32NextW(snap.h,&m));return out;}
bool InModule(const std::vector<Module>& modules,std::uintptr_t address,const wchar_t* name){for(const auto& m:modules)if(!_wcsicmp(m.name.c_str(),name)&&address>=m.base&&address-m.base<m.size)return true;return false;}
DWORD FindProcess(const fs::path& expected){Handle snap{CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0)};if(!snap)throw std::runtime_error("Cannot enumerate BC2 processes");PROCESSENTRY32W p{};p.dwSize=sizeof(p);DWORD found=0;
    if(Process32FirstW(snap.h,&p))do{if(_wcsicmp(p.szExeFile,L"BFBC2Game.exe"))continue;Handle process{OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,p.th32ProcessID)};if(!process)continue;
        wchar_t path[32768]{};DWORD n=32768;if(QueryFullProcessImageNameW(process.h,0,path,&n)&&!_wcsicmp(fs::path(path).lexically_normal().c_str(),expected.c_str())){if(found)throw std::runtime_error("More than one game process matches this installation");found=p.th32ProcessID;}
    }while(Process32NextW(snap.h,&p));return found;
}

std::string WorldLifecycle(HANDLE process,std::uintptr_t base,std::uint32_t game,const fvr::bc2::RenderPathCandidates& render,const fvr::bc2::ViewLifecycleCandidates& life){
    const auto subsystem=Ptr(process,std::uintptr_t(game)+0x108c);
    if(Ptr(process,subsystem)!=base+render.subsystemVtable)throw std::runtime_error("Lifecycle subsystem changed");
    const auto world=Ptr(process,std::uintptr_t(subsystem)+0x2c),request=Ptr(process,std::uintptr_t(subsystem)+0x30);
    if(Ptr(process,world)!=base+render.worldRendererVtable||Ptr(process,request)!=base+life.requestVtable||Ptr(process,std::uintptr_t(request)+8)!=world)throw std::runtime_error("Lifecycle ownership changed");
    struct ListSnapshot {std::uintptr_t address=0;std::vector<std::byte> header,items;};
    const auto snapshot=[&](std::uintptr_t at){
        ListSnapshot result{at,Read(process,at,12),{}};const auto begin=U32(result.header),end=U32(result.header,4),capacity=U32(result.header,8);
        if(end<begin||capacity<end||(end-begin)%4||end-begin>256)throw std::runtime_error("Lifecycle list out of bounds");
        if(end>begin)result.items=Read(process,begin,end-begin);return result;
    };
    const auto all=snapshot(std::uintptr_t(request)+0x14),main=snapshot(std::uintptr_t(request)+0x24),callbacks=snapshot(std::uintptr_t(world)+0x9c);
    std::ostringstream out;out<<"{\"state\":\"observed\",\"world\":"<<world<<",\"request\":"<<request<<",\"request_refs\":"<<Ptr(process,std::uintptr_t(request)+4)<<",\"main_view_count\":"<<main.items.size()/4<<",\"all_views\":[";
    for(std::size_t i=0;i<all.items.size()/4;++i){if(i)out<<',';const auto view=U32(all.items,i*4);const auto header=Read(process,view,0x74);
        if(U32(header,0x70)!=request)throw std::runtime_error("Registered view owner changed");
        out<<"{\"view\":"<<view<<",\"vtable\":"<<U32(header)<<",\"references\":"<<U32(header,0x20)<<'}';
    }
    out<<"],\"world_callbacks\":[";
    for(std::size_t i=0;i<callbacks.items.size()/4;++i){if(i)out<<',';const auto object=U32(callbacks.items,i*4),vtable=Ptr(process,object);const auto entries=Read(process,vtable,36);
        out<<"{\"object\":"<<object<<",\"vtable\":"<<vtable<<",\"update_function\":"<<U32(entries,8)<<",\"create_view_function\":"<<U32(entries,28)<<",\"destroy_view_function\":"<<U32(entries,32)<<'}';
        if(Ptr(process,object)!=vtable)throw std::runtime_error("Lifecycle callback changed");
    }
    for(const auto* list:{&all,&main,&callbacks})if(Read(process,list->address,12)!=list->header||(!list->items.empty()&&Read(process,U32(list->header),list->items.size())!=list->items))throw std::runtime_error("Lifecycle list changed during observation");
    if(Ptr(process,std::uintptr_t(game)+0x108c)!=subsystem||Ptr(process,std::uintptr_t(subsystem)+0x2c)!=world||Ptr(process,std::uintptr_t(subsystem)+0x30)!=request||Ptr(process,std::uintptr_t(request)+8)!=world)throw std::runtime_error("Lifecycle owner changed during observation");
    out<<"],\"snapshot_stable\":true,\"factory_invoked\":false}";return out.str();
}

std::string WorldViews(HANDLE process,std::uintptr_t base,std::uint32_t game,const fvr::bc2::RenderPathCandidates& render,const fvr::bc2::ViewLayoutCandidates& layout){
    const auto subsystem=Ptr(process,std::uintptr_t(game)+0x108c);
    if(Ptr(process,subsystem)!=base+render.subsystemVtable)throw std::runtime_error("World subsystem ownership changed");
    const auto world=Ptr(process,std::uintptr_t(subsystem)+0x2c),request=Ptr(process,std::uintptr_t(subsystem)+0x30);
    if(Ptr(process,world)!=base+render.worldRendererVtable||Ptr(process,std::uintptr_t(request)+8)!=world)throw std::runtime_error("World request ownership changed");
    const auto range=Read(process,std::uintptr_t(request)+0x24,12);const auto begin=U32(range),end=U32(range,4),capacity=U32(range,8);
    if(!begin||end<begin||capacity<end||(end-begin)%4||(end-begin)>64)throw std::runtime_error("World view list unavailable or outside bounds");
    const auto count=(end-begin)/4;if(!count)return "{\"state\":\"no_world_views\",\"view_count\":0}";
    const auto list=Read(process,begin,end-begin);std::ostringstream out;
    out<<"{\"state\":\"observed\",\"view_count\":"<<count<<",\"vtable_rva\":"<<layout.vtable<<",\"copy_render_view_rva\":"<<layout.copyRenderView<<",\"views\":[";
    for(unsigned index=0;index<count;++index){const auto view=U32(list,index*4);const auto snapshot=Read(process,view,layout.fourthOffset+0x460);
        if(U32(snapshot)!=base+layout.vtable||U32(snapshot,layout.ownerRequestOffset)!=request)throw std::runtime_error("Per-view request ownership changed");
        if(index)out<<',';out<<"{\"active\":"<<(snapshot[layout.activeOffset]==std::byte{1}?"true":"false")<<",\"viewport\":[";
        for(unsigned component=0;component<4;++component){if(component)out<<',';out<<U32(snapshot,layout.viewportOffset+component*4);}out<<"],\"camera_blocks\":[";
        unsigned blockIndex=0;for(auto offset:{layout.primaryOffset,layout.secondaryOffset,layout.thirdOffset,layout.fourthOffset}){
            fvr::engine::FrostbiteCameraInput input{};std::memcpy(&input.transform,snapshot.data()+offset+0x50,64);std::memcpy(&input.nearPlane,snapshot.data()+offset+0x1c,4);std::memcpy(&input.farPlane,snapshot.data()+offset+0x20,4);input.worldUnitsPerMeter=1;
            fvr::math::Matrix4 nativeView{},projection{};std::memcpy(&nativeView,snapshot.data()+offset+0x220,64);std::memcpy(&projection,snapshot.data()+offset+0x2e0,64);
            auto transform=input.transform;for(unsigned row=0;row<4;++row)transform.values[row][3]=row==3?1.f:0.f;
            const auto inverse=fvr::interaction::InverseRigid(transform);float error=0;
            if(inverse)for(unsigned row=0;row<4;++row)for(unsigned column=0;column<4;++column){const auto delta=std::abs(inverse->values[row][column]-nativeView.values[row][column]);error=std::isfinite(delta)?std::max(error,delta):INFINITY;}
            const bool coherent=fvr::engine::CanonicalCamera(input)&&inverse&&error<.02f;
            if(blockIndex++)out<<',';out<<"{\"offset\":"<<offset<<",\"coherent\":"<<(coherent?"true":"false")<<",\"projection_rh\":"<<(projection.values[2][3]==-1?"true":"false")<<'}';
        }out<<"]}";
        if(Ptr(process,view)!=base+layout.vtable||Ptr(process,std::uintptr_t(view)+layout.ownerRequestOffset)!=request)throw std::runtime_error("Per-view owner changed during snapshot");
    }
    if(Read(process,begin,end-begin)!=list||Read(process,std::uintptr_t(request)+0x24,12)!=range||Ptr(process,std::uintptr_t(game)+0x108c)!=subsystem||Ptr(process,std::uintptr_t(subsystem)+0x2c)!=world||Ptr(process,std::uintptr_t(subsystem)+0x30)!=request)throw std::runtime_error("World ownership changed during snapshot");
    out<<"],\"owner_stable\":true,\"render_only_verified\":false}";return out.str();
}

}
int wmain(int argc,wchar_t** argv){try{
    if(argc!=3||std::wstring(argv[1])!=L"--game"){std::cerr<<"Usage: BC2Observe.exe --game installation-folder\n";return 1;}
    const auto path=(fs::absolute(fs::path(argv[2]))/L"BFBC2Game.exe").lexically_normal();
    const auto size=fs::file_size(path);if(size<64||size>512ull*1024*1024)throw std::runtime_error("Invalid game image size");
    std::ifstream stream(path,std::ios::binary);std::vector<std::byte> bytes(static_cast<std::size_t>(size));if(!stream.read(reinterpret_cast<char*>(bytes.data()),static_cast<std::streamsize>(size)))throw std::runtime_error("Could not read complete game image");
    const auto pe=fvr::engine::InspectPe(bytes);if(!pe.valid)throw std::runtime_error(pe.error);
    const auto profile=fvr::bc2::DiscoverProfile(bytes,pe.image);
    if(!profile){std::cout<<"{\"schema\":1,\"state\":\"unsupported_discovery_profile\",\"native_capabilities\":0}\n";return 2;}
    const auto pid=FindProcess(path);if(!pid){std::cout<<"{\"schema\":1,\"state\":\"game_not_running\",\"static_profile_found\":true,\"native_capabilities\":0}\n";return 2;}
    Handle process{OpenProcess(PROCESS_QUERY_INFORMATION|PROCESS_VM_READ,FALSE,pid)};if(!process)throw std::runtime_error("Cannot open the game for read-only observation");
    // Recheck process identity after opening to avoid PID reuse during discovery.
    wchar_t openedPath[32768]{};DWORD openedLength=32768;
    if(!QueryFullProcessImageNameW(process.h,0,openedPath,&openedLength)||_wcsicmp(fs::path(openedPath).lexically_normal().c_str(),path.c_str()))throw std::runtime_error("Process identity changed");
    const auto modules=Modules(pid);auto main=std::find_if(modules.begin(),modules.end(),[](const Module& m){return !_wcsicmp(m.name.c_str(),L"BFBC2Game.exe");});
    if(main==modules.end()||main->base>UINT32_MAX||main->size!=profile->imageSize)throw std::runtime_error("Live image layout differs from inspected binary");
    const auto base=main->base;
    const auto sameCode=[&](std::uint32_t rva,std::size_t n){auto live=Read(process.h,base+rva,n);const auto off=Offset(pe.image,rva,n);return std::equal(live.begin(),live.end(),bytes.begin()+off);};
    if(!sameCode(profile->frame,16)||!sameCode(profile->frame+20,12)||!sameCode(profile->dispatch,3)||!sameCode(profile->dispatch+7,28)||!sameCode(profile->present,38)||!sameCode(profile->presentWrapper,51))throw std::runtime_error("Live native code differs from the verified discovery relationships");
    if(Ptr(process.h,base+profile->frame+16)!=base+profile->rendererGlobal||Ptr(process.h,base+profile->dispatch+3)!=base+profile->gameRendererGlobal)throw std::runtime_error("Live singleton references do not match the profile");
    const auto renderer=Ptr(process.h,base+profile->rendererGlobal),game=Ptr(process.h,base+profile->gameRendererGlobal);
    if(!renderer||!game)throw std::runtime_error("Renderer not initialized yet");
    if(Ptr(process.h,renderer)!=base+profile->rendererVtable||Ptr(process.h,base+profile->rendererVtable+20)!=base+profile->present)throw std::runtime_error("Active renderer does not match this D3D11 discovery profile");
    const auto device=Ptr(process.h,renderer+0x7c),swapchain=Ptr(process.h,renderer+0x88);
    const bool d3d11=InModule(modules,Ptr(process.h,device),L"d3d11.dll"),dxgi=InModule(modules,Ptr(process.h,swapchain),L"dxgi.dll");
    if(!d3d11||!dxgi)throw std::runtime_error("Renderer COM object ownership has not been verified");
    const auto dimensions=Read(process.h,renderer+0x18,8);const auto width=U32(dimensions),height=U32(dimensions,4);
    if(!width||!height||width>32768||height>32768)throw std::runtime_error("Invalid live renderer dimensions");
    const auto camera=Read(process.h,game+0x20,0x320);
    fvr::engine::FrostbiteCameraInput input{};std::memcpy(&input.transform,camera.data()+0x50,64);std::memcpy(&input.nearPlane,camera.data()+0x1c,4);std::memcpy(&input.farPlane,camera.data()+0x20,4);input.worldUnitsPerMeter=1; // scale remains uncalibrated: diagnostic only
    const auto canonical=fvr::engine::CanonicalCamera(input);fvr::math::Matrix4 view{},projection{};std::memcpy(&view,camera.data()+0x220,64);std::memcpy(&projection,camera.data()+0x2e0,64);
    float error=0;auto native=input.transform;for(unsigned row=0;row<4;++row)native.values[row][3]=row==3?1.f:0.f;
    auto expected=fvr::interaction::InverseRigid(native);if(expected)for(unsigned row=0;row<4;++row)for(unsigned col=0;col<4;++col){const auto delta=std::abs(expected->values[row][col]-view.values[row][col]);if(!std::isfinite(delta)){error=INFINITY;break;}error=std::max(error,delta);}
    const bool cameraCoherent=canonical&&expected&&error<.02f;
    if(Ptr(process.h,base+profile->rendererGlobal)!=renderer||Ptr(process.h,base+profile->gameRendererGlobal)!=game)throw std::runtime_error("Renderer ownership changed during observation");
    std::string worldViews="{\"state\":\"profile_unavailable\"}";
    const auto renderPath=fvr::bc2::DiscoverRenderPath(bytes,pe.image);const auto viewLayout=fvr::bc2::DiscoverViewLayout(bytes,pe.image);
    if(renderPath&&viewLayout){try {
        if(!sameCode(renderPath->worldRender,40)||!sameCode(renderPath->subsystemDraw,35)||!sameCode(viewLayout->setPrimary,27)||!sameCode(viewLayout->copyRenderView,16)||!sameCode(viewLayout->copyRenderView+0x1c5,3))throw std::runtime_error("Live world/view code changed");
        for(auto slot:{2u,5u,6u,7u,8u,9u,10u,16u,18u}){const auto tableOffset=Offset(pe.image,viewLayout->vtable+slot*4,4);const auto function=U32(bytes,tableOffset)-profile->preferredBase;
            if(Ptr(process.h,base+viewLayout->vtable+slot*4)!=base+function||!sameCode(function,slot==2?4:7))throw std::runtime_error("Live view vtable/getter changed");}
        worldViews=WorldViews(process.h,base,game,*renderPath,*viewLayout);
    }catch(const std::exception&){worldViews="{\"state\":\"ownership_or_snapshot_changed\",\"render_only_verified\":false}";}}
    std::string lifecycleObservation="{\"state\":\"profile_unavailable\"}";
    const auto life=fvr::bc2::DiscoverViewLifecycle(bytes,pe.image);
    if(renderPath&&life){try{lifecycleObservation=WorldLifecycle(process.h,base,game,*renderPath,*life);}catch(const std::exception&){lifecycleObservation="{\"state\":\"ownership_or_snapshot_changed\"}";}}
    std::cout<<"{\n  \"schema\":1,\n  \"state\":\"renderer_observed\",\n  \"pid\":"<<pid<<",\n  \"graphics_api\":\"D3D11\",\n  \"width\":"<<width<<",\n  \"height\":"<<height<<",\n  \"renderer_global_rva\":"<<profile->rendererGlobal<<",\n  \"game_renderer_global_rva\":"<<profile->gameRendererGlobal<<",\n  \"frame_candidate_rva\":"<<profile->frame<<",\n  \"native_present_rva\":"<<profile->present<<",\n  \"dxgi_present_wrapper_rva\":"<<profile->presentWrapper<<",\n  \"camera_pair_coherent\":"<<(cameraCoherent?"true":"false")<<",\n  \"camera_projection_rh\":"<<(std::isfinite(projection.values[2][3])&&projection.values[2][3]==-1?"true":"false")<<",\n  \"world_views\":"<<worldViews<<",\n  \"view_lifecycle\":"<<lifecycleObservation<<",\n  \"world_scale_calibrated\":false,\n  \"render_only_entry_verified\":false,\n  \"native_capabilities\":0,\n  \"game_memory_written\":false,\n  \"headset_tested\":false\n}\n";
    return 0;
}catch(const std::exception& e){std::cerr<<"Read-only BC2 observation failed: "<<e.what()<<'\n';return 1;}}
