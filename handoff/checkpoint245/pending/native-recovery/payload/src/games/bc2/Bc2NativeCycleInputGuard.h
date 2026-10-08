#pragma once
#include <array>
#include <cstdint>
namespace fvr::bc2 {
// Only the original invocation's caller-owned stack context is writable. The
// native firing object, time fields and ammunition never enter this access API.
// words[0] is context+28 (bool plus untouched padding), words[1] is context+2c.
struct NativeCycleInputAccess {
    void* context=nullptr;
    bool (*compareExchange)(void*,unsigned index,std::uint32_t expected,
        std::uint32_t replacement,std::uint32_t& observed)noexcept=nullptr;
};
struct NativeCycleInputGuard {
    std::array<std::uint32_t,2> original{},effective{},beforeRestore{};
    bool requested=false,applied=false,restored=false,rollback=false,unexpectedWrite=false;
    bool operator==(const NativeCycleInputGuard&)const noexcept=default;
    bool Apply(const NativeCycleInputAccess& access,const std::array<std::uint32_t,2>& source)noexcept {
        requested=true;
        if(applied||!access.compareExchange||(source[0]&255u)>1||(source[1]&~7u))return false;
        original=source;effective={(source[0]&~255u)|1u,0};std::uint32_t observed=0;
        if(!access.compareExchange(access.context,1,original[1],effective[1],observed)||observed!=original[1])return false;
        if(!access.compareExchange(access.context,0,original[0],effective[0],observed)||observed!=original[0]){
            rollback=access.compareExchange(access.context,1,effective[1],original[1],observed)&&observed==effective[1];
            unexpectedWrite=!rollback;return false;
        }
        applied=true;return true;
    }
    bool Restore(const NativeCycleInputAccess& access)noexcept {
        if(!applied||restored||!access.compareExchange)return false;
        const bool flags=access.compareExchange(access.context,1,effective[1],original[1],beforeRestore[1])&&beforeRestore[1]==effective[1];
        const bool inhibit=access.compareExchange(access.context,0,effective[0],original[0],beforeRestore[0])&&beforeRestore[0]==effective[0];
        restored=flags&&inhibit;unexpectedWrite=!restored;return restored;
    }
    bool Exact()const noexcept{return requested&&applied&&restored&&!unexpectedWrite&&beforeRestore==effective;}
};
inline void RunNativeCycleInputGuard(const NativeCycleInputAccess& access,
    const std::array<std::uint32_t,2>& originalWords,bool requested,
    void (*original)(void*),void* context,NativeCycleInputGuard& receipt){
    if(requested)receipt.Apply(access,originalWords);
#if defined(_MSC_VER)
    __try {original(context);}
    __finally {if(receipt.applied)receipt.Restore(access);}
#else
    struct Restore {const NativeCycleInputAccess& access;NativeCycleInputGuard& receipt;
        ~Restore(){if(receipt.applied)receipt.Restore(access);}} restore{access,receipt};
    original(context);
#endif
}
}
