#include "Bc2MagazineConsumerFixture.h"
#include "Bc2SightContact.h"
#include "Bc2MagazineAssembly.h"
#include "Bc2WeaponVisibility.h"
#include <algorithm>
#include <bit>
using namespace magazine_consumer_fixture;
namespace {
const MagazineNativeProfile& SecondNative(){static constexpr std::array<ReloadTimingWord,2> timing{{
 {0x10,std::bit_cast<std::uint32_t>(.6f)},{0x18,std::bit_cast<std::uint32_t>(3.6f)}}};
 static const MagazineNativeProfile p{{"fixture_rifle","fixture/native/rifle",
  {2,1,8,29,40,5,0,3.6f,.6f,0,0,0,false,false},timing,ReloadDescriptorAdmission::ReviewedNative},
  MagazineIdentityRoute::SelectedCarriedItem,MagazineCycleAdmission::ReviewedReload11Transfer12,4500000000ll};return p;}
MagazineGeometryProfile SecondGeometry(){auto g=Xm8MagazineGeometry();g.asset="fixture_rifle";g.mesh="fixture/mesh/rifle";
 g.meshKind=SelectedMeshKind::Unknown;g.configurationPath=SecondNative().configuration.assetPath;
 g.bones.weapon="FixtureRoot";g.bones.magazine="FixtureMagazine";
 auto& p=g.interaction.insertion;p.itemFromHand=p.itemFromInsertion=p.weaponFromEntry=Identity();p.travelMeters=.14f;
 g.attachedItem=Multiply(TravelPose(p,p.travelMeters),p.weaponFromEntry);
 for(auto& f:g.wristFromFinger)f=Pose(.01f,.02f,.03f);return g;
}
RigSnapshot Rig(const MagazineGeometryProfile& p){RigSnapshot r;
 r.names={"root",std::string(p.bones.weapon),std::string(p.bones.wrist),"untouched",std::string(p.bones.magazine)};
 r.parents={-1,0,0,0,1};r.weaponBone=1;
 for(unsigned n=0;n<15;++n){r.names.emplace_back(p.bones.fingers[n]);r.parents.push_back(n%3?int(r.names.size()-2):2);}
 r.inverseBind.assign(r.names.size(),Identity());r.evaluatedWorld=r.inverseBind;r.nativeEvaluated.resize(r.names.size());
 for(auto& bytes:r.nativeEvaluated)bytes.fill(std::byte{0x71});return r;
}
RigSnapshot AssemblyRig(const MagazineGeometryProfile& p){auto r=Rig(p);
 for(unsigned n=0;n<p.assemblyCount;++n){const auto& m=p.assembly[n];
  const auto parent=std::find(r.names.begin(),r.names.end(),m.parent);
  r.parents.push_back(int(parent-r.names.begin()));r.names.emplace_back(m.bone);
  r.inverseBind.push_back(Identity());r.evaluatedWorld.push_back(Multiply(m.itemFromBone,p.attachedItem));r.nativeEvaluated.emplace_back();r.nativeEvaluated.back().fill(std::byte{0x72});
 }r.evaluatedWorld[4]=p.attachedItem;return r;
}
const MagazineGeometryProfile& AssemblyGeometry(){static const auto p=[] {
 auto g=SecondGeometry();g.assemblyCount=3;
 g.assembly[0]={"Follower","FixtureMagazine",Pose(0,.02f,0)};
 g.assembly[1]={"UnweightedJoint","Follower",Pose(.01f,.03f,0)};
 g.assembly[2]={"Tip","UnweightedJoint",Pose(.01f,.04f,0)};
 const auto rig=AssemblyRig(g);g.rigFingerprint=SightRigFingerprint(rig.names,rig.parents,rig.inverseBind);return g;
 }();return p;}
const MagazineEquipmentProfile& AssemblyEquipment(){static const MagazineEquipmentProfile p{
 NativeMagazineProfileId::ScopedXm8,&SecondNative(),&AssemblyGeometry(),true};return p;}
struct AssemblyFixture:Fixture {AssemblyFixture():Fixture(true,AssemblyEquipment()){reserve.loaded=33;reserve.capacity=40;reserve.reserve=97;}};
int StructuralClosureAndMalformedData(){const auto& profile=AssemblyGeometry();auto rig=AssemblyRig(profile);
 auto b=magazine_presentation_detail::Derive(rig,profile);CHECK(b&&b->assemblyCount==3&&b->assembly[0]==20&&b->assembly[2]==22);
 for(unsigned bad=0;bad<8;++bad){auto p=profile;auto r=rig;
  if(bad==0)--p.assemblyCount;
  if(bad==1)p.assemblyCount=MagazineAssemblyLimit+1;
  if(bad==2)p.assembly[1].parent="Tip";
  if(bad==3)p.assembly[0].bone=p.bones.wrist;
  if(bad==4)p.assembly[1].bone=p.assembly[0].bone;
  if(bad==5)p.assembly[0].itemFromBone.values[0][0]=2;
  if(bad==6)r.parents[21]=22;
  if(bad==7)r.parents[20]=1;
  CHECK(!magazine_presentation_detail::Derive(r,p));
 }
 auto leaf=profile;leaf.assemblyCount=0;CHECK(!magazine_presentation_detail::Derive(rig,leaf));
 CHECK(!BindMagazinePresentation(rig,profile.asset));return 0;
}
int CompletePoseAndHidePreserveUnrelatedBytes(){AssemblyFixture f;CHECK(f.Insert());auto rig=AssemblyRig(AssemblyGeometry());
 rig.identity.soldier=f.s.nativeOwner.soldier;rig.identity.weak=f.s.nativeOwner.weak;
 const auto before=rig.nativeEvaluated;const auto binding=magazine_presentation_detail::Derive(rig,AssemblyGeometry());CHECK(binding);
 for(const float units:{1.f,100.f}){auto scaled=rig;for(auto& m:scaled.evaluatedWorld)for(unsigned k=0;k<3;++k)m.values[3][k]*=units;
  const auto plan=BuildMagazinePresentation(scaled,*binding,f.result.tracking,Pose(3,4,5),units,f.now);
  CHECK(plan.binding&&plan.writes.size()==4&&!plan.wristTarget);
  for(unsigned n=0;n<3;++n){auto local=AssemblyGeometry().assembly[n].itemFromBone;for(unsigned k=0;k<3;++k)local.values[3][k]*=units;
   CHECK(plan.writes[n+1].index==binding->assembly[n]&&Same(plan.writes[n+1].transform,Multiply(local,plan.writes[0].transform)));
  }
 }
 auto hidden=HideMagazinePackedPalette(rig,*binding,rig.nativeEvaluated);CHECK(hidden&&hidden->size()==rig.names.size());
 for(unsigned n=0;n<rig.names.size();++n){const bool member=n==4||n>=20;CHECK(((*hidden)[n]!=rig.nativeEvaluated[n])==member);}
 CHECK(rig.nativeEvaluated==before);
 auto wrong=*binding;wrong.assembly[0]=3;CHECK(!HideMagazinePackedPalette(rig,wrong,rig.nativeEvaluated));return 0;
}
int NativeChildMotionAndVisibilityCannotBeOverwritten(){AssemblyFixture f;CHECK(f.Insert());auto rig=AssemblyRig(AssemblyGeometry());
 rig.identity.soldier=f.s.nativeOwner.soldier;rig.identity.weak=f.s.nativeOwner.weak;
 const auto binding=magazine_presentation_detail::Derive(rig,AssemblyGeometry());CHECK(binding);
 CHECK(BuildMagazineRawContact(f.result.tracking,rig,f.s.asset,Pose(),Pose(),1,f.now).valid);
 for(unsigned bad=0;bad<4;++bad){auto changed=rig;
  if(bad==0)changed.evaluatedWorld[20].values[3][1]+=.002f;
  if(bad==1)changed.nativeHiddenLeaves={22};
  if(bad==2)changed.evaluatedWorld[21].values[0][0]=2;
  if(bad==3)changed.nativeHiddenLeaves={4};
  CHECK(!BuildMagazineRawContact(f.result.tracking,changed,f.s.asset,Pose(),Pose(),1,f.now).valid);
  CHECK(!BuildMagazinePresentation(changed,*binding,f.result.tracking,Pose(),1,f.now).binding);
 }return 0;
}
int HeldAssemblyUsesExactCarryFrameAndKeepsAuthoredFingers(){AssemblyFixture f;CHECK(f.Eject());f.Send();f.s.bodyFromHand=Pose();f.Send(true,false,-.1f);
 CHECK(f.result.tracking.target&&f.result.tracking.target->role==MagazinePropRole::Replacement);
 auto rig=AssemblyRig(AssemblyGeometry());rig.identity.soldier=f.s.nativeOwner.soldier;rig.identity.weak=f.s.nativeOwner.weak;
 const auto b=magazine_presentation_detail::Derive(rig,AssemblyGeometry());CHECK(b);
 const auto plan=BuildMagazinePresentation(rig,*b,f.result.tracking,Pose(2,3,4),1,f.now);
 CHECK(plan.binding&&plan.writes.size()==20&&plan.wristTarget);
 CHECK(plan.writes[4].index==b->wrist&&Same(plan.writes[4].transform,*plan.wristTarget));
 for(unsigned n=0;n<15;++n)CHECK(plan.writes[5+n].index==b->fingers[n]&&Same(plan.writes[5+n].transform,Multiply(AssemblyGeometry().wristFromFinger[n],*plan.wristTarget)));
 for(unsigned n=0;n<3;++n)CHECK(Same(plan.writes[n+1].transform,Multiply(AssemblyGeometry().assembly[n].itemFromBone,plan.writes[0].transform)));
 return 0;
}
}
int main(){if(StructuralClosureAndMalformedData()||CompletePoseAndHidePreserveUnrelatedBytes()||NativeChildMotionAndVisibilityCannotBeOverwritten()||HeldAssemblyUsesExactCarryFrameAndKeepsAuthoredFingers())return 1;
 std::puts("4 rigid magazine assembly groups using real consumer fixture passed; no registry admission");}
