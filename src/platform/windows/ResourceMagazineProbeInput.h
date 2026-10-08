#pragma once
#include "fvr/interaction/ControllerInput.h"
namespace fvr::probe {
inline constexpr unsigned ResourceMagazineScheduleVersion=2;
inline constexpr unsigned ResourceMagazineHeldScheduleVersion=3;
// Neutral start is explicitly mode7-only; setup ownership is independently
// verified before attachment. It never selects or makes a native item Ready.
inline constexpr bool ValidResourceMagazineStartHeld(bool startHeld,bool resourceInventory,
    bool exitVehicle,bool directionExplicit)noexcept {
    return !startHeld||(resourceInventory&&!exitVehicle&&!directionExplicit);
}
// Caller must independently verify vehicle occupancy and intended slot route.
// This diagnostic emits only existing controller Use/selection actions. It
// proves neither that the exit/selection occurred nor that a resource is Ready.
struct ResourceMagazineSetup {
    bool use=false;float select=0;
};
inline ResourceMagazineSetup ResourceMagazineSchedule(std::uint64_t elapsedMs,bool exitVehicle,float direction,bool startHeld=false)noexcept {
    if(startHeld||(direction!=1&&direction!=-1))return {};
    // Leaving a vehicle starts empty-handed under body inventory. The first
    // selection draws its already selected item; only the later neutral-edge
    // pulse changes the slot. Ordinary on-foot setup retains its single pulse.
    const bool select=(elapsedMs>=1000&&elapsedMs<1800)||(exitVehicle&&elapsedMs>=3000&&elapsedMs<3300);
    return {exitVehicle&&elapsedMs>=400&&elapsedMs<500,select?direction:0.f};
}
inline void ResourceMagazineInput(interaction::InputFrame& in,std::uint64_t elapsedMs,bool exitVehicle,float direction,bool startHeld=false)noexcept {
    const auto step=ResourceMagazineSchedule(elapsedMs,exitVehicle,direction,startHeld);
    in.hands[0].held=step.use?interaction::Primary:0;in.hands[1].stickY=step.select;
}
}
