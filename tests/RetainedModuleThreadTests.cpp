#include "fvr/platform/windows/RetainedModuleThread.h"
#include <Windows.h>
#include <atomic>
#include <iostream>
#include <string>
#include <type_traits>

using fvr::platform::RetainedModuleThread;
static_assert(std::is_trivially_destructible_v<RetainedModuleThread>);
static_assert(sizeof(RetainedModuleThread)==sizeof(std::thread));
static RetainedModuleThread pumpWorker,shellWorker;
static std::atomic<unsigned> entered=0,completed=0;
static std::atomic<bool> quit=false;
#define CHECK(x) do{if(!(x)){std::cerr<<__LINE__<<": " #x "\n";return 1;}}while(false)

static bool WaitFor(const std::atomic<unsigned>& value,unsigned expected){
    const auto until=GetTickCount64()+3000;
    while(value.load()!=expected&&GetTickCount64()<until)Sleep(1);
    return value.load()==expected;
}
static void Work(){
    ++entered;
    while(!quit.load())Sleep(1);
    ++completed;
}
static bool ChildExit(const wchar_t* self,const wchar_t* mode){
    std::wstring command=L"\""+std::wstring(self)+L"\" "+mode;
    STARTUPINFOW startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION child{};
    if(!CreateProcessW(self,command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&child))return false;
    const auto wait=WaitForSingleObject(child.hProcess,5000);
    DWORD code=1;const bool okay=wait==WAIT_OBJECT_0&&GetExitCodeProcess(child.hProcess,&code)&&code==0;
    if(wait!=WAIT_OBJECT_0){TerminateProcess(child.hProcess,99);WaitForSingleObject(child.hProcess,1000);}
    CloseHandle(child.hThread);CloseHandle(child.hProcess);return okay;
}
int wmain(int argc,wchar_t** argv){
    if(argc==2){
        const std::wstring mode=argv[1];
        CHECK(mode==L"--pending-exit"||mode==L"--completed-unjoined-exit");
        quit=mode==L"--completed-unjoined-exit";
        pumpWorker.Start(Work);shellWorker.Start(Work);
        CHECK(WaitFor(entered,2));
        if(quit.load())CHECK(WaitFor(completed,2));
        CHECK(pumpWorker.joinable()&&shellWorker.joinable());
        // Return through ordinary CRT exit with both workers still joinable.
        // No join/detach, quick_exit, _Exit, or custom terminate handler.
        return 0;
    }
    CHECK(argc==1);
    wchar_t self[32768]{};CHECK(GetModuleFileNameW(nullptr,self,32768));
    CHECK(ChildExit(self,L"--pending-exit"));
    CHECK(ChildExit(self,L"--completed-unjoined-exit"));
    // Normal Stop/join and later Install must retain their original semantics.
    for(unsigned cycle=0;cycle<2;++cycle){
        entered=0;completed=0;quit=false;
        CHECK(!pumpWorker.joinable()&&!shellWorker.joinable());
        pumpWorker.Start(Work);shellWorker.Start(Work);CHECK(WaitFor(entered,2));
        quit=true;pumpWorker.join();shellWorker.join();
        CHECK(completed.load()==2&&!pumpWorker.joinable()&&!shellWorker.joinable());
    }
    std::cout<<"Retained module workers: pending exit, completed unjoined exit, normal stop/join/restart passed\n";
    return 0;
}
