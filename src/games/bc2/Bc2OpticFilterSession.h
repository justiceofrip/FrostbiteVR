#pragma once
#include <atomic>
#include <cstdint>
#include <optional>
namespace fvr::bc2 {
constexpr bool ValidOpticFilterProbeConfig(std::uint32_t flags,std::uint32_t durationMs)noexcept {
    return !(flags&0x8000000u)||((flags&0xffu)==9&&(flags&0x17800u)==0x17800u&&
        !(flags&0xe7c68400u)&&durationMs>=100&&durationMs<=15000);
}
// Disconnect cancels all in-flight scopes. Reconnect cannot reuse a cache
// published before that connection, even if its ordinary lease has not expired.
class OpticFilterSession {
public:
    struct Ticket {std::uint64_t epoch=0;std::int64_t floorNs=0;};
    void SetConnected(bool value,std::int64_t now)noexcept {
        if(value&&connected_.load(std::memory_order_acquire))return;
        connected_.store(false,std::memory_order_release);epoch_.fetch_add(1,std::memory_order_acq_rel);
        floor_.store(now,std::memory_order_release);
        if(value&&now>0)connected_.store(true,std::memory_order_release);
    }
    std::optional<Ticket> Begin(std::int64_t sourceObservedNs)const noexcept {
        const Ticket t{epoch_.load(std::memory_order_acquire),floor_.load(std::memory_order_acquire)};
        if(t.floorNs<=0||sourceObservedNs<t.floorNs||!Current(t))return {};return t;
    }
    bool Current(Ticket t)const noexcept {return connected_.load(std::memory_order_acquire)&&t.epoch==epoch_.load(std::memory_order_acquire)&&t.floorNs==floor_.load(std::memory_order_acquire);}
    bool Connected()const noexcept{return connected_.load(std::memory_order_acquire);}
    std::uint64_t Epoch()const noexcept{return epoch_.load(std::memory_order_acquire);}
private:
    std::atomic<bool> connected_=false;std::atomic<std::uint64_t> epoch_=1;std::atomic<std::int64_t> floor_=0;
};
}
