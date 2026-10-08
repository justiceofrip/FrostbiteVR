#include <Windows.h>
#include <tlhelp32.h>
#include "NativeProbeConfig.h"
#include "Bc2MagazineReloadSession.h"
#include "Bc2OpticFilterSession.h"
#include "Bc2BodyInventorySession.h"
#include "Bc2WeaponVisibilityProbeSession.h"
#include "Bc2BodyHolsterProbeSession.h"
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
namespace fs=std::filesystem;
namespace {
void Require(bool okay,const char* message){if(!okay)throw std::runtime_error(std::string(message)+" (Win32="+std::to_string(GetLastError())+")");}
struct Handle {HANDLE h=nullptr;~Handle(){if(h&&h!=INVALID_HANDLE_VALUE)CloseHandle(h);}};
struct Allocation {HANDLE process;void* pointer=nullptr;bool release=true;~Allocation(){if(pointer&&release)VirtualFreeEx(process,pointer,0,MEM_RELEASE);}Allocation(HANDLE p,std::size_t size):process(p){pointer=VirtualAllocEx(p,nullptr,size,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);Require(pointer!=nullptr,"Allocate probe request");}};
void Write(HANDLE process,void* at,const void* data,std::size_t size){SIZE_T written=0;Require(WriteProcessMemory(process,at,data,size,&written)&&written==size,"Write bootstrap request");}
std::wstring ProcessPath(HANDLE process){wchar_t path[32768]{};DWORD count=32768;Require(QueryFullProcessImageNameW(process,0,path,&count)!=0,"Query target path");return fs::path(path).lexically_normal().wstring();}
DWORD FindProcess(const fs::path& path){Handle snapshot{CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0)};Require(snapshot.h!=INVALID_HANDLE_VALUE,"Enumerate processes");PROCESSENTRY32W item{};item.dwSize=sizeof(item);DWORD found=0;
    if(Process32FirstW(snapshot.h,&item))do{if(_wcsicmp(item.szExeFile,L"BFBC2Game.exe"))continue;Handle process{OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,item.th32ProcessID)};
        if(process.h&&!_wcsicmp(ProcessPath(process.h).c_str(),path.c_str())){Require(!found,"More than one matching game process");found=item.th32ProcessID;}}
    while(Process32NextW(snapshot.h,&item));Require(found!=0,"BC2 must already be running");return found;
}
std::uintptr_t RemoteFunction(DWORD pid,FARPROC function){
    HMODULE owner=nullptr;Require(GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(function),&owner)!=0,"Resolve bootstrap code owner");
    wchar_t path[32768]{};Require(GetModuleFileNameW(owner,path,32768)>0,"Bootstrap module path");const auto rva=reinterpret_cast<std::uintptr_t>(function)-reinterpret_cast<std::uintptr_t>(owner);
    Handle snapshot{CreateToolhelp32Snapshot(TH32CS_SNAPMODULE,pid)};Require(snapshot.h!=INVALID_HANDLE_VALUE,"Enumerate target modules");MODULEENTRY32W item{};item.dwSize=sizeof(item);
    if(Module32FirstW(snapshot.h,&item))do{if(!_wcsicmp(item.szExePath,path)&&rva<item.modBaseSize)return reinterpret_cast<std::uintptr_t>(item.modBaseAddr)+rva;}while(Module32NextW(snapshot.h,&item));
    throw std::runtime_error("Matching 32-bit bootstrap module not found");
}
DWORD Run(HANDLE process,std::uintptr_t function,Allocation& memory,DWORD timeout){
    Handle thread{CreateRemoteThread(process,nullptr,0,reinterpret_cast<LPTHREAD_START_ROUTINE>(function),memory.pointer,0,nullptr)};Require(thread.h!=nullptr,"Start bounded probe entry");
    // If the remote operation times out, its argument must remain valid until
    // the game exits. Never terminate a game thread or free memory under it.
    memory.release=false;Require(WaitForSingleObject(thread.h,timeout)==WAIT_OBJECT_0,"Probe operation did not finish within its budget");memory.release=true;
    if(WaitForSingleObject(process,0)==WAIT_OBJECT_0)return 0; // User closed the game.
    DWORD result=0;Require(GetExitCodeThread(thread.h,&result)!=0,"Read probe result");return result;
}
}
int wmain(int argc,wchar_t** argv){try {
    static_assert(sizeof(void*)==4);Require(argc>=7&&std::wstring(argv[1])==L"--game"&&std::wstring(argv[3])==L"--dll"&&std::wstring(argv[5])==L"--report","Usage: BC2NativeTrace --game folder --dll dll --report json [diagnostic mode | --stream CHANNEL] [--seconds N] [--optic-filter-observe] [--body-inventory]");
    bool emptyFireProbe=false,pumpHoldProbe=false;bool passEvidence=false,uncapMirror=false,controllers=false,motionAim=false,bodyFollow=false,poseObserve=false,rigPulse=false,handPoses=false,deathProbe=false,equipProbe=false,muzzleFire=false,twoHandGrip=false,sightFlip=false,reloadHoldProbe=false,reloadRoundProbe=false,reloadRequestProbe=false,physicalReload=false,physicalReloadProbe=false,opticFilterObserve=false,bodyInventory=false,physicalReloadRepeatProbe=false,weaponVisibilityProbe=false,bodyHolsterProbe=false;unsigned flags=0,duration=3000,hostPid=0,magazineSession=0;bool boatHeadAim=false,boatHeadFire=false;auto bodyDiagnostic=fvr::bc2::BodyHolsterDiagnosticProfile::Disabled;std::wstring channel;
    for(int i=7;i<argc;++i){const std::wstring option=argv[i];
        if(option==L"--until-host-exit"&&i+1<argc){hostPid=std::stoul(argv[++i]);Require(hostPid!=0,"Host PID required");continue;}
        if(option==L"--seconds"&&i+1<argc){duration=std::stoul(argv[++i])*1000;Require(duration>=1000&&duration<=60000,"Duration must be1..60seconds");continue;}
        if(option==L"--optic-filter-observe"){controllers=motionAim=bodyFollow=poseObserve=handPoses=opticFilterObserve=true;continue;}
        if(option==L"--weapon-visibility-probe"){controllers=motionAim=bodyFollow=poseObserve=handPoses=muzzleFire=twoHandGrip=weaponVisibilityProbe=true;continue;}
        if(option==L"--body-holster-configured-probe"||option==L"--body-holster-configured-fire-probe"){Require(!bodyHolsterProbe,"Select one holster diagnostic profile");controllers=motionAim=bodyFollow=poseObserve=handPoses=muzzleFire=twoHandGrip=physicalReload=bodyInventory=bodyHolsterProbe=true;bodyDiagnostic=option==L"--body-holster-configured-fire-probe"?fvr::bc2::BodyHolsterDiagnosticProfile::ExactConfiguredTableFire:fvr::bc2::BodyHolsterDiagnosticProfile::ExactConfiguredTable;continue;}
        if(option==L"--body-holster-probe"||option==L"--body-holster-xm8-probe"||option==L"--body-holster-xm8-fire-probe"){Require(!bodyHolsterProbe,"Select one holster diagnostic profile");controllers=motionAim=bodyFollow=poseObserve=handPoses=muzzleFire=twoHandGrip=physicalReload=bodyInventory=bodyHolsterProbe=true;bodyDiagnostic=option==L"--body-holster-xm8-fire-probe"?fvr::bc2::BodyHolsterDiagnosticProfile::ScopedXm8Fire:option==L"--body-holster-xm8-probe"?fvr::bc2::BodyHolsterDiagnosticProfile::ScopedXm8:fvr::bc2::BodyHolsterDiagnosticProfile::Spas;continue;}
        if(option==L"--empty-fire-probe"){Require(!emptyFireProbe&&!pumpHoldProbe&&!magazineSession,"Select one empty-fire diagnostic");emptyFireProbe=true;magazineSession=3;controllers=motionAim=bodyFollow=poseObserve=handPoses=muzzleFire=twoHandGrip=physicalReload=true;continue;}
        if(option==L"--body-inventory"){controllers=motionAim=bodyFollow=poseObserve=handPoses=muzzleFire=twoHandGrip=physicalReload=bodyInventory=true;continue;}
        if(option==L"--inventory-reload-probe"){Require(!magazineSession,"Select one magazine mode");magazineSession=7;controllers=motionAim=bodyFollow=poseObserve=handPoses=muzzleFire=twoHandGrip=physicalReload=bodyInventory=true;continue;}
        if(option==L"--magazine-reload"||option==L"--magazine-physical-probe"||option==L"--magazine-original-return-probe"||option==L"--magazine-full-return-probe"){
            Require(!magazineSession,"Select one magazine mode");magazineSession=option==L"--magazine-reload"?3u:option==L"--magazine-physical-probe"?4u:option==L"--magazine-original-return-probe"?5u:6u;
            controllers=motionAim=bodyFollow=poseObserve=handPoses=muzzleFire=twoHandGrip=physicalReload=true;continue;}
        if(option==L"--xm8-magazine-insert-probe"||option==L"--xm8-magazine-cancel-probe"){Require(!magazineSession,"Select one magazine fixture");magazineSession=option==L"--xm8-magazine-insert-probe"?1u:2u;controllers=motionAim=bodyFollow=poseObserve=handPoses=muzzleFire=twoHandGrip=true;continue;}
        if(option==L"--boat-head-aim"){controllers=motionAim=bodyFollow=boatHeadAim=true;continue;}
        if(option==L"--boat-head-fire"){boatHeadFire=true;continue;}
        if(option==L"--body-follow"){controllers=motionAim=bodyFollow=true;continue;}
        if(option==L"--two-hand-grip"){controllers=motionAim=bodyFollow=poseObserve=handPoses=muzzleFire=twoHandGrip=true;continue;}
        if(option==L"--sight-flip"){controllers=motionAim=bodyFollow=poseObserve=handPoses=muzzleFire=twoHandGrip=sightFlip=true;continue;}
        if((option==L"--muzzle-fire-probe"||option==L"--muzzle-fire")){controllers=motionAim=bodyFollow=poseObserve=handPoses=muzzleFire=true;continue;}
        if(option==L"--reload-hold-probe"){controllers=motionAim=bodyFollow=poseObserve=handPoses=reloadHoldProbe=true;continue;}
        if(option==L"--pump-hold-probe"){pumpHoldProbe=controllers=motionAim=bodyFollow=poseObserve=handPoses=muzzleFire=twoHandGrip=reloadHoldProbe=true;continue;}
        if(option==L"--physical-reload-repeat-probe"){controllers=motionAim=bodyFollow=poseObserve=handPoses=muzzleFire=twoHandGrip=physicalReload=physicalReloadProbe=physicalReloadRepeatProbe=true;continue;}
        if(option==L"--physical-reload-probe"){controllers=motionAim=bodyFollow=poseObserve=handPoses=muzzleFire=twoHandGrip=physicalReload=physicalReloadProbe=true;continue;}
        if(option==L"--physical-reload"){controllers=motionAim=bodyFollow=poseObserve=handPoses=muzzleFire=twoHandGrip=physicalReload=true;continue;}
        if(option==L"--reload-request-probe"){controllers=motionAim=bodyFollow=poseObserve=handPoses=muzzleFire=twoHandGrip=reloadRequestProbe=true;continue;}
        if(option==L"--reload-round-probe"){controllers=motionAim=bodyFollow=poseObserve=handPoses=reloadRoundProbe=true;continue;}
        if(option==L"--equip-probe"){controllers=motionAim=equipProbe=true;continue;}
        if(option==L"--death-probe"){controllers=motionAim=deathProbe=true;continue;}
        if(option==L"--hand-poses"){controllers=motionAim=bodyFollow=poseObserve=handPoses=true;continue;}
        if(option==L"--rig-pulse"){controllers=motionAim=poseObserve=rigPulse=true;continue;}
        if(option==L"--pose-observe"){controllers=motionAim=poseObserve=true;continue;}
        if(option==L"--motion-aim"){controllers=motionAim=true;continue;}
        if(option==L"--controllers"){controllers=true;continue;}
        if(option==L"--uncap-mirror"){uncapMirror=true;continue;}
        if(option==L"--pass-evidence"){passEvidence=true;continue;}
        Require(!flags,"Select one mode");
        if(option==L"--camera-pulse")flags=1;else if(option==L"--visibility-pulse")flags=2;else if(option==L"--ownership-v2")flags=4;
        else if(option==L"--stereo-once")flags=5;else if(option==L"--initialize-view")flags=6;else if(option==L"--work-pool")flags=7;else if(option==L"--stereo-burst")flags=8;
        else if(option==L"--stream"&&i+1<argc){flags=9;channel=argv[++i];}else throw std::runtime_error("Unknown diagnostic option");
    }
    Require(!boatHeadFire||boatHeadAim,"Boat head fire requires explicit --boat-head-aim");
    const auto boatMode=boatHeadFire?fvr::bc2::BoatHeadAimMode::AimAndFire:
        (boatHeadAim?fvr::bc2::BoatHeadAimMode::AimOnly:fvr::bc2::BoatHeadAimMode::Disabled);
    Require(!(rigPulse&&handPoses),"Rig pulse and controller hands are separate modes");
    Require(!physicalReloadProbe||(!hostPid&&duration==30000&&!sightFlip&&flags==9),"Physical consumer fixture requires isolated30s stream");
    Require(!physicalReload||(!reloadHoldProbe&&!reloadRoundProbe&&!reloadRequestProbe&&!rigPulse&&!equipProbe&&!deathProbe&&flags==9),"Physical reload requires a stream without native diagnostic actions");
    Require(unsigned(reloadHoldProbe)+unsigned(reloadRoundProbe)+unsigned(reloadRequestProbe)<=1,"Reload hold and round probes are mutually exclusive");
    Require(!(reloadHoldProbe||reloadRoundProbe)||(!hostPid&&duration<=15000&&!rigPulse&&!equipProbe&&!deathProbe&&!sightFlip),"Reload hold probe requires a bounded <=15s run without other diagnostic actions");
    Require(!reloadRequestProbe||(!hostPid&&duration==30000&&!rigPulse&&!equipProbe&&!deathProbe&&!sightFlip&&flags==9),"Request probe requires isolated30s stream");
    Require(!equipProbe||!hostPid,"Equip probe is bounded only");
    Require(!deathProbe||!hostPid,"Death probe is bounded only");
    Require(!rigPulse||!hostPid,"Rig pulse is bounded only");
    Require(!(passEvidence||uncapMirror||hostPid||controllers)||flags==9,"Pass evidence and desktop pacing require stream mode");
    const auto configFlags=flags|(passEvidence?0x100u:0u)|(uncapMirror?0x200u:0u)|(hostPid?0x400u:0u)|(controllers?0x800u:0u)|(motionAim?0x1000u:0u)|(bodyFollow?0x2000u:0u)|(poseObserve?0x4000u:0u)|(rigPulse?0x8000u:0u)|(handPoses?0x10000u:0u)|(deathProbe?0x20000u:0u)|(equipProbe?0x40000u:0u)|(muzzleFire?0x80000u:0u)|(twoHandGrip?0x100000u:0u)|(sightFlip?0x200000u:0u)|(reloadHoldProbe?0x400000u:0u)|(reloadRoundProbe?0x800000u:0u)|(reloadRequestProbe?0x1000000u:0u)|(physicalReload?0x2000000u:0u)|(physicalReloadProbe?0x4000000u:0u)|(opticFilterObserve?0x8000000u:0u)|(bodyInventory?0x10000000u:0u)|(physicalReloadRepeatProbe?0x20000000u:0u)|(weaponVisibilityProbe?fvr::bc2::WeaponVisibilityProbeFlag:0u)|(bodyHolsterProbe?fvr::bc2::BodyHolsterProbeFlag:0u);
    Require(fvr::bc2::ValidBoatHeadAimConfig(boatMode,configFlags,magazineSession),"Boat head aim requires a normal tracked stream without synthetic diagnostics");
    Require(fvr::bc2::ValidMagazineReloadSession(magazineSession,configFlags,duration),"Magazine reload requires a tracked physical stream or an isolated30s fixture");
    Require(fvr::bc2::ValidPhysicalReloadRepeatProbeConfig(configFlags,duration),"Two-shell fixture requires isolated30s physical consumer stream");
    Require(fvr::bc2::ValidBodyHolsterDiagnosticConfig(bodyDiagnostic,configFlags),"Invalid holster diagnostic profile");
    Require(fvr::bc2::ValidBodyHolsterProbeConfig(configFlags,duration),"Body holster probe requires isolated15s tracked physical/body stream");
    Require(fvr::bc2::ValidWeaponVisibilityProbeConfig(configFlags,duration),"Visibility probe requires isolated15s tracked stream");
    Require(fvr::bc2::ValidBodyInventoryConfig(configFlags),"Body inventory requires a tracked physical session without synthetic action/reload/optic diagnostics");
    Require(fvr::bc2::ValidOpticFilterProbeConfig(configFlags,duration),"Optic filter observation requires isolated <=15s stream without continuous/physical/reload/action diagnostics");
    Require(!emptyFireProbe||(!pumpHoldProbe&&magazineSession==3&&!hostPid),"Empty-fire diagnostic cannot combine manual fixture or continuous host");
    Require(fvr::bc2::ValidPumpHoldDiagnostic(emptyFireProbe?fvr::bc2::PumpHoldDiagnostic::SelectedManualEmptyFire:pumpHoldProbe?fvr::bc2::PumpHoldDiagnostic::SpasOneShot:fvr::bc2::PumpHoldDiagnostic::Disabled,configFlags,duration),"Pump diagnostic requires isolated15s hands/fire/support tracked stream");
    const auto game=(fs::absolute(argv[2])/L"BFBC2Game.exe").lexically_normal(),dll=fs::absolute(argv[4]).lexically_normal(),report=fs::absolute(argv[6]).lexically_normal();
    Require(fs::is_regular_file(dll)&&!fs::exists(report),"Probe DLL missing or report already exists");DWORD type=0;Require(GetBinaryTypeW(game.c_str(),&type)&&type==SCS_32BIT_BINARY,"Expected the installed 32-bit game");
    const auto pid=FindProcess(game);Handle process{OpenProcess(PROCESS_QUERY_INFORMATION|PROCESS_VM_READ|PROCESS_VM_WRITE|PROCESS_VM_OPERATION|PROCESS_CREATE_THREAD|SYNCHRONIZE,FALSE,pid)};Require(process.h!=nullptr,"Open authorized game process");Require(!_wcsicmp(ProcessPath(process.h).c_str(),game.c_str()),"Target process identity changed");
    const auto local=LoadLibraryExW(dll.c_str(),nullptr,DONT_RESOLVE_DLL_REFERENCES);Require(local!=nullptr,"Inspect probe DLL exports");const auto entry=GetProcAddress(local,"FvrRunNativeProbe");
    const auto entryRva=entry?reinterpret_cast<std::uintptr_t>(entry)-reinterpret_cast<std::uintptr_t>(local):0;FreeLibrary(local);Require(entryRva!=0,"Probe export missing");
    const auto loader=GetProcAddress(GetModuleHandleW(L"kernel32.dll"),"LoadLibraryW");Require(loader!=nullptr,"LoadLibrary entry unavailable");const auto remoteLoader=RemoteFunction(pid,loader);
    const auto dllText=dll.wstring();Allocation pathMemory(process.h,(dllText.size()+1)*sizeof(wchar_t));Write(process.h,pathMemory.pointer,dllText.c_str(),(dllText.size()+1)*sizeof(wchar_t));
    const auto module=Run(process.h,remoteLoader,pathMemory,8000);Require(module!=0,"Game did not load the probe module");
    fvr::bc2::NativeProbeConfig config{};config.bodyHolsterDiagnostic=bodyDiagnostic;config.magazineReloadSession=magazineSession;config.boatHeadAim=boatMode;config.flags=configFlags;config.pumpHoldDiagnostic=emptyFireProbe?fvr::bc2::PumpHoldDiagnostic::SelectedManualEmptyFire:pumpHoldProbe?fvr::bc2::PumpHoldDiagnostic::SpasOneShot:fvr::bc2::PumpHoldDiagnostic::Disabled;if(hostPid)config.hostPid=hostPid;else config.durationMs=duration;Require(channel.size()<std::size(config.frameChannel),"Channel token too long");std::copy(channel.begin(),channel.end(),config.frameChannel);const auto output=report.wstring();Require(output.size()<std::size(config.reportPath),"Report path too long");std::copy(output.begin(),output.end(),config.reportPath);
    Allocation request(process.h,sizeof(config));Write(process.h,request.pointer,&config,sizeof(config));const auto result=Run(process.h,std::uintptr_t(module)+entryRva,request,hostPid?INFINITE:config.durationMs+8000);
    std::cout<<"{\"pid\":"<<pid<<",\"probe_exit\":"<<result<<",\"module_retained_until_game_exit\":true,\"game_files_modified\":false}\n";return result?2:0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
