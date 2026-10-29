#include "Test.h"
#include "fvr/interaction/TrackedBodyAnchor.h"
#include "fvr/interaction/TrackingReference.h"
#include "fvr/interaction/SupportGrip.h"
#include "fvr/interaction/TrackedRig.h"
#include "fvr/interaction/ComfortCamera.h"
#include "fvr/interaction/WeaponProfile.h"
#include "fvr/ipc/InputProtocol.h"
#include "Bc2RigPaletteCopy.h"
#include "Bc2ViewBasisEvidence.h"
#include <limits>
using namespace fvr;using namespace interaction;
namespace {
constexpr float radians=.017453292519943295f;
math::Quaternion Yaw(float r){return {0,std::sin(r*.5f),0,std::cos(r*.5f)};}
math::Matrix4 At(float x=0,float y=0,float z=0){math::Matrix4 m{};for(unsigned n=0;n<4;++n)m.values[n][n]=1;m.values[3]={x,y,z,1};return m;}
bool SameDirection(const math::Matrix4& a,const math::Matrix4& b){for(unsigned n=0;n<3;++n)if(!Near(a.values[2][n],b.values[2][n],.0003f))return false;return true;}
InputFrame Input(){InputFrame f;f.generation=f.spaceGeneration=f.trackingBasisGeneration=1;f.predictedNs=1000000000;
 f.focused=f.headValid=true;for(auto& h:f.hands){h.gripTracked=h.aimTracked=true;h.active=Components;}
 f.hands[0].grip.position={-.2f,-.2f,-.55f};f.hands[1].grip.position={.2f,-.2f,-.25f};return f;}
void Next(InputFrame& f){++f.generation;f.predictedNs+=11000000;}
struct Scene {
 InputFrame raw=Input();TrackedBodyAnchor anchor;SupportGrip support;TrackedRig rig;
 float nativeYaw=.8f,lastHeadYaw=0;TrackedBodyAnchorResult body{};SupportGripResult held{};
 std::optional<TrackedRigPose> pose;float fireYaw=0,firePitch=0;
 bool Step(){
  // The same wire decode, anchor, support, native-angle and render consumers
  // used by the host/game path. The native setter itself is outside this CPU test.
  ipc::InputPacket wire;InputFrame input;
  if(!ipc::EncodeInput(raw,10000000000,wire)||!ipc::DecodeInput(wire,input))return false;
  const auto base=anchor.Update(71,input,nativeYaw,lastHeadYaw,0);if(!base)return false;body=*base;
  if(body.referenceChanged)lastHeadYaw=0;
  held=support.Update({71,1,9},input,{true,0,{}});input=held.input;
  const auto aim=ControllerAim(input.referenceHead,input.hands[1].aim);
  const auto head=ControllerAim(input.referenceHead,input.head,lastHeadYaw);if(!aim||!head)return false;
  fireYaw=body.yaw+aim->yaw;firePitch=aim->pitch;nativeYaw=fireYaw;lastHeadYaw=head->yaw;
  const auto frame=*MakeComfortCamera(At(3,1.6f,8),body.yaw/radians);
  const auto aimFrame=TrackedAimFrame(input,frame);if(!aimFrame)return false;
  // Exercise the same model-axis conversion used by authored gun rendering,
  // including a model whose barrel points along -Z rather than controller +Z.
  const auto orientation=ModelAimFrame({0,0,-1},{0,1,0},*aimFrame);if(!orientation)return false;
  const std::array<ArmAnchor,2> arms{ArmAnchor{{2.8f,1.4f,8},{-.1f,-.2f,.1f}},ArmAnchor{{3.2f,1.4f,8},{.1f,-.2f,.1f}}};
  pose=rig.Update({71,1,9,5,1},input,frame,At(2.8f,1.3f,8.4f),At(3.2f,1.3f,8.4f),At(3.2f,1.3f,8.7f),arms,{}, {},orientation,AuthoredGripAttachment{At(.03f,-.07f,-.22f)});
  if(!pose)return false;
  return Near(-pose->weapon.values[2][0],std::sin(fireYaw)*std::cos(firePitch),.0003f)&&
    Near(-pose->weapon.values[2][1],std::sin(firePitch),.0003f)&&
    Near(-pose->weapon.values[2][2],std::cos(fireYaw)*std::cos(firePitch),.0003f);
 }
 bool Recenter(){const auto reference=ExplicitRecenterReference(raw.head,raw.referenceHead);if(!reference)return false;
  if(!AdvanceTrackingReference(raw.spaceGeneration,raw.trackingBasisGeneration,TrackingReferenceChange::ManualRecenter))return false;
  raw.referenceHead=*reference;Next(raw);return Step();}
};
int SupportAndRecenterUseOneAim(){
 Scene s;s.raw.referenceHead.orientation=s.raw.head.orientation=Yaw(.35f);
 s.raw.hands[1].aim.orientation=s.raw.hands[1].grip.orientation={.12f,-.18f,.09f,.97f};
 CHECK(s.Step());const auto free=*s.pose;
 Next(s.raw);s.raw.hands[0].squeeze=1;CHECK(s.Step()&&s.held.engaged&&s.held.holding);
 CHECK(SameDirection(free.weapon,s.pose->weapon)); // Acquiring support cannot snap aim.
 Next(s.raw);s.raw.hands[0].grip.position.x+=.15f;CHECK(s.Step()&&s.held.correctionRadians>.1f);
 CHECK(!SameDirection(free.weapon,s.pose->weapon));
 Next(s.raw);s.raw.hands[0].squeeze=0;CHECK(s.Step()&&!s.held.holding&&s.held.released);
 CHECK(SameDirection(free.weapon,s.pose->weapon));
 // New head orientation arrives on the recenter packet, not the preceding
 // sample. Adding cached lastHeadYaw would use the wrong delta.
 s.raw.head.orientation=Yaw(-.6f);s.raw.head.position={.1f,.2f,.1f};
 CHECK(s.Recenter()&&!s.body.basisReplaced&&SameDirection(free.weapon,s.pose->weapon));
 const auto recentered=*s.pose;const auto priorBody=s.body.yaw;
 Next(s.raw);s.raw.head.orientation=Yaw(-1.0f);CHECK(s.Step());CHECK(std::abs(s.lastHeadYaw)>.3f);
 // At the vertical singularity the real host keeps the previous reference
 // orientation. The body and the stationary gun must not gain lastHeadYaw.
 s.raw.head.orientation={std::sqrt(.5f),0,0,std::sqrt(.5f)};
 CHECK(s.Recenter()&&Near(s.body.yaw,priorBody)&&SameDirection(recentered.weapon,s.pose->weapon));
 // A real support lifetime break requires release/new squeeze in the new space.
 s.raw.hands[0].squeeze=0;Next(s.raw);CHECK(s.Step());s.raw.hands[0].squeeze=1;Next(s.raw);CHECK(s.Step()&&s.held.holding);
 CHECK(s.Recenter()&&!s.held.holding);Next(s.raw);CHECK(s.Step()&&!s.held.holding);
 s.raw.hands[0].squeeze=0;Next(s.raw);CHECK(s.Step());s.raw.hands[0].squeeze=1;Next(s.raw);CHECK(s.Step()&&s.held.holding);
 return 0;
}
int ReplacedBasisAndReconnect(){
 for(const auto cause:{TrackingReferenceChange::AutomaticRecovery,TrackingReferenceChange::RuntimeSpaceReplacement,TrackingReferenceChange::SessionRestart}){
  Scene s;s.raw.head.orientation=Yaw(-.4f);s.raw.hands[1].aim.orientation=Yaw(.7f);CHECK(s.Step());
  const float expected=s.body.yaw+s.lastHeadYaw;CHECK(!Near(expected,s.nativeYaw));
  const auto previous=s.raw;CHECK(AdvanceTrackingReference(s.raw.spaceGeneration,s.raw.trackingBasisGeneration,cause));
  s.raw.referenceHead.orientation=s.raw.head.orientation=Yaw(2.0f);s.raw.hands[1].aim.orientation=Yaw(2.0f);Next(s.raw);
  CHECK(s.Step()&&s.body.basisReplaced&&Near(s.body.yaw,expected));
  // Existing rendering admission must reject the old-space palette even while
  // both the previous and current packets are unexpired.
  bc2::RigPaletteCopyState old{{1,2,3,4,5,previous.spaceGeneration},previous.generation,1000,true};
  auto current=old;current.owner.space=s.raw.spaceGeneration;current.input=s.raw.generation;
  CHECK(!bc2::RigPaletteCopyFresh(old,{current,100}));
 }
 Scene unknown;unknown.raw.trackingBasisGeneration=0;unknown.raw.head.orientation=Yaw(-.25f);CHECK(unknown.Step());
 const float expected=unknown.body.yaw+unknown.lastHeadYaw;Next(unknown.raw);++unknown.raw.spaceGeneration;
 unknown.raw.referenceHead.orientation=unknown.raw.head.orientation=Yaw(1.5f);CHECK(unknown.Step()&&unknown.body.basisReplaced&&Near(unknown.body.yaw,expected));
 // A restarted sender cannot turn a coincident reused basis counter into
 // authority to compare old and new reference quaternions.
 Scene restarted;restarted.raw.head.orientation=Yaw(-.3f);CHECK(restarted.Step());Next(restarted.raw);CHECK(restarted.Step());
 const float previousView=restarted.body.yaw+restarted.lastHeadYaw;
 restarted.raw.generation=1;restarted.raw.predictedNs=10;restarted.raw.referenceHead.orientation=restarted.raw.head.orientation=Yaw(2.2f);
 CHECK(restarted.Step()&&restarted.body.basisReplaced&&Near(restarted.body.yaw,previousView));return 0;
}
int EpochBoundsAndActor(){
 std::uint64_t space=7;std::uint32_t basis=4;
 CHECK(AdvanceTrackingReference(space,basis,TrackingReferenceChange::ManualRecenter)&&space==8&&basis==4);
 CHECK(AdvanceTrackingReference(space,basis,TrackingReferenceChange::RuntimeSpaceReplacement)&&space==9&&basis==5);
 space=std::numeric_limits<std::uint64_t>::max();CHECK(!AdvanceTrackingReference(space,basis,TrackingReferenceChange::ManualRecenter)&&basis==5);
 space=1;basis=std::numeric_limits<std::uint32_t>::max();CHECK(!AdvanceTrackingReference(space,basis,TrackingReferenceChange::SessionRestart)&&space==1);
 TrackedBodyAnchor anchor;auto input=Input();CHECK(anchor.Update(1,input,.2f,0,0));
 Next(input);auto next=anchor.Update(2,input,1.3f,.8f,0);CHECK(next&&Near(next->yaw,1.3f)&&next->referenceChanged&&!next->basisReplaced);
 input.focused=false;input.hands={};CHECK(!anchor.Update(2,input,1.3f,.8f,0));return 0;
}
int ViewEvidenceSpansLateScenes(){
 bc2::ViewBasisEvidence<std::uint64_t> evidence;
 CHECK(evidence.Size()==0&&evidence.Samples()==0&&evidence.Stride()==1);
 // Ten minutes includes a scene change beyond the old 192 x 100ms prefix.
 for(std::uint64_t ms=0;ms<=600000;ms+=100){
  CHECK(evidence.Observe(ms,ms)&&evidence.Size()<=192);
  CHECK(evidence.At(0)==0&&evidence.At(evidence.Size()-1)==ms);
 }
 CHECK(evidence.Samples()==6001&&evidence.Compactions()>0&&evidence.Stride()>1);
 bool lateScene=false;
 for(std::size_t i=1;i<evidence.Size();++i){
  const auto prior=evidence.At(i-1),current=evidence.At(i);
  CHECK(current>prior&&current-prior<=evidence.Stride()*100);
  lateScene|=current>=137000&&current<=147000;
 }
 CHECK(lateScene);const auto snapshot=evidence;
 CHECK(evidence.Observe(600100,600100)&&snapshot.At(snapshot.Size()-1)==600000);
 return 0;
}
int ViewEvidenceCadenceAndCompaction(){
 bc2::ViewBasisEvidence<std::uint64_t,4> evidence;
 CHECK(evidence.Observe(1000,1000));
 CHECK(!evidence.Observe(1099,1099)&&!evidence.Observe(1000,1000)&&!evidence.Observe(900,900));
 CHECK(evidence.Samples()==1&&evidence.At(0)==1000);
 CHECK(evidence.Observe(1100,1100)&&evidence.Observe(1200,1200)&&evidence.Observe(1300,1300));
 CHECK(evidence.Size()==3&&evidence.Compactions()==1&&evidence.Stride()==2);
 CHECK(evidence.At(0)==1000&&evidence.At(1)==1200&&evidence.At(2)==1300);
 // Real timestamps and the newest record survive gaps and irregular cadence.
 CHECK(evidence.Observe(9000,9000)&&evidence.At(evidence.Size()-1)==9000);
 const auto maximum=(std::numeric_limits<std::uint64_t>::max)();
 CHECK(evidence.Observe(maximum,maximum)&&evidence.At(evidence.Size()-1)==maximum);
 CHECK(!evidence.Observe(0,0)&&!evidence.Observe(maximum,maximum));
 return 0;
}
}
int main(){if(SupportAndRecenterUseOneAim()||ReplacedBasisAndReconnect()||EpochBoundsAndActor()||ViewEvidenceSpansLateScenes()||ViewEvidenceCadenceAndCompaction())return 1;
 std::puts("Tracked body anchor: reference delta, support/render/native-angle composition, basis recovery and bounded late-session evidence passed");}
