#pragma once
#include <Windows.h>
#include <cwchar>
namespace fvr::platform {
// A held process handle observes the original process even if its PID is reused.
class ProcessLifetime {
public:
    ~ProcessLifetime(){if(handle_)CloseHandle(handle_);}
    ProcessLifetime()=default;ProcessLifetime(const ProcessLifetime&)=delete;
    ProcessLifetime& operator=(const ProcessLifetime&)=delete;
    bool Open(DWORD pid,const wchar_t* expectedName)noexcept {
        if(handle_)return false;
        HANDLE next=OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid);
        if(!next)return false;
        wchar_t path[32768]{};DWORD length=32768;
        if(!QueryFullProcessImageNameW(next,0,path,&length)){CloseHandle(next);return false;}
        const auto slash=std::wcsrchr(path,L'\\');const auto name=slash?slash+1:path;
        if(_wcsicmp(name,expectedName)||WaitForSingleObject(next,0)!=WAIT_TIMEOUT){CloseHandle(next);return false;}
        handle_=next;return true;
    }
    bool Ended()const noexcept{return !handle_||WaitForSingleObject(handle_,0)!=WAIT_TIMEOUT;}
private:HANDLE handle_=nullptr;
};
}
