#pragma once
#include "fvr/engine/ModuleApi.h"
// Optional BC2-specific discovery extension to the engine-neutral metadata ABI.
// Returned addresses are image-relative evidence, never writable native pointers.
#ifdef __cplusplus
extern "C" {
#endif
typedef struct FvrBc2Profile {
    uint32_t size,abi,preferredBase,imageSize;
    uint32_t rendererGlobal,gameRendererGlobal,frame,dispatch,present,presentWrapper,rendererVtable;
    uint32_t reserved;
} FvrBc2Profile;
typedef int32_t (FVR_CALL *FvrDiscoverBc2ProfileFn)(uint32_t abi,const uint8_t* image,uint64_t byteCount,uint32_t outputSize,FvrBc2Profile* output);
#ifdef __cplusplus
}
#endif