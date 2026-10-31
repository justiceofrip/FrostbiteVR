#include "Test.h"
#include "Bc2MagazinePhysicalReload.h"
#include "Bc2SelectedCarriedFixture.h"
#include "Bc2MagazineRawContactPipelineFixture.generated.h"
#include "Bc2SightContact.h"
#include "Bc2HandPose.h"
#include "Bc2RigPublication.h"
#include <cstdio>
#include <cstring>
using namespace fvr;
using namespace fvr::bc2;
using namespace fvr::interaction;
using namespace fvr::interaction::reload_insertion_detail;
namespace {
constexpr std::int64_t Ms=1000000;
ReloadObservedConfig Configuration(const MagazineEquipmentProfile& profile){
 const auto& d=profile.native->configuration;const auto& v=d.values;ReloadObservedConfig c;
 c.weaponData=fvr::test::Fixture::ad;c.firingData=0xc0000;c.primaryFire=0xd0000;c.ammoAddress=c.primaryFire+0x170;
 std::copy(d.assetName.begin(),d.assetName.end(),c.assetName.begin());std::copy(d.assetPath.begin(),d.assetPath.end(),c.assetPath.begin());
 c.fireLogicType=v.fireLogicType;c.reloadType=v.reloadType;c.fireInputAction=v.fireInputAction;c.reloadInputAction=v.reloadInputAction;
 c.baseCapacity=v.baseCapacity;c.numberOfMagazines=v.numberOfMagazines;c.reloadDelay=v.reloadDelay;c.reloadTime=v.reloadTime;
 c.reloadThreshold=v.reloadThreshold;c.postReloadTime=v.postReloadTime;c.boltDelay=v.boltDelay;c.boltTime=v.boltTime;
 c.holdBoltUntilFireRelease=v.holdBoltUntilFireRelease;c.holdBoltUntilZoomRelease=v.holdBoltUntilZoomRelease;return c;
}
const magazine_raw_fixture::PoseCase* Authored(const MagazineEquipmentProfile& profile){
 for(const auto& c:magazine_raw_fixture::Cases)if(c.configuration==profile.native->configuration.assetPath&&
  c.asset==profile.geometry->asset&&c.mesh==profile.geometry->mesh)return &c;
 return nullptr;
}
struct Pipeline {
 fvr::test::Fixture memory;
 const MagazineEquipmentProfile& profile;
 ReloadHoldIdentity native{};
 AmmoResourceBinding binding{};
 AmmoResourceService service;
 AmmoResourceChannel channel;
 std::optional<Bc2MagazinePhysicalReload> consumer;
 HandInteraction hands;
 std::optional<HandClaim> gun;
 MagazinePhysicalSample sample;
 MagazinePhysicalResult result;
 std::shared_ptr<SelectedMeshesSnapshot> meshes=std::make_shared<SelectedMeshesSnapshot>();
 std::optional<AmmoResourceRequest> submitted;
 std::uint64_t sequence=0,intent=0;
 std::int64_t now=1000*Ms;
 unsigned submits=0;
 explicit Pipeline(const MagazineEquipmentProfile& p):profile(p){
  memory.bytes.resize(0x80000);
  memory.Word(memory.ad+0xc,0x31000);memory.Text(0x31000,std::string(p.geometry->asset).c_str());memory.Word(memory.ad+0x64,0x32000);
  native={memory.owner,{0x50000,0x51000,0x52000},0x53000,0x54000,0x55000};
  binding={native.owner,{{native.owner.soldier,native.owner.actorGeneration,native.owner.weapon,45},native.owner.equipGeneration,native.owner.space},
   memory.inventory,memory.switching,memory.ad,0x32000,now,now+100*Ms};
  sample.nativeOwner=native.owner;sample.weapon={native.owner.weapon,17};sample.trackingEpoch=native.owner.space;
  sample.input={{(std::uint64_t(native.owner.weak)<<32)|native.owner.soldier,native.owner.actorGeneration,17,native.owner.space},
   0,now,now+100*Ms,now,true,{true,true},{true,false}};
  sample.asset=p.geometry->asset;sample.meshes=meshes;sample.bodyFromHand=Identity();sample.bodyFromHand.values[3][0]=2;
  sample.entryProfileObserved=true;
  meshes->owner=native.owner;meshes->weaponData=memory.ad;meshes->stateCount=1;meshes->soleConfiguredArray=0x110000;meshes->states[0].count=1;
  auto& mesh=meshes->states[0].meshes[0];mesh.kind=p.geometry->meshKind;mesh.address=0x120000;
  std::copy(p.geometry->mesh.begin(),p.geometry->mesh.end(),mesh.assetPath.begin());
  MagazinePhysicalApi api;api.context=this;
  api.resourceRead=[](void* c,const ReloadStateOwner& owner,std::int64_t now)noexcept{return static_cast<Pipeline*>(c)->channel.Read(owner,now);};
  api.resourceSubmit=[](void* c,const AmmoResourceRequest& request)noexcept{
   auto& f=*static_cast<Pipeline*>(c);++f.submits;f.submitted=request;return f.channel.Submit(request);};
  consumer.emplace(true,api);consumer->EnableBodyAmmo();
 }
 void Tick(bool grip=false){
  now+=20*Ms;++sequence;sample.input.sequence=sample.geometrySequence=sequence;
  sample.input.observedNs=sample.input.nowNs=now;sample.input.deadlineNs=now+100*Ms;
  sample.input.released[0]=!grip;sample.gripPressed=grip;
  binding.observedNs=now;binding.deadlineNs=now+100*Ms;
  const int capacity=profile.native->configuration.values.baseCapacity;
  service.Select(binding,native,{binding.context,sequence,now,now+100*Ms,{capacity-1,83,capacity},true},now);channel.Publish(service.View());
  sample.entryProfile=FindMagazineEquipment(Configuration(profile));
  sample.family=sample.entryProfile?ResolveMagazineFamily(memory.Memory(),native.owner,sample.input,sample.weapon,*sample.entryProfile).value_or(MagazineFamilyEvidence{}):MagazineFamilyEvidence{};
  meshes=std::make_shared<SelectedMeshesSnapshot>(*meshes);sample.meshes=meshes;
  meshes->sequence=sequence;meshes->observedNs=now;meshes->deadlineNs=now+100*Ms;
  // Raw contact is always the preceding actual builder packet. No readiness,
  // raw.valid, fingerprint, native attachment or hand transform is seeded here.
  if(sample.raw.valid)sample.originalHandEvidence=sample.raw.inputEvidence;
  hands.Update(sample.input);HandContactProof proof{{1002,1},sequence,sample.input.deadlineNs,true};
  if(gun)gun=hands.Renew(sample.input,gun->token,proof).claim;
  if(!gun)gun=hands.Acquire(sample.input,{sample.input.owner,InteractionHand::Right,HandClaimKind::GunHold,sample.weapon,proof,++intent,0}).claim;
  result=consumer->Tick(sample,hands,intent);
 }
 // Exercise the production reader and native palette reconstruction. The
 // selected weapon controls come from the exact source clip; unrelated bones
 // retain the exact skeleton bind pose. No native writes are performed.
 std::optional<RigSnapshot> ReadRig(const magazine_raw_fixture::PoseCase& c,unsigned frame){
  const auto authored=magazine_raw_fixture::Rig(c,frame);const auto count=unsigned(authored.names.size());
  constexpr unsigned animation=0x60000,root=0x61000,skeleton=0x62000,pose=0x63000,header=0x64000,definition=0x65000,
   records=0x66000,names=0x70000,strings=0x71000,world=0x74000,skin=0x78000;
  memory.Byte(memory.soldier+0x475,1);memory.Word(memory.soldier+0x3b4,animation);memory.Word(memory.soldier+0x3b8,root);memory.Word(root+4,memory.soldier);
  memory.Word(animation+0x50,skeleton);memory.Word(animation+0x54,pose);memory.Word(animation+0x938,authored.weaponBone);memory.Byte(animation+0x99c,0);
  memory.Word(skeleton+0xc,definition);memory.Word(skeleton+0x10,count);memory.Word(skeleton+0x18,names);memory.Word(skeleton+0x1c,names+count*4);
  memory.Word(pose+0x28,skeleton);memory.Word(pose+0x1c,count);memory.Word(pose+0x18,header);memory.Word(pose+0x10,definition);
  memory.Word(header,count);memory.Word(header+4,world);memory.Word(header+8,skin);memory.Word(definition+4,count);memory.Word(definition+8,records);
  const auto put=[&](unsigned address,math::Matrix4 transform){
   for(unsigned n=0;n<4;++n){transform.values[2][n]=-transform.values[2][n];transform.values[n][2]=-transform.values[n][2];}
   std::memcpy(memory.bytes.data()+address-0x10000,&transform,64);
  };
  for(unsigned n=0;n<count;++n){memory.Word(names+n*4,strings+n*64);memory.Text(strings+n*64,authored.names[n].c_str());
   memory.Word(records+n*0x98,strings+n*64);memory.Word(records+n*0x98+4,unsigned(authored.parents[n]));
   put(records+n*0x98+0x4c,authored.inverseBind[n]);put(world+n*64,authored.evaluatedWorld[n]);
   put(skin+n*64,Multiply(authored.inverseBind[n],authored.evaluatedWorld[n]));}
  const RigMemory reader{this,[](void* c,unsigned address,void* out,std::size_t bytes){auto& f=*static_cast<Pipeline*>(c);
   if(address<0x10000||std::uint64_t(address)+bytes>0x10000+f.memory.bytes.size())return false;
   std::memcpy(out,f.memory.bytes.data()+address-0x10000,bytes);return true;}};
  return ReadFirstPersonRig(reader,native.owner.soldier,native.owner.weak);
 }
 MagazineRawContact Build(const RigSnapshot& rig,float units=1.f){
  const auto publication=Publication();
  if(!rigPublication::MagazineTrackingAdmitted(publication,sample.input.deadlineNs,now))
   return RejectedMagazineRawContact(MagazineRawContactReason::TrackingNotFresh);
  auto weapon=Identity();weapon.values[3]={.2f,.4f,-.3f,1.f};
  auto wrist=InsertionHandWorld(profile.geometry->interaction.insertion,profile.geometry->interaction.insertion.travelMeters,weapon);
  for(unsigned n=0;n<3;++n){weapon.values[3][n]*=units;wrist.values[3][n]*=units;}
  return BuildMagazineRawContact(result.tracking,rig,sample.asset,wrist,weapon,units,now);
 }
 rigPublication::Tracking Publication()const {
  rigPublication::Tracking out;out.magazine=result.tracking;out.soldier=native.owner.soldier;out.weak=native.owner.weak;
  out.weapon=native.owner.weapon;out.ownerGeneration=native.owner.actorGeneration;
  out.input.generation=sample.input.sequence;out.input.spaceGeneration=sample.input.owner.space;out.input.predictedNs=now;
  out.input.focused=sample.input.focused;out.input.headValid=true;
  for(unsigned n=0;n<2;++n){out.input.hands[n].gripTracked=sample.input.tracked[n];out.input.hands[n].aimTracked=true;}
  return out;
 }
};
int AuthoredBootstrapAndActivation(){
 unsigned ready=0,unavailable=0,children=0,completedSubtrees=0,failed=0;
 for(const auto& registered:RegisteredMagazineNativeProfiles()){
  const auto p=FindMagazineEquipment(registered.id);if(!p||!p->Ready()){++unavailable;continue;}++ready;
  const auto exercise=[&]()->int {
  const auto authored=Authored(*p);
  if(!authored)std::printf("Missing authored raw fixture: %.*s\n",int(p->native->configuration.assetPath.size()),p->native->configuration.assetPath.data());
  CHECK(authored);Pipeline f(*p);CHECK(!f.sample.raw.valid);f.Tick();
  CHECK(f.result.tracking.enabled&&MagazineTrackingFresh(f.result.tracking,f.now));
  CHECK(f.result.tracking.family.binding.profile==p&&f.submits==0);
  for(unsigned frame=0;frame<3;++frame){const auto rig=f.ReadRig(*authored,frame);CHECK(rig);
   CHECK(SightRigFingerprint(rig->names,rig->parents,rig->inverseBind)==p->geometry->rigFingerprint);
   CHECK(BindLeftHandPose(rig->names,rig->parents,rig->inverseBind,1));
   CHECK(BindRightHandPose(rig->names,rig->parents,rig->inverseBind,1));
   const auto raw=f.Build(*rig);
   if(!raw.valid)std::printf("Raw rejected: %.*s frame %u reason %s child %u\n",int(authored->configuration.size()),authored->configuration.data(),frame,
    MagazineRawContactReasonNames[unsigned(raw.evidence.reason)],raw.evidence.childBone);
   CHECK(raw.valid&&raw.evidence.reason==MagazineRawContactReason::Accepted);
   CHECK(raw.inputEvidence.sequence==f.sequence&&raw.inputEvidence.deadlineNs==f.sample.input.deadlineNs);
   CHECK(AdmitMagazineRawContact(&f.result.tracking,f.native.owner,&raw,f.sequence,f.sample.input.deadlineNs,f.now).valid);
   const auto binding=BindMagazinePresentation(*rig,*p);CHECK(binding);
   if(binding->assemblyCount>p->geometry->assemblyCount){
    CHECK(!magazine_presentation_detail::Derive(*rig,*p->geometry));
    if(frame==0)++completedSubtrees;
   }
   if(f.result.tracking.target){const auto plan=BuildMagazinePresentation(*rig,*binding,f.result.tracking,raw.weaponWorldMeters,1,f.now);
    CHECK(plan.binding);CHECK(plan.writes.size()==1+binding->assemblyCount);CHECK(BuildRigPosePlan(*rig,plan.writes));
    for(unsigned n=0;n<binding->assemblyCount;++n)CHECK(plan.writes[n+1].index==binding->assembly[n]);}
   const auto hidden=HideMagazinePackedPalette(*rig,*binding,rig->nativeEvaluated);CHECK(hidden);
   for(unsigned n=0;n<rig->names.size();++n){const bool member=n==binding->magazine||
    std::find(binding->assembly.begin(),binding->assembly.begin()+binding->assemblyCount,n)!=binding->assembly.begin()+binding->assemblyCount;
    CHECK(((*hidden)[n]!=rig->nativeEvaluated[n])==member);}
   if(frame==0)f.sample.raw=raw;
  }
  f.Tick();CHECK(f.result.tracking.enabled&&f.submits==0); // Actual raw neutral packet arms contact.
  const auto rig=f.ReadRig(*authored,0);CHECK(rig);f.sample.raw=f.Build(*rig);CHECK(f.sample.raw.valid);f.Tick(true);
  CHECK(f.result.tracking.enabled&&f.result.interaction.phase==DetachableMagazinePhase::PreparingRemoval);
  CHECK(f.submits==1&&f.submitted&&f.submitted->intent.operation==AmmunitionOperation::RemoveMagazine);
  return 0;
  };
  if(p->geometry->assemblyCount)++children;
  if(exercise())++failed;
 }
 CHECK(ready>0);std::printf("Authored raw pipeline: %u ready configurations, %u child assemblies, %u completed subtrees, %u unavailable, %u failed; native rig read, bootstrap, three selected frames, remove request.\n",ready,children,completedSubtrees,unavailable,failed);
 CHECK(failed==0);
 return 0;
}
int VisibilityAndOriginalLifetimes(){
 for(const auto& registered:RegisteredMagazineNativeProfiles()){
  const auto p=FindMagazineEquipment(registered.id);if(!p||!p->Ready())continue;Pipeline f(*p);f.Tick();
  const auto authored=Authored(*p);CHECK(authored);const auto rig=f.ReadRig(*authored,0);CHECK(rig);
  const auto binding=BindMagazinePresentation(*rig,*p);CHECK(binding);const auto raw=f.Build(*rig);CHECK(raw.valid);
  auto hidden=*rig;hidden.nativeHiddenLeaves.push_back(binding->magazine);
  CHECK(f.Build(hidden).evidence.reason==MagazineRawContactReason::MagazineHidden);
  auto scaled=*rig;for(unsigned n=0;n<3;++n)scaled.evaluatedWorld[binding->magazine].values[0][n]*=2;
  CHECK(f.Build(scaled).evidence.reason==MagazineRawContactReason::MagazineNotRigid);
  for(unsigned n=0;n<binding->assemblyCount;++n){auto moved=*rig;moved.evaluatedWorld[binding->assembly[n]].values[3][1]+=.002f;
   CHECK(f.Build(moved).evidence.reason==MagazineRawContactReason::ChildPositionMismatch);
   auto childHidden=*rig;childHidden.nativeHiddenLeaves.push_back(binding->assembly[n]);
   CHECK(f.Build(childHidden).evidence.reason==MagazineRawContactReason::ChildHidden);}
  const auto originalDeadline=raw.inputEvidence.deadlineNs;const auto frameDeadline=f.now+30*Ms;
  auto publication=f.Publication();CHECK(rigPublication::MagazineTrackingAdmitted(publication,originalDeadline,f.now));
  CHECK(!rigPublication::MagazineTrackingAdmitted(publication,originalDeadline-1,f.now));
  publication.input.hands[0].gripTracked=false;CHECK(!rigPublication::MagazineTrackingAdmitted(publication,originalDeadline,f.now));
  publication=f.Publication();++publication.input.generation;CHECK(!rigPublication::MagazineTrackingAdmitted(publication,originalDeadline,f.now));
  publication=f.Publication();++publication.ownerGeneration;CHECK(!rigPublication::MagazineTrackingAdmitted(publication,originalDeadline,f.now));
  f.Tick();const auto& guard=f.result.tracking;CHECK(guard.inputEvidence.sequence>raw.inputEvidence.sequence);
  CHECK(AdmitMagazineRawContact(&guard,f.native.owner,&raw,raw.inputEvidence.sequence,frameDeadline,f.now).valid);
  CHECK(AdmitMagazineRawContact(&guard,f.native.owner,&raw,raw.inputEvidence.sequence,frameDeadline,frameDeadline).evidence.reason==MagazineRawContactReason::FrameUnavailable);
  CHECK(AdmitMagazineRawContact(&guard,f.native.owner,&raw,raw.inputEvidence.sequence,guard.inputEvidence.deadlineNs,originalDeadline).evidence.reason==MagazineRawContactReason::InputExpired);
  CHECK(!AdmitMagazineRawContact(&guard,f.native.owner,&raw,raw.inputEvidence.sequence+1,frameDeadline,f.now).valid);
  auto other=f.native.owner;++other.equipGeneration;CHECK(!AdmitMagazineRawContact(&guard,other,&raw,raw.inputEvidence.sequence,frameDeadline,f.now).valid);
  const auto rejected=RejectedMagazineRawContact(MagazineRawContactReason::ChildPositionMismatch);
  CHECK(AdmitMagazineRawContact(&guard,f.native.owner,&rejected,raw.inputEvidence.sequence,frameDeadline,f.now).evidence.reason==MagazineRawContactReason::ChildPositionMismatch);
  CHECK(raw.inputEvidence.deadlineNs==originalDeadline);
 }
 return 0;
}
}
int main(){if(AuthoredBootstrapAndActivation()||VisibilityAndOriginalLifetimes())return 1;
 std::puts("Production magazine raw/contact integration passed; offline source evidence only.");}
