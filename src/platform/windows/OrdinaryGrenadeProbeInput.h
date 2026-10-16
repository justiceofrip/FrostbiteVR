#pragma once
#include "fvr/interaction/ControllerInput.h"
#include <algorithm>
#include <cstdint>
#include <string_view>
namespace fvr::probe {
constexpr bool OrdinaryGrenadeOption(std::wstring_view s)noexcept {
    return s==L"--ordinary-grenade-probe"||s==L"--pairs"||s==L"--static-pose"||s==L"--async";
}
// One setup draw through the real shoulder/holster path, then one right-click
// gesture. This supplies no native authority and never scripts any trigger.
inline void OrdinaryGrenadeInput(interaction::InputFrame& input,std::uint64_t ms)noexcept {
    for(auto& h:input.hands){h.held=h.touched=h.touchActive=0;h.stickX=h.stickY=h.trigger=h.squeeze=0;}
    const math::Vec3 home{.15f,-.10f,-.20f},shoulder{-.20f,-.12f,.18f};
    auto& right=input.hands[1];right.grip.position=home;
    if(ms>=1200&&ms<1800){const float t=float(ms-1200)/600.f;right.grip.position={home.x+(shoulder.x-home.x)*t,home.y+(shoulder.y-home.y)*t,home.z+(shoulder.z-home.z)*t};}
    else if(ms>=1800&&ms<2500){right.grip.position=shoulder;right.squeeze=1;}
    else if(ms>=2500&&ms<3100){const float t=float(ms-2500)/600.f;right.squeeze=1;right.grip.position={shoulder.x+(home.x-shoulder.x)*t,shoulder.y+(home.y-shoulder.y)*t,shoulder.z+(home.z-shoulder.z)*t};}
    if(ms>=5000&&ms<5650)right.held=interaction::StickClick;
    // Ordinary forward pose is retained. No downward death-probe aiming.
    for(auto& h:input.hands)h.aim=h.grip;
}
inline bool OrdinaryGrenadeControls(const interaction::InputFrame& input,std::uint64_t ms)noexcept {
    if(input.hands[0].held||(input.hands[1].held&~interaction::StickClick))return false;
    for(unsigned i=0;i<2;++i){const auto& h=input.hands[i];
        if(h.trigger!=0||h.stickX!=0||h.stickY!=0||h.touched||h.touchActive)return false;
        if(h.squeeze!=0&&!(i==1&&ms>=1800&&ms<3100&&h.squeeze==1))return false;}
    return (input.hands[1].held!=0)==(ms>=5000&&ms<5650);
}
}
