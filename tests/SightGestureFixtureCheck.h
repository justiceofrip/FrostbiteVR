#pragma once
#include "../src/platform/windows/SightGestureFixtureGeometry.h"
#include "fvr/interaction/SightFlip.h"
#include "Test.h"
#include <iostream>
namespace sight_receiver_regression {
using namespace fvr;using namespace fvr::interaction;using namespace fvr::bc2::sight_fixture;
math::Vec3 Reconstruct(math::Vec3 input,bool secondary){const auto w=secondary?SecondaryWristOffset:PrimaryWristOffset;
 return {-input.x+w.x+PalmOffset.x,input.y+.4f+w.y+PalmOffset.y,input.z+w.z+PalmOffset.z};}
float Distance(math::Vec3 p,bool secondary=false){const auto along=std::clamp(secondary?p.y-Pivot.y:Pivot.z-p.z,.04f,.11f);
 return std::hypot(p.x-Pivot.x,p.y-Pivot.y-(secondary?along:0.f),p.z-Pivot.z+(secondary?0.f:along));}
inline int CheckGeometry(){
 const math::Vec3 oldInput{-(.03208f+.021819f),-.4f+.05370f+.071912f,-.58540f-.09f+.210652f};
 const auto oldDistance=Distance(Reconstruct(oldInput,false));CHECK(oldDistance>.11f&&oldDistance<.112f);
 for(bool secondary:{false,true}){
  for(unsigned n=0;n<=100;++n){const float angle=float(n)*1.570796327f/100;
   const auto p=Reconstruct(SightGrip(secondary,angle),secondary);
   CHECK(std::abs(p.x-Pivot.x)<.000001f);
   CHECK(std::abs(p.y-(Pivot.y+.09f*std::sin(angle)))<.000001f);
   CHECK(std::abs(p.z-(Pivot.z-.09f*std::cos(angle)))<.000001f);
  }
  SightFlipConfig c;c.pivotMeters=Pivot;c.axis={1,0,0};c.grabRadiusMeters=.08f;c.holdRadiusMeters=.20f;c.minLeverMeters=.015f;
  c.thresholdRadians=.40f;c.hysteresisRadians=.08f;c.maxStepRadians=.70f;c.detentHoldNs=70000000;c.gestureTimeoutNs=2500000000;c.ackTimeoutNs=1500000000;c.maxSampleGapNs=250000000;
  SightFlip policy(c);SightFlipSample s;s.owner={1,2,3,4};s.focused=s.tracked=s.contactValid=s.nativeModeValid=true;
  s.nativeMode=secondary?SightMode::Secondary:SightMode::Primary;s.sequence=1;s.nowNs=1000000000;
  const auto at=[&](float angle){s.handLocalMeters=Reconstruct(SightGrip(secondary,angle),secondary);s.contactDistanceMeters=Distance(s.handLocalMeters,secondary);};
  const float begin=secondary?1.570796327f:0.f;at(begin);policy.Update(s);s.squeeze=1;++s.sequence;s.nowNs+=20000000;
  CHECK(policy.Update(s).grabbed);std::optional<SightFlipRequest> request;
  for(unsigned n=1;n<=50;++n){at(secondary?begin-float(n)*begin/50:float(n)*1.570796327f/50);++s.sequence;s.nowNs+=20000000;
   const auto out=policy.Update(s);if(out.request){request=out.request;break;}}
  CHECK(request&&request->target==(secondary?SightMode::Primary:SightMode::Secondary));
  s.nativeMode=request->target;s.acknowledgedRequest=request->id;++s.sequence;s.nowNs+=20000000;
  CHECK(policy.Update(s).committedMode==request->target);
 }
 const auto support=LauncherSupportGrip();const math::Vec3 wrist{-support.x+SecondaryWristOffset.x,support.y+.4f+SecondaryWristOffset.y,support.z+SecondaryWristOffset.z};
 CHECK(std::abs(std::hypot(wrist.x-LauncherSupportWrist.x,wrist.y-LauncherSupportWrist.y,wrist.z-LauncherSupportWrist.z)-.01f)<.000001f);
 std::cout<<"Sight fixture: legacy111mm miss reproduced; both mapped sweeps and actual policy open/close/acks pass; launcher support10mm approach passes\n";
 return 0;
}



} // namespace sight_receiver_regression
