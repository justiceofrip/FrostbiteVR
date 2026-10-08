#pragma once
#include "Bc2BoltControllerProbe.h"
#include "Bc2M95BoltCalibration.h"
namespace fvr::bc2 {
inline Bc2BoltCalibration M95ObservedBoltCalibration235(){
    auto c=M95AuthoredBoltCalibration(235);
    // Unchanged-callback prefire row31, m95-stock235-01; cross-checked against
    // stable early8 and settled2 rows. Authored grasp independently matches
    // same-frame native row56 (1.744mm/.005341rad, fingers .003224rad).
    c.profile.closedContact={{{{.99999994f,0.f,-2.08616257e-7f,0.f},{0.f,.999999642f,-2.38418579e-7f,0.f},
        {-2.08616257e-7f,-2.38418579e-7f,.999999702f,0.f},{.00521850586f,.0471191406f,-.205810547f,1.f}}}};
    c.nativeJoined=true;return c;
}
inline std::optional<Bc2BoltCalibration> M95PrivateBoltCalibration235(){
#ifdef FVR_BC2_MANUAL_BOLT_PROBE
    return M95ObservedBoltCalibration235();
#else
    return {};
#endif
}
// Independent ordinary activation guard; no finite diagnostic driver needed.
inline std::optional<Bc2BoltCalibration> M95OrdinaryBoltCalibration235(){
#ifdef FVR_BC2_ORDINARY_BOLT
    return M95ObservedBoltCalibration235();
#else
    return {};
#endif
}
inline bool BoltTrackingFresh(const Bc2BoltTracking& t,std::int64_t now)noexcept {
    return t.enabled&&t.calibration&&BoltCalibrationValid(*t.calibration)&&t.input.sequence&&t.nativeOwner.weapon>=0x10000&&
        t.input.owner.actor==((std::uint64_t(t.nativeOwner.weak)<<32)|t.nativeOwner.soldier)&&
        t.input.owner.actorGeneration==t.nativeOwner.actorGeneration&&t.input.owner.space==t.nativeOwner.space&&
        t.input.focused&&t.input.tracked[0]&&t.input.tracked[1]&&
        interaction::weapon_cycle_detail::Window(t.input.observedNs,t.input.deadlineNs,now);
}
inline bool BoltCustodyFresh(const Bc2BoltTracking& t,std::int64_t now)noexcept {
    if(!BoltTrackingFresh(t,now)||!t.weapon||!t.gun||
       (t.custody!=interaction::BoltCustodyPhase::Manipulating&&t.custody!=interaction::BoltCustodyPhase::Returned))return false;
    auto input=t.input;input.nowNs=now;return interaction::CurrentBoltCustodyWeaponTarget(*t.weapon,input,*t.gun);
}
inline bool BoltTargetFresh(const Bc2BoltTracking& t,std::int64_t now)noexcept {
    if(!BoltCustodyFresh(t,now)||!t.target||!t.held||!t.mechanism)return false;
    auto input=t.input;input.nowNs=now;
    return interaction::CurrentPhysicalWeaponCycleTarget(*t.target,*t.held,input,*t.mechanism,*t.gun,t.calibration->profile.hands);
}
inline bool BoltCustodyRetained(const Bc2BoltTracking& old,const Bc2BoltTracking& current,std::int64_t now)noexcept {
    if(!BoltCustodyFresh(old,now)||!BoltCustodyFresh(current,now)||old.nativeOwner!=current.nativeOwner||
       old.calibration!=current.calibration||old.input.sequence>current.input.sequence||old.gun->token!=current.gun->token)return false;
    if(!old.target)return true;
    if(!BoltTargetFresh(old,now)||!BoltTargetFresh(current,now)||old.mechanism->token!=current.mechanism->token)return false;
    auto input=current.input;input.nowNs=now;
    return interaction::CurrentPhysicalWeaponCycleTarget(*old.target,*current.held,input,*current.mechanism,*current.gun,current.calibration->profile.hands);
}
}
