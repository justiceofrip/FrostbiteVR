#pragma once
#include "fvr/interaction/ControllerInput.h"
#include <string_view>
namespace fvr::probe {
constexpr bool PhysicalPumpReceiverOption(std::wstring_view option)noexcept {
    return option==L"--physical-pump-probe"||option==L"--pump-empty-cycle"||option==L"--pump-exit-vehicle"||option==L"--pairs"||
        option==L"--static-pose"||option==L"--async"||option==L"--capture-poses"||option==L"--request-lifetime";
}
constexpr bool PhysicalPumpExit(std::uint64_t ms,bool enabled)noexcept{return enabled&&ms>=400&&ms<500;}
inline void PhysicalPumpInput(interaction::InputFrame& in,std::uint64_t ms,bool exitVehicle)noexcept {
    for(auto& h:in.hands){h.held=0;h.trigger=h.squeeze=h.stickX=h.stickY=0;}
    in.hands[0].grip={};in.hands[0].grip.position={-.2f,-.25f,-.45f};
    in.hands[1].grip={};in.hands[1].grip.position={0,-.4f,0};
    for(auto& h:in.hands)h.aim=h.grip;
    in.hands[0].held=PhysicalPumpExit(ms,exitVehicle)?interaction::Primary:0;
}
inline bool PhysicalPumpReceiverActions(const interaction::InputFrame& in,std::uint64_t ms,bool exitVehicle)noexcept {
    return in.hands[0].held==(PhysicalPumpExit(ms,exitVehicle)?interaction::Primary:0)&&!in.hands[1].held&&
        !in.hands[0].trigger&&!in.hands[1].trigger&&!in.hands[0].squeeze&&!in.hands[1].squeeze&&
        !in.hands[0].stickX&&!in.hands[0].stickY&&!in.hands[1].stickX&&!in.hands[1].stickY;
}
}
