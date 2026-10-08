#include "Bc2MagazineConsumerFixture.h"
#include "fvr/interaction/AmmoSupplyVisual.h"
#include "fvr/interaction/ReloadGrip.h"
#include <cstdio>
#include "Bc2SightContact.h"
using namespace magazine_consumer_fixture;
namespace {
std::vector<const MagazineEquipmentProfile*> Profiles(){
 std::vector<const MagazineEquipmentProfile*> result{&Xm8MagazineEquipment()};
 const auto* optional=FindMagazineEquipment("AEK971_sp");if(optional&&optional->Ready())result.push_back(optional);return result;
}
math::Matrix4 Turn(float x,float y,float z,float angle){auto m=Pose(x,y,z);m.values[0]={std::cos(angle),0,-std::sin(angle),0};
 m.values[2]={std::sin(angle),0,std::cos(angle),0};return m;}
math::Matrix4 Units(math::Matrix4 m,float scale){for(unsigned n=0;n<3;++n)m.values[3][n]*=scale;return m;}
RigSnapshot Rig(const MagazineGeometryProfile& p,const ReloadStateOwner& owner){RigSnapshot r;
 r.names={"scene",std::string(p.bones.weapon),std::string(p.bones.wrist),std::string(p.bones.magazine)};
 r.parents={-1,0,0,1};r.weaponBone=1;r.identity.soldier=owner.soldier;r.identity.weak=owner.weak;
 for(unsigned n=0;n<15;++n){r.parents.push_back(n%3?int(r.names.size()-1):2);r.names.push_back(std::string(p.bones.fingers[n]));}
 r.inverseBind.assign(r.names.size(),Identity());r.evaluatedWorld=r.inverseBind;return r;}
// Actual consumer input, supply, hand claims and presentation; native calls are mocked.
void WorldSend(Fixture& f,bool grip,const math::Matrix4& wrist,const math::Matrix4& weapon,const math::Matrix4& bodyHand,float squeeze=-1,bool legacyThreshold=false){
 f.now+=20*Ms;++f.s.input.sequence;f.s.input.observedNs=f.s.input.nowNs=f.now;f.s.input.deadlineNs=f.now+100*Ms;
 f.s.input.released[0]=!grip;f.s.gripPressed=grip;f.s.ejectPressed=false;f.s.geometrySequence=f.s.input.sequence;
 f.s.raw.inputEvidence=f.s.input;f.s.originalHandEvidence=f.s.input;f.s.raw.rawLeftWristWorldMeters=wrist;
 f.s.raw.weaponWorldMeters=weapon;f.s.bodyFromHand=bodyHand;
 if(squeeze>=0){f.s.input.released[0]=squeeze<=.35f;f.s.raw.inputEvidence=f.s.input;f.s.originalHandEvidence=f.s.input;
  f.s.gripPressed=legacyThreshold?squeeze>=.75f:ReloadGripActive(squeeze,f.s.input,f.hands.Current(InteractionHand::Left));}
 f.Sync();
}
int ReloadGripPressureUsesReleaseThresholdForOwnedObjects(){
 for(const auto* profile:Profiles())for(bool legacy:{true,false}){
  Fixture f(true,*profile);const auto travel=profile->geometry->interaction.insertion.travelMeters;
  f.Send(false,false,travel);f.Send(true,false,travel);CHECK(f.starts==1);f.held=true;f.reserve.reloadInputReady=false;
  f.Send(true,false,travel-.01f);CHECK(f.result.interaction.phase==DetachableMagazinePhase::Pulling);
  const auto wrist=f.s.raw.rawLeftWristWorldMeters;
  WorldSend(f,true,wrist,Identity(),Pose(5,5,5),.6f,legacy);
  if(legacy){CHECK(f.cancels==1&&f.result.interaction.reason==DetachableMagazineReason::PullAbandoned);
   CHECK(f.result.interaction.motionFailure&&f.result.interaction.motionFailure->check==MagazineMotionFailureCheck::GripReleased);
  }else{
   CHECK(f.cancels==0&&f.result.interaction.phase==DetachableMagazinePhase::Pulling&&f.result.ownsLeftHand);
   WorldSend(f,true,wrist,Identity(),Pose(5,5,5),.35f);
   CHECK(f.cancels==1&&!f.hands.Current(InteractionHand::Left));
  }
  CHECK(f.submits==0&&f.reserve.loaded==27&&f.reserve.reserve==83);
 }
 Fixture f;f.Send();const auto in=f.s.input;auto held=f.gun;CHECK(held);
 CHECK(!ReloadGripActive(.6f,in,std::nullopt));
 auto input=in;input.released[0]=false;
 held->token.hand=InteractionHand::Left;held->token.kind=HandClaimKind::AmmoObject;
 CHECK(ReloadGripActive(.6f,input,held));
 held->token.kind=HandClaimKind::Sight;CHECK(!ReloadGripActive(.6f,input,held));
 held->token.kind=HandClaimKind::AmmoObject;held->deadlineNs=input.nowNs;CHECK(!ReloadGripActive(.6f,input,held));
 held->deadlineNs=input.deadlineNs;++held->token.owner.space;CHECK(!ReloadGripActive(.6f,input,held));
 CHECK(ReloadGripActive(.8f,input,std::nullopt));input.released[0]=true;CHECK(!ReloadGripActive(.8f,input,held));
 return 0;
}
#ifndef FVR_REMOVAL_BASELINE
InputFrame BodyInput(std::uint64_t sequence,std::uint64_t space,float yaw,math::Vec3 origin){InputFrame in{};
 in.generation=sequence;in.spaceGeneration=space;in.predictedNs=1000*Ms;in.focused=in.headValid=true;
 in.referenceHead.position=origin;in.referenceHead.orientation={0,std::sin(yaw*.5f),0,std::cos(yaw*.5f)};
 in.head.position=origin;in.head.orientation={0,std::sin((yaw+.8f)*.5f),0,std::cos((yaw+.8f)*.5f)};
 for(auto& h:in.hands){h.gripTracked=h.aimTracked=true;h.grip.orientation.w=h.aim.orientation.w=1;}
 const float x=-.23f,z=-.02f;
 in.hands[0].grip.position={origin.x+std::cos(yaw)*x+std::sin(yaw)*z,origin.y-.55f,origin.z-std::sin(yaw)*x+std::cos(yaw)*z};
 return in;}
math::Matrix4 RawWorld(const InputFrame& input,const math::Matrix4& body){
 const auto relative=math::MakeRelativePose(input.referenceHead,input.hands[0].grip);
 const auto view=math::MakeLhViewFromOpenXRPose(*relative);return Multiply(*InverseRigid(*view),body);}
#endif
int FreeCarryThenReplacementCompletesThroughConsumer(){
 for(const auto* profile:Profiles()){
  Fixture f(true,*profile);const auto travel=profile->geometry->interaction.insertion.travelMeters;
  f.Send(false,false,travel);f.Send(true,false,travel);CHECK(f.starts==1);f.held=true;f.reserve.reloadInputReady=false;
  for(float d=.02f;d<=.24f;d+=.02f)f.Send(true,false,travel-d);
  CHECK(f.result.interaction.phase==DetachableMagazinePhase::RemovedHeld);
  const auto hand=f.hands.Current(InteractionHand::Left);CHECK(hand);
  const auto wrist=Multiply(f.s.raw.rawLeftWristWorldMeters,Turn(.3f,-.4f,.2f,1.3f));
  WorldSend(f,true,wrist,Identity(),Pose(5,5,5));
  CHECK(f.result.interaction.phase==DetachableMagazinePhase::RemovedHeld&&f.cancels==0);
  CHECK(f.hands.Current(InteractionHand::Left)->token==hand->token&&f.result.tracking.target);
  CHECK(!f.result.interaction.originalSeat&&f.submits==0&&f.reserve.loaded==27&&f.reserve.reserve==83);
  f.Send(false,false,-.4f);CHECK(f.result.interaction.phase==DetachableMagazinePhase::WellEmpty);
  f.s.bodyFromHand=Pose();f.Send(true,false,-.1f);CHECK(f.result.acquired==1);
  for(float z=-.08f;z<travel;z+=.02f)f.Send(true,false,z);
  for(unsigned n=0;n<16&&!f.submits;++n)f.Send(true,false,travel);
  CHECK(f.submits==1&&f.cancels==0&&f.reserve.loaded==27&&f.reserve.reserve==83);
  f.Complete();f.Send(false,false,travel);
  CHECK(f.result.completed==1&&f.reserve.loaded==30&&f.reserve.reserve==80);
 }
 return 0;
}
int OriginalFreeCarryUsesCapturedWeaponFrame(){
 for(const auto* profile:Profiles()){
  Fixture f(true,*profile);const auto travel=profile->geometry->interaction.insertion.travelMeters;
  f.Send(false,false,travel);f.Send(true,false,travel);CHECK(f.starts==1);f.held=true;f.reserve.reloadInputReady=false;
  for(float d=.02f;d<=.32f;d+=.02f)f.Send(true,false,travel-d);
  CHECK(f.result.tracking.target&&f.result.tracking.target->role==MagazinePropRole::Removed);
  CHECK(f.result.interaction.insertion.phase==ReloadInsertionPhase::Free);
  const auto original=f.result.tracking;const auto rig=Rig(*profile->geometry,f.s.nativeOwner);
  const auto binding=magazine_presentation_detail::Derive(rig,*profile->geometry);CHECK(binding);
  const auto expectedItem=original.target->weaponFromItemMeters;
  for(float scale:{.7f,1.f,1.4f}){
   const auto plan=BuildMagazinePresentation(rig,*binding,original,Units(Turn(.45f,.2f,-.4f,.7f),scale),scale,f.now);
   CHECK(plan.binding&&plan.wristTarget);
   const auto error=Distance(plan.writes.front().transform,Units(expectedItem,scale));
   if(error>.00001f)std::printf("original free carry profile=%s error=%gm\n",profile->geometry->asset.data(),double(error)/scale);
   CHECK(Same(plan.writes.front().transform,Units(expectedItem,scale)));
   CHECK(Same(*plan.wristTarget,Units(Multiply(profile->geometry->interaction.insertion.itemFromHand,expectedItem),scale)));
  }
  CHECK(f.result.acquired==0&&f.submits==0&&f.reserve.loaded==27&&f.reserve.reserve==83);
 }return 0;
}
int OutwardRemovalDoesNotRecaptureUntilReturn(){
 for(const auto* profile:Profiles()){
  Fixture f(true,*profile);const auto travel=profile->geometry->interaction.insertion.travelMeters;
  f.Send(false,false,travel);f.Send(true,false,travel);CHECK(f.starts==1);f.held=true;f.reserve.reloadInputReady=false;
  f.Send(true,false,travel);f.Send(true,false,travel);
  const float withdrawal=profile->geometry->interaction.pullMeters+.02f;
  for(float d=.01f;d<=withdrawal+.00001f;d+=.01f){f.Send(true,false,travel-d);
   CHECK(!f.result.interaction.insertion.captured&&!f.result.interaction.originalSeat);
   CHECK(f.result.tracking.target&&f.result.tracking.target->role==MagazinePropRole::Removed);
  }
  for(unsigned n=0;n<8;++n){f.Send(true,false,travel-withdrawal);CHECK(!f.result.interaction.insertion.captured&&!f.result.interaction.originalSeat);}
  bool captured=false;
  for(float d=withdrawal-.01f;d>=-.00001f;d-=.01f){f.Send(true,false,travel-std::max(0.f,d));captured|=f.result.interaction.insertion.captured;
   if(f.result.interaction.insertion.phase==ReloadInsertionPhase::Guided&&f.result.tracking.target){
    CHECK(!f.result.tracking.removalFrame);const auto rig=Rig(*profile->geometry,f.s.nativeOwner);
    const auto binding=magazine_presentation_detail::Derive(rig,*profile->geometry);CHECK(binding);
    const auto current=Turn(3,2,1,.6f);const auto plan=BuildMagazinePresentation(rig,*binding,f.result.tracking,current,1,f.now);
    CHECK(plan.binding&&Same(plan.writes.front().transform,Multiply(f.result.tracking.target->weaponFromItemMeters,current)));
   }}
  for(unsigned n=0;n<12&&!f.result.interaction.originalSeat;++n)f.Send(true,false,travel);
  CHECK(captured&&f.result.interaction.originalSeat&&f.submits==0&&f.result.acquired==0);
  CHECK(f.reserve.loaded==27&&f.reserve.reserve==83);
 }return 0;
}
#ifndef FVR_REMOVAL_BASELINE
struct DetachedFixture {
 MagazineGeometryProfile geometry;MagazineEquipmentProfile profile;Fixture f;Bc2MagazineDetached controller{true};
 MagazinePhysicalResult result;std::array<std::byte,InputBytes> cache{};bool post=true,proof=true,raw=true;
 std::optional<MagazineDetachPairReceipt> pair;
 DetachedFixture(const MagazineEquipmentProfile& original):geometry(*original.geometry),profile(original),f(false,profile){
  profile.geometry=&geometry;const auto rig=Rig(geometry,f.s.nativeOwner);geometry.rigFingerprint=SightRigFingerprint(rig.names,rig.parents,rig.inverseBind);
  f.s.raw.rigFingerprint=geometry.rigFingerprint;f.reserve.loaded=29;f.reserve.reserve=0;f.reserve.allThreeIdle=true;f.reserve.reloadInputReady=false;
 }
 void Send(bool grip,float travel,const math::Matrix4& frame=Identity()){
  f.Send(grip,false,travel);f.s.raw.weaponWorldMeters=frame;
  f.s.raw.rawLeftWristWorldMeters=Multiply(f.s.raw.rawLeftWristWorldMeters,frame);f.s.raw.valid=raw;
  controller.Prepare(f.s,f.reserve,f.seq,0xe0000,f.hands);const auto demand=controller.Demand();
  std::optional<HolsterSuppressionReceipt> receipt;
  if(proof&&demand){HolsterInputOverride patch;const HolsterInputOwner owner{nullptr,[](void*,const HolsterSuppressionRequest&)noexcept{return true;}};
   if(patch.Apply(cache,*demand,owner))receipt=patch.Commit();}
  result=controller.Commit(receipt,pair,post?std::optional{f.reserve}:std::nullopt,f.hands,f.intent,f.now,f.now);
 }
 bool Grab(){const auto travel=geometry.interaction.insertion.travelMeters;Send(false,travel);Send(true,travel);return controller.Grabs()==1;}
};
int DetachedFreeCarryDoesNotRestartOrReturnMagazine(){
 for(const auto* p:Profiles()){
  DetachedFixture d(*p);CHECK(d.Grab());const auto travel=d.geometry.interaction.insertion.travelMeters;
  for(float off=.02f;off<=.24f;off+=.02f)d.Send(true,travel-off);
  const auto hand=d.f.hands.Current(InteractionHand::Left);CHECK(hand);
  d.Send(true,travel-.65f);
  CHECK(d.controller.Phase()==MagazineDetachPhase::Detached&&d.result.tracking.target);
  CHECK(d.f.hands.Current(InteractionHand::Left)->token==hand->token);
  CHECK(d.controller.Seats()==0&&d.controller.Returned()==0&&d.controller.Recovered()==0);
  CHECK(d.f.reserve.loaded==29&&d.f.reserve.reserve==0&&d.f.starts==0&&d.f.submits==0);
 }
 return 0;
}
int DetachedFreeCarryAndReadGapRetainOnlyOriginalPose(){
 for(const auto* p:Profiles()){
  DetachedFixture d(*p);CHECK(d.Grab());const auto travel=d.geometry.interaction.insertion.travelMeters;
  for(float off=.02f;off<=.08f;off+=.02f)d.Send(true,travel-off,Turn(.2f,.1f,-.3f,.2f));
  CHECK(d.result.tracking.target&&d.result.tracking.removalFrame&&!d.result.tracking.retainedVisualSuppression);
  const auto old=d.result.tracking;const auto rig=Rig(d.geometry,d.f.s.nativeOwner);const auto binding=magazine_presentation_detail::Derive(rig,d.geometry);CHECK(binding);
  const auto expected=Multiply(old.target->weaponFromItemMeters,old.removalFrame->weaponWorldMeters);
  d.post=false;d.Send(true,travel,Turn(2,3,4,.6f));
  CHECK(d.result.tracking.target&&d.result.tracking.retainedVisualSuppression&&MagazineTargetFresh(d.result.tracking,d.f.now));
  CHECK(d.controller.Seats()==0&&!d.result.interaction.insertion.seat&&!d.result.interaction.originalSeat);
  CHECK(d.result.tracking.target->inputSequence==old.target->inputSequence&&d.result.tracking.target->deadlineNs==old.target->deadlineNs);
  CHECK(d.result.tracking.detach->current.observedNs==old.detach->current.observedNs&&d.result.tracking.detach->current.deadlineNs==old.detach->current.deadlineNs);
  const auto plan=BuildMagazinePresentation(rig,*binding,d.result.tracking,Turn(-4,6,3,1),1,d.f.now);
  CHECK(plan.binding&&Same(plan.writes.front().transform,expected));
  CHECK(!BuildMagazineRawContact(d.result.tracking,rig,d.geometry.asset,Identity(),Identity(),1,d.f.now).valid);
  // A fresh post-read must recover ordinary contact even if no new raw pose was produced during the gap.
  d.post=true;d.raw=false;d.Send(true,travel);CHECK(!d.result.tracking.retainedVisualSuppression&&MagazineTrackingFresh(d.result.tracking,d.f.now));
  CHECK(BuildMagazineRawContact(d.result.tracking,rig,d.geometry.asset,Identity(),Identity(),1,d.f.now).valid);
  CHECK(d.f.reserve.loaded==29&&d.f.reserve.reserve==0&&d.f.starts==0&&d.f.submits==0);
 }return 0;
}
int ReadGapCannotRenewOrCrossSafetyBoundary(){
 for(const auto* p:Profiles())for(unsigned fault=0;fault<9;++fault){
  DetachedFixture d(*p);CHECK(d.Grab());const auto old=d.result.tracking;
  CHECK(old.target&&old.detach);d.post=false;
  if(fault==0)d.proof=false;
  if(fault==1)d.f.s.cancel=true;
  if(fault==2)d.f.s.input.focused=false;
  if(fault==3)d.f.now=old.target->deadlineNs;
  if(fault==4)--d.f.reserve.loaded;
  if(fault==5)d.f.reserve.verified=false;
  if(fault==6)d.f.reserve.allThreeIdle=false;
  if(fault==7){++d.f.s.input.owner.space;++d.f.s.nativeOwner.space;}
  d.Send(fault!=8,d.geometry.interaction.insertion.travelMeters);
  CHECK(!d.result.tracking.target&&!d.result.tracking.retainedVisualSuppression&&d.controller.Seats()==0&&d.controller.Returned()==0);
 }
 return 0;
}
#endif

}
int main(){
 std::setvbuf(stdout,nullptr,_IONBF,0);
 if(ReloadGripPressureUsesReleaseThresholdForOwnedObjects())return 1;
 if(FreeCarryThenReplacementCompletesThroughConsumer())return 1;
#ifdef FVR_EXPECT_GENERATED_MAGAZINE
 std::puts("profiles");CHECK(Profiles().size()==2);
#endif
 std::puts("original frame");if(OriginalFreeCarryUsesCapturedWeaponFrame())return 1;
 std::puts("original reversal");if(OutwardRemovalDoesNotRecaptureUntilReturn())return 1;
#ifndef FVR_REMOVAL_BASELINE
 if(DetachedFreeCarryDoesNotRestartOrReturnMagazine())return 1;
 std::puts("detached gap");if(DetachedFreeCarryAndReadGapRetainOnlyOriginalPose())return 1;
 std::puts("detached safety");if(ReadGapCannotRenewOrCrossSafetyBoundary())return 1;
#endif
 std::printf("7 removal presentation actual-consumer groups passed; profiles=%zu; native API mocked\n",Profiles().size());
}
