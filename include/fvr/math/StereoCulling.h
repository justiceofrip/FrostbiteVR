#pragma once
#include "fvr/math/StereoMath.h"
#include <array>
#include <optional>
namespace fvr::math {
// Canonical LH row-vector coordinates, distances in the caller's world units.
// A conservative symmetric volume enclosing both complete eye frusta. Adapters
// with native multiview culling may instead build independent eye visibility.
struct StereoCullEnvelope {
    Matrix4 world{};
    FovTangents fov{};
    float nearPlane=0,farPlane=0;
};
// Diagnostic evidence for planes and points stored by a float renderer. This
// never changes a frustum. The adapter supplies its plane in the same handedness
// as the point and independently checks the plane's normal is unit length.
struct FloatPlaneContainment {
    bool valid=false,inside=false;
    double distance=0,roundoffAllowance=0,excess=0;
};
FloatPlaneContainment MeasureFloatPlaneContainment(const Vec4& plane,const Vec4& point,
    double absoluteTolerance=.001)noexcept;
std::optional<StereoCullEnvelope> EncloseStereoFrusta(const Matrix4& centerCamera,
    const std::array<Matrix4,2>& eyeCameras,const std::array<FovTangents,2>& eyeFov,
    float nearPlane,float farPlane) noexcept;
}
