#include "fvr/engine/ModuleApi.h"
#include "fvr/engine/BindingValidation.h"
#include <cstring>
extern "C" __declspec(dllexport) int32_t FVR_CALL FvrGetModuleInfo(uint32_t abi,uint32_t outputSize,FvrModuleInfo* output){
    if(abi!=FVR_MODULE_ABI||!output||outputSize!=sizeof(FvrModuleInfo))return 0;
    FvrModuleInfo info{};info.size=sizeof(info);info.abi=FVR_MODULE_ABI;info.pointerBits=sizeof(void*)*8;
    // Empty evidence deliberately advertises zero native capabilities. There
    // are no Refractor offsets or speculative Frostbite memory writes here.
    info.capabilities=fvr::engine::FrostbiteEvidence{}.Capabilities();
    strcpy_s(info.id,"frostbite.bc2");strcpy_s(info.engine,"Frostbite / BC2 adapter");
    strcpy_s(info.game,"BFBC2Game.exe");
    strcpy_s(info.status,"Signature-based BC2 discovery available. Native stereo, input and skeleton hooks remain unverified and disabled.");
    *output=info;return 1;
}