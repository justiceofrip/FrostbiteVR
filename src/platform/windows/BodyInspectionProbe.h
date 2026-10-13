#pragma once
#include "SceneRecoveryProbe.h"
#include <algorithm>
#include <cmath>
namespace fvr::probe {
// Receiver-only inspection: one deliberate grab at an explicitly supplied
// tracking-space contact. No weapon name, native selection or reload authority.
inline bool ValidBodyInspectionContact(math::Vec3 contact)noexcept {
    return std::isfinite(contact.x)&&std::isfinite(contact.y)&&std::isfinite(contact.z)&&
        std::abs(contact.x)<=1&&std::abs(contact.y)<=1&&std::abs(contact.z)<=1;
}
inline bool BodyInspectionInput(interaction::InputFrame& input,math::Vec3 contact,
    std::uint64_t ms)noexcept {
    if(!ValidBodyInspectionContact(contact))return false;
    for(auto& h:input.hands){h.trigger=h.squeeze=h.stickX=h.stickY=0;h.held=h.touched=h.touchActive=0;}
    input.hands[0].grip.position={-.2f,-.25f,-.45f};
    constexpr math::Vec3 front{.2f,-.25f,-.45f};
    const auto blend=[](math::Vec3 a,math::Vec3 b,float t){
        return math::Vec3{a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t,a.z+(b.z-a.z)*t};};
    auto& hand=input.hands[1];hand.grip.position=front;
    if(ms>=1000&&ms<1500)hand.grip.position=blend(front,contact,float(ms-1000)/500);
    else if(ms>=1500&&ms<3000)hand.grip.position=contact;
    else if(ms>=3000&&ms<4000)hand.grip.position=blend(contact,front,float(ms-3000)/1000);
    hand.squeeze=ms>=2000?1.f:0.f;
    for(auto& h:input.hands)h.aim=h.grip;
    return true;
}
inline bool BodyInspectionActions(const interaction::InputFrame& input)noexcept {
    if(input.hands[1].squeeze!=0&&input.hands[1].squeeze!=1)return false;
    auto neutral=input;neutral.hands[1].squeeze=0;
    return NeutralSceneActions(neutral);
}
}
