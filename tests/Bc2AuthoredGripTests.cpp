#include "Test.h"
#include "Bc2AuthoredGrip.h"
#include "Bc2WeaponProfiles.h"
#include "fvr/interaction/SupportGrip.h"
#include "fvr/interaction/HandInteraction.h"
#include "fvr/interaction/GripAttachment.h"
#include <limits>
#include <iostream>
using namespace fvr;using namespace fvr::bc2;using namespace fvr::interaction;
namespace {
math::Matrix4 At(float x=0,float y=0,float z=0){math::Matrix4 m{};for(unsigned n=0;n<4;++n)m.values[n][n]=1;m.values[3]={x,y,z,1};return m;}
bool Close(const math::Matrix4& a,const math::Matrix4& b){for(unsigned n=0;n<16;++n)if(std::abs(a.values[n/4][n%4]-b.values[n/4][n%4])>.0001f)return false;return true;}
constexpr std::string_view digest="0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
template<std::size_t N> void Text(std::array<char,N>& out,std::string_view text){out={};std::copy(text.begin(),text.end(),out.begin());}
struct Fixture {
 static constexpr std::int64_t now=1000000000;
 AuthoredGripProfile profile{"SyntheticRifle","Objects/Weapons/ExactMesh",digest,digest,digest,91,At(.04f,-.08f,-.23f),At(-.02f,-.09f,-.6f)};
 std::shared_ptr<SelectedMeshesSnapshot> snapshot=std::make_shared<SelectedMeshesSnapshot>();
 WeaponEquipmentIdentity equipment{};AuthoredGripOwner owner{10,11,12,13,14,20,now+90000000};
 Fixture(){equipment.weapon=15;equipment.data=16;equipment.persistence=17;Text(equipment.asset,profile.assetName);
  auto& s=*snapshot;s.owner={9,10,11,15,12,18,14};s.sequence=19;s.observedNs=now-10000000;s.deadlineNs=now+200000000;
  s.weaponData=16;s.stateTypeInfo=1;s.meshTypeInfo=2;s.inventory=3;s.selectedSlot=4;Text(s.weaponName,profile.assetName);
  s.stateCount=1;s.soleConfiguredArray=5;s.states[0].array=5;s.states[0].state=6;s.states[0].count=1;
  auto& m=s.states[0].meshes[0];m.address=7;m.typeInfo=2;m.namePointer=8;Text(m.assetPath,profile.meshPath);
 }
 AuthoredGripResult Bind(bool baseline=false,bool explicitSelect=false){return BindAuthoredGrip({&profile,1},snapshot,equipment,owner,91,now,baseline,explicitSelect);}
 bool Current(const AuthoredGripBinding& b,std::int64_t t=now){return AuthoredGripCurrent(b,snapshot.get(),equipment,owner,t);}
};
int ExactIdentityAndOriginalLease(){
 Fixture f;const auto result=f.Bind();CHECK(result.status==AuthoredGripStatus::Bound&&result.binding);const auto b=*result.binding;CHECK(f.Current(b));
 const auto original=f.snapshot;f.snapshot=std::make_shared<SelectedMeshesSnapshot>(*original);f.snapshot->sequence++;f.snapshot->observedNs=Fixture::now;f.snapshot->deadlineNs=Fixture::now+250000000;
 CHECK(f.Current(b));CHECK(!f.Current(b,f.owner.inputDeadlineNs));
 f.owner.inputDeadlineNs=Fixture::now+300000000;CHECK(!f.Current(b,Fixture::now+100000000)); // no input renewal
 f.owner=b.owner;f.owner.inputDeadlineNs=Fixture::now+400000000;
 auto longer=b;longer.owner.inputDeadlineNs=f.owner.inputDeadlineNs;CHECK(!f.Current(longer,original->deadlineNs)); // no configuration renewal
 f.owner=b.owner;f.snapshot->sequence=original->sequence-1;CHECK(!f.Current(b));
 f.snapshot=original;f.equipment.data++;CHECK(!f.Current(b));f.equipment=b.equipment;f.equipment.persistence++;CHECK(!f.Current(b));
 f.equipment=b.equipment;f.owner.equipmentGeneration++;CHECK(!f.Current(b));f.owner=b.owner;f.owner.space++;CHECK(!f.Current(b));
 return 0;
}
int BaselineAndAmbiguity(){
 Fixture f;CHECK(f.Bind(true).status==AuthoredGripStatus::PreservedBaseline);CHECK(f.Bind(true,true).binding);
 std::array<AuthoredGripProfile,2> profiles{f.profile,f.profile};
 CHECK(BindAuthoredGrip(profiles,f.snapshot,f.equipment,f.owner,91,Fixture::now,false).status==AuthoredGripStatus::AmbiguousConfiguration);
 profiles[1].meshPath="Objects/Weapons/OtherMesh";CHECK(BindAuthoredGrip(profiles,f.snapshot,f.equipment,f.owner,91,Fixture::now,false).binding);
 f.snapshot->states[0].count=2;f.snapshot->states[0].meshes[1]=f.snapshot->states[0].meshes[0];CHECK(!f.Bind().binding);
 f.snapshot->states[0].count=1;f.snapshot->stateCount=2;CHECK(!f.Bind().binding);f.snapshot->stateCount=1;
 f.snapshot->states[0].meshes[0].kind=SelectedMeshKind::Unknown;CHECK(f.Bind().binding); // exact path, no XM8 enum gate
 const auto b=*f.Bind().binding;f.snapshot=std::make_shared<SelectedMeshesSnapshot>(*f.snapshot);f.snapshot->states[0].header[1]++;CHECK(!f.Current(b));
 return 0;
}
int MalformedAndCapabilities(){
 Fixture f;CHECK(!BindAuthoredGrip({&f.profile,1},f.snapshot,f.equipment,f.owner,92,Fixture::now,false).binding);
 f.snapshot->weaponName.fill('a');CHECK(!f.Bind().binding);Text(f.snapshot->weaponName,f.profile.assetName);
 f.profile.rightInWeapon.values[0][0]=std::numeric_limits<float>::quiet_NaN();CHECK(!f.Bind().binding);
 f.profile.rightInWeapon=At(2,0,0);CHECK(!f.Bind().binding);f.profile.rightInWeapon=At();
 f.profile.bindingDigest="invalid";CHECK(!f.Bind().binding);f.profile.bindingDigest=digest;
 f.snapshot->sequence=f.owner.inputSequence+1;CHECK(!f.Bind().binding);f.snapshot->sequence=19;
 f.snapshot->deadlineNs=Fixture::now+500000000;CHECK(!f.Bind().binding);
 CHECK(!AuthoredGripBinding::aimCapability&&!AuthoredGripBinding::muzzleCapability&&!AuthoredGripBinding::reloadCapability&&!AuthoredGripBinding::holsterCapability);
 CHECK(FindWeaponProfile("AEK971_sp")==nullptr);return 0;
}
struct RigFixture {
 InputFrame input{};TrackedRigOwner owner{1,2,3,4,5};TrackedRig rig;
 math::Matrix4 body=At(3,1.5f,8),left=At(2.8f,1.2f,8.4f),right=At(3.2f,1.2f,8.4f),gun=At(3.7f,1.4f,9.5f);
 std::array<ArmAnchor,2> arms{ArmAnchor{{2.8f,1.4f,8},{-.1f,-.3f,.1f}},ArmAnchor{{3.2f,1.4f,8},{.1f,-.3f,.1f}}};
 RigFixture(){input.generation=input.spaceGeneration=1;input.predictedNs=1000000000;input.focused=input.headValid=true;for(auto& h:input.hands){h.gripTracked=h.aimTracked=true;h.active=Components;}input.hands[0].grip.position={-.2f,-.3f,-.4f};input.hands[1].grip.position={.2f,-.3f,-.4f};}
 std::optional<TrackedRigPose> Step(std::optional<AuthoredGripAttachment> source){return rig.Update(owner,input,body,left,right,gun,arms,{},{},{},source);}
};
int ActualConsumerIgnoresTransientGun(){
 RigFixture f;AuthoredGripAttachment grip{At(.04f,-.08f,-.23f)};
 const auto a=f.Step(grip);CHECK(a&&a->weaponAttachmentAuthored&&!a->weaponAttachmentPending);
 CHECK(Close(Multiply(grip.rightInWeapon,a->weapon),a->right));CHECK(!Close(a->weapon,f.gun));
 f.gun=At(5,4,3);f.right=At(-1,4,10);f.input.predictedNs+=10000000;f.input.generation++;
 const auto b=f.Step(grip);CHECK(b&&Close(a->weapon,b->weapon)&&Close(a->right,b->right));
 // Static root attachment preserves every current native child relation.
 const std::array<std::int32_t,4> parents{-1,0,1,1};
 const std::array<math::Matrix4,4> native{f.body,f.gun,Multiply(At(.03f,-.02f,.1f),f.gun),Multiply(At(.05f,.04f,.2f),f.gun)};
 const auto writes=RetargetRigSubtree(parents,native,1,b->weapon);CHECK(writes&&writes->size()==3);
 auto after=native;for(const auto& w:*writes)after[w.index]=w.transform;
 for(unsigned n=2;n<4;++n)CHECK(Close(Multiply(after[n],*InverseAnimatedTransform(after[1])),Multiply(native[n],*InverseAnimatedTransform(native[1]))));
 return 0;
}
int SourceLossRecenterAndReplacement(){
 RigFixture f;AuthoredGripAttachment grip{At(.04f,-.08f,-.23f)};CHECK(f.Step(grip));
 auto no=f.Step({});CHECK(no&&!no->weaponAttachmentAuthored&&no->weaponAttachmentPending);CHECK(Close(Multiply(no->weapon,*InverseAnimatedTransform(no->right)),Multiply(f.gun,*InverseAnimatedTransform(f.right))));
 const auto before=f.Step(grip);CHECK(before);f.input.spaceGeneration++;auto after=f.Step(grip);CHECK(after&&Close(Multiply(grip.rightInWeapon,after->weapon),after->right));
 f.owner.equipmentGeneration++;AuthoredGripAttachment different{At(-.06f,-.09f,-.18f)};auto replacement=f.Step(different);CHECK(replacement&&replacement->weaponAttachmentAuthored&&Close(Multiply(different.rightInWeapon,replacement->weapon),replacement->right));
 auto invalid=grip;invalid.rightInWeapon.values[3][0]=std::numeric_limits<float>::infinity();CHECK(!f.Step(invalid));return 0;
}
int WorldUnitsAndNativeBaseline(){
 RigFixture f;f.input.worldUnitsPerMeter=2;const AuthoredGripAttachment grip{At(.04f,-.08f,-.23f)};auto out=f.Step(grip);CHECK(out);
 CHECK(Close(Multiply(AuthoredGripWorldUnits(grip.rightInWeapon,2),out->weapon),out->right));
 RigFixture native;const auto old=native.Step({});CHECK(old&&!old->weaponAttachmentAuthored&&old->weaponAttachmentPending);
 native.input.predictedNs+=800000000;auto settled=native.Step({});CHECK(settled&&!settled->weaponAttachmentPending);return 0;
}
int AuthoredSupportComposesExistingPolicy(){
 Fixture f;CHECK(!AuthoredRifleSupport(*f.Bind().binding));f.profile.authoredRifleSupport=true;
 const auto binding=f.Bind();CHECK(binding.binding&&AuthoredRifleSupport(*binding.binding));
 RigFixture rig;rig.body.values[0]={0,0,-1,0};rig.body.values[2]={1,0,0,0};
 for(auto& h:rig.input.hands)h.grip.orientation=h.aim.orientation={0,.258819045f,0,.965925826f};
 const auto initial=rig.Step(AuthoredGripAttachment{f.profile.rightInWeapon});CHECK(initial);
 const auto supportWorld=Multiply(f.profile.leftInWeapon,initial->weapon);
 const auto local=Multiply(supportWorld,*InverseRigid(rig.body));
 rig.input.hands[0].grip.position={local.values[3][0],local.values[3][1],-local.values[3][2]};
 SupportGrip policy;const SupportGripOwner owner{rig.owner.actor,rig.owner.generation,rig.owner.equipped};
 SupportGripContact contact{true,0,{}};
 CHECK(!policy.Update(owner,rig.input,contact).holding);
 ++rig.input.generation;rig.input.predictedNs+=10000000;rig.input.hands[0].squeeze=.9f;
 const auto held=policy.Update(owner,rig.input,contact);CHECK(held.engaged&&held.holding);
 CHECK(std::memcmp(&held.input.hands[0],&rig.input.hands[0],sizeof(ControllerState))==0);
 GripAttachment attachment;
 const auto fixed=attachment.Update({1,2,3,4,1},held.token,f.profile.leftInWeapon);CHECK(fixed);
 CHECK(Close(Multiply(*fixed,initial->weapon),supportWorld));
 // Current animation is not allowed to move the captured support socket.
 CHECK(Close(*attachment.Update({1,2,3,4,1},held.token,At(1,1,1)),*fixed));
 rig.input.hands[0].grip.position.y+=.08f;++rig.input.generation;rig.input.predictedNs+=10000000;
 const auto steered=policy.Update(owner,rig.input,contact);CHECK(steered.holding&&steered.correctionRadians>.1f);
 CHECK(std::memcmp(&steered.input.hands[1].grip.position,&rig.input.hands[1].grip.position,sizeof(math::Vec3))==0);
 // Feed the actual corrected input into the same renderer consumer WITHOUT
 // an AimAlignment/WeaponProfile. Grip and aim must rotate coherently; this
 // preserves (and does not claim to resolve) the initial model-to-ray offset.
 const auto beforeAim=TrackedAimFrame(rig.input,rig.body),afterAim=TrackedAimFrame(steered.input,rig.body);CHECK(beforeAim&&afterAim);
 const auto aimDelta=Multiply(*InverseRigid(*beforeAim),*afterAim);
 const auto beforePose=rig.Step(AuthoredGripAttachment{f.profile.rightInWeapon});CHECK(beforePose);
 const auto rawInput=rig.input;rig.input=steered.input;
 const auto afterPose=rig.Step(AuthoredGripAttachment{f.profile.rightInWeapon});CHECK(afterPose);
 CHECK(!Close(beforePose->weapon,afterPose->weapon));
 const auto expectedWeapon=Multiply(beforePose->weapon,aimDelta);
 for(unsigned row=0;row<3;++row)for(unsigned column=0;column<3;++column)
  CHECK(Near(afterPose->weapon.values[row][column],expectedWeapon.values[row][column],.0001f));
 for(unsigned axis=0;axis<3;++axis)CHECK(Near(beforePose->right.values[3][axis],afterPose->right.values[3][axis],.0001f));
 CHECK(Close(Multiply(f.profile.rightInWeapon,afterPose->weapon),afterPose->right));
 rig.input=rawInput;
 contact.distanceMeters=.4f;++rig.input.generation;CHECK(policy.Update(owner,rig.input,contact).released);
 CHECK(!attachment.Update({1,2,3,4,1},0,f.profile.leftInWeapon));
 return 0;
}
int SupportCannotStealAmmoClaim(){
 Fixture f;f.profile.authoredRifleSupport=true;CHECK(AuthoredRifleSupport(*f.Bind().binding));
 HandInteraction arbiter;HandInteractionSample s{{1,2,3,4},1,Fixture::now,Fixture::now+90000000,Fixture::now,true,{true,true},{true,false}};
 const HandInteractionKey gunItem{20,3},ammoItem{21,3};
 auto request=[&](HandClaimKind kind,InteractionHand hand,HandInteractionKey item,std::uint64_t intent,std::uint64_t prerequisite=0){
  const HandInteractionKey contact{hand==InteractionHand::Right?30u:31u,item.generation};
  return HandClaimRequest{s.owner,hand,kind,item,{contact,s.sequence,s.deadlineNs,true},intent,prerequisite};};
 arbiter.Update(s);const auto gun=arbiter.Acquire(s,request(HandClaimKind::GunHold,InteractionHand::Right,gunItem,1));CHECK(gun.claim);
 ++s.sequence;s.observedNs+=1000000;s.nowNs=s.observedNs;s.released[0]=false;arbiter.Update(s);
 const auto ammo=arbiter.Acquire(s,request(HandClaimKind::AmmoObject,InteractionHand::Left,ammoItem,1));CHECK(ammo.claim);
 const auto denied=arbiter.Acquire(s,request(HandClaimKind::WeaponSupport,InteractionHand::Left,gunItem,2,gun.claim->token.id));CHECK(!denied.accepted&&arbiter.Current(InteractionHand::Left)->token.kind==HandClaimKind::AmmoObject);
 CHECK(arbiter.Release(s,ammo.claim->token).accepted);
 ++s.sequence;s.observedNs+=1000000;s.nowNs=s.observedNs;s.released[0]=true;arbiter.Update(s);
 ++s.sequence;s.observedNs+=1000000;s.nowNs=s.observedNs;s.released[0]=false;arbiter.Update(s);
 const auto acquired=arbiter.Acquire(s,request(HandClaimKind::WeaponSupport,InteractionHand::Left,gunItem,3,gun.claim->token.id));CHECK(acquired.accepted);
 CHECK(arbiter.Release(s,gun.claim->token).accepted);CHECK(!arbiter.Current(InteractionHand::Left));
 return 0;
}
int SupportRequiresFreshOriginalBinding(){
 Fixture f;f.profile.authoredRifleSupport=true;const auto bound=*f.Bind().binding;
 RigFixture rig;SupportGrip policy;const SupportGripOwner owner{1,2,3};const SupportGripContact contact{true,0,{}};
 rig.input.hands[0].grip.position={.2f,-.3f,-.8f};
 CHECK(f.Current(bound)&&AuthoredRifleSupport(bound));policy.Update(owner,rig.input,contact);
 ++rig.input.generation;rig.input.predictedNs+=10000000;rig.input.hands[0].squeeze=.9f;CHECK(policy.Update(owner,rig.input,contact).holding);
 auto current=f.snapshot;f.snapshot=std::make_shared<SelectedMeshesSnapshot>(*current);f.snapshot->observedNs=Fixture::now+100000000;f.snapshot->deadlineNs=Fixture::now+350000000;++f.snapshot->sequence;f.owner.inputDeadlineNs=Fixture::now+300000000;
 CHECK(!f.Current(bound,Fixture::now+100000000));
 ++rig.input.generation;rig.input.predictedNs+=10000000;const auto expired=policy.Update(owner,rig.input,{});CHECK(expired.released&&!expired.holding);
 f.profile.leftInWeapon=f.profile.rightInWeapon;CHECK(!AuthoredRifleSupport(bound));
 CHECK(!FindWeaponProfile("AEK971_sp")); // no other weapon capabilities promoted
 return 0;
}

int PackBoundaryLeaseComposition(){
 Fixture f;RigFixture rig;const auto bound=f.Bind();CHECK(bound.binding);
 const auto pose=rig.Step(AuthoredGripAttachment{bound.binding->profile->rightInWeapon});CHECK(pose&&pose->weaponAttachmentAuthored);
 // Same policy used before both native Pack callbacks and the shot/output read.
 CHECK(f.Current(*bound.binding));f.snapshot=std::make_shared<SelectedMeshesSnapshot>(*f.snapshot);f.snapshot->owner.equipGeneration++;
 CHECK(!f.Current(*bound.binding)); // first-eye success cannot grant the second or paired completion
 f.snapshot=std::const_pointer_cast<SelectedMeshesSnapshot>(bound.binding->selected);CHECK(f.Current(*bound.binding));
 f.owner.inputDeadlineNs=Fixture::now+300000000;CHECK(!f.Current(*bound.binding,Fixture::now+100000000));return 0;
}
math::Vec3 AxisDirection(math::Vec3 v,const math::Matrix4& m){return {
 v.x*m.values[0][0]+v.y*m.values[1][0]+v.z*m.values[2][0],
 v.x*m.values[0][1]+v.y*m.values[1][1]+v.z*m.values[2][1],
 v.x*m.values[0][2]+v.y*m.values[1][2]+v.z*m.values[2][2]};}
bool AxisClose(math::Vec3 a,math::Vec3 b){return std::hypot(a.x-b.x,a.y-b.y,a.z-b.z)<.0001f;}
int AuthoredModelAxesActualConsumer(){
 for(float units:{1.f,2.f})for(bool alternate:{false,true}){
  Fixture f;f.profile.modelForward=alternate?math::Vec3{1,0,0}:math::Vec3{0,0,-1};
  f.profile.modelUp=alternate?math::Vec3{0,0,1}:math::Vec3{0,1,0};f.profile.authoredAxisEvidence=digest;
  const auto bound=f.Bind();CHECK(bound.binding);CHECK(f.Bind(true).status==AuthoredGripStatus::PreservedBaseline);
  RigFixture rig;rig.input.worldUnitsPerMeter=units;rig.input.hands[1].grip.orientation={0,.258819f,0,.965926f};
  rig.input.hands[1].aim.orientation={.198669f,0,0,.980067f};
  rig.body.values[0]={0,0,1,0};rig.body.values[2]={-1,0,0,0};
  const auto raw=rig.Step(AuthoredGripAttachment{f.profile.rightInWeapon});CHECK(raw);
  const auto aim=TrackedAimFrame(rig.input,rig.body);CHECK(aim);
  const auto orientation=AuthoredWeaponAimFrame(*bound.binding,*aim);CHECK(orientation);
  const auto posed=rig.rig.Update(rig.owner,rig.input,rig.body,rig.left,rig.right,rig.gun,rig.arms,{},{},orientation,AuthoredGripAttachment{f.profile.rightInWeapon});
  CHECK(posed&&posed->weaponAttachmentAuthored&&!posed->weaponAttachmentPending);
  CHECK(Close(Multiply(AuthoredGripWorldUnits(f.profile.rightInWeapon,units),posed->weapon),posed->right));
  for(unsigned n=0;n<3;++n)CHECK(std::abs(raw->right.values[3][n]-posed->right.values[3][n])<.0001f);
  CHECK(AxisClose(AxisDirection(f.profile.modelForward,posed->weapon),AxisDirection({0,0,1},*aim)));
  CHECK(AxisClose(AxisDirection(f.profile.modelUp,posed->weapon),AxisDirection({0,1,0},*aim)));
  CHECK(orientation->values[3]==At().values[3]);
  WeaponProfile unverified{};unverified.stableId="synthetic_authored";unverified.revision=1;
  unverified.modelForward=f.profile.modelForward;unverified.modelUp=f.profile.modelUp;
  CHECK(ValidateWeaponProfile(unverified));CHECK(!WeaponAimFrame(unverified,*aim));
  CHECK(WeaponFeatureStatus(&unverified,WeaponFeature::AimAlignment)==WeaponStatus::Unverified);
 }
 return 0;
}
int AuthoredAxesRejectMalformedAndExpired(){
 Fixture f;auto bound=f.Bind();CHECK(bound.binding&&!AuthoredWeaponAimFrame(*bound.binding,At()));
 f.profile.modelForward={0,0,-1};f.profile.modelUp={0,1,0};CHECK(!f.Bind().binding); // axes without explicit evidence
 f.profile.authoredAxisEvidence=digest;bound=f.Bind();CHECK(bound.binding&&AuthoredWeaponAimFrame(*bound.binding,At()));
 auto reflection=At();reflection.values[0][0]=-1;CHECK(!AuthoredWeaponAimFrame(*bound.binding,reflection));
 f.profile.modelUp=f.profile.modelForward;CHECK(!f.Bind().binding);
 f.profile.modelUp={0,1,0};f.profile.modelForward.z=std::numeric_limits<float>::quiet_NaN();CHECK(!f.Bind().binding);
 f.profile.modelForward={0,0,-1};f.profile.authoredAxisEvidence="invalid";CHECK(!f.Bind().binding);
 f.profile.authoredAxisEvidence=digest;bound=f.Bind();CHECK(bound.binding);
 const auto original=f.snapshot;f.snapshot=std::make_shared<SelectedMeshesSnapshot>(*original);
 f.snapshot->sequence++;f.snapshot->observedNs=Fixture::now;f.snapshot->deadlineNs=Fixture::now+250000000;
 f.owner.inputDeadlineNs=Fixture::now+300000000;CHECK(!f.Current(*bound.binding,Fixture::now+100000000));
 f.owner=bound.binding->owner;CHECK(f.Current(*bound.binding));f.equipment.data++;CHECK(!f.Current(*bound.binding));
 CHECK(!AuthoredGripBinding::aimCapability&&!AuthoredGripBinding::muzzleCapability);return 0;
}

}
int main(){if(AuthoredModelAxesActualConsumer()||AuthoredAxesRejectMalformedAndExpired()||ExactIdentityAndOriginalLease()||BaselineAndAmbiguity()||MalformedAndCapabilities()||ActualConsumerIgnoresTransientGun()||SourceLossRecenterAndReplacement()||WorldUnitsAndNativeBaseline()||PackBoundaryLeaseComposition()||AuthoredSupportComposesExistingPolicy()||SupportCannotStealAmmoClaim()||SupportRequiresFreshOriginalBinding())return 1;std::cout<<"12 authored grip/support/aim consumer groups passed\n";}
