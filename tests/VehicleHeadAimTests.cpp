#include "Test.h"
#include "Bc2BoatAim.h"
#include "fvr/interaction/VehicleHeadAim.h"
#include <iostream>
#include <limits>
using namespace fvr;using namespace fvr::interaction;
namespace {
math::Matrix4 Identity(){math::Matrix4 m{};for(unsigned n=0;n<4;++n)m.values[n][n]=1;return m;}
math::Matrix4 Yaw(float y){auto m=Identity();m.values[0]={std::cos(y),0,-std::sin(y),0};m.values[2]={std::sin(y),0,std::cos(y),0};return m;}
bool Same(const math::Matrix4&a,const math::Matrix4&b){for(unsigned r=0;r<4;++r)for(unsigned c=0;c<4;++c)if(!Near(a.values[r][c],b.values[r][c],.0005f))return false;return true;}
InputFrame Input(){InputFrame i;i.generation=i.spaceGeneration=1;i.predictedNs=1000000000;i.focused=i.headValid=true;i.worldUnitsPerMeter=1;return i;}
VehicleAimObservation Sample(){return {{1,2,3,4},1,Identity(),Identity(),Identity(),1000000000,1100000000,true};}
int StableCameraDoesNotDoubleTurret(){VehicleHeadAim a;auto s=Sample();auto i=Input();CHECK(a.Update(s,i,1000000000).ready);
 i.head.orientation={0,-std::sin(.15f),0,std::cos(.15f)};++i.generation;const auto desired=a.Update(s,i,1010000000);CHECK(desired.ready&&desired.yaw>0);
 s.camera=Yaw(.3f);const auto caught=a.Update(s,i,1020000000);CHECK(caught.ready&&Near(caught.yaw,0)&&Same(caught.stableCamera,Identity()));
 CHECK(Same(caught.desiredCamera,Yaw(.3f)));return 0;}
int NativeHullMotionAndHeadLean(){VehicleHeadAim a;auto s=Sample();auto i=Input();i.head.position={.6f,.1f,-.2f};CHECK(a.Update(s,i,1000000000).ready);
 s.hull=Yaw(.7f);s.hull.values[3]={5,6,7,1};s.camera=s.hull;i.head.position.x+=.1f;++i.generation;
 const auto result=a.Update(s,i,1010000000);CHECK(result.ready&&Same(result.stableCamera,s.camera)&&Near(result.yaw,0));
 float distance=0;for(unsigned n=0;n<3;++n)distance+=std::pow(result.desiredCamera.values[3][n]-s.camera.values[3][n],2.f);CHECK(Near(std::sqrt(distance),.1f));return 0;}
int OriginalDeadlineAndLoss(){VehicleHeadAim a;auto s=Sample();auto i=Input();CHECK(a.Update(s,i,1000000000).ready);
 CHECK(!a.Update(s,i,1100000000).ready);CHECK(!a.Update(s,i,999999999).ready);i.focused=false;CHECK(!a.Update(s,i,1000000001).ready);i.focused=true;s.verified=false;CHECK(!a.Update(s,i,1000000001).ready);return 0;}
int OwnerAndSpaceRebase(){VehicleHeadAim a;auto s=Sample();auto i=Input();CHECK(a.Update(s,i,1000000000).ready);i.head.orientation={0,-std::sin(.4f),0,std::cos(.4f)};CHECK(a.Update(s,i,1000000001).yaw>0);
 ++s.owner.seat;CHECK(Near(a.Update(s,i,1000000002).yaw,0));++i.spaceGeneration;CHECK(!a.Update(s,i,1000000003).ready);s.space=i.spaceGeneration;CHECK(Near(a.Update(s,i,1000000004).yaw,0));return 0;}
int BoundedCommandsAndNoRoll(){VehicleHeadAim a;auto s=Sample();auto i=Input();CHECK(a.Update(s,i,1000000000).ready);i.head.orientation={0,-std::sin(1.3f),0,std::cos(1.3f)};
 auto r=a.Update(s,i,1000000001);CHECK(r.ready&&Near(std::abs(r.yaw),.08f)&&r.pitch==0);i.head.orientation={0,0,std::sin(.4f),std::cos(.4f)};r=a.Update(s,i,1000000002);CHECK(r.ready&&r.yaw==0&&r.pitch==0);return 0;}
int InvalidNativeOrTuning(){VehicleHeadAim a;auto s=Sample();auto i=Input();s.hull.values[0][0]=std::numeric_limits<float>::quiet_NaN();CHECK(!a.Update(s,i,1000000000).ready);s=Sample();VehicleAimLimits l;l.yawSign=0;CHECK(!a.Update(s,i,1000000000,l).ready);l={};l.maxInput=1;CHECK(!a.Update(s,i,1000000000,l).ready);return 0;}
int StereoUsesOneStableBase(){VehicleHeadAim a;auto s=Sample();auto i=Input();i.head.position={.6f,.1f,-.2f};CHECK(a.Update(s,i,1000000000).ready);
 s.camera=Yaw(.3f);s.camera.values[3]={10,20,30,1};i.head.orientation={0,-std::sin(.15f),0,std::cos(.15f)};auto o=a.Update(s,i,1000000001);CHECK(o.ready);
 auto base=VehicleAimViewBase(o.stableCamera,a.Reference(),i.referenceHead,1);CHECK(base);
 auto eyes=math::ComputeEyePoses(i.head,.064f);CHECK(eyes);auto l=math::ComposeRuntimeHeadWithLhCamera(*base,i.referenceHead,eyes->left,1),r=math::ComposeRuntimeHeadWithLhCamera(*base,i.referenceHead,eyes->right,1);CHECK(l&&r);
 float distance=0;for(unsigned n=0;n<3;++n)distance+=std::pow(l->values[3][n]-r->values[3][n],2.f);CHECK(Near(std::sqrt(distance),.064f));return 0;}
int NativeCameraBasisAndSignedResponse(){
 // Authored component and selected native camera differ by local yaw pi.
 // Response signs below were measured by the bounded native 045259 fixture.
 const auto facing=Yaw(3.141592653589793f);
 const auto pitch=[](float p){auto m=Identity();m.values[1]={0,std::cos(p),-std::sin(p),0};m.values[2]={0,std::sin(p),std::cos(p),0};return m;};
 const auto limits=bc2::PblDriverAimLimits();CHECK(limits.yawSign==1&&limits.pitchSign==1);
 CHECK(Near(limits.pitchMin,-.2617994f)&&Near(limits.pitchMax,.6108652f));
 for(unsigned axis=0;axis<2;++axis)for(float direction:{-1.f,1.f}){
  VehicleHeadAim a;auto s=Sample();auto i=Input();s.camera=s.neutralCameraLocal=facing;CHECK(a.Update(s,i,1000000000,limits).ready);
  if(axis==0)i.head.orientation={0,direction*std::sin(.1f),0,std::cos(.1f)};
  else i.head.orientation={direction*std::sin(.1f),0,0,std::cos(.1f)};
  ++i.generation;auto before=a.Update(s,i,1000000001,limits);CHECK(before.ready&&Same(before.stableCamera,facing));
  const float error=axis==0?before.yawError:before.pitchError,command=axis==0?before.yaw:before.pitch;
  CHECK(std::abs(command)>.001f);
  // Apply only the actual observed native response sign, not an invented rate.
  const auto component=axis==0?Yaw(command):pitch(-command);
  s.camera=Multiply(facing,component);++i.generation;auto after=a.Update(s,i,1000000002,limits);
  CHECK(after.ready&&std::abs(axis==0?after.yawError:after.pitchError)<std::abs(error));
  auto base=VehicleAimViewBase(after.stableCamera,a.Reference(),i.referenceHead,1);CHECK(base);
  auto view=math::ComposeRuntimeHeadWithLhCamera(*base,i.referenceHead,i.head,1);CHECK(view&&Same(*view,after.desiredCamera));
 }
 return 0;
}
int SharedReferenceSurvivesReconnect(){VehicleHeadAim a;auto s=Sample();auto i=Input();i.head.position={.7f,1.4f,-.3f};i.head.orientation={0,-std::sin(.2f),0,std::cos(.2f)};
 CHECK(a.Update(s,i,1000000000).ready);i.head.orientation={0,-std::sin(.35f),0,std::cos(.35f)};auto aim=a.Update(s,i,1000000001);CHECK(aim.ready);
 i.referenceHead.position={2,3,4};i.referenceHead.orientation={0,std::sin(.5f),0,std::cos(.5f)};
 auto base=VehicleAimViewBase(aim.stableCamera,a.Reference(),i.referenceHead,1);CHECK(base);auto view=math::ComposeRuntimeHeadWithLhCamera(*base,i.referenceHead,i.head,1);CHECK(view&&Same(*view,aim.desiredCamera));
 i.focused=false;CHECK(!a.Update(s,i,1000000002).ready);i.focused=true;++i.spaceGeneration;++s.space;i.head.position={3,4,5};
 aim=a.Update(s,i,1000000003);CHECK(aim.ready&&Near(aim.yaw,0));base=VehicleAimViewBase(aim.stableCamera,a.Reference(),i.referenceHead,1);CHECK(base);view=math::ComposeRuntimeHeadWithLhCamera(*base,i.referenceHead,i.head,1);CHECK(view&&Same(*view,aim.desiredCamera));return 0;}
}
int main(){if(StableCameraDoesNotDoubleTurret()||NativeHullMotionAndHeadLean()||OriginalDeadlineAndLoss()||OwnerAndSpaceRebase()||BoundedCommandsAndNoRoll()||InvalidNativeOrTuning()||StereoUsesOneStableBase()||SharedReferenceSurvivesReconnect()||NativeCameraBasisAndSignedResponse())return 1;std::cout<<"9 vehicle head-aim policy groups passed\n";}
