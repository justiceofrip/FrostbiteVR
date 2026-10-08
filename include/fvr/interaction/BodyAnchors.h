#pragma once
#include "fvr/interaction/AmmoSupply.h"
#include "fvr/interaction/ControllerInput.h"
#include "fvr/interaction/SupportGrip.h"
#include <cmath>

namespace fvr::interaction {
// Comfort geometry in metres relative to upright recentered body forward.
// +X right, +Y up, +Z forward after the existing OpenXR-to-LH conversion.
// Not an inferred animated torso or weapon socket. All contacts use original
// controller poses in the same XR space; no cached world location survives recenter.
struct BodyAnchor {std::uint32_t slot=0;math::Vec3 center{};float radius=.16f;};
struct BodyAnchorConfig {
    BodyAnchor chest{3,{-.16f,-.25f,.10f},.16f};
    std::array<BodyAnchor,2> shoulders{{{1,{-.20f,-.12f,-.18f},.17f},{2,{.20f,-.12f,-.18f},.17f}}};
};
inline bool ValidBodyAnchor(const BodyAnchor& a)noexcept {
    return a.slot&&std::isfinite(a.center.x)&&std::isfinite(a.center.y)&&std::isfinite(a.center.z)&&
        std::hypot(a.center.x,a.center.y,a.center.z)<=1.5f&&std::isfinite(a.radius)&&a.radius>=.03f&&a.radius<=.35f;
}
inline bool ValidBodyAnchors(const BodyAnchorConfig& c)noexcept {
    return ValidBodyAnchor(c.chest)&&ValidBodyAnchor(c.shoulders[0])&&ValidBodyAnchor(c.shoulders[1])&&
        c.chest.slot!=c.shoulders[0].slot&&c.chest.slot!=c.shoulders[1].slot&&c.shoulders[0].slot!=c.shoulders[1].slot;
}
inline std::optional<math::Matrix4> BodyAnchorHandPose(const InputFrame& input,InteractionHand hand)noexcept {
    const auto index=static_cast<unsigned>(hand);
    if(index>1||!ValidInput(input)||!input.focused||!input.headValid||!input.hands[index].gripTracked)return {};
    auto body=UprightReference(input.referenceHead);if(!body)return {};
    body->position=input.head.position; // Looking over a shoulder must not move it.
    const auto relative=math::MakeRelativePose(*body,input.hands[index].grip);if(!relative)return {};
    const auto view=math::MakeLhViewFromOpenXRPose(*relative);if(!view)return {};
    return InverseRigid(*view);
}
inline bool BodyAnchorContains(const BodyAnchor& anchor,const math::Matrix4& hand)noexcept {
    const auto& p=hand.values[3];return ValidBodyAnchor(anchor)&&std::isfinite(p[0])&&std::isfinite(p[1])&&std::isfinite(p[2])&&
        std::hypot(p[0]-anchor.center.x,p[1]-anchor.center.y,p[2]-anchor.center.z)<=anchor.radius;
}
inline AmmoSupplyConfig ChestAmmoSupply(const BodyAnchorConfig& c={})noexcept {
    // Identity changes with future persisted anchor edits; invalid geometry is
    // rejected by AmmoSupply itself and by the adapter's enable operation.
    // Body inventory adds a chest contact without removing belt access. The
    // belt uses the established local dimensions in THIS recentered body frame;
    // it is not the old head-yaw-following pouch's exact world position.
    AmmoSupplyConfig supply{InteractionHand::Left,0x424332414d4d4full,{0x42433243485354ull,1},
        {c.chest.center.x,c.chest.center.y,c.chest.center.z},c.chest.radius,200000000};
    supply.alternateContact=AmmoSupplyContact{{-.23f,-.55f,.02f},.18f};
    return supply;
}
}
