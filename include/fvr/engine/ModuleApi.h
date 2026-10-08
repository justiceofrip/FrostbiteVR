#pragma once
#include <stdint.h>
#ifdef _WIN32
#define FVR_CALL __cdecl
#else
#define FVR_CALL
#endif
// Fixed-width C ABI. No STL, exceptions, native engine pointers or allocator
// ownership cross the DLL boundary. Each architecture loads matching modules.
#define FVR_MODULE_ABI 1u
#define FVR_CAP_STEREO (UINT64_C(1)<<0)
#define FVR_CAP_INPUT (UINT64_C(1)<<1)
#define FVR_CAP_WEAPON_AIM (UINT64_C(1)<<2)
#define FVR_CAP_HANDS (UINT64_C(1)<<3)
#define FVR_CAP_UI (UINT64_C(1)<<4)
#define FVR_CAP_VEHICLES (UINT64_C(1)<<5)
#ifdef __cplusplus
extern "C" {
#endif
typedef struct FvrModuleInfo {
    uint32_t size,abi,pointerBits,reserved;
    uint64_t capabilities;
    char id[64],engine[64],game[64],status[256];
} FvrModuleInfo;
typedef int32_t (FVR_CALL *FvrGetModuleInfoFn)(uint32_t abi,uint32_t outputSize,FvrModuleInfo* output);
#ifdef __cplusplus
}
#endif