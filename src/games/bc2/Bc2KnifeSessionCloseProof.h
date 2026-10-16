#pragma once
#include <cmath>
namespace fvr::bc2::private_knife {
// A return is admitted initially only after all three knife copies are idle.
// During the two real chooser continuations, the native deselection callback
// legitimately puts the returned inventory's old knife in state1. It cannot
// remain an idle prerequisite for the other inventory's return. The +3c copy
// is observed prediction; the actual chooser consumers are client +40/server.
// This does not acknowledge either return. Completion still requires both
// chooser boundaries plus all three original selections/states and drain.
template<class Snapshot>bool KnifeCloseConsumerReady(const Snapshot& snapshot,unsigned inventory)noexcept {
    if(inventory>=2)return false;
    const auto& state=inventory==0?snapshot.knife.client.branches[1]:snapshot.knife.server;
    return state.currentState==2&&state.nextState==2&&std::isfinite(state.phaseTimer)&&
        state.phaseTimer>=0&&state.phaseTimer<=1000000&&!(state.flagsA8&8);
}
}
