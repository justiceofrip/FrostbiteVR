#include "Test.h"
#include "Bc2AuthoredSupport.h"
#include "fvr/interaction/SupportGrip.h"
#include "fvr/interaction/GripAttachment.h"
#include "../profiles/weapons43/CombinedSupport.h"
#include <cstring>
#include <iostream>
using namespace fvr;using namespace fvr::bc2;
template<std::size_t N> void Text(std::array<char,N>& out,std::string_view text){out={};std::copy(text.begin(),text.end(),out.begin());}
struct SupportFixture {
 static constexpr std::int64_t now=1000000000;
 std::shared_ptr<SelectedMeshesSnapshot> snapshot=std::make_shared<SelectedMeshesSnapshot>();
 WeaponEquipmentIdentity equipment{};AuthoredGripOwner owner{10,11,12,13,14,20,now+90000000};
 explicit SupportFixture(const AuthoredSupportProfile& p){
  equipment.weapon=15;equipment.data=16;equipment.persistence=17;Text(equipment.asset,p.assetName);
  auto& s=*snapshot;s.owner={9,10,11,15,12,18,14};s.sequence=19;s.observedNs=now-10000000;s.deadlineNs=now+200000000;
  s.weaponData=16;s.stateTypeInfo=1;s.meshTypeInfo=2;s.inventory=3;s.selectedSlot=4;Text(s.weaponName,p.assetName);
  s.stateCount=1;s.soleConfiguredArray=5;s.states[0].array=5;s.states[0].state=6;s.states[0].count=1;
  auto& m=s.states[0].meshes[0];m.address=7;m.typeInfo=2;m.namePointer=8;Text(m.assetPath,p.meshPath);
 }
};
int EnrolledSupportComposesExistingGrip(){
 using namespace interaction;
 bool scar=false,pistol=false,nambu=false;unsigned enrolled=0,primary=0;
 for(const auto& p:generated::AuthoredSupports){
  if(!p.contactReference)continue;
  ++enrolled;scar|=p.assetName=="SCAR_sp";pistol|=p.assetName=="MP443_sp";nambu|=p.assetName=="Nambu100";
  SupportFixture f(p);if(p.assetName=="Nambu100")f.equipment.persistence=0;
  const auto bound=BindAuthoredSupportReference(generated::AuthoredSupports,f.snapshot,f.equipment,f.owner,p.rigFingerprint,f.now);
  CHECK(bound&&AuthoredSupportContact(bound));
  CHECK(AuthoredSupportCurrent(*bound,f.snapshot.get(),f.equipment,f.owner,f.now));
  InputFrame input{};input.generation=input.spaceGeneration=1;input.predictedNs=f.now;input.focused=input.headValid=true;
  for(auto& h:input.hands){h.gripTracked=h.aimTracked=true;h.active=Components;}
  input.hands[0].grip.position={0,-.2f,-.7f};input.hands[1].grip.position={0,-.2f,-.3f};
  if(p.aimMode==SupportAimMode::PrimaryHand){
   ++primary;
   input.hands[0].grip.position={.02f,-.2f,-.31f};
  }else CHECK(p.aimMode==SupportAimMode::TwoPoint);
  SupportGrip policy;const SupportGripOwner owner{1,2,3};const SupportGripContact contact{AuthoredSupportContact(bound),0,{},p.aimMode};
  CHECK(!policy.Update(owner,input,contact).holding);
  ++input.generation;input.predictedNs+=10000000;input.hands[0].squeeze=.9f;
  const auto held=policy.Update(owner,input,contact);CHECK(held.engaged&&held.holding);
  CHECK(std::memcmp(&input.hands[0],&held.input.hands[0],sizeof(ControllerState))==0);
  CHECK(input.hands[1].grip.position.x==held.input.hands[1].grip.position.x);
  GripAttachment attachment;const auto fixed=attachment.Update({1,2,3,p.rigFingerprint,1},held.token,p.leftInWeapon);CHECK(fixed&&fixed->values==p.leftInWeapon.values);
  auto animated=p.leftInWeapon;animated.values[3][2]+=.2f;
  CHECK(attachment.Update({1,2,3,p.rigFingerprint,1},held.token,animated)->values==fixed->values);
  // Later source observations cannot renew the original pose/contact lease.
  auto refreshed=*f.snapshot;refreshed.sequence++;refreshed.observedNs=f.now+100000000;refreshed.deadlineNs=f.now+350000000;
  f.owner.inputDeadlineNs=f.now+300000000;
  CHECK(!AuthoredSupportCurrent(*bound,&refreshed,f.equipment,f.owner,f.now+100000000));
  ++input.generation;input.predictedNs+=10000000;CHECK(policy.Update(owner,input,{}).released);
 }
 CHECK(scar&&pistol&&nambu&&enrolled==77&&primary==6);
 CHECK(generated::AuthoredSupports[0].assetName=="SPAS12_sp");
 CHECK(generated::AuthoredSupports[0].leftInWeapon.values==generated::PreservedSupport202[0].leftInWeapon.values);
 return 0;
}
int OriginalSupportConfigurationAndInputLifetime(){
 const auto& p=generated::EnrolledSupport312.front();SupportFixture f(p);
 const auto bound=BindAuthoredSupportReference(generated::AuthoredSupports,f.snapshot,f.equipment,f.owner,p.rigFingerprint,f.now);CHECK(bound);
 const auto valid=[&](const SelectedMeshesSnapshot* s){return AuthoredSupportCurrent(*bound,s,f.equipment,f.owner,f.now);};
 CHECK(valid(f.snapshot.get()));CHECK(!valid(nullptr));
 auto changed=*f.snapshot;changed.owner.equipGeneration++;CHECK(!valid(&changed));
 changed=*f.snapshot;changed.states[0].header[1]++;CHECK(!valid(&changed));
 changed=*f.snapshot;Text(changed.states[0].meshes[0].assetPath,"Objects/Weapons/Foreign");CHECK(!valid(&changed));
 changed=*f.snapshot;changed.weaponData++;CHECK(!valid(&changed));
 changed=*f.snapshot;changed.owner.space++;CHECK(!valid(&changed));
 changed=*f.snapshot;changed.sequence--;CHECK(!valid(&changed));
 changed=*f.snapshot;changed.deadlineNs=f.now;CHECK(!valid(&changed));
 f.equipment.persistence++;CHECK(!valid(f.snapshot.get()));f.equipment=bound->equipment;
 f.owner.equipmentGeneration++;CHECK(!valid(f.snapshot.get()));f.owner=bound->owner;
 f.owner.space++;CHECK(!valid(f.snapshot.get()));f.owner=bound->owner;
 auto later=*f.snapshot;later.observedNs=f.now+190000000;later.deadlineNs=f.now+400000000;++later.sequence;
 f.owner.inputDeadlineNs=f.now+400000000;
 CHECK(!AuthoredSupportCurrent(*bound,&later,f.equipment,f.owner,f.now+210000000));
 CHECK(!BindAuthoredSupportReference(generated::AuthoredSupports,f.snapshot,f.equipment,bound->owner,p.rigFingerprint+1,f.now));
 return 0;
}
int NullPersistenceDoesNotHideEquipmentReplacement(){
 const auto& p=generated::Opening426Supports.front();SupportFixture f(p);f.equipment.persistence=0;
 const auto bound=BindAuthoredSupportReference(generated::AuthoredSupports,f.snapshot,f.equipment,f.owner,p.rigFingerprint,f.now);CHECK(bound);
 const auto current=[&]{return AuthoredSupportCurrent(*bound,f.snapshot.get(),f.equipment,f.owner,f.now);};CHECK(current());
 f.equipment.persistence=17;CHECK(!current());f.equipment=bound->equipment;
 ++f.equipment.data;++f.snapshot->weaponData;CHECK(!current());
 f.equipment=bound->equipment;f.snapshot->weaponData=f.equipment.data;
 Text(f.equipment.asset,"Replacement");Text(f.snapshot->weaponName,"Replacement");CHECK(!current());
 f.equipment=bound->equipment;Text(f.snapshot->weaponName,p.assetName);CHECK(current());
 f.equipment.data=0;CHECK(!BindAuthoredSupportReference(generated::AuthoredSupports,f.snapshot,f.equipment,f.owner,p.rigFingerprint,f.now));
 return 0;
}
int ConfigurationCadenceDoesNotReleaseHeldSupport(){
 using namespace interaction;
 constexpr std::int64_t ms=1000000;
 bool an94=false,t194=false,qju88=false,spas=false;unsigned checked=0;
 for(const auto& p:generated::AuthoredSupports){
  ++checked;an94|=p.assetName=="AN94_sp_k";t194|=p.assetName=="T194_sp";
  qju88|=p.assetName=="QJU88_sp";spas|=p.assetName=="SPAS12_sp";
  SupportFixture f(p);f.snapshot->observedNs=f.now;f.snapshot->deadlineNs=f.now+200*ms;
  // The real producer observes configuration every 100ms with a 200ms lease.
  // An immutable render input may still carry that source after 100ms while
  // its own 100ms controller deadline has moved beyond the config deadline.
  auto refreshed=*f.snapshot;++refreshed.sequence;refreshed.observedNs=f.now+100*ms;
  refreshed.deadlineNs=f.now+300*ms;
  InputFrame input{};input.spaceGeneration=f.owner.space;input.focused=input.headValid=true;
  for(auto& hand:input.hands){hand.gripTracked=hand.aimTracked=true;hand.active=Components;}
  input.hands[0].grip.position.z=p.aimMode==SupportAimMode::PrimaryHand?-.02f:-.4f;
  SupportGrip policy;const SupportGripOwner owner{1,2,3};std::uint64_t token=0;
  std::optional<AuthoredSupportBinding> beforeCutoff,afterCutoff;
  for(const auto offset:{89,99,101,111}){
   const auto now=f.now+offset*ms;++f.owner.inputSequence;f.owner.inputDeadlineNs=now+100*ms;
   input.generation=f.owner.inputSequence;input.predictedNs=now;input.hands[0].squeeze=offset==89?0.f:1.f;
   const auto bound=BindAuthoredSupportReference(generated::AuthoredSupports,f.snapshot,f.equipment,f.owner,p.rigFingerprint,now);
   CHECK(bound&&AuthoredSupportContact(bound)==p.contactReference);
   const auto current=offset<100?f.snapshot.get():&refreshed;
   CHECK(AuthoredSupportCurrent(*bound,current,f.equipment,f.owner,now));
   CHECK(bound->selected==f.snapshot&&bound->selected->deadlineNs==f.now+200*ms);
   CHECK(bound->owner.inputDeadlineNs==now+100*ms);
   const auto held=policy.Update(owner,input,{true,.05f,{},p.aimMode});
   if(offset==89)CHECK(!held.holding);
   else{
    CHECK(held.holding&&!held.released&&held.engaged==(offset==99));
    if(offset==99){token=held.token;beforeCutoff=bound;}
    CHECK(held.token==token);
   }
   if(offset==101){
    // This is the former failure: both sources remain live, but their
    // future deadlines are differently ordered. No lifetime is extended.
    CHECK(bound->selected->deadlineNs<bound->owner.inputDeadlineNs);afterCutoff=bound;
   }
  }
  CHECK(beforeCutoff&&afterCutoff);
  ++f.owner.inputSequence;f.owner.inputDeadlineNs=f.now+350*ms;
  CHECK(AuthoredSupportCurrent(*afterCutoff,&refreshed,f.equipment,f.owner,f.now+199*ms));
  CHECK(!AuthoredSupportCurrent(*beforeCutoff,&refreshed,f.equipment,f.owner,f.now+199*ms)); // original input expires
  CHECK(!AuthoredSupportCurrent(*afterCutoff,&refreshed,f.equipment,f.owner,f.now+200*ms)); // original config expires
  CHECK(!BindAuthoredSupportReference(generated::AuthoredSupports,f.snapshot,f.equipment,f.owner,p.rigFingerprint,f.now+200*ms));
  CHECK(!AuthoredSupportCurrent(*afterCutoff,nullptr,f.equipment,f.owner,f.now+112*ms));
  auto changed=refreshed;++changed.owner.equipGeneration;
  CHECK(!AuthoredSupportCurrent(*afterCutoff,&changed,f.equipment,f.owner,f.now+112*ms));
  changed=refreshed;Text(changed.states[0].meshes[0].assetPath,"Objects/Weapons/Replacement");
  CHECK(!AuthoredSupportCurrent(*afterCutoff,&changed,f.equipment,f.owner,f.now+112*ms));
  // Correct binding continuity never changes actual pull-away or squeeze release.
  ++input.generation;input.predictedNs+=ms;
  const auto away=policy.Update(owner,input,{true,SupportGripRetentionRadiusMeters+.01f,{},p.aimMode});
  CHECK(away.released&&!away.holding&&away.reason==SupportRelease::Distance);
  ++input.generation;input.predictedNs+=ms;CHECK(!policy.Update(owner,input,{true,.05f,{},p.aimMode}).holding);
  ++input.generation;input.predictedNs+=ms;input.hands[0].squeeze=0;policy.Update(owner,input,{true,.05f,{},p.aimMode});
  ++input.generation;input.predictedNs+=ms;input.hands[0].squeeze=1;CHECK(policy.Update(owner,input,{true,.05f,{},p.aimMode}).engaged);
  ++input.generation;input.predictedNs+=ms;input.hands[0].squeeze=0;
  const auto released=policy.Update(owner,input,{true,.05f,{},p.aimMode});
  CHECK(released.released&&!released.holding&&released.reason==SupportRelease::Button);
 }
 CHECK(checked==generated::AuthoredSupports.size()&&checked==78&&an94&&t194&&qju88&&spas);
 return 0;
}
int main(){
 if(EnrolledSupportComposesExistingGrip()||OriginalSupportConfigurationAndInputLifetime()||NullPersistenceDoesNotHideEquipmentReplacement()||
    ConfigurationCadenceDoesNotReleaseHeldSupport())return 1;
 math::Matrix4 left{};for(unsigned n=0;n<4;++n)left.values[n][n]=1;left.values[3]={0,-.1f,-.6f,1};
 constexpr std::string_view digest="0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
 const std::array<AuthoredSupportProfile,1> profiles{{{"SyntheticSupport","Objects/Weapons/SyntheticMesh",digest,digest,digest,91,left}}};
 const auto& p=profiles[0];constexpr auto now=1000000000ll;
 WeaponEquipmentIdentity equipment;equipment.weapon=15;equipment.data=16;equipment.persistence=17;Text(equipment.asset,p.assetName);
 AuthoredGripOwner owner{10,11,12,13,14,20,now+90000000};SelectedMeshesSnapshot s;
 s.owner={9,10,11,15,12,18,14};s.sequence=19;s.observedNs=now-10000000;s.deadlineNs=now+200000000;
 s.weaponData=16;s.stateTypeInfo=1;s.meshTypeInfo=2;s.inventory=3;s.selectedSlot=4;Text(s.weaponName,p.assetName);
 s.stateCount=1;s.soleConfiguredArray=5;s.states[0].array=5;s.states[0].state=6;s.states[0].count=1;
 auto& m=s.states[0].meshes[0];m.address=7;m.typeInfo=2;m.namePointer=8;Text(m.assetPath,p.meshPath);
 const auto bind=[&]{return BindAuthoredSupport(profiles,s,equipment,owner,p.rigFingerprint,now);};
 CHECK(bind()==&p);const auto originalEquipment=equipment;
 // Current binding does not conflate physical equip13 with native equip18.
 // Replacing the actual selected native identity invalidates the anchor.
 CHECK(bind());CHECK(equipment==originalEquipment);
 auto changed=s;changed.owner.weapon++;CHECK(!BindAuthoredSupport(profiles,changed,equipment,owner,p.rigFingerprint,now));
 changed=s;changed.weaponData++;CHECK(!BindAuthoredSupport(profiles,changed,equipment,owner,p.rigFingerprint,now));
 changed=s;changed.deadlineNs=now;CHECK(!BindAuthoredSupport(profiles,changed,equipment,owner,p.rigFingerprint,now));
 changed=s;changed.sequence=owner.inputSequence+1;CHECK(!BindAuthoredSupport(profiles,changed,equipment,owner,p.rigFingerprint,now));
 changed=s;changed.states[0].count=2;changed.states[0].meshes[1]=m;CHECK(!BindAuthoredSupport(profiles,changed,equipment,owner,p.rigFingerprint,now));
 auto duplicate=std::array{p,p};CHECK(!BindAuthoredSupport(duplicate,s,equipment,owner,p.rigFingerprint,now));
 // First same-asset row can target another configured mesh. Order is immaterial.
 auto variants=std::array{p,p};variants[0].meshPath="Objects/Weapons/OtherMesh";
 CHECK(BindAuthoredSupport(variants,s,equipment,owner,p.rigFingerprint,now)==&variants[1]);
 std::swap(variants[0],variants[1]);CHECK(BindAuthoredSupport(variants,s,equipment,owner,p.rigFingerprint,now)==&variants[0]);
 changed=s;changed.deadlineNs=owner.inputDeadlineNs-1;
 CHECK(BindAuthoredSupport(profiles,changed,equipment,owner,p.rigFingerprint,now)==&p);
 CHECK(!BindAuthoredSupport(profiles,s,equipment,owner,p.rigFingerprint+1,now));
 owner.space++;CHECK(!bind());owner.space--;owner.inputDeadlineNs=now;CHECK(!bind());
 std::cout<<"Measured support-only binding ownership, ambiguity and expiry passed\n";
 return 0;
}

