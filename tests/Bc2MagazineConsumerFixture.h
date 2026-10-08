// Shared actual-consumer test harness derived from the predecessor XM8 regression fixture.
#pragma once
#include "Bc2MagazinePhysicalReload.h"
#include "Bc2MagazineDetached.h"
#include "Bc2Xm8MagazineCalibration.h"
#include "Test.h"
#include <cstring>
#include <iostream>
#include <sstream>
using namespace fvr;using namespace fvr::bc2;using namespace fvr::interaction;using namespace fvr::interaction::reload_insertion_detail;
namespace magazine_consumer_fixture {
constexpr std::int64_t Ms=1000000;
auto Pose(float x=0,float y=0,float z=0){auto m=Identity();m.values[3]={x,y,z,1};return m;}
bool Same(const math::Matrix4& a,const math::Matrix4& b,float e=.00001f){for(unsigned n=0;n<4;++n)for(unsigned k=0;k<4;++k)
 if(std::abs(a.values[n][k]-b.values[n][k])>e)return false;return true;}
struct Fixture {
 const MagazineEquipmentProfile* profile=&Xm8MagazineEquipment();
 WeaponEquipmentIdentity equipment{};
 bool directIdentity=false;std::optional<WeaponModeMemory> familyMemory;
 MagazineCycleStartResult startResult=MagazineCycleStartResult::Started,inspectResult=MagazineCycleStartResult::Unknown;unsigned inspections=0,familyFault=0;
 std::int64_t now=1000*Ms;std::uint64_t seq=100,intent=0,cycle=0,event=0;
 unsigned starts=0,submits=0,cancels=0;bool held=false,gateSent=false,allowRetire=false,keep=true,source=true;
 Bc2AmmoReserveLease reserve{};MagazinePhysicalSample s{};MagazinePhysicalApi api{};HandInteraction hands;
 std::optional<HandClaim> gun;std::optional<ManualReloadRequest> unseat;
 std::optional<ReloadMagazineNativeRequest> submitted;std::optional<ReloadMagazineAckEvidence> ack;
 std::shared_ptr<SelectedMeshesSnapshot> meshes=std::make_shared<SelectedMeshesSnapshot>();
 std::optional<Bc2MagazinePhysicalReload> policy;MagazinePhysicalResult result;
 Fixture(bool enabled=true,const MagazineEquipmentProfile& selected=Xm8MagazineEquipment(),bool direct=false):profile(&selected),directIdentity(direct){
  reserve.identity.owner={0x10000,0x20000,0x30000,0x40000,5,3,7};reserve.identity.firing={0x50000,0x60000,0x70000};
  reserve.identity.serverPlayer=0x80000;reserve.identity.serverSoldier=0x90000;reserve.identity.serverItem=0xa0000;
  reserve.loaded=27;reserve.reserve=83;reserve.capacity=30;reserve.verified=reserve.reloadInputReady=true;
  s.nativeOwner=reserve.identity.owner;s.weapon={0xb0000,17};s.trackingEpoch=7;
  s.input={{(std::uint64_t(0x30000)<<32)|0x20000,5,17,7},1,now,now+100*Ms,now,true,{true,true},{true,false}};
  s.asset=profile->geometry->asset;s.meshes=meshes;s.bodyFromHand=Pose(2);s.geometrySequence=1;
  meshes->owner=s.nativeOwner;meshes->stateCount=1;meshes->soleConfiguredArray=0x110000;meshes->states[0].count=1;
  auto& mesh=meshes->states[0].meshes[0];mesh.kind=profile->geometry->meshKind;mesh.address=0x120000;
  std::memcpy(mesh.assetPath.data(),profile->geometry->mesh.data(),profile->geometry->mesh.size());
  s.raw.valid=true;s.raw.owner=s.nativeOwner;s.raw.rigFingerprint=profile->geometry->rigFingerprint;s.raw.weaponWorldMeters=Pose();
  s.raw.nativeMagazineAttached=true;
  if(directIdentity||profile->native->identityRoute==MagazineIdentityRoute::SelectedCarriedItem){
   s.weapon.id=s.nativeOwner.weapon;equipment.weapon=s.nativeOwner.weapon;equipment.data=0x210000;equipment.persistence=0x220000;
   std::memcpy(equipment.asset.data(),s.asset.data(),s.asset.size());meshes->weaponData=equipment.data;
  }
  api.context=this;api.clock=[](void* p)noexcept{return static_cast<Fixture*>(p)->now;};
  api.reserve=[](void* p)noexcept->std::optional<Bc2AmmoReserveLease>{auto& f=*static_cast<Fixture*>(p);return f.source?std::optional{f.reserve}:std::nullopt;};
  api.identity=[](void* p)noexcept->std::optional<ReloadHoldIdentity>{return static_cast<Fixture*>(p)->reserve.identity;};
  api.start=[](void* p,const ReloadCycleControl& c,const ManualReloadRequest& r,const ReloadMagazineStartupPulse& pulse)noexcept{
   auto& f=*static_cast<Fixture*>(p);++f.starts;f.cycle=c.cycle;f.unseat=r;
   if(!pulse.ValidFor(c)||pulse.endNs<=f.now)return MagazineCycleStartResult::NotStarted;
   return r.owner.actor==f.s.nativeOwner.soldier&&r.owner.equipGeneration==f.s.nativeOwner.equipGeneration&&c.deadlineNs==f.s.input.deadlineNs?f.startResult:MagazineCycleStartResult::NotStarted;};
  api.inspectStart=[](void* p,const ReloadHoldIdentity&,std::uint64_t,const std::optional<ReloadMagazineStartupPulse>&)noexcept{auto& f=*static_cast<Fixture*>(p);++f.inspections;return f.inspectResult;};
  api.keep=[](void* p,const ReloadCycleControl&)noexcept{return static_cast<Fixture*>(p)->keep;};
  api.lease=[](void* p,const ReloadHoldIdentity& id,std::uint64_t cycle)noexcept->std::optional<ReloadMagazineLease>{
   auto& f=*static_cast<Fixture*>(p);if(!f.held&&!f.submitted)return {};
   return ReloadMagazineLease{id,cycle,f.seq,f.now,f.now+100*Ms,f.reserve.loaded,f.reserve.reserve,f.reserve.capacity,true,f.held};};
  api.gate=[](void* p,const ReloadHoldIdentity& id,std::uint64_t cycle)noexcept->std::optional<ReloadMagazineGateAcknowledgement>{
   auto& f=*static_cast<Fixture*>(p);if(!f.held||f.gateSent||!f.unseat)return {};f.gateSent=true;
   const auto& r=*f.unseat;return ReloadMagazineGateAcknowledgement{{r.id,r.owner,r.operation,ReloadAcknowledgement::Applied},
    {id,cycle,f.seq,f.now,f.now+100*Ms,f.reserve.loaded,f.reserve.reserve,f.reserve.capacity,true,true}};};
  api.submit=[](void* p,const ReloadMagazineNativeRequest& r)noexcept{auto& f=*static_cast<Fixture*>(p);++f.submits;f.submitted=r;f.held=false;return true;};
  api.ack=[](void* p,const ReloadHoldIdentity&,std::uint64_t)noexcept->std::optional<ReloadMagazineAckEvidence>{return static_cast<Fixture*>(p)->ack;};
  api.cancel=[](void* p)noexcept{++static_cast<Fixture*>(p)->cancels;};
  api.retire=[](void* p,const ReloadHoldIdentity& id,std::uint64_t cycle)noexcept->std::optional<ReloadCycleRetirement>{auto& f=*static_cast<Fixture*>(p);
   if(!f.allowRetire)return {};return ReloadCycleRetirement{id,cycle,++f.event,f.now,f.now+200*Ms,true};};
  policy.emplace(enabled,api,AmmoSupplyConfig{InteractionHand::Left,1000,{1001,1},{0,0,0},.15f,200*Ms});
 }
 void Geometry(float z){const auto p=profile->geometry->interaction.insertion;
  s.raw.rawLeftWristWorldMeters=Multiply(p.itemFromHand,Multiply(*InverseRigid(p.itemFromInsertion),Multiply(TravelPose(p,z),p.weaponFromEntry)));}
 void Sync(){s.input.nowNs=now;s.family={{s.nativeOwner,s.weapon,0xd0000,0xc0000,2},now,now+100*Ms,true};s.family.binding.profile=profile;
  if(directIdentity||profile->native->identityRoute==MagazineIdentityRoute::SelectedCarriedItem){
   s.family.binding.launcher=s.family.binding.launcherSlot=0;s.family.binding.equipment=equipment;
   s.family.carried=SelectedCarriedWeaponEvidence{s.nativeOwner,s.input.owner,s.weapon,equipment,0xd0000,0xe0000,0,0,s.input.sequence,now,now+100*Ms,true};
  }
  if(familyMemory)s.family=ResolveMagazineFamily(*familyMemory,s.nativeOwner,s.input,s.weapon,*profile).value_or(MagazineFamilyEvidence{});
  if(familyFault==1)s.family.verified=false;
  if(familyFault==2)s.family.deadlineNs=now;
  if(familyFault==3)++s.family.binding.weapon.id;
  if(familyFault==4)++s.family.binding.owner.weapon;
  if(familyFault==5)++s.family.binding.owner.equipGeneration;
  if(familyFault==6)s.family.binding.inventory=0;
  if(familyFault==7)s.family.binding.launcher=s.nativeOwner.weapon;
  if(familyFault==8)s.family.binding.launcherSlot=9;
  if(familyFault==9)++s.family.binding.weapon.generation;
  if(familyFault==10)++s.family.binding.owner.soldier;
  if(familyFault==11)++s.family.binding.inventory;
  reserve.sequence=100000+(++seq);reserve.observedNs=now;reserve.deadlineNs=now+100*Ms;
  meshes->sequence=seq;meshes->observedNs=now;meshes->deadlineNs=now+200*Ms;
  hands.Update(s.input);HandContactProof proof{{1002,1},s.input.sequence,s.input.deadlineNs,true};
  if(gun)gun=hands.Renew(s.input,gun->token,proof).claim;
  if(!gun)gun=hands.Acquire(s.input,{s.input.owner,InteractionHand::Right,HandClaimKind::GunHold,s.weapon,proof,++intent,0}).claim;
  result=policy->Tick(s,hands,intent);
 }
 void Send(bool grip=false,bool eject=false,float z=-100.f,bool previous=false){const auto old=s.input;
  if(z==-100.f)z=profile->geometry->interaction.insertion.travelMeters;
  now+=20*Ms;++s.input.sequence;s.input.observedNs=s.input.nowNs=now;s.input.deadlineNs=now+100*Ms;
  s.input.released[0]=!grip;s.gripPressed=grip;s.ejectPressed=eject;s.geometrySequence=s.input.sequence;
  s.raw.inputEvidence=previous?old:s.input;s.originalHandEvidence=s.raw.inputEvidence;Geometry(z);Sync();}
 bool Eject(){Send();Send(false,true);if(starts!=1)return false;held=true;Send(false,true);return result.interaction.phase==DetachableMagazinePhase::WellEmpty;}
 bool Insert(bool previous=false){if(!Eject())return false;Send();s.bodyFromHand=Pose();Send(true,false,-.1f,previous);
  for(float z:{-.075f,-.04f,0.f,.025f,.05f,.075f,.1f,.12f,.14f,.14f,.14f,.14f,.14f,.14f}){Send(true,false,z,previous);if(submits)return true;}return false;}
 void Complete(){const auto& r=*submitted;const auto units=int(r.reservedUnits);const int beforeLoaded=reserve.loaded,beforeReserve=reserve.reserve;
  reserve.loaded+=units;reserve.reserve-=units;reserve.reloadInputReady=false;
  ack=ReloadMagazineAckEvidence{{{r.request.id,r.request.owner,r.request.operation,ReloadAcknowledgement::Applied},reserve.identity,cycle,
   seq+1,700,beforeLoaded,beforeReserve,reserve.loaded,reserve.reserve},now,now+100*Ms,true};}
 std::string Report(){std::ostringstream o;policy->Report(o);return o.str();}
};
} // namespace magazine_consumer_fixture
