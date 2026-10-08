#include "Bc2ModuleApi.h"
#include "Bc2Profile.h"
#include <cstddef>
#include <span>
extern "C" __declspec(dllexport) int32_t FVR_CALL FvrDiscoverBc2Profile(uint32_t abi,const uint8_t* image,uint64_t byteCount,uint32_t outputSize,FvrBc2Profile* output){
    if(abi!=FVR_MODULE_ABI||!image||byteCount<64||byteCount>512ull*1024*1024||outputSize!=sizeof(FvrBc2Profile)||!output)return 0;
    try{
        const auto data=std::span(reinterpret_cast<const std::byte*>(image),static_cast<std::size_t>(byteCount));
        const auto pe=fvr::engine::InspectPe(data);if(!pe.valid)return 0;
        const auto profile=fvr::bc2::DiscoverProfile(data,pe.image);if(!profile)return 0;
        const auto& p=*profile;*output={sizeof(FvrBc2Profile),FVR_MODULE_ABI,p.preferredBase,p.imageSize,p.rendererGlobal,p.gameRendererGlobal,p.frame,p.dispatch,p.present,p.presentWrapper,p.rendererVtable,0};
        return 1;
    }catch(...){return 0;} // Never propagate C++ exceptions across the C module ABI.
}