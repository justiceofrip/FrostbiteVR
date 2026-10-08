#pragma once
#include "Bc2SightContact.h"
#include "fvr/interaction/HandPose.h"
#include "fvr/interaction/FingerCurlOverlay.h"
#include <array>
#include <limits>

namespace fvr::bc2 {
struct Bc2HandBinding {
    interaction::HandPoseBinding pose;
    bool rightHand=false;
    // Canonical bind worlds in the supplied native unit scale. Only the selected
    // hand subtree is populated; unrelated native/hidden branches stay unused.
    std::vector<math::Matrix4> referenceWorld;
    math::Matrix4 wristToGrip{};
    math::Vec3 mechanismPointWristMeters{};
};
inline interaction::HandPoseTargets LeftHandTargets(
    interaction::HandPoseRole role,float squeeze,float trigger)noexcept {
    interaction::HandPoseTargets out;out.role=role;
    if(role==interaction::HandPoseRole::Free){
        out.curl.fill(std::clamp(squeeze,0.f,1.f));
        out.curl[std::size_t(interaction::HandFinger::Index)]=std::clamp(trigger,0.f,1.f);
    }else if(role==interaction::HandPoseRole::MechanismGrip){
        // Tentative authored compact pinch/wrap, not measured native limits or
        // a claim of optical finger tracking. The adapter owns role/release.
        out.curl={.45f,.35f,.35f,.30f,.25f};out.pinch=1;
    }
    return out;
}
inline interaction::HandPoseTargets RightHandTargets(
    interaction::HandPoseRole role,float squeeze,float trigger)noexcept {
    return LeftHandTargets(role,squeeze,trigger);
}
namespace bc2_hand_detail {
inline math::Vec3 Add(math::Vec3 a,math::Vec3 b)noexcept{return {a.x+b.x,a.y+b.y,a.z+b.z};}
inline math::Vec3 Sub(math::Vec3 a,math::Vec3 b)noexcept{return {a.x-b.x,a.y-b.y,a.z-b.z};}
inline math::Vec3 Scale(math::Vec3 p,float s)noexcept{return {p.x*s,p.y*s,p.z*s};}
inline float Dot(math::Vec3 a,math::Vec3 b)noexcept{return a.x*b.x+a.y*b.y+a.z*b.z;}
inline math::Vec3 Cross(math::Vec3 a,math::Vec3 b)noexcept{return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
inline math::Vec3 Position(const math::Matrix4& m)noexcept{return {m.values[3][0],m.values[3][1],m.values[3][2]};}
inline math::Vec3 Vector(math::Vec3 p,const math::Matrix4& m)noexcept {
    return {p.x*m.values[0][0]+p.y*m.values[1][0]+p.z*m.values[2][0],
        p.x*m.values[0][1]+p.y*m.values[1][1]+p.z*m.values[2][1],
        p.x*m.values[0][2]+p.y*m.values[1][2]+p.z*m.values[2][2]};
}
inline std::optional<math::Vec3> Unit(math::Vec3 p)noexcept {
    const float length=std::hypot(p.x,p.y,p.z);
    if(!std::isfinite(length)||length<1e-6f)return {};
    return Scale(p,1/length);
}
inline math::Matrix4 Identity()noexcept {
    math::Matrix4 out{};for(unsigned n=0;n<4;++n)out.values[n][n]=1;return out;
}
// Anatomy derivation separated from the production metadata gate to permit
// synthetic tests without shipping a captured game skeleton. Runtime callers
// must use BindLeftHandPose or BindRightHandPose below.
inline std::optional<Bc2HandBinding> Derive(
    std::span<const std::string> names,std::span<const std::int32_t> parents,
    std::span<const math::Matrix4> inverseBind,float unitsPerMetre,bool rightHand=false) {
    using namespace interaction;
    if(names.empty()||names.size()>1024||parents.size()!=names.size()||inverseBind.size()!=names.size()||
       !std::isfinite(unitsPerMetre)||unitsPerMetre<=0)return {};
    const auto named=[&](std::string_view name)->std::optional<std::uint32_t>{
        const auto found=std::find(names.begin(),names.end(),name);
        if(found==names.end()||std::find(found+1,names.end(),name)!=names.end())return {};
        return std::uint32_t(found-names.begin());
    };
    const std::string_view handName=rightHand?"RightHand":"LeftHand";
    const auto wrist=named(handName);if(!wrist)return {};
    Bc2HandBinding out;out.rightHand=rightHand;out.pose.wrist=*wrist;out.referenceWorld.resize(names.size());
    std::vector<bool> member(names.size(),false);
    for(std::size_t n=0;n<parents.size();++n){
        auto at=std::int32_t(n);std::size_t steps=0;
        while(at!=-1){
            if(at<0||std::size_t(at)>=parents.size()||++steps>parents.size())return {};
            if(at==std::int32_t(*wrist))member[n]=true;at=parents[at];
        }
        if(member[n]){
            if(!hand_pose_detail::Proper(inverseBind[n]))return {};
            const auto world=InverseAnimatedTransform(inverseBind[n]);
            if(!world||!hand_pose_detail::Proper(*world))return {};
            out.referenceWorld[n]=*world;
        }
    }
    const std::array<const char*,5> fingers{"Thumb","Index","Middle","Ring","Pinky"};
    for(unsigned finger=0;finger<fingers.size();++finger){
        auto& chain=out.pose.fingers[finger];chain.count=3;auto parent=*wrist;
        for(unsigned joint=0;joint<3;++joint){
            const auto index=named(std::string(handName)+fingers[finger]+std::to_string(joint+1));
            if(!index||parents[*index]!=std::int32_t(parent))return {};
            chain.joints[joint].index=*index;parent=*index;
        }
    }
    const auto wristLocal=[&](unsigned index){return Position(Multiply(out.referenceWorld[index],inverseBind[*wrist]));};
    math::Vec3 center{};
    for(unsigned finger=1;finger<5;++finger)center=Add(center,Scale(wristLocal(out.pose.fingers[finger].joints[0].index),.25f));
    const auto forward=Unit(Scale(center,1/unitsPerMetre));if(!forward)return {};
    auto across=Sub(wristLocal(out.pose.fingers[1].joints[0].index),wristLocal(out.pose.fingers[4].joints[0].index));
    across=Sub(across,Scale(*forward,Dot(across,*forward)));
    const auto a=Unit(Scale(across,1/unitsPerMetre));if(!a)return {};
    const auto normal=Unit(Cross(*a,*forward));if(!normal)return {};
    // Independently captured right bind anatomy mirrors the left wrist-local
    // Z direction: its palmar normal is -(A cross F). Keep the accepted left
    // calculation unchanged; side is explicit, never guessed from a weapon.
    const auto palmarNormal=rightHand?Scale(*normal,-1):*normal;
    const auto thumbBase=wristLocal(out.pose.fingers[0].joints[0].index);
    if(Dot(thumbBase,palmarNormal)/unitsPerMetre<=1e-5f)return {};
    out.wristToGrip=Identity();
    // Canonical grip +Z follows the curled-finger tube toward the index/thumb;
    // anatomical fingers point along grip -Y. The palmar normal maps to +X
    // on the left and -X on the right; [A cross F,-F,A] stays proper on both.
    const std::array<math::Vec3,3> columns{*normal,Scale(*forward,-1),*a};
    for(unsigned column=0;column<3;++column){
        out.wristToGrip.values[0][column]=columns[column].x;
        out.wristToGrip.values[1][column]=columns[column].y;
        out.wristToGrip.values[2][column]=columns[column].z;
    }
    if(!hand_pose_detail::Proper(out.wristToGrip))return {};
    const auto normalWorld=Unit(Vector(palmarNormal,out.referenceWorld[*wrist]));if(!normalWorld)return {};
    const auto indexDistal=Position(out.referenceWorld[out.pose.fingers[1].joints[2].index]);
    // Ranges below are deliberately authored, conservative starting poses.
    // The axes and lengths come from bind anatomy; these are not joint limits.
    const std::array<float,3> curlRange{1.05f,1.30f,.75f};
    const std::array<float,3> thumbRange{.50f,.65f,.45f};
    const std::array<float,3> thumbPinch{.16f,.12f,.08f};
    const std::array<float,3> indexPinch{.08f,.06f,.04f};
    for(unsigned finger=0;finger<5;++finger){
        auto& chain=out.pose.fingers[finger];
        for(unsigned joint=0;joint<3;++joint){
            auto& binding=chain.joints[joint];const auto at=binding.index;
            const auto from=Position(out.referenceWorld[at]);
            const auto tangent=joint<2?
                Sub(Position(out.referenceWorld[chain.joints[joint+1].index]),from):
                Sub(from,Position(out.referenceWorld[chain.joints[joint-1].index]));
            const auto tangentUnit=Unit(Scale(tangent,1/unitsPerMetre));if(!tangentUnit)return {};
            // Thumb opposition follows the actual index distal-knuckle direction. Other
            // fingers flex toward the measured palm normal.
            const auto toward=finger==0?Unit(Scale(Sub(indexDistal,from),1/unitsPerMetre)):normalWorld;
            if(!toward)return {};
            const auto worldAxis=Unit(Cross(*tangentUnit,*toward));if(!worldAxis)return {};
            const auto localAxis=Unit(Vector(*worldAxis,inverseBind[at]));if(!localAxis)return {};
            binding.curlAxisLocal=*localAxis;binding.openRadians=0;
            binding.closedRadians=finger==0?thumbRange[joint]:curlRange[joint];
            binding.pinchRadians=finger==0?thumbPinch[joint]:finger==1?indexPinch[joint]:0;
        }
    }
    const auto mechanism=GenerateHandPose(parents,out.referenceWorld,out.referenceWorld,out.pose,
        out.referenceWorld[*wrist],LeftHandTargets(HandPoseRole::MechanismGrip,0,0));
    if(!mechanism)return {};
    std::array<std::optional<math::Vec3>,2> tips;
    const std::array<unsigned,2> tipIndices{out.pose.fingers[0].joints[2].index,out.pose.fingers[1].joints[2].index};
    for(const auto& write:mechanism->writes)for(unsigned side=0;side<2;++side)if(write.index==tipIndices[side])
        tips[side]=Position(Multiply(write.transform,inverseBind[*wrist]));
    if(!tips[0]||!tips[1])return {};
    // The thumb3/index3 midpoint is a distal-knuckle contact proxy, not
    // fingertip pads or collision geometry. This follows the generated pose,
    // independent of the currently equipped weapon's under-gun hand shape.
    out.mechanismPointWristMeters=Scale(Add(*tips[0],*tips[1]),.5f/unitsPerMetre);
    const auto p=out.mechanismPointWristMeters;
    if(!std::isfinite(p.x)||!std::isfinite(p.y)||!std::isfinite(p.z))return {};
    return out;
}
}
[[nodiscard]] inline std::optional<Bc2HandBinding> BindLeftHandPose(
    std::span<const std::string> names,std::span<const std::int32_t> parents,
    std::span<const math::Matrix4> inverseBind,float unitsPerMetre) {
    if(SightRigFingerprint(names,parents,inverseBind)!=0xa7f219a1426216abull)return {};
    return bc2_hand_detail::Derive(names,parents,inverseBind,unitsPerMetre);
}
[[nodiscard]] inline std::optional<Bc2HandBinding> BindRightHandPose(
    std::span<const std::string> names,std::span<const std::int32_t> parents,
    std::span<const math::Matrix4> inverseBind,float unitsPerMetre) {
    if(SightRigFingerprint(names,parents,inverseBind)!=0xa7f219a1426216abull)return {};
    return bc2_hand_detail::Derive(names,parents,inverseBind,unitsPerMetre,true);
}
// Visual trigger-finger feedback only. Does not animate a weapon mesh trigger,
// alter the accepted wrist/attachment, or dispatch native fire. currentWorld is
// the fresh native pose plus other private layers, never this overlay's prior
// output. The adapter invokes it once after resolving the held right wrist.
[[nodiscard]] inline std::optional<interaction::HandPose> PoseRightTrigger(
    std::span<const std::int32_t> parents,std::span<const math::Matrix4> currentWorld,
    const Bc2HandBinding& binding,float trigger) {
    if(!binding.rightHand||!std::isfinite(trigger)||trigger<0||trigger>1)return {};
    const auto& index=binding.pose.fingers[std::size_t(interaction::HandFinger::Index)];
    if(index.count!=3)return {};
    // Tentative authored ADDITIVE maxima (6.9 /12.6 /6.9 degrees), deliberately
    // smaller than the free-hand curl. These are not measured native limits.
    const std::array<float,3> radians{.12f*trigger,.22f*trigger,.12f*trigger};
    return interaction::GenerateFingerCurlOverlay(parents,currentWorld,binding.pose.wrist,index,radians);
}
}
