#include "Test.h"
#include "Bc2MagazinePhysicalReload.h"
#include "Bc2SelectedCarriedFixture.h"
#include <cstdio>
#include <cstring>
#include <sstream>
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
enum class Fault {None,Configuration,Owner,Rig,Resource,Raw,Mapping,Restore};
struct EntryFixture {
 fvr::test::Fixture memory;
 const MagazineEquipmentProfile& profile;
 const MagazineEquipmentProfile* selected=nullptr;
 ReloadObservedConfig configuration;
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
 unsigned reads=0,submits=0;
 Fault fault=Fault::None;
 bool rawAvailable=true;
 static constexpr unsigned Name=0x31000,Persistence=0x32000;
 explicit EntryFixture(const MagazineEquipmentProfile& p):profile(p),selected(&p),configuration(Configuration(p)){
  memory.Word(memory.ad+0xc,Name);memory.Text(Name,std::string(profile.geometry->asset).c_str());memory.Word(memory.ad+0x64,Persistence);
  native={memory.owner,{0x50000,0x60000,0x70000},0x80000,0x90000,0xa0000};
  binding={native.owner,{{native.owner.soldier,native.owner.actorGeneration,native.owner.weapon,45},native.owner.equipGeneration,native.owner.space},
   memory.inventory,memory.switching,memory.ad,Persistence,now,now+100*Ms};
  sample.nativeOwner=native.owner;sample.weapon={native.owner.weapon,17};sample.trackingEpoch=native.owner.space;
  sample.input={{(std::uint64_t(native.owner.weak)<<32)|native.owner.soldier,native.owner.actorGeneration,17,native.owner.space},
   0,now,now+100*Ms,now,true,{true,true},{true,false}};
  sample.asset=profile.geometry->asset;sample.meshes=meshes;sample.bodyFromHand=Identity();sample.bodyFromHand.values[3][0]=2;
  sample.entryProfileObserved=true;
  meshes->owner=native.owner;meshes->weaponData=memory.ad;meshes->stateCount=1;meshes->soleConfiguredArray=0x110000;meshes->states[0].count=1;
  auto& mesh=meshes->states[0].meshes[0];mesh.kind=profile.geometry->meshKind;mesh.address=0x120000;
  std::copy(profile.geometry->mesh.begin(),profile.geometry->mesh.end(),mesh.assetPath.begin());
  sample.raw.valid=true;sample.raw.owner=native.owner;sample.raw.rigFingerprint=profile.geometry->rigFingerprint;
  sample.raw.weaponWorldMeters=Identity();sample.raw.nativeMagazineAttached=true;
  MagazinePhysicalApi api;api.context=this;
  api.resourceRead=[](void* context,const ReloadStateOwner& owner,std::int64_t now)noexcept{
   auto& f=*static_cast<EntryFixture*>(context);++f.reads;return f.channel.Read(owner,now);};
  api.resourceReadObserved=[](void* context,const ReloadStateOwner& owner,std::int64_t now)noexcept{
   auto& f=*static_cast<EntryFixture*>(context);++f.reads;
   if(f.fault==Fault::Resource)return AmmoResourceRead{AmmoResourceReadReason::Owner,{}};
   auto read=f.channel.ReadObserved(owner,now);
   if(read.view&&f.fault==Fault::Mapping)++read.view->binding.persistence;
   if(read.view&&f.fault==Fault::Restore)read.view->originalState=MagazineResourceState::Held;
   return read;};
  api.resourceSubmit=[](void* context,const AmmoResourceRequest& request)noexcept{
   auto& f=*static_cast<EntryFixture*>(context);++f.submits;f.submitted=request;return f.channel.Submit(request);};
  consumer.emplace(true,api);consumer->EnableBodyAmmo();
 }
 void Send(bool grip){
  const auto& profile=*selected;
  now+=20*Ms;++sequence;sample.input.sequence=sample.geometrySequence=sequence;
  sample.input.observedNs=sample.input.nowNs=now;sample.input.deadlineNs=now+100*Ms;
  sample.input.released[0]=!grip;sample.gripPressed=grip;
  binding.observedNs=now;binding.deadlineNs=now+100*Ms;
  const int capacity=profile.native->configuration.values.baseCapacity;
  service.Select(binding,native,{binding.context,sequence,now,now+100*Ms,{capacity-1,83,capacity},true},now);
  channel.Publish(service.View());
  // The production entry uses the exact native configuration, then the real
  // selected-carried-item reader. No profile is chosen by weapon-name alone.
  auto observed=configuration;if(fault==Fault::Configuration)++observed.baseCapacity;
  sample.entryProfile=FindMagazineEquipment(observed);
  sample.family=sample.entryProfile?ResolveMagazineFamily(memory.Memory(),sample.nativeOwner,sample.input,sample.weapon,*sample.entryProfile).value_or(MagazineFamilyEvidence{}):MagazineFamilyEvidence{};
  meshes->sequence=sequence;meshes->observedNs=now;meshes->deadlineNs=now+100*Ms;
  sample.raw.valid=rawAvailable;sample.raw.rigFingerprint=profile.geometry->rigFingerprint;sample.raw.evidence={};
  sample.raw.inputEvidence=sample.input;sample.originalHandEvidence=sample.input;
  const auto& insertion=profile.geometry->interaction.insertion;
  sample.raw.rawLeftWristWorldMeters=InsertionHandWorld(insertion,insertion.travelMeters,Identity());
  if(fault==Fault::Owner)++sample.family.binding.owner.equipGeneration;
  if(fault==Fault::Rig)sample.raw.rigFingerprint=profile.geometry->rigFingerprint^1;
  if(fault==Fault::Raw){sample.raw.valid=false;
   sample.raw.evidence={MagazineRawContactReason::ChildPositionMismatch,4,20,.002f,{}};}
  hands.Update(sample.input);HandContactProof proof{{1002,1},sequence,sample.input.deadlineNs,true};
  if(gun)gun=hands.Renew(sample.input,gun->token,proof).claim;
  if(!gun)gun=hands.Acquire(sample.input,{sample.input.owner,InteractionHand::Right,HandClaimKind::GunHold,sample.weapon,proof,++intent,0}).claim;
  result=consumer->Tick(sample,hands,intent);
 }
 void Select(const MagazineEquipmentProfile& next,unsigned slot){
  selected=&next;configuration=Configuration(next);memory.Select(slot);native.owner=memory.owner;
  const auto data=slot?memory.bd:memory.ad;
  memory.Word(data+0x84,0);
  memory.Word(data+0xc,Name+slot*256);memory.Text(Name+slot*256,std::string(next.geometry->asset).c_str());
  memory.Word(data+0x64,Persistence+slot*256);configuration.weaponData=data;
  binding={native.owner,{{native.owner.soldier,native.owner.actorGeneration,native.owner.weapon,45},native.owner.equipGeneration,native.owner.space},
   memory.inventory,memory.switching,data,Persistence+slot*256,now,now+100*Ms};
  ++sample.input.owner.equipGeneration;sample.weapon={native.owner.weapon,sample.input.owner.equipGeneration};
  sample.nativeOwner=native.owner;sample.asset=next.geometry->asset;
  meshes->owner=native.owner;meshes->weaponData=data;
  auto& mesh=meshes->states[0].meshes[0];mesh.kind=next.geometry->meshKind;mesh.assetPath={};
  std::copy(next.geometry->mesh.begin(),next.geometry->mesh.end(),mesh.assetPath.begin());
  sample.raw.owner=native.owner;
 }
 WeaponMagazineEntryRow Last()const {const auto snapshot=consumer->EntryTelemetry().Snapshot();
  for(std::size_t n=0;n<snapshot->count;++n)if(snapshot->rows[n].profile==sample.entryProfile&&std::string_view(snapshot->rows[n].asset.data())==sample.asset)return snapshot->rows[n];
  return {};}
};
int AllRegisteredConsumers(){
 unsigned registered=0,ready=0,unavailable=0;
 for(const auto& row:RegisteredMagazineNativeProfiles()){
  ++registered;const auto profile=FindMagazineEquipment(row.id);
  if(!profile||!profile->Ready()){++unavailable;continue;}
  ++ready;EntryFixture f(*profile);CHECK(f.consumer->ResourceBackend());f.Send(false);
  CHECK(f.sample.entryProfile==profile&&f.sample.family.verified&&f.sample.family.carried);
  CHECK(f.result.tracking.enabled&&f.result.tracking.family.binding.profile==profile);
  CHECK(f.Last().last==WeaponMagazineEntryStage::Contact&&f.reads==1&&f.submits==0);
  f.Send(true);
  CHECK(f.reads==2&&f.submits==1&&f.submitted);
  CHECK(f.submitted->intent.operation==AmmunitionOperation::RemoveMagazine&&f.submitted->binding==f.binding);
  CHECK(f.result.interaction.phase==DetachableMagazinePhase::PreparingRemoval);
  CHECK(f.Last().last==WeaponMagazineEntryStage::Request&&f.Last().gripSamples==1);
  const auto request=f.channel.Take();CHECK(request&&*request==*f.submitted);
  const auto command=f.service.Accept(*request,f.now);CHECK(command&&command->operation==AmmunitionOperation::RemoveMagazine);
  // Acceptance in the deterministic service is not a native dispatch/receipt.
  CHECK(f.result.completed==0);
  for(const auto fault:{Fault::Configuration,Fault::Owner,Fault::Rig,Fault::Resource,Fault::Raw}){
   EntryFixture denied(*profile);denied.fault=fault;denied.Send(false);denied.Send(true);
   CHECK(denied.submits==0&&!denied.channel.Take()&&denied.result.completed==0&&denied.reads==2);
   const auto stage=fault==Fault::Configuration?WeaponMagazineEntryStage::ProfileMissing:
    fault==Fault::Owner?WeaponMagazineEntryStage::Family:fault==Fault::Rig?WeaponMagazineEntryStage::Rig:
    fault==Fault::Resource?WeaponMagazineEntryStage::ResourceRead:WeaponMagazineEntryStage::RawMissing;
   CHECK(denied.Last().last==stage);
   if(fault==Fault::Resource)CHECK(denied.Last().resourceReadReason==unsigned(AmmoResourceReadReason::Owner));
   if(fault==Fault::Raw){const auto row=denied.Last();
    CHECK(row.rawReasons[unsigned(MagazineRawContactReason::ChildPositionMismatch)]==2&&row.raw.childBone==20);
    CHECK(row.raw.relativeDistanceMeters==std::optional{.002f}&&!row.raw.relativeAngleRadians);}
  }
 }
 CHECK(ready>0);std::printf("Magazine entry: %u registered, %u ready consumer profiles, %u unavailable; five refusal cases per ready profile.\n",registered,ready,unavailable);
 return 0;
}
int BoundedTelemetryRetainsFailures(){
 WeaponMagazineEntryTelemetry telemetry;WeaponMagazineEntryObservation observation;
 observation.asset="unpublished_weapon";observation.stage=WeaponMagazineEntryStage::ProfileMissing;observation.nowNs=1;
 telemetry.Observe(observation);
 observation.stage=WeaponMagazineEntryStage::RawMissing;observation.nowNs=2;observation.input=7;
 observation.raw={MagazineRawContactReason::ChildHidden,4,22,{},{}};telemetry.Observe(observation);
 observation.raw={MagazineRawContactReason::ChildPositionMismatch,4,20,.002f,{}};telemetry.Observe(observation);
 observation.stage=WeaponMagazineEntryStage::Contact;observation.nowNs=3;observation.input=8;observation.raw={};telemetry.Observe(observation);
 auto snapshot=telemetry.Snapshot();CHECK(snapshot&&snapshot->count==1&&snapshot->rows[0].samples==4);
 CHECK(snapshot->rows[0].first==WeaponMagazineEntryStage::ProfileMissing&&snapshot->rows[0].last==WeaponMagazineEntryStage::Contact);
 CHECK(snapshot->rows[0].stages[unsigned(WeaponMagazineEntryStage::ProfileMissing)]==1);
 CHECK(snapshot->rows[0].rawReasons[unsigned(MagazineRawContactReason::ChildHidden)]==1);
 CHECK(snapshot->rows[0].rawReasons[unsigned(MagazineRawContactReason::ChildPositionMismatch)]==1);
 CHECK(snapshot->rows[0].rawReasons[unsigned(MagazineRawContactReason::NotProduced)]==0);
 CHECK(snapshot->rows[0].raw.reason==MagazineRawContactReason::ChildPositionMismatch&&snapshot->rows[0].raw.childBone==20);
 CHECK(snapshot->rows[0].rawFailureObserved&&snapshot->rows[0].rawFailureInput==7&&snapshot->rows[0].rawFailureNs==2);
 for(unsigned n=1;n<=WeaponMagazineEntryTelemetry::Capacity;++n){const auto asset=std::to_string(n);observation.asset=asset;telemetry.Observe(observation);}
 snapshot=telemetry.Snapshot();CHECK(snapshot&&snapshot->count==WeaponMagazineEntryTelemetry::Capacity&&snapshot->dropped==1);
 std::ostringstream report;telemetry.Report(report);CHECK(report.str().find("unpublished_weapon")!=std::string::npos);
 CHECK(report.str().find("\"profile\":null")!=std::string::npos);
 CHECK(report.str().find("\"raw_reason_names\":[\"not_produced\",\"accepted\"")!=std::string::npos);
 const auto expected="\"last_raw_failure\":{\"reason\":"+std::to_string(unsigned(MagazineRawContactReason::ChildPositionMismatch))+
  ",\"input\":7,\"now_ns\":2,\"magazine_bone\":4,\"child_bone\":20,\"relative_distance_m\":0.002,\"relative_angle_rad\":null}";
 CHECK(report.str().find(expected)!=std::string::npos);
 CHECK(report.str().find("\"last_raw_failure\":null")!=std::string::npos);
 return 0;
}
int ConsumerRefusalPrecedesMissingRaw(){
 using Cause=WeaponMagazineConsumerCancelCause;
 for(const auto fault:{Fault::Mapping,Fault::Restore}){
  EntryFixture f(Xm8MagazineEquipment());f.fault=fault;f.rawAvailable=false;f.Send(false);
  auto row=f.Last();CHECK(!f.result.tracking.enabled&&row.last==WeaponMagazineEntryStage::Consumer);
  CHECK(row.consumerCancellation&&row.consumerCancellation->cause==(fault==Fault::Mapping?Cause::Mapping:Cause::Restore));
  CHECK(!row.consumerCancellation->pending&&!row.consumerCancellation->request&&!row.consumerCancellation->cycle);
  CHECK(!row.rawFailureObserved&&row.stages[unsigned(WeaponMagazineEntryStage::RawMissing)]==0);
  std::ostringstream refused;f.consumer->EntryTelemetry().Report(refused);
  CHECK(refused.str().find("\"consumer_cancel_cause_names\":[\"other\",\"input\",\"api\",\"view\",\"mapping\"")!=std::string::npos);
  CHECK(refused.str().find("\"consumer_cancel\":{\"cause\":"+std::to_string(unsigned(row.consumerCancellation->cause))+",\"request\":0,\"cycle\":0,\"pending\":0")!=std::string::npos);
  f.fault=Fault::Raw;f.Send(false);row=f.Last();
  CHECK(f.result.tracking.enabled&&row.last==WeaponMagazineEntryStage::RawMissing&&!row.consumerCancellation);
  CHECK(row.rawFailureObserved&&row.raw.reason==MagazineRawContactReason::ChildPositionMismatch);
  const auto rawInput=row.rawFailureInput;const auto rawNs=row.rawFailureNs;
  f.fault=Fault::None;f.rawAvailable=true;f.Send(false);row=f.Last();
  CHECK(row.last==WeaponMagazineEntryStage::Contact&&!row.consumerCancellation);
  CHECK(row.stages[unsigned(WeaponMagazineEntryStage::Consumer)]==1&&row.stages[unsigned(WeaponMagazineEntryStage::RawMissing)]==1);
  CHECK(row.rawFailureInput==rawInput&&row.rawFailureNs==rawNs&&row.raw.childBone==20);
  std::ostringstream recovered;f.consumer->EntryTelemetry().Report(recovered);
  CHECK(recovered.str().find("\"consumer_cancel\":null")!=std::string::npos);
  CHECK(f.reads==3&&f.submits==0);
 }
 EntryFixture pending(Xm8MagazineEquipment());pending.Send(false);pending.Send(true);
 CHECK(pending.submitted&&pending.channel.Take());const auto request=pending.submitted->intent.id;
 pending.sample.cancel=true;pending.Send(true);
 CHECK(pending.Last().consumerCancellation&&pending.Last().consumerCancellation->cause==Cause::Input);
 pending.sample.cancel=false;pending.rawAvailable=false;pending.Send(false);
 const auto row=pending.Last();CHECK(row.last==WeaponMagazineEntryStage::Consumer&&row.consumerCancellation);
 CHECK(row.consumerCancellation->cause==Cause::Terminal&&row.consumerCancellation->pending);
 CHECK(row.consumerCancellation->request==request&&row.consumerCancellation->cycle==0);
 CHECK(!row.rawFailureObserved&&pending.submits==1&&pending.result.completed==0);
 return 0;
}
int PersistentSelectionWithoutInitialRaw(){
 const auto an94=FindMagazineEquipment("AN94");
 if(!an94||!an94->Ready()){std::puts("Persistent XM8/AN94 selection unavailable without the generated AN94 profile.");return 0;}
 EntryFixture f(Xm8MagazineEquipment());const auto consumer=&*f.consumer;
 for(unsigned step=0;step<3;++step){
  if(step)f.Select(step==1?*an94:Xm8MagazineEquipment(),step==1?1:0);
  f.rawAvailable=false;f.Send(false);
  CHECK(&*f.consumer==consumer&&f.result.tracking.enabled&&f.result.tracking.family.binding.profile==f.selected);
  CHECK(f.Last().last==WeaponMagazineEntryStage::RawMissing&&!f.Last().consumerCancellation);
  f.rawAvailable=true;f.Send(false);
  CHECK(f.Last().last==WeaponMagazineEntryStage::Contact&&!f.Last().consumerCancellation);
  CHECK(f.sample.family.verified&&f.sample.family.carried&&f.submits==0);
 }
 f.Send(true);CHECK(f.submits==1&&f.result.interaction.phase==DetachableMagazinePhase::PreparingRemoval);
 CHECK(f.Last().last==WeaponMagazineEntryStage::Request&&f.submitted->binding==f.binding);
 const auto rows=f.consumer->EntryTelemetry().Snapshot();CHECK(rows&&rows->count==2);
 CHECK(f.reads==7&&f.result.completed==0);
 return 0;
}
}
int main(){if(AllRegisteredConsumers()||BoundedTelemetryRetainsFailures()||ConsumerRefusalPrecedesMissingRaw()||PersistentSelectionWithoutInitialRaw())return 1;
 std::puts("Shared normal resource-consumer entry and bounded telemetry passed; no native/headset claim.");}
