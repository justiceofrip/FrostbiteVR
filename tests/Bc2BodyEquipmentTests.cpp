#include "Bc2BodyEquipment.h"
#include "Bc2StowedAmmo.h"
#include "Bc2BodyPropPair.h"
#include "Bc2StowedAmmoReserve.h"
// Reuse the real coordinator/cache/hand composition fixture. Its native pack
// receipt is an explicit CPU fixture, not a claim of a GPU or headset run.
#define main ExistingBodyHolsterRegressionMain
#include "Bc2BodyHolsterTests.cpp"
#undef main
namespace {
auto IdentityBody(){return interaction::reload_insertion_detail::Identity();}
bool MatrixNear(const math::Matrix4& a,const math::Matrix4& b,float e=.0004f){
 for(unsigned r=0;r<4;++r)for(unsigned c=0;c<4;++c)if(std::abs(a.values[r][c]-b.values[r][c])>e)return false;return true;
}
InputFrame BodyInput(const Fixture& f){InputFrame in;in.generation=f.s.hand.sequence;in.spaceGeneration=f.s.hand.owner.space;
 in.predictedNs=f.s.hand.nowNs;in.focused=in.headValid=true;in.referenceHead.orientation.w=in.head.orientation.w=1;
 in.referenceHead.position=in.head.position={0,1.7f,0};
 for(auto& hand:in.hands){hand.gripTracked=hand.aimTracked=true;hand.grip.orientation.w=hand.aim.orientation.w=1;}return in;}
WeaponEquipmentIdentity Equipment(const Fixture& f){WeaponEquipmentIdentity e;e.weapon=f.s.nativeOwner.weapon;
 e.data=f.s.selected->weaponData;e.persistence=0x180000;
 const auto text=body_equipment_detail::Text(f.s.selected->weaponName);std::memcpy(e.asset.data(),text.data(),text.size());return e;}
std::optional<BodyHolsteredRenderSource> Source(Fixture& f){
 if(!f.out.freeRight)return {};
 const auto slot=std::find_if(f.out.inventory.slots.begin(),f.out.inventory.slots.end(),[&](const auto& a){return a.item.id==f.s.nativeOwner.weapon;});
 if(slot==f.out.inventory.slots.end())return {};
 return BuildBodyHolsteredSource(std::make_shared<const BodyFreeRightEvidence>(*f.out.freeRight),*slot,Equipment(f),BodyInput(f),IdentityBody(),{},f.s.hand.nowNs);
}
int ActualHiddenSelectedProfiles(){
 for(bool xm8:{false,true}){Fixture f;f.xm8=xm8;f.adapter=Bc2BodyHolster{BodyInventoryHolsterAcceptance};f.Metadata();
  CHECK(!Source(f));CHECK(f.Empty());const auto source=Source(f);CHECK(source);
  CHECK(source->slot.item==f.items[0].key&&!f.hands.Current(InteractionHand::Right));
  CHECK(source->spaceGeneration==f.s.hand.owner.space&&source->equipment.Asset()==(xm8?"XM8_sp_s":"SPAS12_sp"));
  CHECK(BodyHolsteredHostInstance(*source,*source,f.s.hand.nowNs));
  CHECK(source->freeRight->receipt.verifiedCopyMask==3&&source->freeRight->suppression.request);
  CHECK(f.out.inventory.slots.size()==2); // No display source is minted for the spare item.
  auto spare=*source;spare.slot=f.out.inventory.slots[1];CHECK(!BodyHolsteredSourceFresh(spare,f.s.hand.nowNs));
 }return 0;
}
int SourceIdentityAndReceiptFailures(){
 Fixture f;CHECK(f.Empty());const auto source=Source(f);CHECK(source);
 for(unsigned bad=0;bad<13;++bad){auto changed=*source;auto free=std::make_shared<BodyFreeRightEvidence>(*source->freeRight);changed.freeRight=free;
  if(bad==0)++changed.equipment.weapon;
  if(bad==1)++changed.equipment.data;
  if(bad==2)changed.equipment.asset[0]='X';
  if(bad==3)++changed.spaceGeneration;
  if(bad==4)changed.slot.item.generation=0;
  if(bad==5)changed.prop.deadlineNs=source->prop.deadlineNs+1;
  if(bad==6)free->receipt.verifiedCopyMask=1;
  if(bad==7)free->receipt.hidden=false;
  if(bad==8)free->suppression.equipmentDispatched=true;
  if(bad==9)++free->visibility.nativeOwner.equipGeneration;
  if(bad==10){auto mesh=std::make_shared<SelectedMeshesSnapshot>(*free->visibility.selected);mesh->states[0].meshes[0].assetPath[0]='X';free->visibility.selected=mesh;}
  if(bad==11)free->authorizationDeadlineNs=f.s.hand.nowNs;
  if(bad==12)free->input.focused=false;
  CHECK(!BodyHolsteredSourceFresh(changed,f.s.hand.nowNs));CHECK(!BodyHolsteredHostInstance(*source,changed,f.s.hand.nowNs));
 }
 auto other=*source;++other.equipment.persistence;CHECK(!BodyHolsteredHostInstance(*source,other,f.s.hand.nowNs));
 other=*source;++other.slot.item.generation;CHECK(!BodyHolsteredHostInstance(*source,other,f.s.hand.nowNs));
 other=*source;other.slot.slot=2;CHECK(!BodyHolsteredHostInstance(*source,other,f.s.hand.nowNs));
 return 0;
}
int OriginalDeadlinesAndDrawCancellation(){
 Fixture f;CHECK(f.Empty());const auto before=Source(f);CHECK(before);
 f.Cycle();const auto current=Source(f);CHECK(current);
 const auto retained=BodyHolsteredHostInstance(*before,*current,f.s.hand.nowNs);CHECK(retained);
 CHECK(retained->deadlineNs==before->prop.deadlineNs&&MatrixNear(retained->world,before->prop.world));
 while(f.s.hand.nowNs<before->prop.deadlineNs)f.Cycle();const auto later=Source(f);CHECK(later);
 CHECK(!BodyHolsteredHostInstance(*before,*later,f.s.hand.nowNs));
 f.Advance();f.Suppress();f.Render(f.out.visibility);f.s.body.intent={++f.intent,BodyInventoryOperation::Draw,1,f.items[0].key};f.Tick();
 CHECK(!Source(f));f.Cycle();f.Cycle();CHECK(f.out.phase==BodyHolsterPhase::Held&&!Source(f));
 return 0;
}
int PoseUsesBodyShoulderAndScale(){
 Fixture f;auto input=BodyInput(f);const auto anchors=BodyAnchorConfig{};const auto shoulder=anchors.shoulders[0];
 for(float scale:{1.f,100.f}){input.worldUnitsPerMeter=scale;const float yaw=.7f;
  input.referenceHead.orientation={0,std::sin(yaw/2),0,std::cos(yaw/2)};
  input.head.position={.2f,1.2f,-.1f};input.head.orientation={0,std::sin(.9f),0,std::cos(.9f)};
  const float x=shoulder.center.x,z=-shoulder.center.z;
  input.hands[0].grip.position={input.head.position.x+std::cos(yaw)*x+std::sin(yaw)*z,
   input.head.position.y+shoulder.center.y,input.head.position.z-std::sin(yaw)*x+std::cos(yaw)*z};
  input.hands[0].grip.orientation=input.referenceHead.orientation;
  const auto contact=BodyAnchorHandPose(input,InteractionHand::Left);CHECK(contact&&BodyAnchorContains(shoulder,*contact));
  for(unsigned n=0;n<3;++n)CHECK(Near(contact->values[3][n],n==0?shoulder.center.x:n==1?shoulder.center.y:shoulder.center.z));
  auto eye=IdentityBody();eye.values[3]={20,-10,30,1};
  const auto anchor=BodyEquipmentPose(input,eye,shoulder,IdentityBody());CHECK(anchor);
  for(const auto& p:BodyEquipmentProfiles){const auto pose=BodyEquipmentPose(input,eye,shoulder,p.modelToAnchor);CHECK(pose);
   auto inverse=*InverseRigid(p.modelToAnchor);const auto grip=Multiply(inverse,*pose);
   for(unsigned n=0;n<3;++n)CHECK(Near(grip.values[3][n],anchor->values[3][n],.0005f));
   CHECK(Near(std::hypot(pose->values[0][0],pose->values[0][1],pose->values[0][2]),scale,.0001f));
   const auto saved=input;input.head.orientation={0,std::sin(-.5f),0,std::cos(-.5f)};
   const auto looked=BodyEquipmentPose(input,eye,shoulder,p.modelToAnchor);CHECK(looked&&MatrixNear(*looked,*pose));
   input=saved;input.referenceHead.position.x+=3;input.referenceHead.position.z-=2;input.head.position.x+=3;input.head.position.z-=2;
   const auto movedSpace=BodyEquipmentPose(input,eye,shoulder,p.modelToAnchor);CHECK(movedSpace&&MatrixNear(*movedSpace,*pose));input=saved;
  }
 }return 0;
}
Bc2AmmoReserveLease Reserve(const Fixture& f){
 Bc2AmmoReserveLease r;r.identity={f.s.nativeOwner,{0x150000,0x160000,0x170000},0x180000,0x190000,0x1a0000};
 r.sequence=10;r.observedNs=f.s.hand.observedNs;r.deadlineNs=f.s.hand.deadlineNs;r.loaded=4;r.reserve=24;r.capacity=7;
 r.verified=r.allThreeIdle=true;return r;
}
int EmptyAmmoWithoutGunClaim(){
 for(bool xm8:{false,true}){Fixture f;f.xm8=xm8;f.adapter=Bc2BodyHolster{BodyInventoryHolsterAcceptance};f.Metadata();CHECK(f.Empty());
  const auto h=Source(f);CHECK(h);const auto reserve=Reserve(f);const auto* profile=xm8?&Xm8MagazineEquipment():nullptr;
  const auto supply=ChestAmmoSupply();const auto contact=xm8?AmmoSupplyContact{supply.pouchCenterMeters,supply.pouchRadiusMeters}:*supply.alternateContact;
  for(float scale:{1.f,100.f}){auto input=BodyInput(f);input.worldUnitsPerMeter=scale;
   const auto source=BuildStowedAmmoSource(*h,reserve,profile,input,IdentityBody(),contact,f.s.hand.nowNs);CHECK(source);
   CHECK(!f.hands.Current(InteractionHand::Right)&&!f.hands.Current(InteractionHand::Left));
   CHECK(StowedAmmoHostInstance(*source,*source,f.s.hand.nowNs));
   AmmoSupplyVisualSample visual;visual.input=f.s.hand;visual.contact=contact;visual.frame=SupplyAnchorFrame::RecenteredBody;
   const auto anchor=AmmoSupplyAnchorWorld(input,IdentityBody(),visual);CHECK(anchor);
   const auto geometry=stowed_ammo_detail::ResolveGeometry(*h,profile,f.s.hand.nowNs);CHECK(geometry);
   auto part=geometry->partFromContact;
   for(unsigned row=0;row<4;++row)for(unsigned col=0;col<3;++col)part.values[row][col]*=scale;
   CHECK(MatrixNear(source->prop.world,Multiply(part,*anchor)));
   // Turning the head does not relocate recentered-body equipment.
   input.head.orientation={0,.6f,0,.8f};const auto looked=BuildStowedAmmoSource(*h,reserve,profile,input,IdentityBody(),contact,f.s.hand.nowNs);
   CHECK(looked&&MatrixNear(looked->prop.world,source->prop.world));
  }
 }return 0;
}
int EmptyAmmoRevocationAndStereo(){
 Fixture f;CHECK(f.Empty());const auto h=Source(f);CHECK(h);const auto now=f.s.hand.nowNs;const auto r=Reserve(f);
 const auto contact=*ChestAmmoSupply().alternateContact;
 const auto original=BuildStowedAmmoSource(*h,r,nullptr,BodyInput(f),IdentityBody(),contact,now);CHECK(original);
 for(unsigned n=0;n<15;++n){auto s=*original;
  if(n==0)s.reserve.verified=false;if(n==1)s.reserve.reserve=0;if(n==2)s.reserve.loaded=s.reserve.capacity+1;
  if(n==3)++s.reserve.identity.owner.weapon;if(n==4)s.reserve.identity.serverItem=0;if(n==5)s.reserve.allThreeIdle=false;
  if(n==6)s.reserve.identity.firing[2]=s.reserve.identity.firing[0];if(n==7)s.reserve.deadlineNs=now;
  if(n==8)--s.reserve.sequence;if(n==9)++s.reserve.loaded;if(n==10)++s.reserve.reserve;if(n==11)++s.reserve.deadlineNs;
  if(n==12)++s.prop.equipmentGeneration;if(n==13)++s.prop.geometry.revision;if(n==14)++s.holstered.slot.item.generation;
  CHECK(!StowedAmmoHostInstance(*original,s,now));
 }
 auto source=*original;auto free=std::make_shared<BodyFreeRightEvidence>(*h->freeRight);source.holstered.freeRight=free;
 free->receipt.hidden=false;CHECK(!StowedAmmoHostInstance(*original,source,now));
 CHECK(!StowedAmmoHostInstance(*original,*original,r.deadlineNs));
 BodyPropSources sources;sources.stowedAmmo=original;
 BodyCarriedPairKey key{1,2,3,4,5,6,h->spaceGeneration};BodyPropPair pair;const auto start=pair.Begin(key,0,sources);CHECK(start);
 graphics::BodyPropEye eye;eye.view=IdentityBody();eye.projection=*math::MakeLhProjectionFromFovTangents({-1,1,1,-1},.05f,100);
 CHECK(!pair.End(start,key,0,sources,sources,eye,now));
 auto later=sources;later.stowedAmmo->prop.world.values[3][0]+=.5f;
 const auto packet=pair.End(start,key,1,later,later,eye,now);CHECK(packet);
 CHECK((*packet)[0].count==1&&(*packet)[1].count==1);
 CHECK((*packet)[0].instances[0].world.values==original->prop.world.values&&(*packet)[1].instances[0].world.values==original->prop.world.values);
 const auto revoked=pair.End(start,key,1,sources,{},eye,now);CHECK(revoked&&!(*revoked)[0].count&&!(*revoked)[1].count);
 // Crossing to the held path drops this original empty-hands source.
 auto held=sources;held.ammo=BodyAmmoRenderSource{};
 CHECK(!BuildBodyPropPairEye({key,sources},held,held,eye,now).count);
 auto moved=*original;moved.contact.centerMeters[0]+=.1f;CHECK(!StowedAmmoHostInstance(*original,moved,now));
 return 0;
}
int EmptyAmmoReserveGapsKeepOriginalExpiry(){
 Fixture f;const auto r=Reserve(f);const auto owner=r.identity.owner;const auto now=f.s.hand.nowNs;
 for(auto gap:{ReloadObservationResult::Deferred,ReloadObservationResult::CohortGap}){
  StowedAmmoReserveCache cache;CHECK(!cache.Observe({gap,{}},owner,now));
  CHECK(cache.Observe({ReloadObservationResult::Available,r},owner,now));
  for(auto time:{now+1000000,now+50000000}){const auto kept=cache.Observe({gap,{}},owner,time);CHECK(kept);
   CHECK(kept->sequence==r.sequence&&kept->observedNs==r.observedNs&&kept->deadlineNs==r.deadlineNs&&kept->reserve==r.reserve);}
  CHECK(!cache.Observe({gap,{}},owner,r.deadlineNs));
  CHECK(cache.Observe({ReloadObservationResult::Available,r},owner,now));
  CHECK(!cache.Observe({ReloadObservationResult::Rejected,{}},owner,now));
  CHECK(!cache.Observe({gap,{}},owner,now));
  CHECK(cache.Observe({ReloadObservationResult::Available,r},owner,now));
  auto changed=owner;++changed.equipGeneration;CHECK(!cache.Observe({gap,{}},changed,now));
  CHECK(cache.Observe({ReloadObservationResult::Available,r},owner,now));cache.Clear();CHECK(!cache.Observe({gap,{}},owner,now));
  auto empty=r;empty.reserve=0;++empty.sequence;
  CHECK(cache.Observe({ReloadObservationResult::Available,r},owner,now));
  const auto updated=cache.Observe({ReloadObservationResult::Available,empty},owner,now);CHECK(updated&&updated->reserve==0);
 }
 return 0;
}
int DisplaySnapshotTrackingLossAndExpiry(){
 Fixture f;CHECK(f.Empty());const auto input=BodyInput(f);const auto now=f.s.hand.nowNs,deadline=f.s.hand.deadlineNs;
 CHECK(BodyDisplayInputFresh(input,deadline,now));
 for(unsigned n=0;n<8;++n){auto lost=input;
  if(n==0)lost.focused=false;if(n==1)lost.headValid=false;if(n==2)lost.hands[0].gripTracked=false;
  if(n==3)lost.hands[1].gripTracked=false;if(n==4)lost.hands[1].aimTracked=false;
  if(n==5)lost.generation=0;if(n==6)lost.spaceGeneration=0;if(n==7)lost.worldUnitsPerMeter=0;
  CHECK(!BodyDisplayInputFresh(lost,deadline,now));
 }
 CHECK(!BodyDisplayInputFresh(input,deadline,deadline));CHECK(!BodyDisplayInputFresh(input,0,now));
 CHECK(!BodyDisplayInputFresh(input,deadline,0));
 auto missingLeft=input;missingLeft.hands[0].gripTracked=false;
 CHECK(BodyDisplayInputFresh(missingLeft,deadline,now,false));
 // An ordinary later packet is admissible with its own original deadline.
 auto advanced=input;++advanced.generation;
 CHECK(BodyDisplayInputFresh(advanced,deadline,now+1));
 CHECK(!BodyDisplayInputFresh(advanced,deadline,deadline));
 // Admission alone still cannot manufacture a body prop or native hide.
 CHECK(!BuildBodyHolsteredSource({}, {}, Equipment(f),advanced,IdentityBody(),{},now));
 return 0;
}
int RecenterRequiresNewTypedSource(){
 Fixture f;CHECK(f.Empty());const auto source=Source(f);CHECK(source);auto input=BodyInput(f);++input.spaceGeneration;
 CHECK(!BuildBodyHolsteredSource(source->freeRight,source->slot,source->equipment,input,IdentityBody(),{},f.s.hand.nowNs));
 auto bad=IdentityBody();bad.values[0][0]=-1;CHECK(!BodyEquipmentPose(input,IdentityBody(),BodyAnchorConfig{}.shoulders[0],bad));
 auto anchors=BodyAnchorConfig{};anchors.shoulders[0].slot=anchors.shoulders[1].slot;
 CHECK(!BuildBodyHolsteredSource(source->freeRight,source->slot,source->equipment,BodyInput(f),IdentityBody(),anchors,f.s.hand.nowNs));
 // Selected hide uses the same new weapon anchor as the unselected batch.
 // This isolates the shared slot geometry using the existing accepted fixture;
 // it does not enroll SPAS at the hip or claim native pistol visibility.
 auto hip=source->slot;hip.slot=BodyAnchorConfig{}.hip.slot;
 CHECK(BuildBodyHolsteredSource(source->freeRight,hip,source->equipment,BodyInput(f),IdentityBody(),{},f.s.hand.nowNs));
 hip.slot=BodyAnchorConfig{}.chest.slot;
 CHECK(!BuildBodyHolsteredSource(source->freeRight,hip,source->equipment,BodyInput(f),IdentityBody(),{},f.s.hand.nowNs));
 return 0;
}
int RecordedShoulderReachesKeepDisplayAnchor(){
 // Actual right-controller positions from the October 12 headset trace, in
 // the recentered body frame. All had a fresh press and no reload/cancel flag.
 const std::array<math::Vec3,14> points{{
  {-.154748f,-.0906072f,-.00839434f},{-.207845f,-.050405f,-.0236019f},
  {-.232815f,-.053871f,-.0305545f},{-.247364f,-.0293434f,.072198f},
  {-.24203f,-.0573574f,-.0269204f},{-.245271f,-.0861503f,.00259072f},
  {-.259893f,-.0695138f,.00664356f},{-.216877f,-.0569062f,-.0392822f},
  {-.242981f,-.0553565f,-.0179569f},{-.233523f,-.0591148f,-.0503592f},
  {-.20487f,-.0523991f,-.0350384f},{-.217553f,-.0591738f,-.00722751f},
  {-.217402f,-.0881056f,-.0117807f},{-.22134f,-.0634803f,-.0262862f}}};
 BodyAnchorConfig anchors;auto strict=anchors;strict.shoulderReachForwardMeters=0;
 Fixture f;const auto input=BodyInput(f);
 for(unsigned side=0;side<2;++side){const auto slot=anchors.shoulders[side].slot;
  unsigned oldContacts=0,newContacts=0;
  for(unsigned n=0;n<points.size();++n){const auto& p=points[n];auto hand=IdentityBody();
   hand.values[3]={side?-p.x:p.x,p.y,p.z,1};
   oldContacts+=BodyWeaponContactContains(strict,slot,hand);
   const bool accepted=BodyWeaponContactContains(anchors,slot,hand);newContacts+=accepted;
   CHECK(accepted==(n!=3)); // The forward outlier still must not draw a gun.
   CHECK(!BodyWeaponContactContains(anchors,anchors.shoulders[1-side].slot,hand));
   CHECK(!BodyWeaponContactContains(anchors,anchors.hip.slot,hand));
  }
  CHECK(oldContacts==5&&newContacts==13);
  const auto before=BodyEquipmentPose(input,IdentityBody(),strict.shoulders[side],IdentityBody());
  const auto after=BodyEquipmentPose(input,IdentityBody(),anchors.shoulders[side],IdentityBody());
  CHECK(before&&after&&MatrixNear(*before,*after));
  for(const auto p:{math::Vec3{0,0,0},math::Vec3{-.2f,-.12f,.25f},
      math::Vec3{.2f,-.12f,.25f},anchors.chest.center,anchors.hip.center}){
   auto hand=IdentityBody();hand.values[3]={p.x,p.y,p.z,1};CHECK(!BodyWeaponContactContains(anchors,slot,hand));
  }
  auto rear=IdentityBody();const auto& a=anchors.shoulders[side];rear.values[3]={a.center.x,a.center.y,a.center.z-.15f,1};
  CHECK(BodyWeaponContactContains(anchors,slot,rear));
 }
 auto hip=IdentityBody();hip.values[3]={anchors.hip.center.x,anchors.hip.center.y,anchors.hip.center.z+.18f,1};
 CHECK(!BodyWeaponContactContains(anchors,anchors.hip.slot,hip));
 CHECK(!BodyWeaponContactContains(anchors,anchors.chest.slot,hip));
 for(float invalid:{-.01f,.11f,std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()}){
  auto bad=anchors;bad.shoulderReachForwardMeters=invalid;CHECK(!ValidBodyAnchors(bad));
  CHECK(!BodyWeaponContactContains(bad,anchors.shoulders[0].slot,IdentityBody()));
 }
 return 0;
}
}
int main(){if(ActualHiddenSelectedProfiles()||SourceIdentityAndReceiptFailures()||OriginalDeadlinesAndDrawCancellation()||
 PoseUsesBodyShoulderAndScale()||RecenterRequiresNewTypedSource()||EmptyAmmoWithoutGunClaim()||EmptyAmmoRevocationAndStereo()||EmptyAmmoReserveGapsKeepOriginalExpiry()||DisplaySnapshotTrackingLossAndExpiry()||RecordedShoulderReachesKeepDisplayAnchor())return 1;
 std::puts("10 holstered body-source/empty-ammo groups passed (real coordinator/cache, synthetic native pack receipts; CPU only)");}
