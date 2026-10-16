#pragma once
#include "fvr/interaction/MenuPointer.h"

namespace fvr::interaction {
// Host policy, evaluated on raw controls before the menu filters gameplay.
// A toggle is acknowledged only by an observed native menu epoch transition.
// Losing the channel or focus cannot manufacture that acknowledgement.
class MenuOwnershipPolicy {
public:
    InputMenuOwnership Update(const InputFrame& raw,const MenuPointerFrame& pointer,
        const ipc::MenuState* state,std::int64_t sourceDeadlineNs,std::int64_t nowNs)noexcept {
        const bool known=state&&ipc::ValidMenuState(*state)&&state->mode!=ipc::MenuMode::Unknown&&
            nowNs>0&&sourceDeadlineNs>nowNs&&sourceDeadlineNs-nowNs<=100000000;
        if(pointer.control.toggle!=toggle_||pointer.control.cancel!=cancel_){
            pending_=true;pendingEpoch_=known?state->epoch:0;
            toggle_=pointer.control.toggle;cancel_=pointer.control.cancel;
        }
        if(pending_&&known&&((pendingEpoch_&&state->epoch!=pendingEpoch_)||
           (!pendingEpoch_&&state->mode==ipc::MenuMode::Menu)))pending_=false;
        if(!known||!ValidInput(raw)||!raw.focused||!raw.headValid)return {};
        const bool rawMenu=(raw.hands[0].held&MenuClick)!=0;
        const auto owner=state->mode==ipc::MenuMode::Menu?InputMenuOwner::Menu:
            pending_||pointer.suppressLeftFaceButtons||rawMenu?InputMenuOwner::Pending:InputMenuOwner::Gameplay;
        return {owner,state->sequence,state->epoch,sourceDeadlineNs};
    }
private:
    std::uint64_t toggle_=0,cancel_=0,pendingEpoch_=0;
    bool pending_=false;
};
}
