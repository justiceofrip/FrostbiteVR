#include "Bc2MagazineDetachedProbe.h"
#include "Bc2PhysicalReload.h"
#include "fvr/interaction/TrackedRig.h"
#include "Test.h"
#include <bit>
#include <cstring>
#include <iostream>
#include <sstream>
using namespace fvr;using namespace fvr::bc2;using namespace fvr::interaction;using namespace fvr::interaction::reload_insertion_detail;
namespace {
constexpr std::int64_t Ms=1000000;
auto Pose(float x=0,float y=0,float z=0){auto m=Identity();m.values[3]={x,y,z,1};return m;}
auto Matrix(const math::Pose& p){return *InverseRigid(*math::MakeLhViewFromOpenXRPose(p));}
InputFrame Input(std::uint64_t sequence){InputFrame in;in.generation=sequence;in.spaceGeneration=7;in.predictedNs=1000*Ms+std::int64_t(sequence)*10*Ms;
 in.focused=in.headValid=true;for(auto& h:in.hands){h.gripTracked=h.aimTracked=true;h.active=Components;}
 in.hands[0].grip.position={-.2f,-.4f,-.3f};in.hands[1].grip.position={0,-.25f,-.45f};return in;}
std::string Report(const Bc2MagazineDetachedProbe& p){std::ostringstream o;p.Report(o);return o.str();}
struct Loop {
 std::int64_t now=1000*Ms;std::uint64_t sequence=0,intent=0,tick=0;
 bool anatomical=true,publishRemoved=true,publishAttached=true,allowCommit=true,mutateCounts=false;
 std::int64_t readDelay=0;
 bool refreshFixtureClock=true,refreshControllerClock=true;
 bool sourceEvidencePreserved=true;
 bool missingPrepareOnce=false,missingCommitOnce=false;unsigned prepareMisses=0,commitMisses=0;
 Bc2AmmoReserveLease reserve{};Bc2MagazineDetached controller{true};Bc2MagazineDetachedProbe probe{true};
 MagazinePackCounters packs{};std::optional<MagazineDetachPairReceipt> pair;
 HandInteraction hands;std::optional<HandClaim> gun;TrackedRig rig;MagazineRawContact raw{};
 std::shared_ptr<SelectedMeshesSnapshot> meshes=std::make_shared<SelectedMeshesSnapshot>();
 std::array<std::byte,InputBytes> cache{};MagazinePhysicalSample sample{};
 unsigned suppressionCommits=0,removedSeen=0;
 explicit Loop(int loaded=30,int rounds=83){auto& n=reserve.identity;n.owner={0x10000,0x20000,0x30000,0x40000,5,3,7};n.firing={0x50000,0x60000,0x70000};n.serverPlayer=0x80000;n.serverSoldier=0x90000;n.serverItem=0xa0000;
 reserve.loaded=loaded;reserve.reserve=rounds;reserve.capacity=30;reserve.verified=reserve.allThreeIdle=true;
 reserve.reloadInputReady=loaded<30&&rounds>0;
 meshes->owner=n.owner;meshes->stateCount=1;meshes->soleConfiguredArray=0x110000;meshes->states[0].count=1;
 auto& m=meshes->states[0].meshes[0];m.kind=SelectedMeshKind::Xm8;m.address=0x120000;std::memcpy(m.assetPath.data(),Xm8MagazineMesh.data(),Xm8MagazineMesh.size());}
 void Tick(){now+=10*Ms;++sequence;++tick;auto in=Input(sequence);const auto owner=reserve.identity.owner;
  reserve.sequence=100000+sequence;reserve.observedNs=now+readDelay;reserve.deadlineNs=now+100*Ms;
  probe.Prepare(in,owner,Xm8MagazineAsset,raw,reserve,controller.Phase(),now,now+100*Ms,now+(refreshFixtureClock?2*readDelay:0));
  HandInteractionSample h{{(std::uint64_t(owner.weak)<<32)|owner.soldier,5,17,7},sequence,now,now+100*Ms,now,true,{true,true},{in.hands[0].squeeze<=.35f,false}};
  hands.Update(h);HandContactProof proof{{1002,1},sequence,h.deadlineNs,true};
  if(gun)gun=hands.Renew(h,gun->token,proof).claim;
  if(!gun)gun=hands.Acquire(h,{h.owner,InteractionHand::Right,HandClaimKind::GunHold,{0xb0000,17},proof,++intent,0}).claim;
  meshes->sequence=sequence;meshes->observedNs=now;meshes->deadlineNs=now+200*Ms;
  sample={};sample.nativeOwner=owner;sample.input=h;sample.weapon={0xb0000,17};sample.trackingEpoch=7;sample.geometrySequence=sequence;
  sample.family={{owner,sample.weapon,0xd0000,0xc0000,2},now,now+100*Ms,true};sample.bodyFromHand=*PhysicalReloadPouchPose(in);
  sample.gripPressed=in.hands[0].squeeze>=.75f;sample.cancel=probe.CancelConsumer();sample.asset=Xm8MagazineAsset;sample.meshes=meshes;sample.raw=raw;
  if(raw.valid)sample.originalHandEvidence=raw.inputEvidence;
  if(refreshControllerClock)sample.input.nowNs=now+2*readDelay;
  sourceEvidencePreserved=sourceEvidencePreserved&&sample.input.observedNs==h.observedNs&&sample.input.deadlineNs==h.deadlineNs&&
   reserve.observedNs==now+readDelay&&reserve.deadlineNs==now+100*Ms;
  const bool stroking=Report(probe).find("\"phase\":6,\"failure\":0")!=std::string::npos;
  std::optional<Bc2AmmoReserveLease> preReserve=reserve;
  if(stroking&&missingPrepareOnce&&!prepareMisses){preReserve.reset();++prepareMisses;}
  controller.Prepare(sample,preReserve,tick,0xe0000,hands);const auto demand=controller.Demand();
  std::optional<HolsterSuppressionReceipt> committed;
  if(demand&&allowCommit){
   // Local byte-array challenge exercises the real production override; no
   // synthetic receipt constructor, game access, reload adapter or ammo writes.
   const auto put=[&](unsigned at,unsigned v){std::memcpy(cache.data()+at,&v,4);};
   put(8+4*7,std::bit_cast<unsigned>(1.f));put(8+4*8,std::bit_cast<unsigned>(1.f));put(0x98,~0u);put(0x9c,~0u);
   HolsterInputOverride patch;HolsterInputOwner checker{this,[](void* p,const HolsterSuppressionRequest& r)noexcept{const auto& f=*static_cast<Loop*>(p);return r.owner==f.reserve.identity.owner&&r.nativeTick==f.tick&&r.cache==0xe0000;}};
   if(patch.Apply(cache,*demand,checker)){committed=patch.Commit();if(committed)++suppressionCommits;}
  }
  // Read actual post-commit counts, then sample processing time. The source
  // packet and reserve deadline retain their original expiration boundaries.
  auto postReserve=reserve;postReserve.observedNs=now+3*readDelay;
  const auto processingNow=now+4*readDelay;
  std::optional<Bc2AmmoReserveLease> postRead=postReserve;
  if(stroking&&missingCommitOnce&&!commitMisses){postRead.reset();++commitMisses;}
  const auto result=controller.Commit(committed,pair,postRead,hands,intent,now+2*readDelay,processingNow);
  probe.Observe(controller,result,committed,pair,postRead,packs,bool(demand),processingNow);
  if(result.tracking.target&&result.tracking.detach&&MagazineTargetFresh(result.tracking,processingNow)){
   const auto role=result.tracking.target->role;const bool attached=role==MagazinePropRole::Attached;
   if(role==MagazinePropRole::Removed)++removedSeen;
   if((attached&&publishAttached)||(!attached&&publishRemoved)){
    ++packs.pairs;packs.copies+=2;const auto r=unsigned(role);++packs.rolePairs[r];packs.roleCopies[r]+=2;
    if(!result.tracking.retainedVisualSuppression)pair=MagazineDetachPackedPair(*result.tracking.detach,*result.tracking.detach,attached,sequence,3,true,processingNow);
   }
   if(mutateCounts&&role==MagazinePropRole::Removed)--reserve.loaded;
  }
  if(result.tracking.retainedVisualSuppression)raw={};
  const auto body=Pose(5,2,3),left=Multiply(Pose(-.2f,-.4f,.3f),body),right=Multiply(Pose(0,-.25f,.45f),body);
  std::array<ArmAnchor,2> arms{{{{4.8f,1.8f,3},{0,1,0}},{{5.2f,1.8f,3},{0,1,0}}}};
  const auto posed=rig.Update({1,2,3,4},in,body,left,right,right,arms);
  if(posed&&!result.tracking.retainedVisualSuppression&&MagazineTrackingFresh(result.tracking,processingNow)){raw.valid=true;raw.owner=owner;raw.rigFingerprint=Xm8MagazineRig;raw.inputEvidence=h;
   raw.rawLeftWristWorldMeters=posed->left;raw.weaponWorldMeters=posed->weapon;raw.trackingBodyWorldMeters=body;raw.nativeMagazineAttached=true;
   if(anatomical){auto attachment=Pose();attachment.values[1][1]=attachment.values[2][2]=-1;
    auto wrist=Multiply(attachment,Multiply(Matrix(in.hands[0].grip),body));wrist.values[3]=posed->left.values[3];raw.rawLeftWristWorldMeters=wrist;}
  }
 }
 bool Run(){for(unsigned n=0;n<3000&&!probe.Finished();++n)Tick();return Report(probe).find("\"actual_consumer_completed\":true")!=std::string::npos;}
};
int ActualControllerReturnsFullAndZeroReserve(){for(bool anatomical:{false,true})for(auto counts:{std::pair{30,83},std::pair{30,0},std::pair{27,0}}){
 Loop f(counts.first,counts.second);f.anatomical=anatomical;const auto pass=f.Run();if(!pass){std::cerr<<Report(f.probe)<<'\n';f.controller.Report(std::cerr);}
 CHECK(pass&&f.controller.Returned()==1&&!f.controller.BlocksActions()&&f.controller.Recovered()==0);
 CHECK(f.reserve.loaded==counts.first&&f.reserve.reserve==counts.second&&f.suppressionCommits>0&&f.removedSeen>0);
 CHECK(f.packs.rolePairs[unsigned(MagazinePropRole::Removed)]>0&&f.packs.rolePairs[unsigned(MagazinePropRole::Attached)]>0);
 CHECK(Report(f.probe).find("\"suppression_released\":1")!=std::string::npos);
 }return 0;}
int NoCommitCannotDetach(){Loop f;f.allowCommit=false;CHECK(!f.Run()&&f.removedSeen==0&&f.controller.Returned()==0&&f.reserve.loaded==30);return 0;}
int MissingPairCannotReturn(){for(bool attached:{false,true}){Loop f;if(attached)f.publishAttached=false;else f.publishRemoved=false;
 CHECK(!f.Run()&&f.controller.Returned()==0&&f.reserve.loaded==30&&f.reserve.reserve==83);}return 0;}
int ChangedAmmoRejectsSuccess(){Loop f;f.mutateCounts=true;CHECK(!f.Run()&&f.controller.Returned()==0);return 0;}
int PartialWithReserveAndEmptyDoNotBegin(){for(auto counts:{std::pair{27,83},std::pair{0,0}}){Loop f(counts.first,counts.second);
 CHECK(!f.Run()&&f.suppressionCommits==0&&f.removedSeen==0);}return 0;}
int FreshReadNeedsLaterProcessingClock(){
 Loop staleFixture;staleFixture.readDelay=1000;staleFixture.refreshFixtureClock=false;
 CHECK(!staleFixture.Run()&&staleFixture.controller.Grabs()==0&&staleFixture.suppressionCommits==0);
 CHECK(Report(staleFixture.probe).find("\"failure\":6")!=std::string::npos);
 CHECK(Report(staleFixture.probe).find("\"reserve_future\":1001")!=std::string::npos);
 Loop staleController;staleController.readDelay=1000;staleController.refreshControllerClock=false;
 CHECK(!staleController.Run()&&staleController.controller.Grabs()==0&&staleController.suppressionCommits==0);
 Loop ordered;ordered.readDelay=1000;CHECK(ordered.Run()&&ordered.controller.Returned()==1&&ordered.sourceEvidencePreserved);
 CHECK(Report(ordered.probe).find("\"reserve_future\":0")!=std::string::npos);
 return 0;
}
int ExpiredPacketCannotBeRenewedByProcessingClock(){
 Loop expired;expired.readDelay=60*Ms;
 CHECK(!expired.Run()&&expired.controller.Grabs()==0&&expired.suppressionCommits==0&&expired.sourceEvidencePreserved);
 return 0;
}
int OneMissingNativeReadDuringStrokeDoesNotDiscardOriginal(){
 for(unsigned mode=1;mode<=3;++mode){Loop f;f.readDelay=1000;
  f.missingPrepareOnce=(mode&1)!=0;f.missingCommitOnce=(mode&2)!=0;
  const auto success=f.Run();if(!success){std::cerr<<Report(f.probe)<<'\n';f.controller.Report(std::cerr);}
  CHECK(success&&f.controller.Grabs()==1&&f.controller.Seats()==1&&f.controller.Returned()==1&&f.controller.Recovered()==0);
  CHECK(f.prepareMisses==unsigned(f.missingPrepareOnce)&&f.commitMisses==unsigned(f.missingCommitOnce));
  CHECK(f.reserve.loaded==30&&f.reserve.reserve==83&&f.sourceEvidencePreserved&&!f.controller.BlocksActions());
 }return 0;
}
int WrongCommittedTickCannotBeEvidence(){Loop f;for(unsigned n=0;n<1600&&!f.controller.BlocksActions();++n)f.Tick();CHECK(f.controller.BlocksActions());
 const auto demand=f.controller.Demand();CHECK(demand);HolsterSuppressionReceipt falseReceipt;static_cast<HolsterSuppressionRequest&>(falseReceipt)=*demand;falseReceipt.nativeTick=0;
 f.probe.Observe(f.controller,{},falseReceipt,{},f.reserve,f.packs,true,f.now);CHECK(f.probe.CancelConsumer());return 0;}
}
int main(){if(ActualControllerReturnsFullAndZeroReserve()||NoCommitCannotDetach()||MissingPairCannotReturn()||ChangedAmmoRejectsSuccess()||PartialWithReserveAndEmptyDoNotBegin()||FreshReadNeedsLaterProcessingClock()||ExpiredPacketCannotBeRenewedByProcessingClock()||OneMissingNativeReadDuringStrokeDoesNotDiscardOriginal()||WrongCommittedTickCannotBeEvidence())return 1;
 std::cout<<"Full magazine probe: 9 actual-controller/cache-commit/TrackedRig/paired-proof groups passed; runtime/headset unverified\n";}
