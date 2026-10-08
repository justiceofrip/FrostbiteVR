#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <vector>
namespace fvr::graphics {
// Retiring a retained diagnostic module must not retain already-saved CPU
// pixels. Hook disabling alone does not prove its last callback has returned.
// Seal only at zero readers; a callback arriving after sealing must delegate
// directly to the native original without touching diagnostic images.
class DiagnosticImageLifetime {
    static constexpr std::uint32_t Closed=0x80000000u;
    std::atomic<std::uint32_t> readers_{0};
public:
    class Scope {
        DiagnosticImageLifetime* owner_=nullptr;
    public:
        explicit Scope(DiagnosticImageLifetime& owner)noexcept {
            auto state=owner.readers_.load(std::memory_order_acquire);
            while(state<Closed-1){
                if(owner.readers_.compare_exchange_weak(state,state+1,std::memory_order_acq_rel,
                    std::memory_order_acquire)){owner_=&owner;return;}
            }
        }
        Scope(const Scope&)=delete;
        Scope& operator=(const Scope&)=delete;
        ~Scope(){if(owner_)owner_->readers_.fetch_sub(1,std::memory_order_release);}
        bool Active()const noexcept{return owner_!=nullptr;}
    };
    bool TryQuiesce(bool hooksDisabled)noexcept {
        if(!hooksDisabled)return false;
        std::uint32_t empty=0;
        return readers_.compare_exchange_strong(empty,Closed,std::memory_order_acq_rel,
            std::memory_order_acquire)||empty==Closed;
    }
    bool Quiescent()const noexcept{return readers_.load(std::memory_order_acquire)==Closed;}
    struct Released {std::size_t bytes=0,capacityBytes=0;bool released=false;};
    Released ReleaseSaved(std::vector<unsigned char>& pixels,bool captured,bool pixelsSaved,bool reportSaved)const noexcept {
        if(!Quiescent()||!captured||!pixelsSaved||!reportSaved||pixels.empty())return {};
        const Released result{pixels.size(),pixels.capacity(),true};
        std::vector<unsigned char>{}.swap(pixels);return result;
    }
};
}
