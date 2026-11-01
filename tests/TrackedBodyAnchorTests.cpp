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
#include "Bc2RenderedViewEvidence.h"
#include <limits>
#include <sstream>
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
ipc::FrameLease RenderLease(std::uint64_t request=1){
 ipc::FrameLease lease;lease.channelGeneration=9;lease.requestId=request;lease.native={17,request+100,3};
 lease.tracking.generation=request+200;lease.tracking.spaceGeneration=7;lease.tracking.predictedNs=1000000000+std::int64_t(request);
 lease.tracking.worldUnitsPerMeter=1;return lease;
}
graphics::PairTicket RenderTicket(const bc2::RenderedViewKey& key){
 graphics::PairTicket ticket;ticket.resourceEpoch=13;ticket.sequence=key.frame.request;ticket.session[0]=5;
 ticket.frameId=key.frame.frame;ticket.trackingGeneration=key.frame.tracking;ticket.spaceGeneration=key.frame.space;
 ticket.predictedNs=key.frame.predictedNs;return ticket;
}
math::Matrix4 RenderProjection(){auto m=At();m.values[2][3]=1;m.values[3][3]=0;return m;}
bc2::RenderedViewEvidence RenderedPair(unsigned mask=3,bool restored=true,bool captured=true,std::uint64_t request=1){
 bc2::RenderedViewEvidence row;row.key=bc2::RenderedViewLeaseKey(RenderLease(request));const auto projection=RenderProjection();
 for(unsigned eye=0;eye<2;++eye)if(mask&(1u<<eye)){
  const auto view=At(eye?.03f:-.03f,0,-2);row.Eye(row.key,eye,1000+eye,&view,&projection);row.Capture(row.key,eye,captured);
 }
 row.Finish(row.key,restored,restored&&captured);return row;
}
int RenderedPublicationAdmission(){
 const auto lease=RenderLease();const auto key=bc2::RenderedViewLeaseKey(lease);const auto ticket=RenderTicket(key);
 CHECK(key.frame.channel==lease.channelGeneration&&key.frame.request==lease.requestId&&key.frame.owner==lease.native.owner&&
       key.frame.frame==lease.native.frameId&&key.frame.device==lease.native.deviceEpoch&&key.frame.tracking==lease.tracking.generation&&
       key.frame.space==lease.tracking.spaceGeneration&&key.frame.predictedNs==lease.tracking.predictedNs&&
       key.sourceSlot==lease.sourceSlot&&key.sourceSlotCount==lease.sourceSlotCount&&key.worldUnitsPerMeter==lease.tracking.worldUnitsPerMeter);
 auto delayed=RenderedPair();delayed.publicationAccepted=true;CHECK(!delayed.Usable()&&!delayed.published);
 CHECK(delayed.Published(key,ticket)&&delayed.Usable()); // The later LegacyFenced Published event supplies the ticket.
 auto synchronous=RenderedPair();CHECK(synchronous.Published(key,ticket)&&!synchronous.Usable());
 synchronous.publicationAccepted=true;CHECK(synchronous.Usable()); // Published may run inside PublishRestored.
 for(unsigned mask:{0u,1u,2u}){auto missing=RenderedPair(mask);missing.publicationAccepted=true;
  CHECK(missing.Published(key,ticket)&&!missing.Usable());}
 for(bool restore:{false,true}){auto failed=RenderedPair(3,restore,!restore);failed.publicationAccepted=false;
  CHECK(!failed.Published(key,ticket)&&!failed.Usable());}
 auto noFinish=bc2::RenderedViewEvidence{};noFinish.key=key;CHECK(!noFinish.Published(key,ticket)&&!noFinish.Usable());
 auto alias=RenderedPair();alias.nativeView[1]=alias.nativeView[0];alias.publicationAccepted=true;
 CHECK(alias.Published(key,ticket)&&!alias.Usable());
 auto duplicate=RenderedPair();const auto view=At(),projection=RenderProjection();
 CHECK(!duplicate.Eye(key,0,1000,&view,&projection)&&duplicate.invalid);
 // Empty prop counts do not excuse an absent/invalid camera; use the same
 // production rigid-view/projection validator independently of prop instances.
 for(unsigned bad=0;bad<4;++bad){bc2::RenderedViewEvidence row;row.key=key;auto v=At(),p=projection;
  if(bad==0)v.values[0][0]=0;if(bad==1)v.values[3][0]=std::numeric_limits<float>::quiet_NaN();
  if(bad==2)p.values[2][3]=0;if(bad==3)p.values[0][0]=std::numeric_limits<float>::infinity();
  CHECK(!row.Eye(key,0,1000,&v,&p)&&row.drawReturnedMask==1&&row.matrixValidMask==0&&!row.Usable());
  std::ostringstream json;row.Write(json);CHECK(json.str().find("\"view\":null")!=std::string::npos&&json.str().find("\"projection\":null")!=std::string::npos);
 }
 bc2::RenderedViewEvidence unreadable;unreadable.key=key;
 CHECK(!unreadable.Eye(key,0,1000,nullptr,nullptr)&&unreadable.matrixValidMask==0);
 return 0;
}
int RenderedExactIdentity(){
 const auto key=bc2::RenderedViewLeaseKey(RenderLease());const auto ticket=RenderTicket(key);
 for(unsigned field=0;field<11;++field){auto wrong=key;
  switch(field){case 0:++wrong.frame.channel;break;case 1:++wrong.frame.request;break;case 2:++wrong.frame.owner;break;
   case 3:++wrong.frame.frame;break;case 4:++wrong.frame.device;break;case 5:++wrong.frame.tracking;break;
   case 6:++wrong.frame.space;break;case 7:++wrong.frame.predictedNs;break;case 8:++wrong.sourceSlot;break;
   case 9:++wrong.sourceSlotCount;break;default:wrong.worldUnitsPerMeter=2;break;}
  auto row=RenderedPair();row.publicationAccepted=true;CHECK(!row.Published(wrong,ticket)&&row.invalid&&!row.Usable());
  bc2::RenderedViewEvidence eye;eye.key=key;const auto v=At(),p=RenderProjection();CHECK(!eye.Eye(wrong,0,1000,&v,&p)&&eye.invalid);
 }
 for(unsigned field=0;field<12;++field){auto wrong=ticket;
  switch(field){case 0:++wrong.magic;break;case 1:++wrong.version;break;case 2:++wrong.bytes;break;case 3:++wrong.reserved0;break;
   case 4:++wrong.reserved[1];break;case 5:wrong.resourceEpoch=0;break;case 6:wrong.sequence=0;break;case 7:wrong.session={};break;
   case 8:++wrong.frameId;break;case 9:++wrong.trackingGeneration;break;case 10:++wrong.spaceGeneration;break;default:++wrong.predictedNs;break;}
  auto row=RenderedPair();row.publicationAccepted=true;CHECK(!row.Published(key,wrong)&&row.invalid&&!row.Usable());
 }
 for(bool session:{false,true}){auto row=RenderedPair();row.publicationAccepted=true;CHECK(row.Published(key,ticket)&&row.Usable());
  auto reused=ticket;if(session)++reused.session[1];else ++reused.resourceEpoch;
  CHECK(!row.Published(key,reused)&&!row.Usable()&&row.ticket.session==ticket.session&&row.ticket.resourceEpoch==ticket.resourceEpoch);
 }
 return 0;
}
int RenderedRetentionAndJournalWrap(){
 using Row=bc2::RenderedViewEvidence;bc2::ViewBasisEvidence<Row,4> samples;auto row=RenderedPair();row.publicationAccepted=true;
 const auto key=row.key;const auto ticket=RenderTicket(key);CHECK(samples.Observe(row,1000));
 auto unfinished=RenderedPair(1,true,true,2);CHECK(samples.Observe(unfinished,1100));
 // Publication enriches the retained original after a successor sample. The
 // missing-eye successor stays missing; neither latest nor timestamp can join it.
 CHECK(samples.Enrich([&](const Row& r){return r.key==key;},[&](Row& r){r.Published(key,ticket);}));
 CHECK(samples.At(0).Usable()&&!samples.At(1).Usable()&&!samples.At(1).published);
 auto latest=RenderedPair(3,true,true,3);latest.publicationAccepted=true;
 CHECK(samples.Observe(latest,1200));const auto latestTicket=RenderTicket(latest.key);
 CHECK(samples.Enrich([&](const Row& r){return r.key==latest.key;},[&](Row& r){r.Published(latest.key,latestTicket);}));
 CHECK(samples.At(samples.Size()-1).Usable());
 const auto snapshot=samples;
 for(unsigned i=4;i<40;++i){auto next=RenderedPair(3,true,true,i);CHECK(samples.Observe(next,900+i*100));}
 CHECK(samples.Compactions()>0&&samples.At(0).Usable()&&snapshot.At(snapshot.Size()-1).Usable());
 graphics::FrameBridgeDiagnostics journal;graphics::FrameBridgeTimingSnapshot timing;
 for(unsigned i=1;i<=graphics::FrameBridgeTimingCapacity+7;++i){const auto k=bc2::RenderedViewLeaseKey(RenderLease(i));const auto t=RenderTicket(k);
  journal.Begin(k.frame,64,48,i*10);journal.Ticket(k.frame,t);journal.Mark(k.frame,graphics::FrameBridgeTimingStage::Published,i*10+1);
  journal.Reaped(k.frame,t,i*10+2);journal.Released(t,true,i*10+3);
 }
 CHECK(journal.Snapshot(timing)&&timing.overwritten==7&&timing.rows[0].key.request==8);
 const auto& retained=samples.At(0);CHECK(retained.Usable()&&retained.key==key&&retained.ticket.session==ticket.session&&
  retained.ticket.resourceEpoch==ticket.resourceEpoch&&retained.ticket.sequence==ticket.sequence&&retained.view[1].values[3][0]==.03f);
 std::ostringstream json;retained.Write(json);CHECK(json.str().find("\"usable\":true")!=std::string::npos&&
  json.str().find("\"sequence\":1")!=std::string::npos&&json.str().find("\"session\":[5,0")!=std::string::npos);
 return 0;
}
}
int main(){if(SupportAndRecenterUseOneAim()||ReplacedBasisAndReconnect()||EpochBoundsAndActor()||ViewEvidenceSpansLateScenes()||ViewEvidenceCadenceAndCompaction()||
 RenderedPublicationAdmission()||RenderedExactIdentity()||RenderedRetentionAndJournalWrap())return 1;
 std::puts("Tracked body anchor: reference delta, support/render/native-angle composition, basis recovery and bounded late-session evidence passed");}
