#include "Bc2MagazinePresentation.h"
#include "Bc2Xm8MagazineCalibration.h"
#include "Test.h"
#include <cstring>
#include <cstdio>
using namespace fvr;using namespace fvr::bc2;using namespace fvr::interaction;
using namespace fvr::interaction::reload_insertion_detail;
namespace {
bool Same(const math::Matrix4& a,const math::Matrix4& b){return std::memcmp(&a,&b,sizeof(a))==0;}
RigSnapshot Rig(const MagazineGeometryProfile& p){RigSnapshot r;
 r.names={"scene",std::string(p.bones.weapon),std::string(p.bones.wrist),"unrelated",std::string(p.bones.magazine)};
 r.parents={-1,0,0,0,1};r.weaponBone=1;
 for(unsigned n=0;n<15;++n){r.parents.push_back(n%3?int(r.names.size()-1):2);r.names.push_back(std::string(p.bones.fingers[n]));}
 r.inverseBind.assign(r.names.size(),Identity());r.evaluatedWorld=r.inverseBind;return r;}
MagazineGeometryProfile Second(){auto p=Xm8MagazineGeometry();p.asset="synthetic_smg_unregistered";p.mesh="synthetic/calibration";
 p.rigFingerprint=123;p.bones.weapon="SyntheticRoot";p.bones.magazine="SyntheticMagazine";p.bones.wrist="SyntheticWrist";
 p.bones.fingers={"t0","t1","t2","i0","i1","i2","m0","m1","m2","r0","r1","r2","p0","p1","p2"};
 auto& c=p.interaction;auto& q=c.insertion;q.id=0x53594e5448ull;q.itemFromHand=q.itemFromInsertion=q.weaponFromEntry=Identity();
 q.weaponFromEntry.values[0]={0,0,-1,0};q.weaponFromEntry.values[2]={1,0,0,0};q.weaponFromEntry.values[3]={.2f,-.1f,.3f,1};
 q.travelMeters=.14f;c.pullMeters=.12f;c.maxPullStepMeters=.05f;return p;}
int ExactRegistryDoesNotAdmitOtherWeapons(){CHECK(FindMagazineGeometry(Xm8MagazineAsset)==&Xm8MagazineGeometry());
 for(const auto asset:{"","XM8","XM8_sp","XM8_sp_s_extra","xm8_sp_s","40mmgl","AK74","UMP45","synthetic_smg_unregistered"})CHECK(!FindMagazineGeometry(asset));
 const std::string embedded("XM8_sp_s\0suffix",15);CHECK(!FindMagazineGeometry(embedded));
 const auto second=Second();CHECK(!BindMagazinePresentation(Rig(second),second.asset));return 0;}
int CapturedXm8CalibrationIsUnchanged(){const auto& p=Xm8MagazineGeometry();const auto c=Xm8MagazineConfig();
 CHECK(p.rigFingerprint==Xm8MagazineRig&&p.mesh==Xm8MagazineMesh&&p.meshKind==SelectedMeshKind::Xm8);
 CHECK(Same(c.insertion.itemFromHand,xm8_magazine_calibration::ItemFromHand));
 CHECK(Same(c.insertion.itemFromInsertion,xm8_magazine_calibration::ItemFromInsertion));
 CHECK(Same(c.insertion.weaponFromEntry,xm8_magazine_calibration::WeaponFromEntry));
 CHECK(Same(p.attachedItem,xm8_magazine_calibration::AttachedItem));
 for(unsigned n=0;n<15;++n)CHECK(Same(p.wristFromFinger[n],xm8_magazine_calibration::WristFromFinger[n]));
 CHECK(c.ackTimeoutNs==4000000000ll&&c.transactionTimeoutNs==30000000000ll);
 CHECK(c.insertion.id==0x584d384d4147ull&&c.insertion.revision==1&&c.pullMeters==.09f);
 return 0;}
int BoneRolesAreDataNotXm8Indices(){const auto a=Rig(Xm8MagazineGeometry());const auto second=Second();auto b=Rig(second);
 const auto left=magazine_presentation_detail::Derive(a);const auto right=magazine_presentation_detail::Derive(b,second);
 CHECK(left&&right&&left->magazine==4&&right->magazine==4&&right->wrist==2);
 CHECK(!magazine_presentation_detail::Derive(b)&&!magazine_presentation_detail::Derive(a,second));
 CHECK(!BindMagazinePresentation(a,Xm8MagazineAsset)); // Correct names are not the captured production rig.
 auto wrong=second;wrong.bones.fingers[1]=wrong.bones.fingers[0];CHECK(!magazine_presentation_detail::Derive(b,wrong));
 b.parents[4]=2;CHECK(!magazine_presentation_detail::Derive(b,second));return 0;}
int SharedRemovalCoordinatorConsumesDifferentGeometry(){
 for(const auto& p:{Xm8MagazineGeometry(),Second()}){
  const auto& c=p.interaction;DetachableMagazine policy(c);CHECK(policy.ValidConfig());HandInteraction hands;
  DetachableMagazineSample s;s.input.owner={1,2,3,4};s.input.focused=true;s.input.tracked={true,true};
  s.weapon={5,3};s.trackingEpoch=4;s.native.owner=s.input.owner;s.native.weapon=s.weapon;s.native.bindingsVerified=true;
  std::optional<HandClaim> gun;std::uint64_t intent=0;std::int64_t now=1000000000;
  const auto send=[&](bool grip,float travel){++s.input.sequence;now+=10000000;s.input.nowNs=s.input.observedNs=now;s.input.deadlineNs=now+100000000;
   s.input.released[0]=!grip;s.gripPressed=grip;s.geometrySequence=s.input.sequence;s.intent=++intent;
   s.weaponFromHandMeters=Multiply(c.insertion.itemFromHand,Multiply(*InverseRigid(c.insertion.itemFromInsertion),
    Multiply(TravelPose(c.insertion,travel),c.insertion.weaponFromEntry)));
   s.native.observedNs=now;s.native.deadlineNs=s.input.deadlineNs;hands.Update(s.input);
   const HandContactProof proof{{8,1},s.input.sequence,s.input.deadlineNs,true};
   if(gun)gun=hands.Renew(s.input,gun->token,proof).claim;
   if(!gun)gun=hands.Acquire(s.input,{s.input.owner,InteractionHand::Right,HandClaimKind::GunHold,s.weapon,proof,++intent,0}).claim;
   s.intent=++intent;return policy.Update(s,hands);};
  send(false,c.insertion.travelMeters);auto out=send(true,c.insertion.travelMeters);
  CHECK(out.removalGrabbed&&out.transaction.request&&out.phase==DetachableMagazinePhase::PreparingRemoval);
  const auto request=*out.transaction.request;
  for(unsigned n=0;n<3;++n){out=send(true,c.insertion.travelMeters);CHECK(!out.physicallyRemoved&&!out.transaction.completed);}
  s.native.cycle=22;s.native.allThreeHeld=s.native.acknowledgementVerified=true;
  s.native.acknowledgement={request.id,request.owner,request.operation,ReloadAcknowledgement::Applied};
  out=send(true,c.insertion.travelMeters-.025f);s.native.acknowledgement={};s.native.acknowledgementVerified=false;
  for(unsigned n=2;n<=6;++n)out=send(true,c.insertion.travelMeters-.025f*n);
  CHECK(out.phase==DetachableMagazinePhase::RemovedHeld&&out.prop&&out.prop->role==MagazinePropRole::Removed);
  CHECK((out.prop->profile==HandInteractionKey{c.insertion.id,c.insertion.revision}));
  CHECK(!out.transaction.completed&&!out.transaction.request&&!out.seat);
 }
 return 0;}
}
int main(){if(ExactRegistryDoesNotAdmitOtherWeapons()||CapturedXm8CalibrationIsUnchanged()||BoneRolesAreDataNotXm8Indices()||SharedRemovalCoordinatorConsumesDifferentGeometry())return 1;
 std::puts("4 magazine geometry profile groups passed; synthetic second profile is not registered or native-admitted");}
