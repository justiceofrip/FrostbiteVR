#pragma once
#include "fvr/interaction/SightFlip.h"
#include "fvr/interaction/TrackingMath.h"
#include <algorithm>
#include <cmath>
#include <optional>

namespace fvr::interaction {
namespace sight_grasp_detail {
inline bool Finite(math::Vec3 p)noexcept {
    return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z);
}
inline bool Finite(const math::Matrix4& m)noexcept {
    for(const auto& row:m.values)for(float value:row)if(!std::isfinite(value))return false;
    return true;
}
inline float Dot(math::Vec3 a,math::Vec3 b)noexcept{return a.x*b.x+a.y*b.y+a.z*b.z;}
inline bool Unit(math::Vec3 axis)noexcept {
    return Finite(axis)&&std::abs(Dot(axis,axis)-1.f)<.0001f;
}
inline bool Proper(const math::Matrix4& m)noexcept {
    if(!InverseAnimatedTransform(m))return false;
    const auto& a=m.values;
    const float det=a[0][0]*(a[1][1]*a[2][2]-a[1][2]*a[2][1])-
        a[0][1]*(a[1][0]*a[2][2]-a[1][2]*a[2][0])+
        a[0][2]*(a[1][0]*a[2][1]-a[1][1]*a[2][0]);
    // Animated transforms may be slightly non-orthogonal; reflections cannot
    // describe an orientation and must not silently enter a grasp binding.
    return det>0;
}
inline math::Vec3 Point(math::Vec3 p,const math::Matrix4& m)noexcept {
    return {p.x*m.values[0][0]+p.y*m.values[1][0]+p.z*m.values[2][0]+m.values[3][0],
        p.x*m.values[0][1]+p.y*m.values[1][1]+p.z*m.values[2][1]+m.values[3][1],
        p.x*m.values[0][2]+p.y*m.values[1][2]+p.z*m.values[2][2]+m.values[3][2]};
}
inline math::Matrix4 Hinge(math::Vec3 pivot,math::Vec3 axis,float radians)noexcept {
    const float length=std::sqrt(Dot(axis,axis));
    const float x=axis.x/length,y=axis.y/length,z=axis.z/length;
    const float c=std::cos(radians),s=std::sin(radians),t=1-c;
    math::Matrix4 out{};auto& m=out.values;
    // Row-vector Rodrigues rotation: positive angles use the same
    // axis dot cross(previous,current) convention as SightFlip.
    m[0]={c+x*x*t,x*y*t+z*s,x*z*t-y*s,0};
    m[1]={x*y*t-z*s,c+y*y*t,y*z*t+x*s,0};
    m[2]={x*z*t+y*s,y*z*t-x*s,c+z*z*t,0};
    m[3][3]=1;
    const auto rotated=Point(pivot,out);
    m[3][0]=pivot.x-rotated.x;m[3][1]=pivot.y-rotated.y;m[3][2]=pivot.z-rotated.z;
    return out;
}
}
// All matrices and points share one coordinate frame, normally weapon-local
// metres. This only returns a private visual transform; it never selects a
// native mode. Native geometry/ownership verification belongs to the adapter.
[[nodiscard]] inline std::optional<math::Matrix4> RotateAboutHinge(
    const math::Matrix4& source,math::Vec3 pivotMeters,math::Vec3 unitAxis,float radians)noexcept {
    using namespace sight_grasp_detail;
    if(!Proper(source)||!Finite(pivotMeters)||!Unit(unitAxis)||!std::isfinite(radians))return {};
    const auto result=Multiply(source,Hinge(pivotMeters,unitAxis,radians));
    if(!Finite(result))return {};
    return result;
}
struct SightGraspPose {
    math::Matrix4 sight{},wrist{};
    math::Vec3 graspPoint{};
    float appliedRadians=0;
};
// Immutable visual attachment latched from one coherent grab. Evaluate always
// starts from the captured matrices, so repeated rendering cannot accumulate
// drift. The adapter owns contact selection, phase, expiry and native handoff.
class SightGraspBinding {
public:
    [[nodiscard]] static std::optional<SightGraspBinding> Begin(
        math::Vec3 pivotMeters,math::Vec3 unitAxis,
        const math::Matrix4& sightAtGrab,const math::Matrix4& wristAtGrab,
        math::Vec3 graspPointWeapon,math::Vec3 palmPointWrist,
        float maxTravelRadians=1.5707963267948966f)noexcept {
        using namespace sight_grasp_detail;
        if(!Finite(pivotMeters)||!Unit(unitAxis)||!Proper(sightAtGrab)||!Proper(wristAtGrab)||
           !Finite(graspPointWeapon)||!Finite(palmPointWrist)||!std::isfinite(maxTravelRadians)||
           maxTravelRadians<=0||maxTravelRadians>1.5707963267948966f)return {};
        SightGraspBinding out;
        out.pivot_=pivotMeters;out.axis_=unitAxis;out.sight_=sightAtGrab;out.wrist_=wristAtGrab;
        out.grasp_=graspPointWeapon;out.palm_=palmPointWrist;out.maxTravel_=maxTravelRadians;
        // Also reject finite inputs whose point/matrix arithmetic overflows.
        if(!out.Evaluate(0,SightMode::Primary))return {};
        return out;
    }
    [[nodiscard]] std::optional<SightGraspPose> Evaluate(float signedRadians,SightMode startMode)const noexcept {
        using namespace sight_grasp_detail;
        if(!std::isfinite(signedRadians)||(startMode!=SightMode::Primary&&startMode!=SightMode::Secondary))return {};
        SightGraspPose out;
        out.appliedRadians=startMode==SightMode::Primary?
            std::clamp(signedRadians,0.f,maxTravel_):std::clamp(signedRadians,-maxTravel_,0.f);
        const auto hinge=Hinge(pivot_,axis_,out.appliedRadians);
        out.sight=Multiply(sight_,hinge);out.wrist=Multiply(wrist_,hinge);
        out.graspPoint=Point(grasp_,hinge);
        // Keep the captured wrist orientation, including roll, while its
        // authored palm landmark stays exactly on the selected frame point.
        // The wrist origin itself is deliberately not snapped onto the frame.
        const auto palm=Point(palm_,out.wrist);
        out.wrist.values[3][0]+=out.graspPoint.x-palm.x;
        out.wrist.values[3][1]+=out.graspPoint.y-palm.y;
        out.wrist.values[3][2]+=out.graspPoint.z-palm.z;
        if(!Finite(out.sight)||!Finite(out.wrist)||!Finite(out.graspPoint))return {};
        return out;
    }
private:
    SightGraspBinding()=default;
    math::Vec3 pivot_{},axis_{},grasp_{},palm_{};float maxTravel_=1.5707963267948966f;
    math::Matrix4 sight_{},wrist_{};
};
}
