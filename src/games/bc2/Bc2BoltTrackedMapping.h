#pragma once
#include "Bc2BoltPart.h"
#include "fvr/interaction/ControllerInput.h"
namespace fvr::bc2 {
struct Bc2BoltControllerContact {
    Bc2BoltRawContact raw{};
    math::Matrix4 bodyWorldMeters{},weaponWorldMeters{};
    std::array<math::Matrix4,2> rawWristWorldMeters{},wristToGrip{},nativeWristInWeapon{};
    bool mappingValid=false;float units=1;
    math::Matrix4 nativePartInWeapon{};bool nativePartValid=false;
};
struct Bc2BoltPackCounters {unsigned contacts=0,poses=0,copies=0,pairs=0,fallbacks=0;};
// The caller supplies its CURRENT reference-to-native-world anchor after any
// snap turn or roomscale compensation. This is not an old renderer anchor
// relabeled with a new generation. Original contact remains separately dated.
struct Bc2BoltBodyFrame {
    ReloadStateOwner owner{};interaction::HandInteractionSample input{};
    math::Pose referenceHead{};math::Matrix4 bodyWorldMeters{};float units=1;
};
std::optional<std::array<math::Matrix4,2>> MapBoltTrackedWrists(const Bc2BoltControllerContact&,
    const interaction::InputFrame&,const interaction::HandInteractionSample&,const Bc2BoltBodyFrame&)noexcept;
// Preserve the original same-frame contact attachment through an updated
// world anchor; roomscale/snap turning cannot become a new arbitrary grasp.
std::optional<math::Matrix4> ReprojectBoltCustodyWeapon(const Bc2BoltControllerContact&,
    const std::array<math::Matrix4,2>& currentWrists,interaction::InteractionHand receiving)noexcept;
}
