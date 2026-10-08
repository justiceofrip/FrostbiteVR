#define Fixture ModeFixture
#define main ExistingModeTests
#include "WeaponModeTests.cpp"
#undef main
#undef Fixture
#define Fixture AuthoredFixture
#define main ExistingAuthoredTests
#include "Bc2AuthoredSightTests.cpp"
#undef main
#undef Fixture
#include "Bc2SightAdapter.h"
namespace {
int ExactFamilyAndGate(){
 ModeFixture f;const auto& p=WeaponModeFamilies[1];
 f.Text(0x16000,"AEK971_sp");f.Text(0x16200,"Objects/Weapons/Handheld/RU_rgl_AEK971/SP_rgl_AEK971");
 f.Text(0x16300,"Objects/Weapons/Handheld/RU_rgl_AEK971/SP_rgl_GP30");f.Text(0x16400,"fixture_owned_aek");
 const auto original=f.bytes;auto mode=ObserveWeaponModeFamily(f.Memory(),f.soldier,f.rifle,p);
 CHECK(mode&&mode->family==&p&&mode->persistent==f.persistent&&!mode->selectedSecondary&&mode->action==33&&f.bytes==original);
 CHECK(!ResolveWeaponMode(f.Memory(),f.soldier,f.rifle)); // Offline evidence is not dispatch admission.
 f.Word(f.inventory+0x14c,3);mode=ObserveWeaponModeFamily(f.Memory(),f.soldier,f.launcher,p);
 CHECK(mode&&mode->selectedSecondary&&mode->action==36&&mode->targetWeapon==f.rifle);
 f.Text(0x16300,"Objects/Weapons/Handheld/Other/SP_rgl_Smoke");CHECK(!ObserveWeaponModeFamily(f.Memory(),f.soldier,f.launcher,p));
 CHECK(SightAdapterForFamily(&p)&&!SightAdapterForFamily(&p)->nativeSightAccepted);
 CHECK(!SightAdapterForFamily(nullptr));return 0;
}
struct AssemblyFixture:AuthoredFixture {
 AuthoredSightGeometry geometry;SightAdapterProfile adapter;
 AssemblyFixture(){
  names[0]="jntWpn_1";names[1]="jntWpnwpnJnt_16";names[2]="jntWpn_17";names[3]="jntWpn_18";
  geometry=Gp30SightGeometry;geometry.rigFingerprint=SightRigFingerprint(names,parents,inverse);
  adapter=SightAdapterProfiles[1];adapter.authored=&geometry;
 }
 AuthoredSightObservation Observation(){auto s=Sample();s.asset=geometry.primaryAsset;s.configuration=geometry.primaryConfiguration;
  s.selectedMeshPath=geometry.meshPath;s.rigFingerprint=geometry.rigFingerprint;return s;}
};
int ActualAssemblyConsumer(){
 AssemblyFixture f;const auto c=MeasureSightAdapterContact(f.adapter,f.Observation());CHECK(c.valid&&c.previewValid);
 auto pending=f.Observation();pending.attachmentReady=false;
 CHECK(!MeasureSightAdapterContact(f.adapter,pending).valid);
 CHECK(ReadSightAdapterNativeFrame(f.adapter,pending));
 pending.configuration="foreign/shared/launcher";CHECK(!ReadSightAdapterNativeFrame(f.adapter,pending));
 auto grasp=SightGraspBinding::Begin(c.pivotMeters,c.axis,c.sightLocalMeters,c.handLocalFrameMeters,c.graspPointMeters,c.palmPointWristMeters,f.adapter.travelRadians);CHECK(grasp);
 const auto opened=grasp->Evaluate(1.f,SightMode::Primary);CHECK(opened&&Near(opened->appliedRadians,.7179032976f));
 const auto original=f.native;const auto edits=BuildSightAssemblyWrites(f.adapter,f.names,f.parents,f.native,f.hidden,opened->sight);CHECK(edits&&edits->size()==2);
 CHECK((*edits)[0].index==2&&(*edits)[1].index==3);
 for(unsigned n=0;n<f.native.size();++n)CHECK(f.native[n].values==original[n].values);
 const auto localBefore=Multiply(f.native[3],*InverseAnimatedTransform(f.native[2]));
 const auto localAfter=Multiply((*edits)[1].transform,*InverseAnimatedTransform((*edits)[0].transform));
 for(unsigned r=0;r<4;++r)for(unsigned col=0;col<4;++col)CHECK(Near(localBefore.values[r][col],localAfter.values[r][col]));
 f.hidden={3};CHECK(!BuildSightAssemblyWrites(f.adapter,f.names,f.parents,f.native,f.hidden,opened->sight));
 f.hidden.clear();f.parents[2]=0;CHECK(!BuildSightAssemblyWrites(f.adapter,f.names,f.parents,f.native,f.hidden,opened->sight));
 return 0;
}
int InterruptionAndHandHandoff(){
 for(unsigned interrupt=0;interrupt<4;++interrupt){
  AssemblyFixture f;const auto c=MeasureSightAdapterContact(f.adapter,f.Observation());CHECK(c.valid);
  SightFlipConfig config;config.pivotMeters=c.pivotMeters;config.axis=c.axis;
  config.grabRadiusMeters=.08f;config.holdRadiusMeters=.2f;config.minLeverMeters=.015f;
  config.thresholdRadians=.4f;config.hysteresisRadians=.08f;config.maxStepRadians=.7f;
  config.detentHoldNs=70000000;config.gestureTimeoutNs=2500000000;config.ackTimeoutNs=1500000000;config.maxSampleGapNs=250000000;
  SightFlip policy(config);SightFlipSample s;s.owner={1,2,3,4};s.sequence=1;s.nowNs=100000000;
  s.focused=s.tracked=s.contactValid=s.nativeModeValid=true;s.handLocalMeters=c.handLocalMeters;
  policy.Update(s);++s.sequence;s.nowNs+=10000000;s.squeeze=1;const auto acquired=policy.Update(s);CHECK(acquired.grabbed);
  // Mechanism pose is independent of the ordinary support-grip pose.
  const auto binding=SightGraspBinding::Begin(c.pivotMeters,c.axis,c.sightLocalMeters,c.handLocalFrameMeters,c.graspPointMeters,c.palmPointWristMeters,f.adapter.travelRadians);CHECK(binding);
  const auto posed=binding->Evaluate(.3f,SightMode::Primary);CHECK(posed);
  const auto hand=f.Hand();const auto fingers=GenerateHandPose(f.parents,hand.referenceWorld,f.native,hand.pose,posed->wrist,LeftHandTargets(HandPoseRole::MechanismGrip,0,0));CHECK(fingers&&fingers->writes.size()==16);
  ++s.sequence;s.nowNs+=10000000;
  if(interrupt==0)s.focused=false;
  if(interrupt==1)s.owner.generation++;
  if(interrupt==2)s.owner.space++;
  if(interrupt==3)s.tracked=false;
  const auto cancelled=policy.Update(s);CHECK(cancelled.cancelled&&cancelled.phase==SightFlipPhase::Idle&&!cancelled.request);
 }
 return 0;
}
}
int main(){CHECK(!ExistingModeTests());CHECK(!ExistingAuthoredTests());CHECK(!ExactFamilyAndGate());CHECK(!ActualAssemblyConsumer());CHECK(!InterruptionAndHandHandoff());
 std::puts("Sight adapter: exact family gate, descendant plan and interrupted hand handoff passed; no native admission");return 0;}
