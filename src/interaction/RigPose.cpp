#include "fvr/interaction/RigPose.h"
#include <algorithm>
#include <array>
namespace fvr::interaction {
bool ValidateArms(std::span<const std::int32_t> parents,ArmJoints left,ArmJoints right) noexcept {
    if(parents.empty()||parents.size()>1024)return false;
    const std::array<std::uint32_t,6> joints{left.shoulder,left.elbow,left.wrist,right.shoulder,right.elbow,right.wrist};
    for(std::size_t i=0;i<joints.size();++i){if(joints[i]>=parents.size())return false;for(std::size_t j=0;j<i;++j)if(joints[i]==joints[j])return false;}
    for(std::size_t i=0;i<parents.size();++i){auto at=std::int32_t(i);std::size_t steps=0;
        while(at!=-1){if(at<0||std::size_t(at)>=parents.size()||++steps>parents.size())return false;at=parents[at];}}
    const auto descends=[&](std::uint32_t child,std::uint32_t ancestor){auto p=parents[child];while(p!=-1){if(std::uint32_t(p)==ancestor)return true;p=parents[p];}return false;};
    return descends(left.wrist,left.elbow)&&descends(left.elbow,left.shoulder)&&descends(right.wrist,right.elbow)&&descends(right.elbow,right.shoulder);
}
std::optional<std::vector<BoneWrite>> RetargetRigSubtree(std::span<const std::int32_t> parents,
    std::span<const math::Matrix4> native,std::uint32_t root,const math::Matrix4& target,std::span<const std::uint32_t> preservedLeaves){
    if(parents.empty()||parents.size()>1024||parents.size()!=native.size()||root>=parents.size()||!InverseAnimatedTransform(target))return {};
    for(const auto leaf:preservedLeaves)if(leaf>=parents.size()||leaf==root||std::find(parents.begin(),parents.end(),std::int32_t(leaf))!=parents.end())return {};
    const auto inverse=InverseAnimatedTransform(native[root]);if(!inverse)return {};
    const auto delta=Multiply(*inverse,target);std::vector<BoneWrite> result;
    for(unsigned n=0;n<parents.size();++n){
        auto at=std::int32_t(n);unsigned steps=0;bool member=false;
        while(at!=-1){if(at<0||std::size_t(at)>=parents.size()||++steps>parents.size())return {};member|=at==std::int32_t(root);at=parents[at];}
        if(member&&std::find(preservedLeaves.begin(),preservedLeaves.end(),n)==preservedLeaves.end()){const auto placed=Multiply(native[n],delta);if(!InverseAnimatedTransform(native[n])||!InverseAnimatedTransform(placed))return {};result.push_back({n,placed});}
    }
    return result;
}
bool AnimationWriteCache::Apply(std::uint64_t generation,std::span<math::Matrix4> bones,std::span<const BoneWrite> writes){
    if(!generation||bones.empty()||bones.size()>1024||writes.size()>bones.size()||!entries_.empty())return false;
    for(std::size_t i=0;i<writes.size();++i){const auto& w=writes[i];
        if(w.index>=bones.size()||!InverseAnimatedTransform(w.transform)||!InverseAnimatedTransform(bones[w.index]))return false;
        for(std::size_t j=0;j<i;++j)if(w.index==writes[j].index)return false;
    }
    entries_.reserve(writes.size()); // Allocate before modifying the caller's pose.
    for(const auto& w:writes)entries_.push_back({w.index,bones[w.index],w.transform});
    generation_=generation;
    for(const auto& entry:entries_)bones[entry.index]=entry.written;
    return true;
}
void AnimationWriteCache::Restore(std::uint64_t generation,std::span<math::Matrix4> bones) noexcept {
    if(generation==generation_){for(const auto& entry:entries_)if(entry.index<bones.size()&&bones[entry.index].values==entry.written.values)bones[entry.index]=entry.native;}
    Reset();
}
}