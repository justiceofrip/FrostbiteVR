#pragma once
#include "fvr/interaction/ControllerInput.h"
#include "fvr/runtime/PresentationPolicy.h"
#include <ostream>
#include <string_view>

namespace fvr::probe {
// An allowlist prevents new action-script options from silently becoming
// legal in this fixture. Option values are consumed by the normal parser.
constexpr bool NeutralSceneOption(std::wstring_view option)noexcept {
    return option==L"--pairs"||option==L"--request-lifetime"||
        option==L"--static-pose"||option==L"--async"||
        option==L"--scene-recovery-probe"||option==L"--scene-controls-neutral"||
        option==L"--controls-observe"||option==L"--capture-poses";
}
constexpr bool NeutralSceneArguments(bool scene,bool staticPose,bool async,
    unsigned pairs,bool optionsOnly)noexcept {
    return scene&&staticPose&&async&&pairs==240&&optionsOnly;
}
inline interaction::InputFrame NeutralSceneInput(const runtime::TrackingFrame& t,
    std::uint64_t sequence)noexcept {
    interaction::InputFrame result{};
    result.generation=sequence;result.spaceGeneration=t.spaceGeneration;
    result.predictedNs=t.predictedNs;result.focused=t.focused;result.headValid=t.headValid;
    result.floorRelative=false;result.worldUnitsPerMeter=t.worldUnitsPerMeter;
    result.referenceHead=t.referenceHead;result.head=t.head;
    for(auto& hand:result.hands){
        hand.gripTracked=hand.aimTracked=true;hand.active=interaction::Components;
    }
    // Static diagnostic placement, not production calibration. The right pose
    // matches the reachable XM8 monitor fixture; neither hand squeezes/grabs.
    result.hands[0].grip.position={-.2f,-.25f,-.45f};
    result.hands[1].grip.position={.15f,-.10f,-.20f};
    for(auto& hand:result.hands)hand.aim=hand.grip;
    return result;
}
inline bool NeutralSceneActions(const interaction::InputFrame& input)noexcept {
    for(const auto& h:input.hands)
        if(h.held||h.touched||h.touchActive||h.stickX!=0||h.stickY!=0||h.trigger!=0||h.squeeze!=0)return false;
    return true;
}
inline void WriteNeutralSceneInput(std::ostream& out,const interaction::InputFrame& input,
    std::uint64_t tickMs) {
    const auto pose=[&](const math::Pose& p){out<<'['<<p.position.x<<','<<p.position.y<<','<<p.position.z<<','
        <<p.orientation.x<<','<<p.orientation.y<<','<<p.orientation.z<<','<<p.orientation.w<<']';};
    out<<"{\"tick_ms\":"<<tickMs<<",\"generation\":"<<input.generation<<",\"space\":"<<input.spaceGeneration
       <<",\"predicted_ns\":"<<input.predictedNs<<",\"focused\":"<<(input.focused?"true":"false")
       <<",\"head_valid\":"<<(input.headValid?"true":"false")<<",\"reference_head\":";pose(input.referenceHead);
    out<<",\"head\":";pose(input.head);out<<",\"hands\":[";
    for(unsigned n=0;n<2;++n){const auto& h=input.hands[n];if(n)out<<',';
        out<<"{\"active\":"<<h.active<<",\"held\":"<<h.held<<",\"touch_active\":"<<h.touchActive<<",\"touched\":"<<h.touched
           <<",\"grip_tracked\":"<<(h.gripTracked?"true":"false")<<",\"aim_tracked\":"<<(h.aimTracked?"true":"false")
           <<",\"axes\":["<<h.stickX<<','<<h.stickY<<','<<h.trigger<<','<<h.squeeze<<"],\"grip\":";pose(h.grip);
        out<<",\"aim\":";pose(h.aim);out<<'}';
    }out<<"]}\n";
}
} // namespace fvr::probe
