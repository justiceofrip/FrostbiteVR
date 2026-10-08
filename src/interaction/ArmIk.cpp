#include "fvr/interaction/ArmIk.h"
#include <algorithm>
#include <cmath>
namespace fvr::interaction {
namespace {
using V=math::Vec3;using M=math::Matrix4;
V Add(V a,V b){return {a.x+b.x,a.y+b.y,a.z+b.z};}
V Sub(V a,V b){return {a.x-b.x,a.y-b.y,a.z-b.z};}
V Scale(V a,float s){return {a.x*s,a.y*s,a.z*s};}
float Dot(V a,V b){return a.x*b.x+a.y*b.y+a.z*b.z;}
V Cross(V a,V b){return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
float Length(V a){return std::sqrt(Dot(a,a));}
bool Finite(V a){return std::isfinite(a.x)&&std::isfinite(a.y)&&std::isfinite(a.z);}
V Position(const M& m){return {m.values[3][0],m.values[3][1],m.values[3][2]};}
void SetPosition(M& m,V p){m.values[3][0]=p.x;m.values[3][1]=p.y;m.values[3][2]=p.z;}
V Perpendicular(V a,V direction){return Sub(a,Scale(direction,Dot(a,direction)));}
V Fallback(V axis){const V basis=std::abs(axis.x)<.6f?V{1,0,0}:V{0,1,0};auto p=Perpendicular(basis,axis);return Scale(p,1/Length(p));}
V TransformVector(V v,const M& m){return {v.x*m.values[0][0]+v.y*m.values[1][0]+v.z*m.values[2][0],v.x*m.values[0][1]+v.y*m.values[1][1]+v.z*m.values[2][1],v.x*m.values[0][2]+v.y*m.values[1][2]+v.z*m.values[2][2]};}
// The minimum rotation keeps authored twist. Antiparallel directions have no
// unique minimum axis; choose the supplied elbow plane deterministically.
M SegmentDelta(V from,V to,V sourcePivot,V targetPivot,V pole){
    from=Scale(from,1/Length(from));to=Scale(to,1/Length(to));
    const float cosine=std::clamp(Dot(from,to),-1.f,1.f);auto axis=Cross(from,to);float sine=Length(axis);
    if(sine>1e-6f)axis=Scale(axis,1/sine);
    else if(cosine<0){axis=Perpendicular(pole,from);axis=Length(axis)>1e-6f?Scale(axis,1/Length(axis)):Fallback(from);sine=0;}
    else {axis={1,0,0};sine=0;}
    M out{};out.values[3][3]=1;
    const std::array<V,3> basis{V{1,0,0},V{0,1,0},V{0,0,1}};
    for(unsigned n=0;n<3;++n){const auto rotated=Add(Add(Scale(basis[n],cosine),Scale(Cross(axis,basis[n]),sine)),Scale(axis,Dot(axis,basis[n])*(1-cosine)));
        out.values[n][0]=rotated.x;out.values[n][1]=rotated.y;out.values[n][2]=rotated.z;}
    SetPosition(out,Sub(targetPivot,TransformVector(sourcePivot,out)));return out;
}
bool Below(std::span<const std::int32_t> parents,unsigned at,unsigned root){
    for(auto p=std::int32_t(at);p!=-1;p=parents[p])if(unsigned(p)==root)return true;return false;
}
}
std::optional<ArmPose> SolveTrackedArms(std::span<const std::int32_t> parents,
    std::span<const math::Matrix4> native,ArmJoints left,ArmJoints right,
    const ArmTarget& leftTarget,const ArmTarget& rightTarget){
    if(parents.size()!=native.size()||!ValidateArms(parents,left,right)||Below(parents,left.shoulder,right.shoulder)||Below(parents,right.shoulder,left.shoulder))return {};
    // Only arm branches participate. Unrelated native geometry may be hidden
    // by a collapsed transform; it is neither read nor written by this solver.
    for(unsigned n=0;n<native.size();++n)if((Below(parents,n,left.shoulder)||Below(parents,n,right.shoulder))&&!InverseAnimatedTransform(native[n]))return {};
    ArmPose result;result.writes.reserve(native.size());
    const std::array<ArmJoints,2> joints{left,right};const std::array<ArmTarget,2> targets{leftTarget,rightTarget};
    for(unsigned side=0;side<2;++side){const auto j=joints[side];const auto& target=targets[side];if(!target.enabled)continue;
        if(!InverseRigid(target.wrist)||!Finite(target.poleDirection)||!std::isfinite(Dot(target.poleDirection,target.poleDirection)))return {};
        const auto nativeShoulder=Position(native[j.shoulder]),shoulder=target.shoulder.value_or(nativeShoulder),elbow=Position(native[j.elbow]),wrist=Position(native[j.wrist]),desired=Position(target.wrist);
        if(!Finite(shoulder))return {};
        const float upper=Length(Sub(elbow,nativeShoulder)),lower=Length(Sub(wrist,elbow)),total=upper+lower;
        if(!std::isfinite(total)||upper<1e-5f||lower<1e-5f)return {};
        const float epsilon=std::max(1e-6f,total*1e-4f);
        const float minimum=std::abs(upper-lower)+epsilon,maximum=total-epsilon;if(minimum>=maximum)return {};
        auto direction=Sub(desired,shoulder);const float requested=Length(direction);if(!std::isfinite(requested))return {};
        if(requested>epsilon)direction=Scale(direction,1/requested);
        else {direction=Sub(wrist,nativeShoulder);const auto distance=Length(direction);direction=distance>epsilon?Scale(direction,1/distance):V{0,0,1};}
        const float distance=std::clamp(requested,minimum,maximum);result.reachClamped[side]=requested<minimum||requested>maximum;
        auto pole=Perpendicular(target.poleDirection,direction);
        if(Length(pole)<1e-5f)pole=Perpendicular(Sub(elbow,nativeShoulder),direction);
        pole=Length(pole)>1e-5f?Scale(pole,1/Length(pole)):Fallback(direction);
        const float along=(upper*upper-lower*lower+distance*distance)/(2*distance),height=std::sqrt(std::max(0.f,upper*upper-along*along));
        const auto newElbow=Add(shoulder,Add(Scale(direction,along),Scale(pole,height))),newWrist=Add(shoulder,Scale(direction,distance));
        const auto upperDelta=SegmentDelta(Sub(elbow,nativeShoulder),Sub(newElbow,shoulder),nativeShoulder,shoulder,pole);
        const auto lowerDelta=SegmentDelta(Sub(wrist,elbow),Sub(newWrist,newElbow),elbow,newElbow,pole);
        auto placedWrist=target.wrist;SetPosition(placedWrist,newWrist);
        const auto wristDelta=Multiply(*InverseAnimatedTransform(native[j.wrist]),placedWrist);
        result.targetError[side]=Length(Sub(newWrist,desired));
        for(unsigned n=0;n<parents.size();++n){if(!Below(parents,n,j.shoulder))continue;
            const auto& delta=Below(parents,n,j.wrist)?wristDelta:(Below(parents,n,j.elbow)?lowerDelta:upperDelta);
            auto transformed=Multiply(native[n],delta);if(!InverseAnimatedTransform(transformed))return {};
            result.writes.push_back({n,transformed});
        }
    }
    return result;
}
}
