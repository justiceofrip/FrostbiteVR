#include "Bc2MagazineConsumerFixture.h"
#include "fvr/interaction/AmmoSupplyVisual.h"
#include <cstdio>
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
void WorldSend(Fixture& f,bool grip,const math::Matrix4& wrist,const math::Matrix4& weapon,const math::Matrix4& bodyHand){
 f.now+=20*Ms;++f.s.input.sequence;f.s.input.observedNs=f.s.input.nowNs=f.now;f.s.input.deadlineNs=f.now+100*Ms;
 f.s.input.released[0]=!grip;f.s.gripPressed=grip;f.s.ejectPressed=false;f.s.geometrySequence=f.s.input.sequence;
 f.s.raw.inputEvidence=f.s.input;f.s.originalHandEvidence=f.s.input;f.s.raw.rawLeftWristWorldMeters=wrist;
 f.s.raw.weaponWorldMeters=weapon;f.s.bodyFromHand=bodyHand;f.Sync();
}
#ifndef FVR_REPLACEMENT_BASELINE
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
int ReplacementStaysAtItsAcquisitionHandWhenWeaponMoves(){
 for(const auto* profile:Profiles()){
  CHECK(profile&&profile->Ready());Fixture f(true,*profile);CHECK(f.Eject());f.Send();
  const auto wrist=Turn(-.23f,-.55f,.02f,.35f),sourceWeapon=Turn(.10f,-.08f,-.35f,-.4f);
  WorldSend(f,true,wrist,sourceWeapon,Pose());
  CHECK(f.result.acquired==1&&f.result.tracking.target&&f.result.tracking.target->role==MagazinePropRole::Replacement);
  CHECK(f.result.interaction.insertion.phase==ReloadInsertionPhase::Free&&f.submits==0);
  const auto rig=Rig(*profile->geometry,f.s.nativeOwner);const auto binding=magazine_presentation_detail::Derive(rig,*profile->geometry);CHECK(binding);
  const auto expectedItem=Multiply(*InverseRigid(profile->geometry->interaction.insertion.itemFromHand),wrist);
  for(float scale:{.7f,1.f,1.4f})for(const auto& currentWeapon:{sourceWeapon,Turn(.5f,.3f,-.7f,.6f)}){
   const auto plan=BuildMagazinePresentation(rig,*binding,f.result.tracking,Units(currentWeapon,scale),scale,f.now);
   if(!plan.wristTarget||!Same(*plan.wristTarget,Units(wrist,scale)))std::printf("profile=%s scale=%g moved=%u binding=%u wrist=%u error=%g\n",
    profile->geometry->asset.data(),double(scale),unsigned(!Same(currentWeapon,sourceWeapon)),unsigned(bool(plan.binding)),unsigned(bool(plan.wristTarget)),
    plan.wristTarget?double(Distance(*plan.wristTarget,Units(wrist,scale))):-1.);
   CHECK(plan.binding&&plan.wristTarget&&Same(*plan.wristTarget,Units(wrist,scale)));
   CHECK(Same(plan.writes.front().transform,Units(expectedItem,scale)));
  }
  CHECK(f.reserve.loaded==27&&f.reserve.reserve==83&&f.submits==0&&f.starts==1);
 }return 0;
}
#ifndef FVR_REPLACEMENT_BASELINE
int BodyFrameSupplyAndRecenter(){
 for(const auto* profile:Profiles())for(float yaw:{0.f,.7f}){
  Fixture f(true,*profile);const auto pouch=ChestAmmoSupply();f.policy.reset();f.policy.emplace(true,f.api,pouch);
  f.policy->EnableBodyAmmo(true,SupplyAnchorFrame::RecenteredBody);CHECK(f.Eject());f.Send();
  const auto input=BodyInput(f.s.input.sequence+1,f.s.input.owner.space,yaw,{1.2f,1.7f,-2.f});
  const auto contact=BodyAnchorHandPose(input,InteractionHand::Left);CHECK(contact);
  CHECK(Distance(*contact,Pose(-.23f,-.55f,.02f))<.0001f);
  const auto body=Turn(4.f,2.f,-3.f,.3f),raw=RawWorld(input,body),weapon=Turn(4.3f,1.8f,-3.4f,-.2f);
  WorldSend(f,true,raw,weapon,*contact);CHECK(f.result.acquired==1&&!f.result.bodyAmmo&&f.result.tracking.replacementFrame);
  const auto rig=Rig(*profile->geometry,f.s.nativeOwner);const auto binding=magazine_presentation_detail::Derive(rig,*profile->geometry);CHECK(binding);
  const auto pose=BuildMagazinePresentation(rig,*binding,f.result.tracking,Turn(-2,4,7,.8f),1,f.now);
  CHECK(pose.wristTarget&&Same(*pose.wristTarget,raw)&&f.submits==0&&f.reserve.reserve==83);
  const auto old=f.result.tracking;++f.s.nativeOwner.space;++f.reserve.identity.owner.space;
  ++f.s.input.owner.space;++f.s.trackingEpoch;f.meshes->owner=f.s.nativeOwner;f.s.raw.owner=f.s.nativeOwner;
  WorldSend(f,true,raw,weapon,*contact);CHECK(!f.result.tracking.target&&!f.result.bodyAmmo&&f.cancels==1);
  CHECK(!MagazineTargetRetained(old,f.result.tracking,f.now));
 }return 0;
}
int ReplacementFrameNeverRenewsOldEvidence(){
 for(const auto* profile:Profiles()){
  Fixture f(true,*profile);CHECK(f.Eject());f.Send();const auto raw=Turn(-.23f,-.55f,.02f,.3f),weapon=Turn(.1f,0,-.3f,.1f);
  WorldSend(f,true,raw,weapon,Pose());CHECK(f.result.tracking.replacementFrame);const auto original=f.result.tracking;
  const auto frame=*original.replacementFrame;
  f.s.raw.weaponWorldMeters=Turn(6,7,8,1.1f);f.now+=Ms;f.Sync();
  CHECK(f.result.tracking.replacementFrame&&f.result.tracking.replacementFrame->weaponWorldMeters.values==frame.weaponWorldMeters.values);
  CHECK(f.result.tracking.replacementFrame->observedNs==frame.observedNs&&f.result.tracking.replacementFrame->deadlineNs==frame.deadlineNs);
  f.source=false;f.now+=Ms;f.Sync();CHECK(f.result.tracking.target&&f.result.tracking.replacementFrame);
  CHECK(f.result.tracking.replacementFrame->weaponWorldMeters.values==frame.weaponWorldMeters.values&&
   f.result.tracking.replacementFrame->deadlineNs==frame.deadlineNs);f.source=true;
  for(unsigned fault=0;fault<8;++fault){auto bad=original;
   if(fault==0)++bad.replacementFrame->inputSequence;if(fault==1)++bad.replacementFrame->owner.space;
   if(fault==2)++bad.replacementFrame->item.generation;if(fault==3)++bad.replacementFrame->handClaim.id;
   if(fault==4)++bad.replacementFrame->cycle;if(fault==5)++bad.replacementFrame->deadlineNs;
   if(fault==6)bad.replacementFrame->weaponWorldMeters.values[0][0]=-1;
   if(fault==7)bad.target->role=MagazinePropRole::Removed;
   CHECK(!MagazineTargetFresh(bad,f.now));}
  auto changed=original;changed.replacementFrame->weaponWorldMeters.values[3][0]+=.1f;
  CHECK(!MagazineTargetRetained(original,changed,f.now));
  CHECK(!MagazineTargetFresh(original,frame.deadlineNs));
  f.now=frame.deadlineNs;f.s.input.nowNs=f.now;f.Sync();CHECK(!f.result.tracking.target);
 }return 0;
}
int GuidedAndAttachedStayWithWeapon(){
 for(const auto* profile:Profiles()){
  Fixture f(true,*profile);CHECK(f.Eject());f.Send();f.s.bodyFromHand=Pose();f.Send(true,false,-.1f);
  CHECK(f.result.tracking.replacementFrame);bool guided=false;
  for(float z:{-.075f,-.04f,0.f,.02f,.04f,.06f,.08f,.1f,.1f,.1f,.1f,.1f,.1f}){
   f.Send(true,false,z);if(!f.result.tracking.target)continue;
   if(f.result.interaction.insertion.phase!=ReloadInsertionPhase::Free){CHECK(!f.result.tracking.replacementFrame);guided=true;
    const auto rig=Rig(*profile->geometry,f.s.nativeOwner);const auto binding=magazine_presentation_detail::Derive(rig,*profile->geometry);CHECK(binding);
    const auto weapon=Turn(2,3,4,.7f);const auto p=BuildMagazinePresentation(rig,*binding,f.result.tracking,weapon,1,f.now);
    CHECK(p.binding&&Same(p.writes.front().transform,Multiply(f.result.tracking.target->weaponFromItemMeters,weapon)));}
   if(f.submits)break;
  }
  CHECK(guided&&f.submits==1&&f.reserve.loaded==27&&f.reserve.reserve==83);
  f.Send(false);CHECK(f.result.tracking.target&&f.result.tracking.target->role==MagazinePropRole::Attached&&!f.result.tracking.replacementFrame);
 }return 0;
}
int OriginalRemovalDoesNotBecomeReplacementSupply(){
 for(const auto* profile:Profiles()){
  Fixture f(true,*profile);f.policy->EnableBodyAmmo();const auto travel=profile->geometry->interaction.insertion.travelMeters;
  f.Send(false,false,travel);f.Send(true,false,travel);CHECK(f.starts==1);f.held=true;f.reserve.reloadInputReady=false;
  for(float d=.02f;d<=.16f;d+=.02f)f.Send(true,false,travel-d);
  CHECK(f.result.tracking.target&&f.result.tracking.target->role==MagazinePropRole::Removed);
  CHECK(!f.result.tracking.replacementFrame&&!f.result.bodyAmmo&&f.result.acquired==0&&f.submits==0);
  for(float d=.16f;d>=0;d-=.02f)f.Send(true,false,travel-d);
  for(unsigned n=0;n<12&&!f.result.interaction.originalSeat;++n)f.Send(true,false,travel);
  CHECK(f.result.interaction.originalSeat&&!f.result.tracking.replacementFrame&&f.result.acquired==0&&f.submits==0);
  CHECK(f.reserve.loaded==27&&f.reserve.reserve==83);
 }return 0;
}
int BodyAmmoUsesSamePoolAndContactWithoutAllocatingItem(){
 for(const auto* profile:Profiles()){
  for(bool alternate:{false,true}){Fixture f(true,*profile);auto pouch=ChestAmmoSupply();if(!alternate)pouch.alternateContact.reset();
   f.policy.reset();f.policy.emplace(true,f.api,pouch);f.Send();CHECK(!f.result.bodyAmmo);
   f.policy->EnableBodyAmmo(true,SupplyAnchorFrame::RecenteredBody);f.Send();CHECK(f.result.bodyAmmo&&AmmoSupplyVisualFresh(*f.result.bodyAmmo,f.now));
   const auto v=*f.result.bodyAmmo;CHECK(v.contact.centerMeters==(alternate?pouch.alternateContact->centerMeters:pouch.pouchCenterMeters));
   CHECK(v.source.reserveUnits==83&&v.source.objectUnits==3&&v.source.identity.pool.id==f.reserve.identity.serverItem);
   CHECK(v.source.observedNs==f.reserve.observedNs&&v.source.deadlineNs==f.reserve.deadlineNs&&v.input.observedNs==f.s.input.observedNs);
   CHECK(!f.hands.Current(InteractionHand::Left)&&f.result.acquired==0&&f.starts==0&&f.submits==0);
   CHECK(f.Eject());CHECK(f.result.bodyAmmo);f.Send();
   f.s.bodyFromHand=Pose(v.contact.centerMeters[0],v.contact.centerMeters[1],v.contact.centerMeters[2]);f.Send(true,false,-.1f);
   CHECK(f.result.acquired==1&&!f.result.bodyAmmo&&f.result.tracking.target&&f.result.tracking.target->role==MagazinePropRole::Replacement);
   CHECK(f.reserve.reserve==83&&f.submits==0);
  }
  for(unsigned fault=0;fault<6;++fault){Fixture f(true,*profile);f.policy->EnableBodyAmmo();f.Send();CHECK(f.result.bodyAmmo);
   if(fault==0)f.reserve.loaded=0;if(fault==1)f.reserve.loaded=f.reserve.capacity;if(fault==2)f.reserve.reserve=0;
   if(fault==3)f.s.input.focused=false;if(fault==4)f.source=false;if(fault==5)f.familyFault=3;
   f.Send();
   // Loaded count does not hide existing reserve ammo. Visibility alone
   // still cannot allocate a replacement or start/submit a native reload.
   CHECK(bool(f.result.bodyAmmo)==(fault<2));
   if(f.result.bodyAmmo)CHECK(AmmoSupplyVisualFresh(*f.result.bodyAmmo,f.now));
   CHECK(f.result.acquired==0&&f.starts==0&&f.submits==0);}
 }return 0;
}
#endif
}
int main(){
#ifdef FVR_EXPECT_GENERATED_MAGAZINE
 CHECK(FindMagazineEquipment("AEK971_sp")&&FindMagazineEquipment("AEK971_sp")->Ready()&&Profiles().size()==2);
#endif
 if(ReplacementStaysAtItsAcquisitionHandWhenWeaponMoves())return 1;
#ifndef FVR_REPLACEMENT_BASELINE
 if(BodyFrameSupplyAndRecenter()||ReplacementFrameNeverRenewsOldEvidence()||
 GuidedAndAttachedStayWithWeapon()||OriginalRemovalDoesNotBecomeReplacementSupply()||BodyAmmoUsesSamePoolAndContactWithoutAllocatingItem())return 1;
#endif
 std::printf("6 replacement spawn actual-consumer groups passed; profiles=%zu; native API mocked\n",Profiles().size());}
