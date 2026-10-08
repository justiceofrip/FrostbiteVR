#include <Windows.h>
#include <DbgHelp.h>
#include <tlhelp32.h>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <array>
#include <stdexcept>
#include <string>
namespace fs=std::filesystem;
namespace {
struct Handle {HANDLE value=nullptr;~Handle(){if(value&&value!=INVALID_HANDLE_VALUE)CloseHandle(value);}};
void Require(bool value,const char* what){if(!value)throw std::runtime_error(std::string(what)+" Win32="+std::to_string(GetLastError()));}
struct Attachment {DWORD pid;bool attached=false;~Attachment(){if(attached)DebugActiveProcessStop(pid);}};
void Memory(std::ostream& out,HANDLE process,DWORD address,unsigned size){
    std::array<unsigned char,4096> bytes{};SIZE_T count=0;
    ReadProcessMemory(process,reinterpret_cast<void*>(address),bytes.data(),size,&count);
    out<<"{\"address\":"<<address<<",\"hex\":\"";
    const char* hex="0123456789abcdef";for(SIZE_T i=0;i<count;++i)out<<hex[bytes[i]>>4]<<hex[bytes[i]&15];out<<"\"}";
}
void Capture(HANDLE process,DWORD pid,const DEBUG_EVENT& event,const fs::path& folder,unsigned index){
    const auto prefix=folder/("exception-"+std::to_string(index));
    Handle thread{OpenThread(THREAD_GET_CONTEXT|THREAD_QUERY_INFORMATION,FALSE,event.dwThreadId)};
    CONTEXT context{};context.ContextFlags=CONTEXT_FULL;
    const bool gotContext=thread.value&&GetThreadContext(thread.value,&context);
    auto record=event.u.Exception.ExceptionRecord;
    std::ofstream out(prefix.string()+".json");
    out<<"{\"pid\":"<<pid<<",\"thread\":"<<event.dwThreadId<<",\"first_chance\":"<<event.u.Exception.dwFirstChance<<",\"code\":"<<record.ExceptionCode<<",\"address\":"<<reinterpret_cast<DWORD>(record.ExceptionAddress)<<",\"context_valid\":"<<gotContext<<",\"parameters\":[";
    for(DWORD i=0;i<record.NumberParameters&&i<EXCEPTION_MAXIMUM_PARAMETERS;++i){if(i)out<<',';out<<record.ExceptionInformation[i];}
    out<<"],\"registers\":{\"eip\":"<<context.Eip<<",\"esp\":"<<context.Esp<<",\"ebp\":"<<context.Ebp<<",\"eax\":"<<context.Eax<<",\"ebx\":"<<context.Ebx<<",\"ecx\":"<<context.Ecx<<",\"edx\":"<<context.Edx<<",\"esi\":"<<context.Esi<<",\"edi\":"<<context.Edi<<"},\"memory\":[";
    const DWORD pointers[]={context.Esp,context.Ebp,context.Eax,context.Ebx,context.Ecx,context.Edx,context.Esi,context.Edi};
    for(unsigned i=0;i<8;++i){if(i)out<<',';Memory(out,process,pointers[i],i?512:4096);}out<<"],\"modules\":[";
    Handle modules{CreateToolhelp32Snapshot(TH32CS_SNAPMODULE,pid)};MODULEENTRY32W module{};module.dwSize=sizeof(module);bool first=true;
    if(Module32FirstW(modules.value,&module))do{if(!first)out<<',';first=false;out<<"{\"name\":"<<std::quoted(fs::path(module.szModule).string())<<",\"base\":"<<reinterpret_cast<DWORD>(module.modBaseAddr)<<",\"size\":"<<module.modBaseSize<<'}';}while(Module32NextW(modules.value,&module));
    EXCEPTION_POINTERS pointersForDump{&record,&context};MINIDUMP_EXCEPTION_INFORMATION info{event.dwThreadId,&pointersForDump,FALSE};
    const fs::path dump=prefix.string()+".dmp";Handle file{CreateFileW(dump.c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr)};
    const bool dumped=gotContext&&file.value!=INVALID_HANDLE_VALUE&&MiniDumpWriteDump(process,pid,file.value,static_cast<MINIDUMP_TYPE>(MiniDumpNormal|MiniDumpWithIndirectlyReferencedMemory),&info,nullptr,nullptr);
    out<<"],\"minidump_written\":"<<dumped<<"}\n";out.flush();
}
unsigned Watch(DWORD pid,unsigned seconds,const fs::path& folder){
    Require(seconds>=1&&seconds<=60,"Watch duration must be 1..60 seconds");fs::create_directories(folder);
    Handle process{OpenProcess(PROCESS_QUERY_INFORMATION|PROCESS_VM_READ|SYNCHRONIZE,FALSE,pid)};Require(process.value!=nullptr,"Open process");
    Attachment attachment{pid};Require(DebugActiveProcess(pid)!=0,"Attach debugger");attachment.attached=true;
    Require(DebugSetProcessKillOnExit(FALSE)!=0,"Preserve game on debugger exit");
    bool firstBreakpoint=true;unsigned captured=0;const auto deadline=GetTickCount64()+seconds*1000ull;
    while(GetTickCount64()<deadline){
        DEBUG_EVENT event{};if(!WaitForDebugEventEx(&event,100)){Require(GetLastError()==ERROR_SEM_TIMEOUT,"Wait for debug event");continue;}
        DWORD status=DBG_CONTINUE;bool exited=false;
        if(event.dwDebugEventCode==EXCEPTION_DEBUG_EVENT){
            const auto code=event.u.Exception.ExceptionRecord.ExceptionCode;status=DBG_EXCEPTION_NOT_HANDLED;
            if(firstBreakpoint&&code==EXCEPTION_BREAKPOINT){firstBreakpoint=false;status=DBG_CONTINUE;std::ofstream(folder/"ready.json")<<"{\"pid\":"<<pid<<",\"attached\":true}\n";}
            else if(captured<8&&(code==EXCEPTION_ACCESS_VIOLATION||code==EXCEPTION_ILLEGAL_INSTRUCTION||code==EXCEPTION_INT_DIVIDE_BY_ZERO||code==EXCEPTION_STACK_OVERFLOW||code==EXCEPTION_BREAKPOINT||!event.u.Exception.dwFirstChance)){
                // Continue the original exception even if disk capture fails.
                try{Capture(process.value,pid,event,folder,captured++);}catch(const std::exception& error){std::ofstream(folder/"capture-error.txt")<<error.what();}
            }
        }else if(event.dwDebugEventCode==CREATE_PROCESS_DEBUG_EVENT){if(event.u.CreateProcessInfo.hFile)CloseHandle(event.u.CreateProcessInfo.hFile);}
        else if(event.dwDebugEventCode==LOAD_DLL_DEBUG_EVENT){if(event.u.LoadDll.hFile)CloseHandle(event.u.LoadDll.hFile);}
        else if(event.dwDebugEventCode==EXIT_PROCESS_DEBUG_EVENT){exited=true;std::ofstream(folder/"exit.json")<<"{\"exit_code\":"<<event.u.ExitProcess.dwExitCode<<"}\n";}
        Require(ContinueDebugEvent(event.dwProcessId,event.dwThreadId,status)!=0,"Continue debug event");
        if(exited){attachment.attached=false;break;}
    }
    if(attachment.attached){Require(DebugActiveProcessStop(pid)!=0,"Detach debugger");attachment.attached=false;}
    std::ofstream(folder/"completion.json")<<"{\"exceptions\":"<<captured<<",\"detached\":true,\"game_running\":"<<(WaitForSingleObject(process.value,0)==WAIT_TIMEOUT)<<"}\n";
    return captured;
}
int Fixture(){Sleep(800);__try{RaiseException(EXCEPTION_ACCESS_VIOLATION,0,0,nullptr);}__except(EXCEPTION_EXECUTE_HANDLER){}Sleep(1600);return 0;}
}
int wmain(int argc,wchar_t** argv){try{
    static_assert(sizeof(void*)==4);
    if(argc==2&&std::wstring(argv[1])==L"--fixture")return Fixture();
    if(argc==3&&std::wstring(argv[1])==L"--self-test"){
        wchar_t executable[32768]{};Require(GetModuleFileNameW(nullptr,executable,32768)!=0,"Self path");std::wstring command=L"\""+std::wstring(executable)+L"\" --fixture";
        STARTUPINFOW startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION child{};
        Require(CreateProcessW(executable,command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&child)!=0,"Start fixture");Handle process{child.hProcess},thread{child.hThread};
        const auto count=Watch(child.dwProcessId,1,fs::absolute(argv[2]));Require(count==1,"Exactly one handled first-chance exception expected");
        Require(WaitForSingleObject(process.value,5000)==WAIT_OBJECT_0,"Fixture continues after detach");DWORD code=1;Require(GetExitCodeProcess(process.value,&code)&&code==0,"Original SEH handler survives capture");std::cout<<"exception capture and live detach passed\n";return 0;
    }
    Require(argc==7&&std::wstring(argv[1])==L"--pid"&&std::wstring(argv[3])==L"--seconds"&&std::wstring(argv[5])==L"--output","Usage: BC2ExceptionWatch --pid PID --seconds 1..60 --output new-folder");
    const auto pid=std::stoul(argv[2]);Handle process{OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid)};wchar_t name[32768]{};DWORD size=32768;
    Require(process.value&&QueryFullProcessImageNameW(process.value,0,name,&size)&&!_wcsicmp(fs::path(name).filename().c_str(),L"BFBC2Game.exe"),"Expected BC2 game process");
    const auto folder=fs::absolute(argv[6]);Require(!fs::exists(folder),"New report folder required");Watch(pid,std::stoul(argv[4]),folder);return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
