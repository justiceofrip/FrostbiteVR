#pragma once
#include "fvr/ipc/FrameChannel.h"
#include "fvr/ipc/MenuProtocol.h"
namespace fvr::bc2 {
enum class MenuControlRead : unsigned char {Busy,Inactive,Rearmed,Ready};
struct MenuControlEdges {
    MenuControlRead read=MenuControlRead::Inactive;
    bool toggle=false,cancel=false;
};
// ReadControl Busy is zero-wait mutex contention, not evidence of expiry or
// focus loss. It authorizes no input and must not consume the next edge.
// Timeout/closed/invalid/unfocused input and a new tracking space deliberately
// require a fresh baseline, so commands made while unavailable never replay.
class MenuControlSequence {
public:
    MenuControlEdges Observe(ipc::ChannelResult result,const ipc::MenuControl& control)noexcept {
        if(result==ipc::ChannelResult::Busy)return {MenuControlRead::Busy};
        if(result!=ipc::ChannelResult::Ok||!ipc::ValidMenuControl(control)||!(control.flags&ipc::MenuFocused)){
            seen_=false;return {MenuControlRead::Inactive};
        }
        const bool rearm=!seen_||space_!=control.space;
        const MenuControlEdges out{rearm?MenuControlRead::Rearmed:MenuControlRead::Ready,
            !rearm&&control.toggle>toggle_,!rearm&&control.cancel>cancel_};
        seen_=true;space_=control.space;toggle_=control.toggle;cancel_=control.cancel;return out;
    }
private:
    bool seen_=false;
    std::uint64_t space_=0,toggle_=0,cancel_=0;
};
}
