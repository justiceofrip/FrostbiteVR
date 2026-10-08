#include "Bc2MagazineConsumerFixture.h"
#include "Bc2SelectedCarriedFixture.h"
#include "Bc2MagazineNativeAdapter.h"
#include "Bc2MagazineNativeFixture.h"
#include <cstdio>
using namespace magazine_consumer_fixture;
namespace {
struct Direct {
 fvr::test::Fixture native;
 Fixture f{true,Xm8MagazineEquipment(),true};
 static constexpr unsigned name=0x31000,persistence=0x32000;
 Direct(){
  native.Word(native.ad+0xc,name);native.Text(name,"XM8_sp_s");native.Word(native.ad+0x64,persistence);
  f.s.nativeOwner=native.owner;f.reserve.identity.owner=native.owner;f.s.raw.owner=native.owner;f.meshes->owner=native.owner;
  f.s.input.owner={(std::uint64_t(native.owner.weak)<<32)|native.owner.soldier,native.owner.actorGeneration,17,native.owner.space};
  f.s.weapon={native.owner.weapon,17};f.s.trackingEpoch=native.owner.space;
  f.equipment={native.owner.weapon,native.ad,persistence,{}};std::memcpy(f.equipment.asset.data(),"XM8_sp_s",9);
  f.meshes->weaponData=native.ad;f.s.family={};f.familyMemory=native.Memory();f.reserve.loaded=21;f.reserve.reserve=210;
 }
};
int DirectItemReaderPublishesOrdinaryContact(){
 Direct d;auto& f=d.f;const auto bytes=d.native.bytes;f.Send();
 CHECK(f.result.tracking.enabled&&f.result.tracking.family.carried);
 CHECK(f.result.tracking.family.binding.weapon.id==f.s.nativeOwner.weapon);
 CHECK(f.result.tracking.family.binding.launcher==0&&f.result.tracking.family.binding.launcherSlot==0);
 CHECK(f.result.tracking.family.binding.equipment==f.equipment);
 CHECK(MagazineTrackingFresh(f.result.tracking,f.now)&&d.native.bytes==bytes);
 CHECK(!ResolveWeaponMode(d.native.Memory(),d.native.owner.soldier,d.native.owner.weapon));
 CHECK(f.result.tracking.family.deadlineNs==f.s.input.deadlineNs);
 CHECK(!MagazineTrackingFresh(f.result.tracking,f.result.tracking.family.deadlineNs));
 auto native=fvr::bc2::test::Input();native.identity=f.reserve.identity;native.config.weaponData=d.native.ad;
 native.nowNs=f.now;native.leaseDeadlineNs=f.s.input.deadlineNs;
 const auto assess=[&](const MagazineNativeIdentityEvidence& evidence,const WeaponEquipmentIdentity& equipment){
  return AssessMagazineNativeAdapter(Xm8MagazineNativeProfile,evidence,native,f.s.input.owner,f.s.weapon,equipment,f.now);};
 CHECK(assess(f.result.tracking.family,f.equipment).ConfigurationReady());
 CHECK(assess(*f.result.tracking.family.carried,f.equipment).ConfigurationReady());
 auto changed=f.equipment;++changed.persistence;CHECK(!assess(f.result.tracking.family,changed).ConfigurationReady());
 auto hybrid=f.result.tracking.family;hybrid.binding.launcher=0xc0000;
 CHECK(!assess(hybrid,f.equipment).ConfigurationReady());
 native.config.reloadTime=3.2f;CHECK(!assess(f.result.tracking.family,f.equipment).ConfigurationReady());
 return 0;
}
int ActualReplacementUsesExactDirectOwnerAndCost(){
 Direct d;auto& f=d.f;CHECK(f.Insert());CHECK(f.starts==1&&f.submits==1&&f.submitted);
 CHECK(f.submitted->reservedUnits==9&&f.reserve.loaded==21&&f.reserve.reserve==210);
 CHECK(f.result.tracking.family.carried&&f.result.tracking.family.binding.weapon.id==d.native.owner.weapon);
 f.Complete();f.Send();CHECK(f.policy->ProbeState(f.now).completed==1&&f.reserve.loaded==30&&f.reserve.reserve==201);
 f.allowRetire=true;f.Send();f.Send();CHECK(!f.policy->BlocksEquipment());return 0;
}
bool OriginalSeat(Fixture& f){
 f.Send();f.Send(true);if(f.starts!=1)return false;f.held=true;f.reserve.reloadInputReady=false;
 for(float z:{.075f,.05f,.025f,0.f,-.025f,-.05f,-.05f,-.025f,0.f,.025f,.05f,.075f,.1f}){
  f.Send(true,false,z);if(f.result.interaction.originalSeat)return true;
 }
 for(unsigned n=0;n<12;++n){f.Send(true,false,.1f);if(f.result.interaction.originalSeat)return true;}
 return false;
}
int OriginalReturnDoesNotRefillDirectItem(){
 Direct d;auto& f=d.f;CHECK(OriginalSeat(f));CHECK(f.submits==0&&f.result.interaction.original->rounds==21);
 f.allowRetire=true;f.Send();f.reserve.reloadInputReady=true;f.Send();f.Send();
 CHECK(!f.policy->BlocksEquipment()&&f.policy->ProbeState(f.now).originalReturns==1);
 CHECK(f.reserve.loaded==21&&f.reserve.reserve==210);return 0;
}
int DirectEvidenceRejectsHybridStaleAndWrongAsset(){
 Direct d;auto& f=d.f;f.Send();CHECK(f.result.tracking.family.carried);const auto saved=f.result.tracking;
 for(unsigned mode=0;mode<8;++mode){auto t=saved;
  if(mode==0)t.family.carried.reset();
  if(mode==1)t.family.binding.launcher=0xc0000;
  if(mode==2)t.family.binding.launcherSlot=3;
  if(mode==3)++t.family.binding.equipment.persistence;
  if(mode==4)++t.family.carried->equipment.data;
  if(mode==5)++t.family.binding.weapon.id;
  if(mode==6)++t.family.carried->physical.space;
  if(mode==7)t.family.deadlineNs=f.now;
  CHECK(!MagazineTrackingFresh(t,f.now));
 }
 ++f.meshes->weaponData;CHECK(!MagazineTrackingFresh(saved,f.now));--f.meshes->weaponData;
 d.native.Text(d.name,"OtherNativeAsset");f.Send();CHECK(!f.result.tracking.enabled&&f.starts==0);
 return 0;
}
int NativeReaderRaceCannotBecomeDirectFallback(){
 for(unsigned mode=0;mode<4;++mode){Direct d;auto& f=d.f;
  if(mode==0)d.native.mutate=true;
  if(mode==1)d.native.denyMap=true;
  if(mode==2)f.s.weapon.id=d.persistence; // An unequal key must prove the alias, never borrow ordinary evidence.
  if(mode==3)d.native.Word(d.native.inventory+0x14c,1);
  f.Send();f.Send(true);CHECK(!f.result.tracking.enabled&&f.starts==0&&!f.policy->BlocksEquipment());
 }return 0;
}
int ChangedDirectEquipmentCancelsWithoutSubmission(){
 for(unsigned mode=0;mode<3;++mode){Direct d;auto& f=d.f;CHECK(f.Eject());
  const auto old=f.result.tracking;
  if(mode==0){d.native.Word(d.native.ad+0x64,d.persistence+4);f.equipment.persistence+=4;}
  if(mode==1){++d.native.owner.equipGeneration;f.s.nativeOwner=d.native.owner;f.reserve.identity.owner=d.native.owner;f.meshes->owner=d.native.owner;f.s.raw.owner=d.native.owner;}
  if(mode==2){f.familyMemory.reset();f.directIdentity=false;f.s.weapon.id=0xb0000;}
  f.Send();CHECK(f.cancels==1&&f.submits==0&&f.policy->BlocksEquipment());
  CHECK(!MagazineTargetRetained(old,f.result.tracking,f.now));CHECK(f.reserve.loaded==21&&f.reserve.reserve==210);
 }return 0;
}
int FullDetachDirectSourceKeepsEquipmentGuard(){
 Direct d;auto& f=d.f;f.reserve.loaded=30;f.reserve.allThreeIdle=true;f.Send();
 Bc2MagazineDetachGate gate;MagazineDetachGateSample sample{f.s.family,f.reserve,f.s.input,*f.gun,f.meshes,1,0x310000,true};
 auto wrong=std::make_shared<SelectedMeshesSnapshot>(*f.meshes);++wrong->weaponData;sample.selected=wrong;
 CHECK(!gate.Begin(sample));sample.selected=f.meshes;CHECK(gate.Begin(sample));
 CHECK(gate.Original()->rounds==30&&f.starts==0&&f.submits==0);return 0;
}
}
int main(){for(auto test:{DirectItemReaderPublishesOrdinaryContact,ActualReplacementUsesExactDirectOwnerAndCost,
 OriginalReturnDoesNotRefillDirectItem,DirectEvidenceRejectsHybridStaleAndWrongAsset,NativeReaderRaceCannotBecomeDirectFallback,
 ChangedDirectEquipmentCancelsWithoutSubmission,FullDetachDirectSourceKeepsEquipmentGuard})if(test())return 1;
 std::puts("7 magazine selected-item route groups passed through actual readers and physical consumer; no native runtime claim");}
