#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include <limits>

namespace fvr::bc2::weatherObservation {
// One immutable owner/frame admission. This is telemetry, never render policy.
struct Key {
    std::uint32_t world=0,request=0,main=0,eye=0,frame=0;
    bool operator==(const Key&) const = default;
    bool Valid() const noexcept {return world&&request&&main&&eye&&main!=eye&&frame;}
};
class Gate {
public:
    static constexpr unsigned CapacityPerEye=128;
    static constexpr unsigned NoSlot=std::numeric_limits<unsigned>::max();
    bool Arm(std::uint64_t now,unsigned delayMs) noexcept {
        if(Closed()||state_.load()!=0||delayMs<1000||delayMs>90000||now>UINT64_MAX-delayMs)return false;
        deadline_=now+delayMs;state_.store(1,std::memory_order_release);return true;
    }
    bool Begin(std::uint64_t now,Key key) noexcept {
        if(Closed()||!key.Valid()||state_.load(std::memory_order_acquire)!=1||now<deadline_)return false;
        unsigned waiting=1;
        if(!state_.compare_exchange_strong(waiting,2,std::memory_order_acq_rel))return false;
        key_=key;state_.store(3,std::memory_order_release);return true;
    }
    bool Active(Key key) const noexcept {return !Closed()&&state_.load(std::memory_order_acquire)==3&&key_==key;}
    // Close admission without rewriting an immutable key or a completed result.
    // Already-admitted callbacks are drained separately before final reporting.
    void Close() noexcept {closed_.store(true,std::memory_order_release);}
    bool Closed() const noexcept {return closed_.load(std::memory_order_acquire);}
    Key Selected() const noexcept {return state_.load(std::memory_order_acquire)>=3?key_:Key{};}
    unsigned Reserve(Key key,unsigned eye) noexcept {
        if(eye>1||!Active(key))return NoSlot;
        auto count=counts_[eye].load(std::memory_order_relaxed);
        while(count<CapacityPerEye){
            if(counts_[eye].compare_exchange_weak(count,count+1,std::memory_order_relaxed))return count;
        }
        overflow_.store(true,std::memory_order_release);return NoSlot;
    }
    void Image(Key key,unsigned eye) noexcept {if(eye<2&&Active(key))images_.fetch_or(1u<<eye,std::memory_order_release);}
    void Finish(Key key,bool published) noexcept {
        if(!Active(key))return;
        published_.store(published,std::memory_order_relaxed);state_.store(4,std::memory_order_release);
    }
    bool Published() const noexcept {return state_.load(std::memory_order_acquire)==4&&published_.load(std::memory_order_relaxed);}
    bool Overflow() const noexcept {return overflow_.load(std::memory_order_acquire);}
    unsigned Images() const noexcept {return images_.load(std::memory_order_acquire);}
    unsigned Count(unsigned eye) const noexcept {return eye<2?counts_[eye].load(std::memory_order_acquire):0;}
    unsigned State() const noexcept {return state_.load(std::memory_order_acquire);}
private:
    std::atomic<unsigned> state_=0; // disabled, waiting, publishing key, active, finished
    Key key_{};std::uint64_t deadline_=0;
    std::array<std::atomic<unsigned>,2> counts_{};
    std::atomic<unsigned> images_=0;
    std::atomic<bool> overflow_=false,published_=false,closed_=false;
};
// Installed build795745: exact immutable code ranges, checked against disk AND
// live bytes before hooking. The full environment function is split at8KiB.
struct CodePin {unsigned rva,bytes;std::uint64_t hash;};
inline constexpr std::array<CodePin,7> CodePins{{
    {0x7dda10,0x2000,0x046ce113f90f8375ULL},
    {0x7dfa10,0x364,0x5977a8ef9595540eULL},
    {0x7e0680,0x105,0xa95700b0a0e92db2ULL},
    {0x569ad0,0x68,0x5d1785e3d82afa97ULL},
    {0x786250,8,0x729372ce154e612cULL},
    {0x7866f0,8,0x4bf339b353393969ULL},
    {0x786320,8,0x373fafd135843180ULL},
}};
inline constexpr unsigned EnvironmentRva=0x7dda10,WorldSetterRva=0x569ad0;
inline bool WorldSite(unsigned rva) noexcept {return rva==0x7de3cc||rva==0x7dea5c||rva==0x7df13e;}
}
