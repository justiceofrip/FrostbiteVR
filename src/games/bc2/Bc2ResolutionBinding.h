#pragma once
#include "Bc2Profile.h"
#include "Bc2MenuStartup.h"
#include <array>
#include <string_view>

namespace fvr::bc2 {
struct ResolutionSize {
    float width=0,height=0;
    bool operator==(const ResolutionSize&)const=default;
};
constexpr bool ValidResolutionSize(ResolutionSize s)noexcept {
    return s.width>=1&&s.height>=1&&s.width<=16384&&s.height<=16384&&
        s.width==float(unsigned(s.width))&&s.height==float(unsigned(s.height))&&
        double(s.width)*s.height<=32000000;
}
struct ResolutionBinding {
    DiscoveryProfile renderer{};
    MenuStartupBinding menu{};
    std::uint32_t registryGlobal=0,setWindowSize=0,setFullscreen=0,getField=0,applySettings=0;
    std::uint32_t optionsGlobal=0,optionsVtable=0,setOptionsSize=0,setOptionsFullscreen=0;
    std::uint32_t dispatcher=0,managerVtable=0;
};
// Original WindowSize float2 setter and renderer-owned reset path. No direct
// renderer/target writes. Every code span must also match the loaded image.
std::optional<ResolutionBinding> DiscoverResolutionBinding(std::span<const std::byte>,const engine::PeImage&);
std::array<MenuStateCodeSpan,24> ResolutionCodeSpans()noexcept;
struct ResolutionSnapshot {
    std::uint32_t manager=0,menu=0,renderer=0,settings=0,registry=0,swapchain=0,options=0;
    ResolutionSize desired{},optionsDesired{};
    std::uint32_t width=0,height=0;
    bool fullscreen=false,optionsFullscreen=false,optionsDirty=false;
    bool SameOwner(const ResolutionSnapshot& other)const noexcept {
        return manager==other.manager&&menu==other.menu&&renderer==other.renderer&&
            settings==other.settings&&registry==other.registry&&swapchain==other.swapchain&&options==other.options;
    }
    bool operator==(const ResolutionSnapshot&)const=default;
};
// Both the renderer registry and upstream graphics options own these settings.
// A later native Options Apply must not restore a stale desktop resolution.
bool ResolutionSettingsMatch(const ResolutionSnapshot&,ResolutionSize,bool fullscreen)noexcept;
bool CanRestoreResolutionSettings(const ResolutionSnapshot& current,const ResolutionSnapshot& original,ResolutionSize target)noexcept;
// Read-only original front-end + renderer/options ownership, checked twice.
std::optional<ResolutionSnapshot> ReadResolutionSnapshot(const MenuMemory&,const ResolutionBinding&,std::uint32_t imageBase);
struct ResolutionGuardOwner {
    std::uint32_t manager=0,renderer=0,options=0,settings=0,registry=0;
    bool operator==(const ResolutionGuardOwner&)const=default;
};
enum class ResolutionGuardPhase : unsigned {Inactive,Prepared,Active,Releasing,Released};
constexpr bool BlockResolutionEvent(std::string_view event,ResolutionGuardPhase phase,bool leaseAlive,
    const ResolutionGuardOwner& expected,const ResolutionGuardOwner& current)noexcept {
    return event=="changeResolution"&&leaseAlive&&
        (phase==ResolutionGuardPhase::Prepared||phase==ResolutionGuardPhase::Active)&&
        expected.manager>=0x10000&&expected.renderer>=0x10000&&expected.options>=0x10000&&
        expected.settings>=0x10000&&expected.registry>=0x10000&&expected==current;
}
// Bounded identity-only validation independent of main/pause menu or current
// dimensions: native resize is asynchronous and Continue replaces menu objects.
std::optional<ResolutionGuardOwner> ReadResolutionGuardOwner(const MenuMemory&,const ResolutionBinding&,std::uint32_t imageBase);
}
