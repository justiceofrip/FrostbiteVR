#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "Test.h"
#include "fvr/engine/ModuleApi.h"
#include "Bc2ModuleApi.h"
#include <array>
int wmain(int argc,wchar_t** argv){
    CHECK(argc==2);const auto module=LoadLibraryExW(argv[1],nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32);CHECK(module);
    const auto entry=reinterpret_cast<FvrGetModuleInfoFn>(GetProcAddress(module,"FvrGetModuleInfo"));CHECK(entry);FvrModuleInfo info{};
    CHECK(!entry(FVR_MODULE_ABI+1,sizeof(info),&info));CHECK(!entry(FVR_MODULE_ABI,sizeof(info)-1,&info));CHECK(!entry(FVR_MODULE_ABI,sizeof(info),nullptr));
    CHECK(entry(FVR_MODULE_ABI,sizeof(info),&info));CHECK(info.abi==FVR_MODULE_ABI&&info.size==sizeof(info));CHECK(info.pointerBits==sizeof(void*)*8);CHECK(!info.capabilities);
    const auto discover=reinterpret_cast<FvrDiscoverBc2ProfileFn>(GetProcAddress(module,"FvrDiscoverBc2Profile"));CHECK(discover);
    FvrBc2Profile profile{};std::array<uint8_t,64> invalid{};
    CHECK(!discover(FVR_MODULE_ABI,nullptr,64,sizeof(profile),&profile));
    CHECK(!discover(FVR_MODULE_ABI,invalid.data(),invalid.size(),sizeof(profile),&profile));
    CHECK(!discover(FVR_MODULE_ABI+1,invalid.data(),invalid.size(),sizeof(profile),&profile));
    CHECK(!discover(FVR_MODULE_ABI,invalid.data(),UINT64_MAX,sizeof(profile),&profile));
    CHECK(FreeLibrary(module));return 0;
}