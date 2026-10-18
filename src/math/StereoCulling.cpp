#include "fvr/math/StereoCulling.h"
#include "fvr/interaction/TrackingMath.h"
#include <algorithm>
#include <cmath>
#include <limits>
namespace fvr::math {
FloatPlaneContainment MeasureFloatPlaneContainment(const Vec4& plane,const Vec4& point,
    double absoluteTolerance)noexcept {
    FloatPlaneContainment out{};
    if(!std::isfinite(absoluteTolerance)||absoluteTolerance<0||point.w!=1)return out;
    for(float value:{plane.x,plane.y,plane.z,plane.w,point.x,point.y,point.z})
        if(!std::isfinite(value))return out;
    const double x=double(plane.x)*point.x,y=double(plane.y)*point.y,z=double(plane.z)*point.z;
    const double magnitude=std::abs(x)+std::abs(y)+std::abs(z)+std::abs(double(plane.w));
    // A float plane test has three multiplies and three adds. Its standard
    // forward-error bound is gamma(6)*sum(abs(terms)), u=epsilon/2. Using double
    // here avoids adding another float cancellation to the observation. Include
    // the much smaller double evaluation bound and gradual-underflow term too.
    // This is a resolution allowance, not evidence that geometry outside it is
    // covered. In particular a fixed 1mm-only test is below two float ULPs at
    // BC2's observed (9999,9999,9999) loading-camera origin.
    if(magnitude>std::numeric_limits<float>::max())return out;
    constexpr double uf=std::numeric_limits<float>::epsilon()*.5;
    constexpr double ud=std::numeric_limits<double>::epsilon()*.5;
    constexpr double gammaFloat=(6*uf)/(1-6*uf),gammaDouble=(6*ud)/(1-6*ud);
    out.distance=x+y+z+double(plane.w);
    out.roundoffAllowance=(gammaFloat+gammaDouble)*magnitude+6*double(std::numeric_limits<float>::denorm_min());
    out.excess=(std::max)(0.,out.distance-absoluteTolerance-out.roundoffAllowance);
    out.valid=true;out.inside=out.excess==0;return out;
}
std::optional<StereoCullEnvelope> EncloseStereoFrusta(const Matrix4& center,
    const std::array<Matrix4,2>& eyes,const std::array<FovTangents,2>& fov,float nearPlane,float farPlane)noexcept {
    const auto inverse=interaction::InverseAnimatedTransform(center);if(!inverse)return {};
    float closest=std::numeric_limits<float>::max(),farthest=0,horizontal=0,vertical=0;
    for(unsigned eye=0;eye<2;++eye){
        if(!interaction::InverseRigid(eyes[eye])||!MakeLhProjectionFromFovTangents(fov[eye],nearPlane,farPlane))return {};
        const auto relative=interaction::Multiply(eyes[eye],*inverse);
        // Every frustum is convex. With positive depth, the extrema of x/z and
        // y/z occur at vertices, so all six enclosing planes cover its interior.
        for(float depth:{nearPlane,farPlane})for(float x:{fov[eye].left,fov[eye].right})for(float y:{fov[eye].down,fov[eye].up}){
            const auto point=TransformRowVector({x*depth,y*depth,depth,1},relative);
            if(!std::isfinite(point.x)||!std::isfinite(point.y)||!std::isfinite(point.z)||point.z<=0)return {};
            closest=(std::min)(closest,point.z);farthest=(std::max)(farthest,point.z);
            horizontal=(std::max)(horizontal,std::abs(point.x/point.z));vertical=(std::max)(vertical,std::abs(point.y/point.z));
        }
    }
    // Outward margin absorbs float matrix composition/plane normalization error.
    horizontal=horizontal*1.0001f+.0001f;vertical=vertical*1.0001f+.0001f;
    StereoCullEnvelope out{center,{-horizontal,horizontal,vertical,-vertical},closest*.9999f,farthest*1.0001f};
    if(!MakeLhProjectionFromFovTangents(out.fov,out.nearPlane,out.farPlane))return {};return out;
}
}
