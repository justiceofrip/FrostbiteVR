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
// One Exit pulse after the harness independently proves checkpoint replacement
// and vehicle occupancy. The scene driver owns ordinary shoulder gestures;
// legacy stick selection must not compete with empty-hand recovery.
class ResourceSceneResume {
public:
    bool Accept(std::uint64_t id,std::uint64_t issued,std::uint64_t deadline,std::uint64_t now)noexcept {
        if(accepted_||failed_||id!=1||!issued||issued>now||deadline<=now||
           deadline<=issued||deadline-issued>1000)return false;
        accepted_=true;began_=last_=now;return true;
    }
    ResourceMagazineSetup Step(std::uint64_t now)noexcept {
        if(!accepted_||failed_)return {};
        if(now<last_){failed_=true;return {};}
        last_=now;auto step=ResourceMagazineSchedule(now-began_,true,1);step.select=0;return step;
    }
    bool Accepted()const noexcept{return accepted_;}
    bool Failed()const noexcept{return failed_;}
    std::uint64_t Began()const noexcept{return began_;}
private:
    bool accepted_=false,failed_=false;std::uint64_t began_=0,last_=0;
};
}
