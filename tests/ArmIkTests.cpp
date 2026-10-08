#include "Test.h"
#include "fvr/interaction/ArmIk.h"
#include <cmath>
#include <limits>
using namespace fvr;using namespace interaction;
math::Matrix4 At(float x,float y,float z){math::Matrix4 m{};for(unsigned i=0;i<4;++i)m.values[i][i]=1;m.values[3][0]=x;m.values[3][1]=y;m.values[3][2]=z;return m;}
float Distance(const math::Matrix4& a,const math::Matrix4& b){float sum=0;for(unsigned i=0;i<3;++i){float d=a.values[3][i]-b.values[3][i];sum+=d*d;}return std::sqrt(sum);}
bool Close(const math::Matrix4& a,const math::Matrix4& b,float epsilon=.0001f){for(unsigned i=0;i<4;++i)for(unsigned j=0;j<4;++j)if(std::abs(a.values[i][j]-b.values[i][j])>epsilon)return false;return true;}
int main(){
 std::array<std::int32_t,13> parents{-1,0,1,2,3,4,0,6,7,8,9,5,10};
 std::array<math::Matrix4,13> native{At(0,1.5f,0),At(-.2f,1.5f,0),At(-.3f,1.375f,.1f),At(-.4f,1.25f,.2f),At(-.35f,1.175f,.325f),At(-.3f,1.1f,.45f),At(.2f,1.5f,0),At(.3f,1.375f,.1f),At(.4f,1.25f,.2f),At(.35f,1.175f,.325f),At(.3f,1.1f,.45f),At(-.3f,1.12f,.5f),At(.3f,1.12f,.5f)};
 const auto original=native;ArmJoints left{1,3,5},right{6,8,10};
 ArmTarget lt{native[5],{-.2f,-.25f,.2f}},rt{native[10],{.2f,-.25f,.2f}};
 auto unchanged=SolveTrackedArms(parents,native,left,right,lt,rt);CHECK(unchanged&&unchanged->writes.size()==12);for(const auto& w:unchanged->writes)CHECK(Close(w.transform,native[w.index]));
 // Controller roll reaches the wrist/fingers while the native finger curl and
 // each segment length survive; twist bones remain relative to their segment.
 lt.wrist.values[0][0]=.8660254f;lt.wrist.values[0][1]=.5f;lt.wrist.values[1][0]=-.5f;lt.wrist.values[1][1]=.8660254f;
 auto result=SolveTrackedArms(parents,native,left,right,lt,rt);CHECK(result);
 auto posed=native;for(const auto& w:result->writes)posed[w.index]=w.transform;
 CHECK(Close(posed[5],lt.wrist));CHECK(Close(Multiply(native[11],*InverseAnimatedTransform(native[5])),Multiply(posed[11],*InverseAnimatedTransform(posed[5]))));
 CHECK(Close(Multiply(native[2],*InverseAnimatedTransform(native[1])),Multiply(posed[2],*InverseAnimatedTransform(posed[1]))));
 CHECK(Close(posed[0],native[0]));
 for(unsigned sample=0;sample<600;++sample){float phase=float(sample)*.03f;
  lt.wrist.values[3][0]=-.2f+std::sin(phase)*.8f;lt.wrist.values[3][1]=1.5f+std::cos(phase)*.5f;lt.wrist.values[3][2]=std::sin(phase*.7f)*.65f;
  result=SolveTrackedArms(parents,native,left,right,lt,rt);CHECK(result);posed=native;for(const auto& w:result->writes)posed[w.index]=w.transform;
  CHECK(Near(Distance(posed[1],posed[3]),Distance(native[1],native[3]),.0001f));CHECK(Near(Distance(posed[3],posed[5]),Distance(native[3],native[5]),.0001f));
  CHECK(Distance(posed[1],native[1])<.0001f);if(!result->reachClamped[0])CHECK(Distance(posed[5],lt.wrist)<.0001f);
  CHECK(Close(Multiply(native[11],*InverseAnimatedTransform(native[5])),Multiply(posed[11],*InverseAnimatedTransform(posed[5]))));
  AnimationWriteCache cache;CHECK(cache.Apply(1,native,result->writes));cache.Restore(1,native);for(unsigned i=0;i<native.size();++i)CHECK(native[i].values==original[i].values);
 }
 // Regression: native gun yaw rotates both shoulders even when the left grip
 // remains stationary. Body anchors must prevent that rotation from dragging
 // the other wrist, without stretching either native arm segment.
 lt.wrist=native[5];rt.wrist=native[10];
 lt.shoulder=math::Vec3{-.2f,1.5f,0};rt.shoulder=math::Vec3{.2f,1.5f,0};
 unsigned coupled=0;
 for(int step=-24;step<=24;++step){
  const float angle=float(step)*.1f;auto aim=At(0,0,0);
  aim.values[0][0]=aim.values[2][2]=std::cos(angle);aim.values[0][2]=std::sin(angle);aim.values[2][0]=-std::sin(angle);
  auto aimed=native;for(auto& m:aimed)m=Multiply(m,aim);
  auto oldLeft=lt,oldRight=rt;oldLeft.shoulder.reset();oldRight.shoulder.reset();
  const auto old=SolveTrackedArms(parents,aimed,left,right,oldLeft,oldRight);CHECK(old);if(old->targetError[0]>.01f)++coupled;
  const auto fixed=SolveTrackedArms(parents,aimed,left,right,lt,rt);CHECK(fixed&&!fixed->reachClamped[0]&&!fixed->reachClamped[1]);
  auto anchored=aimed;for(const auto& w:fixed->writes)anchored[w.index]=w.transform;
  CHECK(Close(anchored[5],lt.wrist)&&Close(anchored[10],rt.wrist));
  CHECK(Distance(anchored[1],native[1])<.0001f&&Distance(anchored[6],native[6])<.0001f);
  CHECK(Near(Distance(anchored[1],anchored[3]),Distance(aimed[1],aimed[3]),.0001f));
  CHECK(Near(Distance(anchored[3],anchored[5]),Distance(aimed[3],aimed[5]),.0001f));
  CHECK(Close(Multiply(aimed[11],*InverseAnimatedTransform(aimed[5])),Multiply(anchored[11],*InverseAnimatedTransform(anchored[5]))));
 }
 CHECK(coupled>0);
 lt.shoulder=math::Vec3{std::numeric_limits<float>::quiet_NaN(),0,0};CHECK(!SolveTrackedArms(parents,native,left,right,lt,rt));
 lt.shoulder.reset();rt.shoulder.reset();
 // A missing controller leaves exactly its own native branch untouched.
 lt.wrist=native[5];rt.wrist=native[10];rt.wrist.values[3][1]+=.05f;
 lt.enabled=false;result=SolveTrackedArms(parents,native,left,right,lt,rt);CHECK(result&&result->writes.size()==6);
 posed=native;for(const auto& w:result->writes)posed[w.index]=w.transform;
 for(unsigned i:{1u,2u,3u,4u,5u,11u})CHECK(posed[i].values==native[i].values);
 CHECK(Close(posed[10],rt.wrist));
 lt.enabled=true;rt.enabled=false;result=SolveTrackedArms(parents,native,left,right,lt,rt);CHECK(result&&result->writes.size()==6);
 posed=native;for(const auto& w:result->writes)posed[w.index]=w.transform;
 for(unsigned i:{6u,7u,8u,9u,10u,12u})CHECK(posed[i].values==native[i].values);
 rt.enabled=true;rt.wrist=native[10];
 // Zero-distance and far targets remain finite and bounded; no stretched arms.
 lt.wrist=At(-.2f,1.5f,0);lt.poleDirection={0,0,0};CHECK(SolveTrackedArms(parents,native,left,right,lt,rt));
 lt.wrist=At(-10,10,10);result=SolveTrackedArms(parents,native,left,right,lt,rt);CHECK(result&&result->reachClamped[0]&&result->targetError[0]>1);
 // The same solution follows an arbitrary rigid game/world frame.
 lt.wrist=At(-.4f,1.4f,.3f);lt.poleDirection={-.2f,-.25f,.2f};result=SolveTrackedArms(parents,native,left,right,lt,rt);CHECK(result);
 auto frame=At(643,121,-107);frame.values[0][0]=0;frame.values[0][2]=1;frame.values[2][0]=-1;frame.values[2][2]=0;
 auto transformed=native;for(auto& m:transformed)m=Multiply(m,frame);
 auto l2=lt,r2=rt;l2.wrist=Multiply(lt.wrist,frame);r2.wrist=Multiply(rt.wrist,frame);l2.poleDirection={-lt.poleDirection.z,lt.poleDirection.y,lt.poleDirection.x};r2.poleDirection={-rt.poleDirection.z,rt.poleDirection.y,rt.poleDirection.x};
 const auto moved=SolveTrackedArms(parents,transformed,left,right,l2,r2);CHECK(moved&&moved->writes.size()==result->writes.size());for(unsigned i=0;i<moved->writes.size();++i)CHECK(Close(moved->writes[i].transform,Multiply(result->writes[i].transform,frame),.001f));
 lt.wrist.values[0][0]=std::numeric_limits<float>::quiet_NaN();CHECK(!SolveTrackedArms(parents,native,left,right,lt,rt));lt.wrist=native[5];
 lt.poleDirection={1e30f,0,0};CHECK(!SolveTrackedArms(parents,native,left,right,lt,rt));lt.poleDirection={-.2f,-.25f,.2f};
 parents[0]=1;CHECK(!SolveTrackedArms(parents,native,left,right,lt,rt));parents[0]=-1;
 // A hidden weapon leaf outside both arm branches cannot drop arm tracking.
 auto withLeafParents=std::vector<std::int32_t>(parents.begin(),parents.end());withLeafParents.push_back(0);
 auto withLeafNative=std::vector<math::Matrix4>(native.begin(),native.end());auto hidden=At(0,1,0);
 hidden.values[0][0]=hidden.values[1][1]=hidden.values[2][2]=.0001f;withLeafNative.push_back(hidden);
 auto armOnly=SolveTrackedArms(withLeafParents,withLeafNative,left,right,lt,rt);CHECK(armOnly);
 for(const auto& w:armOnly->writes)CHECK(w.index<13);
 native[3]=native[1];CHECK(!SolveTrackedArms(parents,native,left,right,lt,rt));
 return 0;
}
