#include "Bc2MagazineConsumerFixture.h"
#include <cstdio>
#include <limits>
using namespace magazine_consumer_fixture;
namespace {
MagazineGeometryProfile Reference(){auto p=Xm8MagazineGeometry();
 p.asset=AekMagazineNativeProfile.configuration.assetName;p.mesh="synthetic/exact-mesh";p.meshKind=SelectedMeshKind::Unknown;
 p.interaction.insertion.id=12345;p.interaction.removalContact={12346,1};return p;}
int RegistryAndMalformedData(){auto valid=Reference();
 CHECK(SelectExperimentalMagazineGeometry({&valid,1},valid.asset)==&valid);
 CHECK(FindMagazineGeometry(Xm8MagazineAsset)==&Xm8MagazineGeometry());
 for(const auto asset:{"","AEK971","aek971_sp","AEK971_sp_extra","unregistered","40mmgl"})
  CHECK(!SelectExperimentalMagazineGeometry({&valid,1},asset));
 auto unknown=valid;unknown.asset="unregistered";CHECK(!SelectExperimentalMagazineGeometry({&unknown,1},unknown.asset));
 const std::array duplicated{valid,valid};CHECK(!SelectExperimentalMagazineGeometry(duplicated,valid.asset));
 for(unsigned bad=0;bad<11;++bad){auto wrong=valid;
  if(bad==0)wrong.rigFingerprint=0;
  if(bad==1)wrong.mesh={};
  if(bad==2)wrong.interaction.insertion.travelMeters=0;
  if(bad==3)wrong.bones.fingers[1]=wrong.bones.fingers[0];
  if(bad==4)wrong.attachedItem.values[3][0]+=.02f;
  if(bad==5)wrong.wristFromFinger[4].values[0][0]=std::numeric_limits<float>::quiet_NaN();
  if(bad==6)wrong.bones.magazine={};
  if(bad==7)wrong.interaction.pullMeters=0;
  if(bad==8)wrong.attachedItem.values[0][0]=-2;
  if(bad==9){wrong.wristFromFinger[0]=Identity();wrong.wristFromFinger[0].values[0][0]=-1;}
  if(bad==10)wrong.interaction.insertion.weaponFromEntry.values[3][0]=std::numeric_limits<float>::max();
  CHECK(!SelectExperimentalMagazineGeometry({&wrong,1},wrong.asset));
 }
 std::array<MagazineGeometryProfile,257> oversized{};CHECK(!SelectExperimentalMagazineGeometry(oversized,valid.asset));
 return 0;
}
int StatusAndDefaultSelection(){
 CHECK(FindMagazineEquipment(NativeMagazineProfileId::ScopedXm8)==&Xm8MagazineEquipment());
 CHECK(Xm8MagazineEquipment().Ready()&&Xm8MagazineEquipment().geometryVerified&&!Xm8MagazineEquipment().experimentalGeometry);
 CHECK(!FindMagazineEquipment("unregistered")&&!FindMagazineGeometry("unregistered"));
 for(const auto& registered:RegisteredMagazineNativeProfiles()){
  const auto native=ResolveMagazineNativeProfile(registered.id);
  if(!native){CHECK(!FindMagazineEquipment(registered.id));continue;}
  CHECK(native==registered.profile);
  const auto selected=FindMagazineEquipment(registered.id);CHECK(selected);
  if(registered.id==NativeMagazineProfileId::ScopedXm8)continue;
  CHECK(!selected->geometryVerified&&selected->native==registered.profile&&selected->nativeId==registered.id);
  CHECK(selected->geometry==FindMagazineGeometry(*registered.profile));
  CHECK(selected->experimentalGeometry==bool(selected->geometry)&&selected->Ready()==bool(selected->geometry));
#ifdef FVR_EXPECT_NO_GENERATED_MAGAZINE
  CHECK(!selected->geometry&&!selected->Ready()&&!selected->experimentalGeometry);
#endif
 }
 auto geometry=Reference();MagazineEquipmentProfile p{NativeMagazineProfileId::AuthoredAek,&AekMagazineNativeProfile,&geometry,false};
 CHECK(!p.Ready());p.experimentalGeometry=true;CHECK(p.Ready()&&!p.geometryVerified);
 auto native=AekMagazineNativeProfile;native.cycleAdmission=MagazineCycleAdmission::Candidate;p.native=&native;CHECK(!p.Ready());
 native=AekMagazineNativeProfile;geometry.asset="unregistered";CHECK(!p.Ready());return 0;
}
int ExactConfigurationJoin(){
 auto native=AekMagazineNativeProfile;
 auto legacy=Reference();
 CHECK(!SelectExperimentalMagazineGeometry({&legacy,1},native));
 auto a=legacy;a.configurationPath=native.configuration.assetPath;
 auto b=a;b.configurationPath="Objects/Weapons/OtherVariant";b.interaction.insertion.id++;
 const std::array rows{b,a};
 CHECK(SelectExperimentalMagazineGeometry(rows,native)==&rows[1]);
 native.configuration.assetPath=b.configurationPath;
 CHECK(SelectExperimentalMagazineGeometry(rows,native)==&rows[0]);
 const std::array duplicates{b,b,a};CHECK(!SelectExperimentalMagazineGeometry(duplicates,native));
 MagazineEquipmentProfile p{NativeMagazineProfileId::AuthoredAek,&native,&a,false,true};
 CHECK(!p.Ready());p.geometry=&b;CHECK(p.Ready());
 native.configuration.assetPath="Objects/Weapons/Missing";CHECK(!p.Ready());
 CHECK(!SelectExperimentalMagazineGeometry(rows,native));
 return 0;
}
bool Insert(Fixture& f){if(!f.Eject())return false;f.Send();f.s.bodyFromHand=Pose();
 const auto travel=f.profile->geometry->interaction.insertion.travelMeters;
 f.Send(true,false,-.1f);
 for(float p=-.08f;p<travel;p+=.02f){f.Send(true,false,p);if(f.submits)return true;}
 for(unsigned n=0;n<10;++n){f.Send(true,false,travel);if(f.submits)return true;}
 return false;
}
void RawSend(Fixture& f,bool grip,float travel,const math::Matrix4& offset){
 f.now+=20*Ms;++f.s.input.sequence;f.s.input.observedNs=f.s.input.nowNs=f.now;f.s.input.deadlineNs=f.now+100*Ms;
 f.s.input.released[0]=!grip;f.s.gripPressed=grip;f.s.ejectPressed=false;f.s.geometrySequence=f.s.input.sequence;
 f.s.raw.inputEvidence=f.s.input;f.s.originalHandEvidence=f.s.input;f.Geometry(travel);
 f.s.raw.rawLeftWristWorldMeters=Multiply(offset,f.s.raw.rawLeftWristWorldMeters);f.Sync();
}
RigSnapshot Rig(const MagazineGeometryProfile& p,const ReloadStateOwner& owner){RigSnapshot r;
 r.names={"test-scene",std::string(p.bones.weapon),std::string(p.bones.wrist),std::string(p.bones.magazine)};
 r.parents={-1,0,0,1};r.weaponBone=1;r.identity.soldier=owner.soldier;r.identity.weak=owner.weak;
 for(unsigned n=0;n<15;++n){r.parents.push_back(n%3?int(r.names.size()-1):2);r.names.push_back(std::string(p.bones.fingers[n]));}
 r.inverseBind.assign(r.names.size(),Identity());r.evaluatedWorld=r.inverseBind;return r;
}
bool ReturnOriginal(Fixture& f,const math::Matrix4& offset){const auto& c=f.profile->geometry->interaction;const auto travel=c.insertion.travelMeters;
 RawSend(f,false,travel,offset);RawSend(f,true,travel,offset);if(f.starts!=1)return false;f.held=true;f.reserve.reloadInputReady=false;
 const auto outward=c.pullMeters+.08f;
 for(float distance=.02f;distance<outward;distance+=.02f)RawSend(f,true,travel-distance,offset);
 RawSend(f,true,travel-outward,offset);
 if(!f.result.tracking.target||f.result.tracking.target->role!=MagazinePropRole::Removed)return false;
 const auto& geometry=*f.profile->geometry;auto rig=Rig(geometry,f.s.nativeOwner);
 const auto binding=magazine_presentation_detail::Derive(rig,geometry);if(!binding)return false;
 const auto removed=BuildMagazinePresentation(rig,*binding,f.result.tracking,Pose(),1,f.now);
 if(!removed.wristTarget||!Same(*removed.wristTarget,Multiply(c.insertion.itemFromHand,f.result.tracking.target->weaponFromItemMeters)))return false;
 for(float distance=outward;distance>0;distance-=.02f){RawSend(f,true,travel-distance,offset);if(f.result.interaction.originalSeat)return true;}
 for(unsigned n=0;n<10;++n){RawSend(f,true,travel,offset);if(f.result.interaction.originalSeat)return true;}
 return false;
}
unsigned actualProfiles=0;
int RegisteredGeometryReachesActualConsumer(){for(const auto& registered:RegisteredMagazineNativeProfiles()){
 const auto p=FindMagazineEquipment(registered.id);
 if(!p||!p->experimentalGeometry)continue;
 CHECK(p->Ready()&&!p->geometryVerified&&p->native->identityRoute==MagazineIdentityRoute::SelectedCarriedItem);
 Fixture f(true,*p);CHECK(Insert(f));CHECK(f.starts==1&&f.submits==1&&f.submitted&&f.submitted->reservedUnits==3);
 CHECK(f.result.tracking.family.binding.launcher==0&&f.result.tracking.family.carried);
 CHECK(f.submitted->request.owner.actor==f.s.nativeOwner.soldier&&f.submitted->heldLease.identity==f.reserve.identity);
 CHECK(f.reserve.loaded==27&&f.reserve.reserve==83); // No geometry event writes ammunition.
 f.Complete();f.Send();f.allowRetire=true;f.Send();f.Send();
 CHECK(f.policy->ProbeState(f.now).completed==1&&f.reserve.loaded==30&&f.reserve.reserve==80&&!f.policy->BlocksEquipment());
 auto offset=Pose(.012f,-.005f,.014f);offset.values[0]={.995004165f,0,-.0998334166f,0};offset.values[2]={.0998334166f,0,.995004165f,0};
 for(const auto& grasp:{Identity(),offset}){Fixture original(true,*p);CHECK(ReturnOriginal(original,grasp));
  CHECK(original.result.interaction.originalSeat&&original.submits==0&&original.result.interaction.original->rounds==27);
  CHECK(original.result.tracking.target&&original.result.tracking.target->role==MagazinePropRole::Attached);
  CHECK(Same(original.result.tracking.target->weaponFromItemMeters,p->geometry->attachedItem));
  original.allowRetire=true;original.Send();CHECK(original.policy->BlocksEquipment());
  original.reserve.reloadInputReady=true;original.Send();
  CHECK(original.policy->ProbeState(original.now).originalReturns==1&&!original.policy->BlocksEquipment());
  CHECK(original.reserve.loaded==27&&original.reserve.reserve==83&&original.submits==0);
 }
 Fixture replaced(true,*p);CHECK(replaced.Eject());++replaced.equipment.data;replaced.meshes->weaponData=replaced.equipment.data;
 replaced.Send();CHECK(replaced.cancels==1&&replaced.submits==0&&!replaced.result.tracking.target);
 ++actualProfiles;
 }return 0;}
}
int main(){if(RegistryAndMalformedData()||StatusAndDefaultSelection()||ExactConfigurationJoin()||RegisteredGeometryReachesActualConsumer())return 1;
#ifdef FVR_EXPECT_GENERATED_MAGAZINE
 CHECK(actualProfiles>0);
#endif
#ifdef FVR_EXPECT_NO_GENERATED_MAGAZINE
 CHECK(actualProfiles==0);
#endif
 std::printf("4 experimental magazine registry groups passed; actual generated consumer profiles=%u; native API is mocked\n",actualProfiles);}
