#include "Test.h"
#include "fvr/interaction/RigPose.h"
#include <array>
using namespace fvr::interaction;
int main(){
    std::array<std::int32_t,9> parents{-1,0,1,2,3,0,5,6,7};
    CHECK(ValidateArms(parents,{1,2,4},{5,6,8}));CHECK(!ValidateArms(parents,{1,2,4},{1,6,8}));CHECK(!ValidateArms(parents,{1,2,5},{5,6,8}));
    parents[0]=4;CHECK(!ValidateArms(parents,{1,2,4},{5,6,8}));parents[0]=-1;parents[4]=99;CHECK(!ValidateArms(parents,{1,2,4},{5,6,8}));
    fvr::math::Matrix4 identity{};for(int i=0;i<4;++i)identity.values[i][i]=1;
    std::array<fvr::math::Matrix4,9> bones;for(auto& b:bones)b=identity;auto posed=identity;posed.values[3][0]=.4f;
    AnimationWriteCache cache;const std::array<BoneWrite,1> writes{BoneWrite{4,posed}};
    for(unsigned frame=0;frame<1000;++frame){CHECK(cache.Apply(1,bones,writes));CHECK(!cache.Apply(1,bones,writes));CHECK(Near(bones[4].values[3][0],.4f));cache.Restore(1,bones);CHECK(bones[4].values==identity.values);}
    CHECK(cache.Apply(1,bones,writes));bones[4].values[3][0]=.8f;cache.Restore(1,bones);CHECK(Near(bones[4].values[3][0],.8f)); // native writer takes precedence
    bones[4]=identity;CHECK(cache.Apply(1,bones,writes));cache.Restore(2,bones);CHECK(Near(bones[4].values[3][0],.4f)); // never restore another actor
    const std::array<BoneWrite,2> invalid{BoneWrite{1,posed},BoneWrite{99,posed}};CHECK(!cache.Apply(2,bones,invalid));CHECK(bones[1].values==identity.values);
    const std::array<BoneWrite,2> duplicate{BoneWrite{1,posed},BoneWrite{1,posed}};CHECK(!cache.Apply(2,bones,duplicate));
    parents[4]=3;
    bones[2].values[3]={1,2,3,1};bones[3].values[3]={2,2,3,1};bones[4].values[3]={2,2.5f,3,1};
    auto target=identity;target.values[0]={0,1,0,0};target.values[1]={-1,0,0,0};target.values[3]={5,6,7,1};
    const auto subtree=RetargetRigSubtree(parents,bones,2,target);CHECK(subtree&&subtree->size()==3);
    CHECK((*subtree)[0].index==2&&(*subtree)[0].transform.values==target.values);
    CHECK(Near((*subtree)[1].transform.values[3][0],5)&&Near((*subtree)[1].transform.values[3][1],7));
    CHECK(Near((*subtree)[2].transform.values[3][0],4.5f)&&Near((*subtree)[2].transform.values[3][1],7));
    CHECK(Near(bones[3].values[3][0],2)); // pure generator, no source writes
    CHECK(!RetargetRigSubtree(parents,bones,99,target));
    parents[0]=4;CHECK(!RetargetRigSubtree(parents,bones,2,target));parents[0]=-1;
    parents[8]=99;CHECK(!RetargetRigSubtree(parents,bones,2,target));parents[8]=7;
    const auto visibleLeaf=bones[4];
    bones[4].values[0][0]=bones[4].values[1][1]=bones[4].values[2][2]=.0001f;
    const auto hiddenSource=bones[4];const std::array<std::uint32_t,1> preserve{4};
    CHECK(!RetargetRigSubtree(parents,bones,2,target));
    const auto hidden=RetargetRigSubtree(parents,bones,2,target,preserve);
    CHECK(hidden&&hidden->size()==2&&bones[4].values==hiddenSource.values);
    const std::array<std::uint32_t,1> notLeaf{3},isRoot{2},outside{99};
    CHECK(!RetargetRigSubtree(parents,bones,2,target,notLeaf));
    CHECK(!RetargetRigSubtree(parents,bones,2,target,isRoot));
    CHECK(!RetargetRigSubtree(parents,bones,2,target,outside));
    bones[4]=visibleLeaf;
    target.values[0]={};CHECK(!RetargetRigSubtree(parents,bones,2,target));
    return 0;
}