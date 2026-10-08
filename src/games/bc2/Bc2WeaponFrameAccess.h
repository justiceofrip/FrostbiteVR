#pragma once
#include <cstdint>
namespace fvr::bc2 {
enum class WeaponFrameUse {Firing,Support,BodyObservation};
// Publication admission only. Native equipment, authored binding, deadline and
// concurrent invalidation checks must also succeed before returning a frame.
// Reload Fire suppression does not remove otherwise-current support geometry.
template<class Guard,class Pose>
bool WeaponFrameAdmitted(const Guard& guard,const Pose& pose,unsigned soldier,
    unsigned weak,unsigned weapon,WeaponFrameUse use)noexcept {
    return guard.valid && (use!=WeaponFrameUse::Firing||!guard.weaponActionsBlocked) &&
        (use!=WeaponFrameUse::Support||(guard.leftTracked&&pose.leftTracked)) &&
        guard.soldier==soldier&&guard.weak==weak&&guard.weapon==weapon&&
        pose.soldier==soldier&&pose.weak==weak&&pose.weapon==weapon&&
        pose.owner==guard.owner&&pose.space==guard.space&&
        pose.equipmentGeneration==guard.equipmentGeneration&&pose.generation<=guard.generation;
}
}
