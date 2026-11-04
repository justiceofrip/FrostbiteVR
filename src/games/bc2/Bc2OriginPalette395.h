#pragma once
// Pure, per-exit palette proof. A mechanical overlay may change unrelated
// bones, but every displayed/fallback choice must preserve this weapon + exit.
#include "Bc2Rig.h"
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>
#include <span>

namespace fvr::bc2::origin_palette395 {
using Bone=std::array<std::byte,64>;
using Palette=std::span<const Bone>;
inline bool SameExitBones(Palette published,Palette alternative,std::span<const unsigned> bones)noexcept {
    if(published.empty()||published.size()>1024||published.size()!=alternative.size()||bones.empty()||bones.size()>2)return false;
    for(const auto bone:bones)if(bone>=published.size()||published[bone]!=alternative[bone])return false;
    return true;
}
inline bool PackedExitMatches(const RigSnapshot& rig,Palette palette,unsigned bone,const math::Matrix4& world)noexcept {
    const auto count=rig.identity.count;
    if(!count||count>1024||bone>=count||palette.size()!=count||rig.inverseBind.size()!=count||
       rig.nativeEvaluated.size()!=count||
       std::find(rig.nativeHiddenLeaves.begin(),rig.nativeHiddenLeaves.end(),bone)!=rig.nativeHiddenLeaves.end()||
       !interaction::InverseAnimatedTransform(world)||!interaction::InverseAnimatedTransform(rig.inverseBind[bone]))return false;
    auto skin=interaction::Multiply(rig.inverseBind[bone],world);
    for(unsigned i=0;i<4;++i){skin.values[2][i]=-skin.values[2][i];skin.values[i][2]=-skin.values[i][2];}
    auto expected=rig.nativeEvaluated[bone];
    for(unsigned row=0;row<4;++row)std::memcpy(expected.data()+row*16,skin.values[row].data(),12);
    return expected==palette[bone];
}
} // namespace fvr::bc2::origin_palette395
