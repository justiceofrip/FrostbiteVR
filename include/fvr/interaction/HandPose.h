#pragma once
#include "fvr/interaction/RigPose.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

namespace fvr::interaction {
enum class HandPoseRole : std::uint8_t {Free,WeaponSupport,MechanismGrip};
enum class HandFinger : std::uint8_t {Thumb,Index,Middle,Ring,Little};
constexpr std::size_t HandFingerCount=5;
constexpr std::size_t HandJointsPerFinger=4;
struct HandJointBinding {
    std::uint32_t index=0;
    // Axis in this joint's reference-local coordinates, with a verified sign.
    math::Vec3 curlAxisLocal{};
    // Additive orientation deltas from the supplied reference. Translation is
    // always retained; adapters supply actual authored ranges, not game names.
    float openRadians=0,closedRadians=0,pinchRadians=0;
};
struct HandFingerBinding {
    std::array<HandJointBinding,HandJointsPerFinger> joints{};
    std::uint8_t count=0;
};
struct HandPoseBinding {
    std::uint32_t wrist=0;
    // Thumb, index, middle, ring, little. A missing finger has count zero.
    std::array<HandFingerBinding,HandFingerCount> fingers{};
};
struct HandPoseTargets {
    HandPoseRole role=HandPoseRole::Free;
    std::array<float,HandFingerCount> curl{};
    float pinch=0; // Independent normalized blend; each joint supplies its range.
};
struct HandPose {std::vector<BoneWrite> writes;};

namespace hand_pose_detail {
inline bool Finite(math::Vec3 p)noexcept {
    return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z);
}
inline float Dot(math::Vec3 a,math::Vec3 b)noexcept{return a.x*b.x+a.y*b.y+a.z*b.z;}
inline bool Proper(const math::Matrix4& m)noexcept {
    if(!InverseAnimatedTransform(m))return false;
    const auto& a=m.values;
    return a[0][0]*(a[1][1]*a[2][2]-a[1][2]*a[2][1])-
        a[0][1]*(a[1][0]*a[2][2]-a[1][2]*a[2][0])+
        a[0][2]*(a[1][0]*a[2][1]-a[1][1]*a[2][0])>0;
}
inline math::Matrix4 Rotation(math::Vec3 axis,float radians)noexcept {
    const float length=std::sqrt(Dot(axis,axis));
    const float x=axis.x/length,y=axis.y/length,z=axis.z/length;
    const float c=std::cos(radians),s=std::sin(radians),t=1-c;
    math::Matrix4 out{};out.values[0]={c+x*x*t,x*y*t+z*s,x*z*t-y*s,0};
    out.values[1]={x*y*t-z*s,c+y*y*t,y*z*t+x*s,0};
    out.values[2]={x*z*t+y*s,y*z*t-x*s,c+z*z*t,0};out.values[3][3]=1;return out;
}
}
// Pure private-pose generation; this never writes a source palette or selects
// interaction roles. All transforms share a coordinate frame and unit scale.
//
// WeaponSupport preserves the current authored hand shape. Free/MechanismGrip
// use a supplied bind/reference shape, preventing an unrelated weapon animation
// from defining their finger curl. They deliberately have no guessed default
// curl or joint axes. The adapter selects targets and the reference.
//
// Bound chains must be direct wrist -> joint -> joint chains; incomplete
// mappings fail instead of rotating an unverified intermediary. Unbound
// descendants retain their current local relation to their newly posed parent.
// The wrist is the exact supplied target; all edits stay inside its subtree.
[[nodiscard]] inline std::optional<HandPose> GenerateHandPose(
    std::span<const std::int32_t> parents,
    std::span<const math::Matrix4> referenceWorld,
    std::span<const math::Matrix4> currentWorld,
    const HandPoseBinding& binding,const math::Matrix4& targetWrist,
    const HandPoseTargets& targets) {
    using namespace hand_pose_detail;
    constexpr float pi=3.1415926535897932f;
    const auto size=parents.size();
    const bool support=targets.role==HandPoseRole::WeaponSupport;
    if(!size||size>1024||currentWorld.size()!=size||binding.wrist>=size||
       (!support&&referenceWorld.size()!=size)||!Proper(targetWrist)||
       (targets.role!=HandPoseRole::Free&&!support&&targets.role!=HandPoseRole::MechanismGrip))return {};
    const auto normalized=[](float value){return std::isfinite(value)&&value>=0&&value<=1;};
    if(!normalized(targets.pinch))return {};
    for(float curl:targets.curl)if(!normalized(curl))return {};
    // Validate the whole graph once, including cycles outside the hand.
    std::vector<bool> member(size,false);
    for(std::size_t n=0;n<size;++n){
        auto at=std::int32_t(n);std::size_t steps=0;
        while(at!=-1){
            if(at<0||std::size_t(at)>=size||++steps>size)return {};
            if(std::uint32_t(at)==binding.wrist)member[n]=true;
            at=parents[at];
        }
    }
    std::vector<const HandJointBinding*> joints(size,nullptr);
    std::vector<float> angles(size,0);
    bool anyJoint=false;
    for(std::size_t finger=0;finger<HandFingerCount;++finger){
        const auto& chain=binding.fingers[finger];
        if(chain.count>chain.joints.size())return {};
        auto previous=binding.wrist;
        for(unsigned n=0;n<chain.count;++n){
            const auto& joint=chain.joints[n];
            if(joint.index>=size||joint.index==binding.wrist||joints[joint.index]||
               parents[joint.index]!=std::int32_t(previous)||
               !Finite(joint.curlAxisLocal)||std::abs(Dot(joint.curlAxisLocal,joint.curlAxisLocal)-1.f)>=.0001f)return {};
            for(float value:{joint.openRadians,joint.closedRadians,joint.pinchRadians})
                if(!std::isfinite(value)||std::abs(value)>pi)return {};
            const float angle=joint.openRadians+(joint.closedRadians-joint.openRadians)*targets.curl[finger]+
                joint.pinchRadians*targets.pinch;
            if(!std::isfinite(angle)||std::abs(angle)>pi)return {};
            joints[joint.index]=&joint;angles[joint.index]=angle;previous=joint.index;anyJoint=true;
        }
    }
    if(!support&&!anyJoint)return {};
    std::vector<math::Matrix4> currentInverse(size),referenceInverse(size);
    for(std::size_t n=0;n<size;++n)if(member[n]){
        if(!Proper(currentWorld[n]))return {};
        currentInverse[n]=*InverseAnimatedTransform(currentWorld[n]);
        if(!support&&(n==binding.wrist||joints[n])){
            if(!Proper(referenceWorld[n]))return {};
            referenceInverse[n]=*InverseAnimatedTransform(referenceWorld[n]);
        }
    }
    if(support){
        // Use the accepted single wrist-delta path. In particular a native
        // wrist target is an exact no-op, including its current finger shape.
        if(targetWrist.values==currentWorld[binding.wrist].values){
            HandPose out;
            for(std::size_t n=0;n<size;++n)if(member[n])out.writes.push_back({std::uint32_t(n),currentWorld[n]});
            return out;
        }
        const auto writes=RetargetRigSubtree(parents,currentWorld,binding.wrist,targetWrist);
        if(!writes)return {};
        HandPose out{*writes};
        for(auto& write:out.writes)if(write.index==binding.wrist)write.transform=targetWrist;
        return out;
    }
    std::vector<math::Matrix4> placed(size);
    std::vector<bool> completed(size,false);
    placed[binding.wrist]=targetWrist;completed[binding.wrist]=true;
    HandPose out;out.writes.reserve(size);out.writes.push_back({binding.wrist,targetWrist});
    std::size_t remaining=0;for(std::size_t n=0;n<size;++n)if(member[n]&&n!=binding.wrist)++remaining;
    while(remaining){
        bool progressed=false;
        for(std::size_t n=0;n<size;++n){
            if(!member[n]||completed[n])continue;
            const auto parent=std::size_t(parents[n]);if(!completed[parent])continue;
            math::Matrix4 local;
            if(!support&&joints[n]){
                local=Multiply(referenceWorld[n],referenceInverse[parent]);
                local=Multiply(Rotation(joints[n]->curlAxisLocal,angles[n]),local);
            }else local=Multiply(currentWorld[n],currentInverse[parent]);
            placed[n]=Multiply(local,placed[parent]);
            if(!Proper(placed[n]))return {};
            completed[n]=true;progressed=true;--remaining;
            out.writes.push_back({std::uint32_t(n),placed[n]});
        }
        if(!progressed)return {};
    }
    return out;
}
}
