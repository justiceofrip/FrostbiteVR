#pragma once
#include <cstdint>
namespace fvr::ipc {
// Setup only. Connected() uses a zero-wait mutex: a second read may be Busy
// immediately after a successful handshake. Retain that observed success;
// normal frame/input operations still check the live peer independently.
template<class Probe,class Clock,class Pause>
bool WaitForConnection(Probe&& probe,Clock&& clock,Pause&& pause,
    std::uint64_t began,std::uint64_t budget) {
    for(;;){
        const auto now=clock();
        if(now<began||now-began>=budget)return false;
        if(probe())return true;
        pause();
    }
}
} // namespace fvr::ipc
