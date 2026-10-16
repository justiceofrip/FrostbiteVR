#include "Test.h"
#include "fvr/interaction/ArmIk.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
using namespace fvr;using namespace interaction;
namespace {
using M=math::Matrix4;
M At(float x,float y,float z){M m{};for(unsigned n=0;n<4;++n)m.values[n][n]=1;m.values[3]={x,y,z,1};return m;}
float Dist(const M& a,const M& b){float sum=0;for(unsigned n=0;n<3;++n){const float d=a.values[3][n]-b.values[3][n];sum+=d*d;}return std::sqrt(sum);}
float Det(const M& m){const auto& a=m.values;return a[0][0]*(a[1][1]*a[2][2]-a[1][2]*a[2][1])-a[0][1]*(a[1][0]*a[2][2]-a[1][2]*a[2][0])+a[0][2]*(a[1][0]*a[2][1]-a[1][1]*a[2][0]);}
bool Close(const M& a,const M& b,float eps){for(unsigned r=0;r<4;++r)for(unsigned c=0;c<4;++c)if(std::abs(a.values[r][c]-b.values[r][c])>eps)return false;return true;}
struct Fixture {
 std::array<std::int32_t,15> parents{-1,0,1,2,3,4,0,6,7,8,9,5,10,0,1};
 std::array<M,15> native{At(0,0,0),At(-.2f,0,0),At(-.2f,0,.12f),At(-.2f,0,.30f),At(-.2f,0,.40f),At(-.2f,0,.56f),
     At(.2f,0,0),At(.2f,0,.12f),At(.2f,0,.30f),At(.2f,0,.40f),At(.2f,0,.56f),At(-.2f,.025f,.60f),At(.2f,.025f,.60f),At(.4f,0,.5f),At(-.18f,.03f,.09f)};
 ArmJoints left{1,3,5},right{6,8,10};
 ArmTarget l{native[5],{0,1,0}},r{native[10],{0,1,0}};
 Fixture(){l.wrist.values[3][2]=.45f;r.wrist.values[3][2]=.45f;}
 auto Pose(const ArmPose& p)const{auto out=native;for(const auto& w:p.writes)out[w.index]=w.transform;return out;}
};
ArmReachPolicy Policy(){
#ifdef FVR_TEST_LEGACY_WRIST_CLAMP
 return ArmReachPolicy::ClampToAnatomy;
#else
 return ArmReachPolicy::ExtendToAdmittedWrist;
#endif
}
int NormalReachUnchanged(){
 Fixture f;unsigned checked=0;
 for(unsigned i=0;i<600;++i){const float t=float(i)*.03f;f.l.wrist.values[3]={-.2f+std::sin(t)*.25f,std::cos(t)*.15f,.25f+std::sin(t*.6f)*.12f,1};
  auto old=SolveTrackedArms(f.parents,f.native,f.left,f.right,f.l,f.r);CHECK(old&&!old->reachClamped[0]);
  auto current=f.l;current.reach=Policy();auto next=SolveTrackedArms(f.parents,f.native,f.left,f.right,current,f.r);CHECK(next&&next->writes.size()==old->writes.size());
  for(unsigned n=0;n<old->writes.size();++n)CHECK(next->writes[n].transform.values==old->writes[n].transform.values);
  CHECK(next->segmentScale[0]==1&&next->segmentScale[1]==1);++checked;
 }CHECK(checked==600);return 0;
}
int MeasuredRangeKeepsWristAndRigidFingers(){
 // Human390 input21880 has a 0.20233804m left-wrist error, bracketed by
 // unit-scale AEK samples. This is a range regression, not a full pose replay.
 for(float units:{.01f,.1f,1.f,2.f,1000.f})for(float gap:{.0002f,.02f,.055f,.20233804f,.30f,.75f}){
  Fixture f;for(auto& m:f.native)for(unsigned n=0;n<3;++n)m.values[3][n]*=units;
  f.l.wrist=At(-.2f*units,0,(.56f+gap)*units);f.l.wrist.values[0]={0,1,0,0};f.l.wrist.values[1]={-1,0,0,0};
  f.r.enabled=false;f.l.reach=Policy();const auto original=f.native;
  const auto out=SolveTrackedArms(f.parents,f.native,f.left,f.right,f.l,f.r);CHECK(out);
  auto posed=f.Pose(*out);CHECK(posed[5].values==f.l.wrist.values);
  CHECK(!out->reachClamped[0]&&out->segmentScale[0]>1.f&&out->targetError[0]==0);
  const float eps=std::max(1e-6f,units*.0002f);
  CHECK(std::abs(Dist(posed[1],posed[3])-Dist(f.native[1],f.native[3])*out->segmentScale[0])<eps);
  CHECK(std::abs(Dist(posed[3],posed[5])-Dist(f.native[3],f.native[5])*out->segmentScale[0])<eps);
  CHECK(posed[1].values[3]==f.native[1].values[3]);
  const auto beforeFinger=Multiply(f.native[11],*InverseAnimatedTransform(f.native[5]));
  const auto afterFinger=Multiply(posed[11],*InverseAnimatedTransform(posed[5]));CHECK(Close(beforeFinger,afterFinger,eps));
  for(const auto& w:out->writes){CHECK((w.index!=13&&w.index<6)||w.index==11||w.index==14);CHECK(InverseRigid(w.transform));CHECK(Det(w.transform)>.999f);}
  // Both roll joints remain at their native segment fraction; bases never scale.
  CHECK(std::abs(Dist(posed[1],posed[2])/Dist(posed[1],posed[3])-.4f)<.0003f);
  CHECK(std::abs(Dist(posed[3],posed[4])/Dist(posed[3],posed[5])-(.1f/.26f))<.0003f);
  for(unsigned n:{0u,6u,7u,8u,9u,10u,12u,13u})CHECK(posed[n].values==f.native[n].values);
  for(unsigned n=0;n<f.native.size();++n)CHECK(f.native[n].values==original[n].values);
 }return 0;
}
int FrameCovarianceAndNoAccumulation(){
 Fixture f;f.l.wrist=At(-.2f,.1f,.78f);f.l.reach=Policy();f.r.enabled=false;
 const auto first=SolveTrackedArms(f.parents,f.native,f.left,f.right,f.l,f.r);CHECK(first&&!first->reachClamped[0]);
 for(unsigned run=0;run<100;++run){const auto again=SolveTrackedArms(f.parents,f.native,f.left,f.right,f.l,f.r);CHECK(again&&again->writes.size()==first->writes.size());for(unsigned n=0;n<first->writes.size();++n)CHECK(again->writes[n].transform.values==first->writes[n].transform.values);}
 M frame=At(643,121,-107);frame.values[0]={0,0,1,0};frame.values[2]={-1,0,0,0};
 auto transformed=f.native;for(auto& m:transformed)m=Multiply(m,frame);auto target=f.l;target.wrist=Multiply(target.wrist,frame);target.poleDirection={0,1,0};
 const auto moved=SolveTrackedArms(f.parents,transformed,f.left,f.right,target,f.r);CHECK(moved&&!moved->reachClamped[0]);
 for(unsigned n=0;n<first->writes.size();++n)CHECK(Close(moved->writes[n].transform,Multiply(first->writes[n].transform,frame),.001f));
 CHECK(f.Pose(*first)[5].values==f.l.wrist.values);return 0;
}
int BothSidesAndControllerRoll(){
 Fixture f;f.l.wrist=At(-.35f,.1f,.81f);f.r.wrist=At(.32f,-.15f,.79f);
 f.l.reach=f.r.reach=Policy();f.r.wrist.values[1][1]=-1;f.r.wrist.values[2][2]=-1;
 const auto both=SolveTrackedArms(f.parents,f.native,f.left,f.right,f.l,f.r);CHECK(both&&!both->reachClamped[0]&&!both->reachClamped[1]);
 CHECK(both->segmentScale[0]>1&&both->segmentScale[1]>1);const auto posed=f.Pose(*both);
 CHECK(posed[5].values==f.l.wrist.values&&posed[10].values==f.r.wrist.values);
 CHECK(Det(posed[5])>.999f&&Det(posed[10])>.999f);
 CHECK(posed[0].values==f.native[0].values&&posed[13].values==f.native[13].values);
 auto justRight=f.l;justRight.enabled=false;const auto right=SolveTrackedArms(f.parents,f.native,f.left,f.right,justRight,f.r);CHECK(right);
 for(const auto& w:right->writes)CHECK(w.transform.values==posed[w.index].values);
 return 0;
}
int LimitsRemainExplicit(){
 Fixture f;f.l.reach=Policy();f.l.wrist=At(-.2f,0,0);
 const auto near=SolveTrackedArms(f.parents,f.native,f.left,f.right,f.l,f.r);CHECK(near&&near->reachClamped[0]&&near->targetError[0]>0&&near->segmentScale[0]==1.f);
 f.l.wrist.values[3][0]=std::numeric_limits<float>::quiet_NaN();CHECK(!SolveTrackedArms(f.parents,f.native,f.left,f.right,f.l,f.r));
 f.l.enabled=false;const auto disabled=SolveTrackedArms(f.parents,f.native,f.left,f.right,f.l,f.r);CHECK(disabled&&disabled->segmentScale[0]==1);for(const auto& w:disabled->writes)CHECK((w.index>=6&&w.index<=10)||w.index==12);
 f.l.enabled=true;f.l.wrist=f.native[5];f.native[3]=f.native[1];CHECK(!SolveTrackedArms(f.parents,f.native,f.left,f.right,f.l,f.r));return 0;
}
}
int main(){if(NormalReachUnchanged()||MeasuredRangeKeepsWristAndRigidFingers()||FrameCovarianceAndNoAccumulation()||BothSidesAndControllerRoll()||LimitsRemainExplicit())return 1;std::cout<<"5 tracked-wrist groups passed\n";return 0;}
