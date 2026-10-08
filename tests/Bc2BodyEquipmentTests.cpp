#include "Bc2BodyEquipment.h"
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
int RecenterRequiresNewTypedSource(){
 Fixture f;CHECK(f.Empty());const auto source=Source(f);CHECK(source);auto input=BodyInput(f);++input.spaceGeneration;
 CHECK(!BuildBodyHolsteredSource(source->freeRight,source->slot,source->equipment,input,IdentityBody(),{},f.s.hand.nowNs));
 auto bad=IdentityBody();bad.values[0][0]=-1;CHECK(!BodyEquipmentPose(input,IdentityBody(),BodyAnchorConfig{}.shoulders[0],bad));
 auto anchors=BodyAnchorConfig{};anchors.shoulders[0].slot=anchors.shoulders[1].slot;
 CHECK(!BuildBodyHolsteredSource(source->freeRight,source->slot,source->equipment,BodyInput(f),IdentityBody(),anchors,f.s.hand.nowNs));
 return 0;
}
}
int main(){if(ActualHiddenSelectedProfiles()||SourceIdentityAndReceiptFailures()||OriginalDeadlinesAndDrawCancellation()||
 PoseUsesBodyShoulderAndScale()||RecenterRequiresNewTypedSource())return 1;
 std::puts("5 holstered body-source groups passed (real coordinator/cache, synthetic native pack receipts; CPU only)");}
