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
#include <string_view>
namespace {
int MeasuredUnscopedVariant(){
 const auto& profile=WeaponModeFamilies[2];const auto* adapter=SightAdapterForFamily(&profile);
 CHECK(profile.primaryAsset=="XM8_sp"&&profile.persistentId=="sp_xm8");
 CHECK(!profile.nativeAccepted&&profile.linkedVariantEligible&&WeaponModeFamilyEligible(profile));
 CHECK(adapter&&!adapter->nativeSightAccepted&&adapter->sharedGeometryEligible&&SightAdapterEnabled(*adapter));
 CHECK(adapter->opposed==SightAdapterProfiles[0].opposed); // Measured bytes, not an asset-name alias.
 ModeFixture f;f.Text(0x16000,profile.primaryAsset.data());f.Text(0x16200,profile.primaryConfiguration.data());
 f.Text(0x16300,profile.secondaryConfiguration.data());f.Text(0x16400,profile.persistentId.data());
 const auto original=f.bytes;
 auto mode=ResolveWeaponMode(f.Memory(),f.soldier,f.rifle);
 CHECK(mode&&mode->family==&profile&&!mode->selectedSecondary&&mode->action==33&&mode->targetWeapon==f.launcher&&f.bytes==original);
 f.Word(f.inventory+0x14c,3);mode=ResolveWeaponMode(f.Memory(),f.soldier,f.launcher);
 CHECK(mode&&mode->family==&profile&&mode->selectedSecondary&&mode->action==36&&mode->targetWeapon==f.rifle);
 // The shared launcher name and exact geometry cannot invent native linkage.
 f.Word(f.launcherData+0x64,f.persistent+4);f.types[f.persistent+4]="PersistentWeapon";
 CHECK(!ResolveWeaponMode(f.Memory(),f.soldier,f.launcher));f.Word(f.launcherData+0x64,f.persistent);
 f.Text(0x16400,"40mmgl");CHECK(!ResolveWeaponMode(f.Memory(),f.soldier,f.launcher));f.Text(0x16400,"sp_xm8");
 f.Text(0x16300,WeaponModeFamilies[0].secondaryConfiguration.data());CHECK(!ResolveWeaponMode(f.Memory(),f.soldier,f.launcher));
 f.Text(0x16300,profile.secondaryConfiguration.data());f.Word(f.map+24+16,33);
 CHECK(!ResolveWeaponMode(f.Memory(),f.soldier,f.launcher));f.Word(f.map+24+16,36);
 f.Word(f.inventory+0x14c,1);f.changeCurrent=true;CHECK(!ResolveWeaponMode(f.Memory(),f.soldier,f.rifle));
 WeaponSightObservation s;s.assetName=profile.primaryAsset;s.rootName=s.sightParentName="jntWpn_1";s.sightName="jntWpn_9";
 s.skeletonFingerprint=adapter->opposed->skeletonFingerprint;s.generation=12;s.deadline=34;
 s.attachmentReady=s.palmValid=s.frontValid=true;
 for(auto m:{&s.nativeWeapon,&s.nativeSight,&s.nativeFront,&s.placedWeapon,&s.rawLeftTarget})for(unsigned n=0;n<4;++n)m->values[n][n]=1;
 s.nativeSight.values[3]={.1f,.2f,.3f,1};s.rawLeftTarget.values[3]={.1f,.2f,.245f,1};
 CHECK(!MeasureWeaponSightContact(s).valid); // Standalone scoped admission is unchanged.
 const auto contact=MeasureOpposedSightAdapterContact(*adapter,s,profile.primaryConfiguration);
 CHECK(contact.valid&&contact.generation==12&&contact.deadline==34&&contact.contactDistanceMeters<.0001f);
 CHECK(!MeasureOpposedSightAdapterContact(*adapter,s,WeaponModeFamilies[0].primaryConfiguration).valid);
 s.assetName=profile.secondaryAsset;CHECK(MeasureOpposedSightAdapterContact(*adapter,s,profile.secondaryConfiguration).valid);
 CHECK(!MeasureOpposedSightAdapterContact(*adapter,s,WeaponModeFamilies[0].secondaryConfiguration).valid);
 s.attachmentReady=false;CHECK(!MeasureOpposedSightAdapterContact(*adapter,s,profile.secondaryConfiguration).valid);s.attachmentReady=true;
 ++s.skeletonFingerprint;CHECK(!MeasureOpposedSightAdapterContact(*adapter,s,profile.secondaryConfiguration).valid);--s.skeletonFingerprint;
 s.hidden=true;CHECK(!MeasureOpposedSightAdapterContact(*adapter,s,profile.secondaryConfiguration).valid);s.hidden=false;
 s.sightParentName="foreign";CHECK(!MeasureOpposedSightAdapterContact(*adapter,s,profile.secondaryConfiguration).valid);
 return 0;
}
int CompleteOpposedGraspRequired(){
 // A raw rear-ladder measurement is useful by itself, but the interaction
 // adapter must not claim the hand for an assembly it cannot pose.
 for(unsigned familyIndex:{0u,2u})for(bool secondary:{false,true}){
  const auto& adapter=SightAdapterProfiles[familyIndex];const auto& family=*adapter.family;
  WeaponSightObservation s;s.assetName=secondary?family.secondaryAsset:family.primaryAsset;
  const auto configuration=secondary?family.secondaryConfiguration:family.primaryConfiguration;
  s.rootName=s.sightParentName=adapter.root;s.sightName=adapter.rear;
  s.skeletonFingerprint=adapter.opposed->skeletonFingerprint;s.generation=12;s.deadline=34;
  s.attachmentReady=s.palmValid=s.frontValid=s.visualHandValid=true;
  s.nativeWeapon=s.placedWeapon=At();s.nativeSight=At(.1f,.2f,.3f);s.nativeFront=At(.1f,.2f,.5f);
  s.rawLeftTarget=s.visualLeftTarget=At(.1f,.2f,.245f);
  const auto complete=MeasureOpposedSightAdapterContact(adapter,s,configuration);
  CHECK(complete.valid&&complete.previewValid);
  CHECK(SightGraspBinding::Begin(complete.pivotMeters,complete.axis,complete.sightLocalMeters,
      complete.handLocalFrameMeters,complete.graspPointMeters,complete.palmPointWristMeters,adapter.travelRadians));
  for(unsigned missing=0;missing<7;++missing){
   auto incomplete=s;
   if(missing==0)incomplete.frontValid=false; // Missing, hidden or non-leaf front.
   if(missing==1)incomplete.nativeFront={};
   if(missing==2)incomplete.visualPointWristMeters.x=std::numeric_limits<float>::quiet_NaN();
   if(missing==3)incomplete.nativeSight.values[1][1]=-1;
   if(missing==4)incomplete.visualLeftTarget.values[0][0]=-1;
   if(missing==5)incomplete.nativeFront.values[0][0]=-1;
   if(missing==6)incomplete.rawLeftTarget.values[0][0]=-1;
   // Preserve the standalone reader's measurement-only contract. Before this
   // fix the adapter forwarded all seven as valid interaction evidence.
   CHECK(MeasureWeaponSightContact(incomplete,*adapter.opposed).valid);
   const auto rejected=MeasureOpposedSightAdapterContact(adapter,incomplete,configuration);
   CHECK(!rejected.valid&&!rejected.previewValid&&!rejected.generation&&!rejected.deadline);
  }
  // A complete recovery uses the exact current publication again.
  ++s.generation;++s.deadline;
  const auto recovered=MeasureOpposedSightAdapterContact(adapter,s,configuration);
  CHECK(recovered.valid&&recovered.previewValid&&recovered.generation==s.generation&&recovered.deadline==s.deadline);
 }
 CHECK(!SightAdapterEnabled(SightAdapterProfiles[1])); // GP30 geometry grants no admission.
 return 0;
}
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
int main(int argc,char** argv){
 if(argc==2&&std::string_view(argv[1])=="--variant-only")return MeasuredUnscopedVariant();
 CHECK(!MeasuredUnscopedVariant());CHECK(!CompleteOpposedGraspRequired());CHECK(!ExistingModeTests());CHECK(!ExistingAuthoredTests());CHECK(!ExactFamilyAndGate());CHECK(!ActualAssemblyConsumer());CHECK(!InterruptionAndHandHandoff());
 std::puts("Sight adapter: exact family gate, descendant plan and interrupted hand handoff passed; no native admission");return 0;}
