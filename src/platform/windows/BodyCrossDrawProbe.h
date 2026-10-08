#pragma once
#include "fvr/interaction/ControllerInput.h"
#include <algorithm>
#include <cstdint>
namespace fvr::probe {
// Receiver-only, finite gesture script. Native inventory and renderer generate
// every selection/claim/receipt; this merely moves a synthetic tracked hand.
inline void BodyCrossDrawInput(interaction::InputFrame& input,std::uint64_t ms)noexcept {
    using math::Vec3;
    const Vec3 neutral{.2f,-.25f,-.45f},right{.2f,-.12f,.18f},left{-.2f,-.12f,.18f};
    const auto between=[&](Vec3 a,Vec3 b,std::uint64_t begin,std::uint64_t end){
        const float t=std::clamp(float(ms-begin)/float(end-begin),0.f,1.f);
        return Vec3{a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t,a.z+(b.z-a.z)*t};};
    for(auto& hand:input.hands){hand.trigger=hand.squeeze=hand.stickX=hand.stickY=0;hand.held=hand.touched=hand.touchActive=0;}
    input.hands[0].grip.position={-.2f,-.25f,-.45f};
    // Keep production default-stow waiting until the scripted right squeeze.
    // Below every grab threshold; right remains neutral so its edge is armed.
    input.hands[0].squeeze=ms<3000?.4f:0.f;
    auto& hand=input.hands[1];hand.grip.position=neutral;
    if(ms>=2000&&ms<3000)hand.grip.position=between(neutral,right,2000,3000);
    else if(ms>=3000&&ms<4500){hand.grip.position=right;hand.squeeze=ms<4000?1.f:0.f;}
    else if(ms>=4500&&ms<5500)hand.grip.position=between(right,neutral,4500,5500);
    else if(ms>=6000&&ms<7000)hand.grip.position=between(neutral,left,6000,7000);
    else if(ms>=7000&&ms<8500){hand.grip.position=left;hand.squeeze=ms<8000?1.f:0.f;}
    else if(ms>=8500&&ms<9500)hand.grip.position=between(left,neutral,8500,9500);
    for(auto& h:input.hands)h.aim=h.grip;
}
}
