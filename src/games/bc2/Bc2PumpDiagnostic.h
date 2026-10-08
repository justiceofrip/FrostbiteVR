#pragma once
#include <cstdint>
#include <string_view>
namespace fvr::bc2 {
enum class PumpHoldDiagnostic:std::uint32_t {Disabled=0,SpasOneShot=1,SelectedManualEmptyFire=2,SpasPhysicalCycle=3,SpasPhysicalCycleOnce=4,SpasPhysicalEmpty=5};
constexpr unsigned PumpPhysicalCycles(PumpHoldDiagnostic mode)noexcept {
    return mode==PumpHoldDiagnostic::SpasPhysicalEmpty?8u:mode==PumpHoldDiagnostic::SpasPhysicalCycle?2u:mode==PumpHoldDiagnostic::SpasPhysicalCycleOnce?1u:0u;
}
constexpr bool ValidPumpHoldDiagnostic(PumpHoldDiagnostic mode,std::uint32_t flags,std::uint32_t duration)noexcept {
    if(mode==PumpHoldDiagnostic::Disabled)return true;
    if(PumpPhysicalCycles(mode)){
        constexpr auto required=9u|0x197800u;
        return duration==(PumpPhysicalCycles(mode)==8?60000u:30000u)&&(flags&required)==required&&!(flags&~(required|0x200u));
    }
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
// The caller must preflight the exact boat/selected-SPAS setup. This emits
// only the existing controller Use action, before either trigger window.
constexpr bool PumpDiagnosticUse(std::uint64_t elapsedMs,bool exitVehicle)noexcept {
    return exitVehicle&&elapsedMs>=400&&elapsedMs<500;
}
// Only ordinary trigger input. The first shot can meet the one-shot hold;
// the second shot observes normal cycling. No reload, equip or state marker.
constexpr float PumpDiagnosticTrigger(std::uint64_t elapsedMs)noexcept {
    return (elapsedMs>=3000&&elapsedMs<3080)||(elapsedMs>=6000&&elapsedMs<6080)?1.f:0.f;
}
}
