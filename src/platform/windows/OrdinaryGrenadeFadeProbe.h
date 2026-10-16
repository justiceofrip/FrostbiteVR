#pragma once
#include "OrdinaryGrenadeProbeInput.h"
#include <cmath>

namespace fvr::probe {
constexpr unsigned GrenadeFadeDurationMs=8000,GrenadeFadeIntervalMs=250,GrenadeFadePairLimit=32;
constexpr bool OrdinaryGrenadeFadeOption(std::wstring_view s)noexcept {
    return s==L"--ordinary-grenade-fade-probe"||s==L"--fade-window"||
        s==L"--pairs"||s==L"--static-pose"||s==L"--async";
}
constexpr bool GrenadeFadeWindow(std::uint64_t start,unsigned duration,unsigned interval)noexcept {
    return start&&start<=UINT64_MAX-GrenadeFadeDurationMs&&duration==GrenadeFadeDurationMs&&interval==GrenadeFadeIntervalMs;
}
constexpr bool GrenadeFadeLead(std::uint64_t start,std::uint64_t connected)noexcept {
    return start>=connected&&start-connected>=4000&&start-connected<=12000;
}
// Do not spend the entire finite pair budget before the timed observation.
constexpr bool GrenadeFadeRequest(std::uint64_t now,std::uint64_t start,unsigned consumed)noexcept {
    return consumed<(now<start?240-GrenadeFadePairLimit:240);
}
// One absolute-time gesture, with no delayed retry if pre-action pixels are
// missing or the receiver misses the start. No native action is authorized here.
class GrenadeFadeGesture {
public:
    bool Held(std::uint64_t now,std::uint64_t start,bool preActionPair)noexcept {
        if(now<start||now-start<1000)return false;
        if(!decided_){decided_=true;admitted_=preActionPair&&now-start<=1100;}
        return admitted_&&now-start<1650;
    }
    bool Admitted()const noexcept{return admitted_;}
    bool Decided()const noexcept{return decided_;}
private:
    bool decided_=false,admitted_=false;
};
inline void OrdinaryGrenadeFadeInput(interaction::InputFrame& input,std::uint64_t elapsed,
    std::uint64_t now,std::uint64_t start,bool held)noexcept {
    OrdinaryGrenadeInput(input,elapsed);
    auto& right=input.hands[1];right.held=held?interaction::StickClick:0;
    // OpenXR local -Z forward and +Y up. Ramp after the setup draw to -84.5deg
    // about X; keep the head and both stereo poses untouched. Aim stays down
    // through the whole observation so native delayed firing sees this pose.
    const float t=now>=start?1.f:(start-now>=500?0.f:float(500-(start-now))/500.f);
    const float half=-0.7374016047f*t;
    right.grip.orientation={std::sin(half),0,0,std::cos(half)};right.aim=right.grip;
}
inline bool OrdinaryGrenadeFadeControls(const interaction::InputFrame& input,std::uint64_t elapsed,bool held)noexcept {
    auto neutral=input;neutral.hands[1].held=0;
    // The original validator's timed click is removed; its setup squeeze
    // bounds are kept. Compare the actual hold separately against the latch.
    const bool originalClick=elapsed>=5000&&elapsed<5650;
    if(originalClick)neutral.hands[1].held=interaction::StickClick;
    return input.hands[0].held==0&&input.hands[1].held==(held?interaction::StickClick:0)&&
        OrdinaryGrenadeControls(neutral,elapsed);
}
}
