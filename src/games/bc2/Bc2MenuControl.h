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
    MenuControlEdges Observe(ipc::ChannelResult result,const ipc::MenuControl& control,std::int64_t nowQpc=0)noexcept {
        if(result==ipc::ChannelResult::Busy){
            if(nowQpc>0&&deadline_&&(nowQpc<observed_||nowQpc>=deadline_)){
                Forget();return {MenuControlRead::Inactive};
            }
            return {MenuControlRead::Busy};
        }
        if(result!=ipc::ChannelResult::Ok||!ipc::ValidMenuControl(control)||!(control.flags&ipc::MenuFocused)||
           (nowQpc>0&&control.deadlineQpc<=nowQpc)){
            Forget();return {MenuControlRead::Inactive};
        }
        const bool rearm=!seen_||space_!=control.space;
        const MenuControlEdges out{rearm?MenuControlRead::Rearmed:MenuControlRead::Ready,
            !rearm&&control.toggle>toggle_,!rearm&&control.cancel>cancel_};
        seen_=true;space_=control.space;toggle_=control.toggle;cancel_=control.cancel;
        epoch_=control.menuEpoch;deadline_=control.deadlineQpc;observed_=nowQpc;
        pointerHeld_=(control.flags&(ipc::MenuPoint|ipc::MenuDown))==(ipc::MenuPoint|ipc::MenuDown);
        return out;
    }
    // Retain an already-owned down through contention only until the ORIGINAL
    // sample expires. This grants no cursor movement, new down or renewed lease.
    bool RetainPointerAcrossContention(std::uint64_t epoch,std::int64_t nowQpc)const noexcept {
        return seen_&&pointerHeld_&&epoch_==epoch&&observed_>0&&nowQpc>=observed_&&nowQpc<deadline_;
    }
private:
    void Forget()noexcept {seen_=pointerHeld_=false;deadline_=observed_=0;}
    bool seen_=false,pointerHeld_=false;
    std::uint64_t space_=0,toggle_=0,cancel_=0,epoch_=0;
    std::int64_t observed_=0,deadline_=0;
};
}
