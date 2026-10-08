#pragma once
#include "fvr/interaction/HandPose.h"

namespace fvr::interaction {
// Additive visual curl over an original, unmodified animation pose. The caller
// must never feed this result back as the next frame's currentWorld. Only the
// selected finger subtree is returned; the wrist and sibling fingers receive
// no edits. Axes are the verified joint-local anatomy axes in the binding.
[[nodiscard]] inline std::optional<HandPose> GenerateFingerCurlOverlay(
    std::span<const std::int32_t> parents,std::span<const math::Matrix4> currentWorld,
    std::uint32_t wrist,const HandFingerBinding& finger,std::span<const float> radians) {
    using namespace hand_pose_detail;
    constexpr float pi=3.1415926535897932f;
    const auto size=parents.size();
    if(!size||size>1024||currentWorld.size()!=size||wrist>=size||!Proper(currentWorld[wrist])||
       !finger.count||finger.count>finger.joints.size()||radians.size()!=finger.count)return {};
    const auto root=finger.joints[0].index;
    std::vector<int> jointAt(size,-1);
    auto parent=wrist;bool active=false;
    for(unsigned n=0;n<finger.count;++n){
        const auto& joint=finger.joints[n];
        if(joint.index>=size||joint.index==wrist||jointAt[joint.index]>=0||
           parents[joint.index]!=std::int32_t(parent)||!Finite(joint.curlAxisLocal)||
           std::abs(Dot(joint.curlAxisLocal,joint.curlAxisLocal)-1.f)>=.0001f||
           !std::isfinite(radians[n])||std::abs(radians[n])>pi)return {};
        jointAt[joint.index]=int(n);parent=joint.index;active|=radians[n]!=0;
    }
    std::vector<bool> member(size,false);
    for(std::size_t n=0;n<size;++n){
        auto at=std::int32_t(n);std::size_t steps=0;
        while(at!=-1){
            if(at<0||std::size_t(at)>=size||++steps>size)return {};
            if(std::uint32_t(at)==root)member[n]=true;
            at=parents[at];
        }
    }
    std::vector<math::Matrix4> inverse(size),placed(size);
    inverse[wrist]=*InverseAnimatedTransform(currentWorld[wrist]);
    std::size_t remaining=0;
    for(std::size_t n=0;n<size;++n)if(member[n]){
        if(!Proper(currentWorld[n]))return {};
        inverse[n]=*InverseAnimatedTransform(currentWorld[n]);++remaining;
    }
    if(!active)return HandPose{}; // Exact native release: no writes at all.
    std::vector<bool> completed(size,false);
    completed[wrist]=true;placed[wrist]=currentWorld[wrist];
    HandPose out;out.writes.reserve(remaining);
    while(remaining){
        bool progressed=false;
        for(std::size_t n=0;n<size;++n){
            if(!member[n]||completed[n])continue;
            const auto p=std::size_t(parents[n]);if(!completed[p])continue;
            auto local=Multiply(currentWorld[n],inverse[p]);
            if(jointAt[n]>=0){
                const auto j=unsigned(jointAt[n]);
                local=Multiply(Rotation(finger.joints[j].curlAxisLocal,radians[j]),local);
            }
            placed[n]=Multiply(local,placed[p]);if(!Proper(placed[n]))return {};
            completed[n]=true;progressed=true;--remaining;
            out.writes.push_back({std::uint32_t(n),placed[n]});
        }
        if(!progressed)return {};
    }
    return out;
}
}
