#pragma once
#include <cstdint>
#include <string_view>
namespace fvr::bc2 {
enum class PumpHoldDiagnostic:std::uint32_t {Disabled=0,SpasOneShot=1,SelectedManualEmptyFire=2};
constexpr bool ValidPumpHoldDiagnostic(PumpHoldDiagnostic mode,std::uint32_t flags,std::uint32_t duration)noexcept {
    if(mode==PumpHoldDiagnostic::Disabled)return true;
    if(mode==PumpHoldDiagnostic::SelectedManualEmptyFire){
        constexpr auto required=9u|0x197800u|0x2000000u;
        return duration==30000&&(flags&required)==required&&!(flags&~(required|0x200u));
    }
    constexpr std::uint32_t required=9u|0x197800u|0x400000u;
    return mode==PumpHoldDiagnostic::SpasOneShot&&duration==15000&&(flags&required)==required&&!(flags&~(required|0x200u));
}
constexpr bool EmptyFireReceiverOption(std::wstring_view option)noexcept {
    return option==L"--empty-fire-neutral"||option==L"--pairs"||option==L"--static-pose"||option==L"--async"||option==L"--capture-poses"||option==L"--request-lifetime";
}
// Only ordinary trigger input. The first shot can meet the one-shot hold;
// the second shot observes normal cycling. No reload, equip or state marker.
constexpr float PumpDiagnosticTrigger(std::uint64_t elapsedMs)noexcept {
    return (elapsedMs>=3000&&elapsedMs<3080)||(elapsedMs>=6000&&elapsedMs<6080)?1.f:0.f;
}
}
