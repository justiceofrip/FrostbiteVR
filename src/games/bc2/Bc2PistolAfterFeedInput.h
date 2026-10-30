#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace fvr::bc2 {
// Native20 pins Update's context+2c bit 0. This scope does not write +28
// (the stock Abort input), timing, reload/order bits, or the firing object.
struct PistolAfterFeedInputAccess {
    void* address=nullptr;
    bool (*compareExchange)(void*,std::uint32_t,std::uint32_t,std::uint32_t&)noexcept=nullptr;
};
struct PistolAfterFeedInputReceipt {
    std::uint32_t original=0,effective=0,beforeRestore=0;
    bool requested=false,applied=false,restored=false,unexpectedWrite=false;
    bool operator==(const PistolAfterFeedInputReceipt&)const=default;
    bool Apply(const PistolAfterFeedInputAccess& a,std::uint32_t flags)noexcept {
        if(requested)return false;requested=true;original=flags;effective=flags&~1u;
        if(!a.address||!a.compareExchange)return false;
        std::uint32_t observed=0;
        applied=a.compareExchange(a.address,original,effective,observed)&&observed==original;
        return applied;
    }
    bool Restore(const PistolAfterFeedInputAccess& a)noexcept {
        if(!applied||restored||!a.compareExchange)return false;
        restored=a.compareExchange(a.address,effective,original,beforeRestore)&&beforeRestore==effective;
        unexpectedWrite=!restored;return restored;
    }
    bool Exact()const noexcept{return requested&&applied&&restored&&!unexpectedWrite&&beforeRestore==effective;}
};
inline void RunPistolAfterFeedInput(const PistolAfterFeedInputAccess& a,std::uint32_t flags,bool requested,
    void (*original)(void*),void* call,PistolAfterFeedInputReceipt& receipt){
    if(requested)receipt.Apply(a,flags);
#if defined(_MSC_VER)
    __try {original(call);}
    __finally {if(receipt.applied)receipt.Restore(a);}
#else
    struct Restore {const PistolAfterFeedInputAccess& access;PistolAfterFeedInputReceipt& receipt;
        ~Restore(){if(receipt.applied)receipt.Restore(access);}} restore{a,receipt};
    original(call);
#endif
}
// The caller-owned entry and exit remain the original bytes. Only the separate
// effective-input projection can claim neutrality during a successful veto.
inline bool PistolAfterFeedEffectiveContext(const std::array<std::byte,0x30>& entry,
    const std::array<std::byte,0x30>& exit,const PistolAfterFeedInputReceipt& receipt,
    std::array<std::byte,0x30>& effective)noexcept {
    if(entry!=exit)return false;effective=entry;
    if(!receipt.requested)return true;
    std::uint32_t flags=0;std::memcpy(&flags,entry.data()+0x2c,4);
    if(!receipt.Exact()||flags!=receipt.original)return false;
    std::memcpy(effective.data()+0x2c,&receipt.effective,4);return true;
}
}
