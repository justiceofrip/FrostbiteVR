#pragma once
#include "fvr/math/StereoMath.h"
#include <array>
#include <cstdint>
#include <span>

namespace fvr::math {
// Optic-local coordinates in metres. +Z points from the eye through the optic;
// this is not an OpenXR-space pose. Transform EACH rendered eye back through the
// same submitted optic/skin transform before calling. No centre-eye surrogate.
struct AperturePoint { float x=0,y=0; };
struct OpticAperture {
    float z=0;
    std::array<AperturePoint,16> outline{};
    std::uint32_t count=0; // Ordered, strictly convex, either winding.
};
struct ReticleBounds { Vec3 minimum{},maximum{}; };
enum class ApertureVisibility : std::uint8_t { Invalid, Hidden, Visible, Partial };
struct AperturePlane {
    // Signed distance >= 0 is inside this eye's aperture cone.
    double x=0,y=0,z=0,w=0;
};
struct ApertureResult {
    ApertureVisibility visibility=ApertureVisibility::Invalid;
    std::array<AperturePlane,32> planes{};
    std::uint32_t planeCount=0;
    // Whole-draw rejection is safe only for Hidden. Partial needs fragment
    // clipping to guarantee confinement; retaining it is a conservative gate.
    bool CanSkipDraw()const noexcept{return visibility==ApertureVisibility::Hidden;}
};
// Classifies the conservative reticle box against up to two aperture cones.
// Invalid means preserve native rendering. Returns Hidden for an eye on/in
// front of the rear aperture. Stateless: neither eye inherits the other's gate.
ApertureResult EvaluateOpticAperture(std::span<const OpticAperture> apertures,
    ReticleBounds reticle,Vec3 eyeInOpticMeters)noexcept;
}
