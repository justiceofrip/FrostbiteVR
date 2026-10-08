#include "Test.h"
#include "fvr/interaction/HandPose.h"
#include <limits>
using namespace fvr;using namespace interaction;
namespace {
math::Matrix4 At(float x=0,float y=0,float z=0){
    math::Matrix4 m{};for(unsigned n=0;n<4;++n)m.values[n][n]=1;
    m.values[3][0]=x;m.values[3][1]=y;m.values[3][2]=z;return m;
}
bool Close(const math::Matrix4& a,const math::Matrix4& b,float epsilon=.00003f){
    for(unsigned r=0;r<4;++r)for(unsigned c=0;c<4;++c)if(!Near(a.values[r][c],b.values[r][c],epsilon))return false;
    return true;
}
float Distance(const math::Matrix4& a,const math::Matrix4& b){
    return std::hypot(a.values[3][0]-b.values[3][0],a.values[3][1]-b.values[3][1],a.values[3][2]-b.values[3][2]);
}
struct Fixture {
    std::array<std::int32_t,20> parents{};
    std::array<math::Matrix4,20> reference{},current{};
    HandPoseBinding binding;HandPoseTargets targets;
    Fixture(){
        parents[0]=-1;parents[1]=0;reference[0]=At();reference[1]=At(.1f,.2f,.3f);binding.wrist=1;
        for(unsigned finger=0;finger<5;++finger){
            auto& chain=binding.fingers[finger];chain.count=3;
            for(unsigned joint=0;joint<3;++joint){
                const auto index=2+3*finger+joint;parents[index]=joint?std::int32_t(index-1):1;
                const auto local=joint?At(0,0,joint==1?.03f:.025f):At((float(finger)-2)*.02f,0,.025f);
                reference[index]=Multiply(local,reference[parents[index]]);
                chain.joints[joint]={index,{1,0,0},0,1.2f,0};
            }
        }
        parents[17]=0;reference[17]=At(5,6,7); // Unrelated branch.
        parents[18]=7;reference[18]=Multiply(At(0,0,.01f),reference[7]); // Unbound fingertip.
        parents[19]=1;reference[19]=Multiply(At(.1f,0,0),reference[1]); // Unbound wrist attachment.
        current=reference;
    }
    std::optional<HandPose> Generate(const math::Matrix4& wrist){return GenerateHandPose(parents,reference,current,binding,wrist,targets);}
    std::array<math::Matrix4,20> Apply(const HandPose& pose)const {
        auto out=current;for(const auto& write:pose.writes)out[write.index]=write.transform;return out;
    }
};
int ReferenceShapeAndLengths(){
    Fixture f;f.targets.role=HandPoseRole::MechanismGrip;f.targets.curl={.2f,.7f,.6f,.5f,.4f};
    auto out=f.Generate(f.current[1]);CHECK(out);
    const auto curled=f.Apply(*out);CHECK(out->writes.size()==18&&Close(curled[1],f.current[1]));
    CHECK(Close(curled[17],f.current[17])&&Close(curled[0],f.current[0]));
    for(unsigned n=2;n<17;++n)CHECK(Near(Distance(curled[n],curled[f.parents[n]]),Distance(f.reference[n],f.reference[f.parents[n]])));
    CHECK(Near(Distance(curled[18],curled[7]),Distance(f.reference[18],f.reference[7])));
    CHECK(!Close(curled[6],f.reference[6]));
    // Free pose comes from the explicit reference, not the currently animated
    // weapon support shape. Re-evaluating it never compounds previous curls.
    f.current=curled;f.targets.role=HandPoseRole::Free;f.targets.curl={};
    out=f.Generate(f.current[1]);CHECK(out);auto relaxed=f.Apply(*out);
    for(unsigned n=1;n<17;++n)CHECK(Close(relaxed[n],f.reference[n]));
    CHECK(Close(relaxed[18],f.reference[18]));
    const auto source=f.current;
    for(unsigned n=0;n<100;++n){const auto repeat=f.Generate(f.current[1]);CHECK(repeat);CHECK(Close(f.Apply(*repeat)[6],f.reference[6]));}
    for(unsigned n=0;n<20;++n)CHECK(Close(source[n],f.current[n]));
    return 0;
}
int SupportAndUnbound(){
    Fixture f;f.targets.role=HandPoseRole::MechanismGrip;f.targets.curl.fill(.7f);
    auto out=f.Generate(f.current[1]);CHECK(out);f.current=f.Apply(*out);
    f.targets.role=HandPoseRole::WeaponSupport;f.targets.curl.fill(0);
    const auto unchanged=f.Generate(f.current[1]);CHECK(unchanged);
    for(const auto& write:unchanged->writes)CHECK(write.transform.values==f.current[write.index].values);
    const auto wrist=At(-1,2,3);out=f.Generate(wrist);CHECK(out);const auto preserved=f.Apply(*out);
    const auto expected=RetargetRigSubtree(f.parents,f.current,1,wrist);CHECK(expected&&expected->size()==out->writes.size());
    for(const auto& write:*expected)CHECK(Close(preserved[write.index],write.transform));
    CHECK(Close(preserved[1],wrist));
    // A deliberately unmapped finger retains its native local pose. Other
    // verified chains can still use an independent interaction role.
    f.binding.fingers[4].count=0;f.targets.role=HandPoseRole::Free;
    out=f.Generate(wrist);CHECK(out);const auto partial=f.Apply(*out);
    for(unsigned n=14;n<17;++n)CHECK(Close(partial[n],preserved[n]));
    CHECK(Close(partial[19],preserved[19]));return 0;
}
int ParameterizedCurlAndPinch(){
    Fixture f;f.targets.role=HandPoseRole::MechanismGrip;
    f.binding.fingers[0].joints[0].pinchRadians=.6f;
    f.binding.fingers[1].joints[0].pinchRadians=-.4f;
    f.targets.pinch=1;
    auto out=f.Generate(f.current[1]);CHECK(out);const auto pinch=f.Apply(*out);
    CHECK(pinch[3].values[3][1]<f.reference[3].values[3][1]);
    CHECK(pinch[6].values[3][1]>f.reference[6].values[3][1]);
    for(unsigned n=8;n<17;++n)CHECK(Close(pinch[n],f.reference[n]));
    f.targets.pinch=0;f.targets.curl[1]=1;
    f.binding.fingers[1].joints[0].closedRadians=1.57079632679f;
    f.binding.fingers[1].joints[1].closedRadians=0;
    f.binding.fingers[1].joints[2].closedRadians=0;
    out=f.Generate(f.current[1]);CHECK(out);const auto curled=f.Apply(*out);
    CHECK(Near(curled[6].values[3][1],curled[5].values[3][1]-.03f));
    CHECK(Near(curled[6].values[3][2],curled[5].values[3][2]));
    // Axis is data, including the opposite sign for mirrored authored hands.
    f.binding.fingers[1].joints[0].curlAxisLocal={-1,0,0};
    out=f.Generate(f.current[1]);CHECK(out);const auto mirrored=f.Apply(*out);
    CHECK(Near(mirrored[6].values[3][1],mirrored[5].values[3][1]+.03f));
    return 0;
}
int WristPlacementAndIndexOrder(){
    Fixture f;f.targets.role=HandPoseRole::MechanismGrip;f.targets.curl.fill(.4f);
    auto wrist=At(2,3,4);wrist.values[0]={0,1,0,0};wrist.values[1]={-1,0,0,0};
    const auto out=f.Generate(wrist);CHECK(out);const auto placed=f.Apply(*out);
    CHECK(Close(placed[1],wrist));
    const auto local=f.Generate(f.current[1]);CHECK(local);const auto localPose=f.Apply(*local);
    const auto delta=Multiply(*InverseAnimatedTransform(f.current[1]),wrist);
    for(const auto& write:out->writes)CHECK(Close(write.transform,Multiply(localPose[write.index],delta)));
    // Bone array order is not a hierarchy order. Swap the wrist and its last
    // descendant to exercise parent-after-child traversal.
    const auto remap=[](unsigned n){return n==1?19u:n==19?1u:n;};
    std::array<std::int32_t,20> parents;std::array<math::Matrix4,20> reference,current;
    for(unsigned n=0;n<20;++n){
        parents[remap(n)]=f.parents[n]<0?-1:std::int32_t(remap(unsigned(f.parents[n])));
        reference[remap(n)]=f.reference[n];current[remap(n)]=f.current[n];
    }
    auto binding=f.binding;binding.wrist=remap(binding.wrist);
    for(auto& finger:binding.fingers)for(unsigned n=0;n<finger.count;++n)finger.joints[n].index=remap(finger.joints[n].index);
    const auto unordered=GenerateHandPose(parents,reference,current,binding,wrist,f.targets);CHECK(unordered);
    for(const auto& write:unordered->writes)CHECK(Close(write.transform,placed[remap(write.index)]));
    return 0;
}
int InvalidInputs(){
    const float nan=std::numeric_limits<float>::quiet_NaN();
    for(unsigned kind=0;kind<15;++kind){
        Fixture f;
        if(kind==0)f.parents[17]=17; // Cycle outside hand is still invalid topology.
        if(kind==1)f.parents[3]=19; // Unverified intermediary in a bound chain.
        if(kind==2)f.binding.fingers[1].joints[0].index=2; // Duplicate across fingers.
        if(kind==3)f.binding.fingers[0].count=5;
        if(kind==4)f.binding.fingers[0].joints[0].curlAxisLocal={};
        if(kind==5)f.binding.fingers[0].joints[0].curlAxisLocal={2,0,0};
        if(kind==6)f.binding.fingers[0].joints[0].closedRadians=nan;
        if(kind==7)f.targets.curl[0]=1.01f;
        if(kind==8)f.targets.pinch=-.1f;
        if(kind==9)f.reference[3].values[0][0]=-1; // Reflection.
        if(kind==10)f.current[18].values[0][0]=0; // Singular participating leaf.
        if(kind==11)f.reference[3].values[3][0]=nan;
        if(kind==12)f.targets.role=static_cast<HandPoseRole>(255);
        if(kind==13)f.binding.wrist=20;
        if(kind==14){for(auto& finger:f.binding.fingers)finger.count=0;}
        CHECK(!f.Generate(f.current[1]));
    }
    Fixture f;auto bad=f.current[1];bad.values[1][1]=0;CHECK(!f.Generate(bad));
    CHECK(!GenerateHandPose(f.parents,std::span<const math::Matrix4>{},f.current,f.binding,f.current[1],f.targets));
    // Unrelated source matrices are neither read as hand geometry nor emitted.
    f.current[17]={};f.reference[17].values[0][0]=nan;CHECK(f.Generate(f.current[1]));
    f.targets.role=HandPoseRole::WeaponSupport;CHECK(GenerateHandPose(f.parents,{},f.current,f.binding,f.current[1],f.targets));
    return 0;
}
}
int main(){
    if(ReferenceShapeAndLengths()||SupportAndUnbound()||ParameterizedCurlAndPinch()||
       WristPlacementAndIndexOrder()||InvalidInputs())return 1;
    return 0;
}
