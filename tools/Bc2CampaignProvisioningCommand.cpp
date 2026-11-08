#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include "Bc2CampaignProvisioning.h"
#include "Bc2CampaignProvisioningSource.h"
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <string>
#include <limits>
namespace cp=fvr::bc2::campaignProvisioning;
namespace {
std::int64_t Now(){return std::chrono::duration_cast<std::chrono::nanoseconds>(
    std::chrono::steady_clock::now().time_since_epoch()).count();}
template<class T>bool Read(const std::filesystem::path& path,T& out){
    const auto file=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
    if(file==INVALID_HANDLE_VALUE)return false;BY_HANDLE_FILE_INFORMATION info{};DWORD read=0;
    const bool okay=GetFileInformationByHandle(file,&info)&&!(info.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT))&&
        !info.nFileSizeHigh&&info.nFileSizeLow==sizeof(T)&&ReadFile(file,&out,sizeof(T),&read,nullptr)&&read==sizeof(T);
    CloseHandle(file);return okay;
}
std::uint64_t Number(const wchar_t* value){
    std::size_t length=0;const auto out=std::stoull(value,&length,0);
    if(length!=std::wcslen(value)||value[0]==L'-')throw 1;return out;
}
}
// Only report-directory IPC. Never opens BC2, injects a DLL, installs a hook,
// sends gameplay input, changes a launch profile or mounts a game bundle.
int wmain(int argc,wchar_t** argv){
    try{
        if(argc!=6&&argc!=9){std::fputs("usage: command <session-dir> <pid> <job> <nonce> <observe|equip|restore> [variant asset data]\n",stderr);return 2;}
        const std::filesystem::path directory=argv[1];if(!directory.is_absolute())return 2;
        const auto pid=Number(argv[2]);
        cp::Request arguments{};arguments.job=Number(argv[3]);arguments.nonce=Number(argv[4]);
        const std::wstring operation=argv[5];
        if(operation==L"equip"&&argc==9){
            arguments.operation=cp::Operation::Equip;
            const auto variant=Number(argv[6]),asset=Number(argv[7]),data=Number(argv[8]);
            if(variant>UINT32_MAX||asset>UINT32_MAX||data>UINT32_MAX)return 2;
            arguments.variant=unsigned(variant);arguments.targetAsset=unsigned(asset);arguments.targetData=unsigned(data);
        }else if(operation==L"observe"&&argc==6)arguments.operation=cp::Operation::Observe;
        else if(operation==L"restore"&&argc==6)arguments.operation=cp::Operation::Restore;
        else return 2;
        cp::Request request{};
        const auto fresh=cp::command::WaitForFreshSource(pid,request,
            [&](cp::Request& source){return Read(directory/L"campaign-provision-source.bin",source);},
            Now,[](unsigned milliseconds){Sleep(milliseconds);});
        if(fresh!=cp::command::SourceResult::Fresh)return fresh==cp::command::SourceResult::TimedOut?4:3;
        // Start the original two-second arm once, only after source acquisition.
        if(!cp::command::ArmRequest(request,Now()))return 4;
        request.job=arguments.job;request.nonce=arguments.nonce;request.operation=arguments.operation;
        request.variant=arguments.variant;request.targetAsset=arguments.targetAsset;request.targetData=arguments.targetData;
        const auto pending=directory/L"campaign-provision-request.bin.pending",target=directory/L"campaign-provision-request.bin";
        const auto file=CreateFileW(pending.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(file==INVALID_HANDLE_VALUE)return 5;DWORD written=0;
        bool okay=WriteFile(file,&request,sizeof(request),&written,nullptr)&&written==sizeof(request);
        okay=CloseHandle(file)&&okay;if(!okay)return 5;
        const auto attributes=GetFileAttributesW(target.c_str());
        if(attributes!=INVALID_FILE_ATTRIBUTES&&(attributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT)))return 5;
        // A delayed write cannot renew the source lease or the original arm.
        if(!cp::command::CanPublishRequest(request,Now())){DeleteFileW(pending.c_str());return 4;}
        if(!MoveFileExW(pending.c_str(),target.c_str(),MOVEFILE_REPLACE_EXISTING))return 5;
        while(Now()<request.deadlineNs+500000000){
            cp::WireReceipt receipt{};
            if(Read(directory/L"campaign-provision-receipt.bin",receipt)&&receipt.magic==0x31524342&&receipt.version==1&&
               receipt.bytes==sizeof(receipt)&&receipt.process==request.process&&receipt.job==request.job&&receipt.nonce==request.nonce){
                const auto status=cp::Status(receipt.status);
                if(status==cp::Status::Ready||status==cp::Status::Equipped||status==cp::Status::Restored||
                   status==cp::Status::Refused||status==cp::Status::Expired||status==cp::Status::Quarantined||status==cp::Status::Stopped){
                    std::printf("nonce=%llu status=%u native_calls=%u returned_item=0x%x converged=%u excluded=%u\n",
                        static_cast<unsigned long long>(receipt.nonce),receipt.status,receipt.nativeCalls,receipt.returnedItem,
                        receipt.clientServerConverged,receipt.inputExcluded);
                    return status==cp::Status::Ready||status==cp::Status::Equipped||status==cp::Status::Restored?0:6;
                }
            }
            Sleep(25);
        }
        std::fputs("No terminal receipt for this exact nonce; outcome unknown, do not retry assignment.\n",stderr);return 7;
    }catch(...){return 2;}
}
