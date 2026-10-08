#include "Bc2MagazineDetachGate.h"
#include "Bc2MagazineDetached.h"
#include "Test.h"
#include <bit>
#include <cstring>
#include <iostream>
using namespace fvr;using namespace fvr::bc2;using namespace fvr::interaction;
namespace {
constexpr std::int64_t Ms=1000000;
struct Fixture {
 Bc2MagazineDetachGate gate;MagazineDetachGateSample s;
 HandInteraction hands;std::uint64_t intent=0;std::array<std::byte,InputBytes> cache{};
 std::shared_ptr<SelectedMeshesSnapshot> selected=std::make_shared<SelectedMeshesSnapshot>();
 std::optional<HandClaim> gun;bool ownerCurrent=true;
 Fixture(int loaded=30,int reserve=191){
  auto& r=s.reserve;r.identity.owner={0x10000,0x20000,0x30000,0x40000,5,3,7};
  r.identity.firing={0x50000,0x60000,0x70000};r.identity.serverPlayer=0x80000;r.identity.serverSoldier=0x90000;r.identity.serverItem=0xa0000;
  r.loaded=loaded;r.reserve=reserve;r.capacity=30;r.verified=r.allThreeIdle=true;r.reloadInputReady=loaded<30&&reserve>0;
  s.family.binding={r.identity.owner,{0xb0000,17},0xd0000,0xc0000,2};s.family.verified=true;
  s.input={{(std::uint64_t(0x30000)<<32)|0x20000,5,17,7},0,1000*Ms,1100*Ms,1000*Ms,true,{true,true},{false,false}};
  s.cache=0xe0000;s.selected=selected;s.nativeMagazineAttached=true;
  selected->owner=r.identity.owner;selected->stateCount=1;selected->soleConfiguredArray=0x110000;selected->states[0].count=1;
  auto& mesh=selected->states[0].meshes[0];mesh.kind=SelectedMeshKind::Xm8;mesh.address=0x120000;
  std::memcpy(mesh.assetPath.data(),Xm8MagazineMesh.data(),Xm8MagazineMesh.size());Next();
 }
 void Next(){auto& i=s.input;++i.sequence;++s.nativeTick;i.nowNs+=10*Ms;i.observedNs=i.nowNs;i.deadlineNs=i.nowNs+100*Ms;
  s.reserve.sequence=i.sequence;s.reserve.observedNs=i.nowNs;s.reserve.deadlineNs=i.deadlineNs;
  s.family.observedNs=i.nowNs;s.family.deadlineNs=i.deadlineNs;
  selected->sequence=i.sequence;selected->observedNs=i.nowNs;selected->deadlineNs=i.deadlineNs;
  hands.Update(i);HandContactProof c{{1,1},i.sequence,i.deadlineNs,true};
  if(gun)gun=hands.Renew(i,gun->token,c).claim;
  if(!gun)gun=hands.Acquire(i,{i.owner,InteractionHand::Right,HandClaimKind::GunHold,s.family.binding.weapon,c,++intent,0}).claim;
  if(gun)s.gun=*gun;
 }
 std::optional<HolsterSuppressionReceipt> Suppress(std::optional<HolsterSuppressionRequest> replacement={}){const auto request=replacement?replacement:gate.Demand(s);if(!request)return {};
  const auto put=[&](unsigned at,unsigned value){std::memcpy(cache.data()+at,&value,4);};
  // A real cache challenge: ordinary native Fire/cycle and reload/grenade bits
  // are initially set, then the existing production override must clear them.
  put(8+4*7,std::bit_cast<unsigned>(1.f));put(8+4*8,std::bit_cast<unsigned>(1.f));put(0x98,~0u);put(0x9c,~0u);
  HolsterInputOverride patch;HolsterInputOwner check{this,[](void* raw,const HolsterSuppressionRequest& r)noexcept{
   const auto& f=*static_cast<Fixture*>(raw);return f.ownerCurrent&&r.owner==f.s.reserve.identity.owner&&r.cache==f.s.cache&&r.nativeTick==f.s.nativeTick;}};
  if(!patch.Apply(cache,*request,check))return {};return patch.Commit();
 }
 bool Commit(){const auto receipt=Suppress();return receipt&&gate.Commit(s,*receipt,s.input.nowNs);}
 ReloadInsertionSeat Seat(){const auto& o=*gate.Original();auto left=hands.Acquire(s.input,{s.input.owner,InteractionHand::Left,
  HandClaimKind::Mechanism,o.weapon,{{2,1},s.input.sequence,s.input.deadlineNs,true},++intent,s.gun.token.id});
  return {1,s.input.sequence,{o.owner,o.weapon,o.item,o.trackingEpoch},o.profile,left.claim?left.claim->token:HandClaimToken{},s.gun.token,ReloadOperation::SeatMagazine};}
};
int FullAndZeroReserveRequireActualCommit(){for(const auto counts:{std::pair{30,191},std::pair{27,0},std::pair{30,0}}){
 Fixture f(counts.first,counts.second);CHECK(f.gate.Begin(f.s)&&f.gate.BlocksActions()&&!f.gate.Authorization());
 CHECK(f.gate.Phase()==MagazineDetachPhase::Suppressing);auto receipt=f.Suppress();CHECK(receipt);
 auto wrong=*receipt;++wrong.nativeTick;CHECK(!f.gate.Commit(f.s,wrong,f.s.input.nowNs)&&!f.gate.Authorization());
 CHECK(f.gate.Phase()==MagazineDetachPhase::Recovering);CHECK(f.Commit());CHECK(f.gate.Authorization()->restoring);
 CHECK(f.s.reserve.loaded==counts.first&&f.s.reserve.reserve==counts.second);
 Fixture valid(counts.first,counts.second);CHECK(valid.gate.Begin(valid.s)&&valid.Commit());
 CHECK(valid.gate.Phase()==MagazineDetachPhase::Detached&&valid.gate.Authorization()&&!valid.gate.Authorization()->restoring);
 unsigned fire=1;std::memcpy(&fire,valid.cache.data()+8+4*8,4);CHECK(fire==0);
 CHECK(valid.gate.Original()->rounds==unsigned(counts.first)&&!valid.s.reserve.reloadInputReady);
 }return 0;}
int NoReloadHoldOrPartialAuthority(){Fixture partial(27,191);CHECK(!partial.gate.Begin(partial.s));
 Fixture empty(0,0);CHECK(!empty.gate.Begin(empty.s));
 for(unsigned bad=0;bad<7;++bad){Fixture f;if(bad==0)f.s.reserve.allThreeIdle=false;if(bad==1)f.s.reserve.verified=false;
  if(bad==2)f.s.reserve.deadlineNs=f.s.input.nowNs;if(bad==3)f.s.family.verified=false;if(bad==4)f.s.nativeMagazineAttached=false;
  if(bad==5)f.s.gun.token.item.id++;if(bad==6)f.s.input.tracked[0]=false;CHECK(!f.gate.Begin(f.s));}
 return 0;}
int SameOriginalReturnsOnlyAfterCurrentAttachedPair(){Fixture f;CHECK(f.gate.Begin(f.s)&&f.Commit());
 const auto original=*f.gate.Original();const auto seat=f.Seat();CHECK(f.gate.Return(f.s,seat));CHECK(!f.gate.Authorization());
 f.Next();CHECK(f.Commit());const auto auth=*f.gate.Authorization();CHECK(auth.restoring);
 CHECK(!MagazineDetachPackedPair(auth,auth,false,1,3,true,f.s.input.nowNs));
 CHECK(!MagazineDetachPackedPair(auth,auth,true,1,1,true,f.s.input.nowNs));
 CHECK(!MagazineDetachPackedPair(auth,auth,true,1,3,false,f.s.input.nowNs));
 auto pair=MagazineDetachPackedPair(auth,auth,true,1,3,true,f.s.input.nowNs);CHECK(pair);
 f.Next();CHECK(f.Commit());CHECK(f.gate.Finish(f.s,*pair));
 CHECK(!f.gate.BlocksActions()&&f.gate.Returned()==1&&f.gate.Recovered()==0&&original.rounds==30);
 CHECK(f.s.reserve.loaded==30&&f.s.reserve.reserve==191);CHECK(!f.gate.Finish(f.s,*pair));return 0;}
int PairCannotRenewOrCrossOperation(){Fixture f;CHECK(f.gate.Begin(f.s)&&f.Commit());CHECK(f.gate.Return(f.s,f.Seat()));
 f.Next();CHECK(f.Commit());auto auth=*f.gate.Authorization();auto pair=MagazineDetachPackedPair(auth,auth,true,1,3,true,f.s.input.nowNs);CHECK(pair);
 for(unsigned bad=0;bad<8;++bad){auto forged=*pair;if(bad==0)++forged.authorization.request;if(bad==1)++forged.authorization.original.item.generation;
  if(bad==2)++forged.authorization.suppression.nativeTick;if(bad==3)++forged.authorization.current.identity.serverItem;
  if(bad==4)forged.copyMask=1;if(bad==5)forged.attached=false;if(bad==6)forged.drawSerial=0;if(bad==7)++forged.authorization.baseline.reserve;
  CHECK(!f.gate.Finish(f.s,forged));}
 f.s.input.nowNs=pair->deadlineNs;CHECK(!f.gate.Finish(f.s,*pair)&&f.gate.BlocksActions());return 0;}
int ChangedAmmoRequiresRecoveryNotOriginalSuccess(){Fixture f;CHECK(f.gate.Begin(f.s)&&f.Commit());CHECK(f.gate.Return(f.s,f.Seat()));
 --f.s.reserve.loaded;f.Next();CHECK(!f.Commit());f.gate.Invalidate();CHECK(f.gate.Phase()==MagazineDetachPhase::Recovering);
 CHECK(f.Commit());const auto auth=*f.gate.Authorization();auto pair=MagazineDetachPackedPair(auth,auth,true,1,3,true,f.s.input.nowNs);CHECK(pair);
 f.Next();CHECK(f.Commit()&&f.gate.Finish(f.s,*pair));CHECK(f.gate.Returned()==0&&f.gate.Recovered()==1&&f.s.reserve.loaded==29);
 return 0;}
int NewOwnerRetiresOnlyOldPresentation(){for(unsigned kind=0;kind<4;++kind){Fixture f;CHECK(f.gate.Begin(f.s)&&f.Commit());
 auto& owner=f.s.reserve.identity.owner;++owner.equipGeneration;
 if(kind==0)owner.weapon=0x140000; // Ordinary SPAS switch: no XM8 mesh/family evidence.
 if(kind==1){owner.weapon=0x150000;owner.soldier=0x160000;owner.weak=0x170000;++owner.actorGeneration;} // Seat/new actor.
 if(kind==2)++owner.space; // Recenter/tracking generation invalidates old private poses.
 if(kind==3){owner.soldier=0x180000;owner.weak=0x190000;++owner.actorGeneration;}
 f.s.input.owner.actor=(std::uint64_t(owner.weak)<<32)|owner.soldier;f.s.input.owner.actorGeneration=owner.actorGeneration;
 f.s.input.owner.space=owner.space;f.Next();f.s.family.verified=false;f.s.selected.reset();f.s.reserve.verified=false;
 const auto demand=f.gate.ReplacementDemand(owner,f.s.input,f.s.nativeTick,f.s.cache);CHECK(demand);
 auto receipt=f.Suppress(demand);CHECK(receipt);auto wrong=*receipt;++wrong.owner.equipGeneration;
 CHECK(!f.gate.RetireReplacedOwner(*demand,wrong)&&f.gate.BlocksActions());
 CHECK(f.gate.RetireReplacedOwner(*demand,*receipt)&&!f.gate.BlocksActions()&&f.gate.Recovered()==1&&f.gate.Returned()==0);
 CHECK(!f.gate.Authorization()&&f.s.reserve.loaded==30&&f.s.reserve.reserve==191);
 }return 0;}
struct ConsumerFixture:Fixture {
 Bc2MagazineDetached consumer{true};MagazinePhysicalSample physical{};MagazinePhysicalResult result{};
 std::optional<MagazineDetachPairReceipt> pair;bool returnPair=true,commitAllowed=true;
 bool preRead=true,postRead=true,invalidPre=false,oldPost=false;std::optional<HolsterSuppressionRequest> lastDemand;
 ConsumerFixture(int loaded=30,int reserve=191):Fixture(loaded,reserve){physical.nativeOwner=s.reserve.identity.owner;
  physical.weapon=s.family.binding.weapon;physical.asset=Xm8MagazineAsset;physical.meshes=selected;physical.trackingEpoch=s.input.owner.space;
  physical.raw.valid=true;physical.raw.owner=physical.nativeOwner;physical.raw.rigFingerprint=Xm8MagazineRig;
  physical.raw.weaponWorldMeters=reload_insertion_detail::Identity();physical.raw.nativeMagazineAttached=true;}
 void Send(bool grip=false,float z=.1f,bool delayed=false){const auto previous=s.input;const auto previousReserve=s.reserve;
  s.input.released[0]=!grip;Next();physical.input=s.input;physical.family=s.family;physical.gripPressed=grip;
  physical.raw.inputEvidence=delayed?previous:s.input;physical.originalHandEvidence=physical.raw.inputEvidence;
  const auto p=Xm8MagazineConfig().insertion;using namespace reload_insertion_detail;
  physical.raw.rawLeftWristWorldMeters=Multiply(p.itemFromHand,Multiply(*InverseRigid(p.itemFromInsertion),Multiply(TravelPose(p,z),p.weaponFromEntry)));
  auto pre=preRead?std::optional(s.reserve):std::nullopt;if(pre&&invalidPre)pre->verified=false;
  result=consumer.Prepare(physical,pre,s.nativeTick,s.cache,hands);
  const auto demand=consumer.Demand();lastDemand=demand;
  const auto receipt=commitAllowed&&demand?Suppress(demand):std::nullopt;
  result=consumer.Commit(receipt,pair,postRead?std::optional(oldPost?previousReserve:s.reserve):std::nullopt,hands,intent,s.input.nowNs,s.input.nowNs);
   if(returnPair&&result.tracking.detach&&result.tracking.target&&result.tracking.target->role==MagazinePropRole::Attached){
    const auto& auth=*result.tracking.detach;pair=MagazineDetachPackedPair(auth,auth,true,s.nativeTick,3,true,s.input.nowNs);
   }
 }
 bool FinishReturn(bool delayed=false){const auto before=consumer.Returned();
  for(float z:{.065f,.03f,-.005f,-.035f,-.035f,0.f,.035f,.07f,.1f,.1f,.1f,.1f,.1f,.1f}){
   Send(true,z,delayed);if(consumer.Returned()>before)return true;
  }for(unsigned n=0;n<25;++n){Send(true,.1f,delayed);if(consumer.Returned()>before)return true;}
  return false;
 }
 bool Return(bool delayed=false){Send();Send(true,.1f,delayed);return FinishReturn(delayed);}
};
int ActualControllerPullReturnUsesNoReloadOperation(){for(bool delayed:{false,true})for(const auto counts:{std::pair{30,191},std::pair{27,0},std::pair{30,0}}){
 ConsumerFixture f(counts.first,counts.second);f.Send();CHECK(MagazineTrackingFresh(f.result.tracking,f.s.input.nowNs));
 CHECK(!f.result.tracking.detach&&!f.result.tracking.target&&!f.result.blocksWeaponActions);CHECK(f.Return(delayed));
 CHECK(f.consumer.Returned()==1&&!f.consumer.BlocksActions()&&!f.result.blocksWeaponActions&&!f.result.reloadHeld);
 CHECK(f.s.reserve.loaded==counts.first&&f.s.reserve.reserve==counts.second&&f.result.acquired==0&&f.result.submitted==0&&f.result.completed==0);
 CHECK(!f.hands.Current(InteractionHand::Left));
 }return 0;}
int ActualControllerNoSuppressionOrPairNeverCompletes(){ConsumerFixture f;f.Send();f.commitAllowed=false;f.Send(true);
 CHECK(f.consumer.BlocksActions()&&!f.result.tracking.target&&!f.hands.Current(InteractionHand::Left));
 f.commitAllowed=true;f.returnPair=false;for(unsigned n=0;n<5;++n)f.Send();CHECK(f.consumer.BlocksActions()&&f.consumer.Returned()==0);
 f.returnPair=true;for(unsigned n=0;n<3;++n)f.Send();CHECK(!f.consumer.BlocksActions()&&f.consumer.Returned()==0);
 ConsumerFixture returnWait;returnWait.returnPair=false;CHECK(!returnWait.Return());
 CHECK(returnWait.consumer.Phase()==MagazineDetachPhase::Returning&&returnWait.consumer.BlocksActions());
 returnWait.returnPair=true;for(unsigned n=0;n<3;++n)returnWait.Send(true);
 CHECK(returnWait.consumer.Returned()==1&&!returnWait.consumer.BlocksActions());return 0;}
int NewSourceWatermarkRetiresBeforeNeutralRearm(){for(bool wasActive:{false,true})for(unsigned changed=0;changed<2;++changed){
 ConsumerFixture f;for(unsigned n=0;n<30;++n)f.Send();
 if(wasActive){f.Send(true);CHECK(f.consumer.BlocksActions()&&f.consumer.Grabs()==1);}
 else CHECK(f.Return());
 const auto oldReturns=f.consumer.Returned();auto& native=f.s.reserve.identity.owner;
 if(changed==0)++native.space;else{native.soldier=0x180000;native.weak=0x190000;++native.actorGeneration;}
 ++native.equipGeneration;f.s.input.owner.actor=(std::uint64_t(native.weak)<<32)|native.soldier;
 f.s.input.owner.actorGeneration=native.actorGeneration;f.s.input.owner.space=native.space;++f.s.input.owner.equipGeneration;
 f.s.input.sequence=0;f.s.family.binding.owner=native;++f.s.family.binding.weapon.generation;
 f.selected->owner=native;f.physical.nativeOwner=f.physical.raw.owner=native;f.physical.trackingEpoch=native.space;
 f.physical.weapon=f.s.family.binding.weapon;f.hands.Reset();f.gun.reset();f.pair.reset();
 f.Send(true);CHECK(!f.consumer.BlocksActions()&&!f.hands.Current(InteractionHand::Left));
 CHECK(f.consumer.Recovered()==unsigned(wasActive)&&f.consumer.Returned()==oldReturns);
 f.Send(true);CHECK(!f.consumer.BlocksActions()); // Held reconnect is not a new grab.
 CHECK(f.Return());CHECK(f.consumer.Returned()==oldReturns+1&&!f.consumer.BlocksActions());
 }return 0;}
int NormalRoutesPreservePhysicalCycleAndEmptyFallback(){
 Bc2MagazineDetached disabled,enabled{true};Fixture full,partial(27,191),empty(0,191),zero(27,0),zeroEmpty(0,0);
 // Native partial replacement/cancellation must finish before the full-mag
 // route can observe its completed capacity; zero cannot mint a loaded item.
 CHECK(!disabled.Routes(full.s.reserve,false)&&enabled.Routes(full.s.reserve,false));
 CHECK(!enabled.Routes(full.s.reserve,true)&&!enabled.Routes(partial.s.reserve,false));
 CHECK(!enabled.Routes(empty.s.reserve,false)&&!enabled.Routes(zeroEmpty.s.reserve,false));
 CHECK(enabled.Routes(zero.s.reserve,false)&&!enabled.Routes(zero.s.reserve,true));
 CHECK(!enabled.Routes(std::nullopt,false));
 full.s.reserve.allThreeIdle=false;CHECK(!enabled.Routes(full.s.reserve,false));
 full.s.reserve.allThreeIdle=true;full.s.reserve.verified=false;CHECK(!enabled.Routes(full.s.reserve,false));
 return 0;
}
int ActiveDetachCannotFallThroughDuringMissingNativeRead(){
 ConsumerFixture f;f.Send();f.Send(true);CHECK(f.consumer.BlocksActions()&&f.consumer.Grabs()==1);
 CHECK(f.consumer.Routes(std::nullopt,false)&&f.consumer.Routes(std::nullopt,true));
 Fixture partial(27,191);CHECK(f.consumer.Routes(partial.s.reserve,true));
 f.consumer.Invalidate();CHECK(f.consumer.Routes(std::nullopt,true));
 // Recovery restores the existing item, then normal partial/empty fallback
 // becomes eligible. It is never reported as a successful magazine return.
 for(unsigned n=0;n<4;++n)f.Send(false);
 CHECK(!f.consumer.BlocksActions()&&f.consumer.Recovered()==1&&f.consumer.Returned()==0);
 CHECK(!f.consumer.Routes(partial.s.reserve,false)&&f.consumer.Routes(f.s.reserve,false));
 CHECK(f.s.reserve.loaded==30&&f.s.reserve.reserve==191);
 return 0;
}
int SameOwnerCannotRewindRecognitionAfterReturn(){ConsumerFixture f;CHECK(f.Return());const auto grabs=f.consumer.Grabs();
 for(unsigned n=0;n<3;++n)f.Send(true);CHECK(f.consumer.Grabs()==grabs&&!f.consumer.BlocksActions());
 f.s.input.sequence=0;f.Send(false);f.Send(true);CHECK(f.consumer.Grabs()==grabs&&!f.consumer.BlocksActions());return 0;}
int FocusRecoverySuppressesActualCacheWithoutRenewingPose(){for(bool expired:{false,true}){
 ConsumerFixture f;f.Send();f.Send(true);CHECK(f.consumer.BlocksActions()&&f.consumer.Authorization());
 const auto original=*f.consumer.Original();const auto packet=f.s.input;f.consumer.Invalidate();
 f.s.input.focused=false;f.s.input.tracked={false,false};if(expired)f.s.input.nowNs=f.s.input.deadlineNs+1;
 ++f.s.nativeTick;
 const auto demand=f.consumer.RecoveryDemand(f.s.reserve.identity.owner,f.s.input,f.s.nativeTick,f.s.cache);CHECK(demand);
 CHECK(demand->input.sequence==packet.sequence&&demand->input.observedNs==packet.observedNs&&demand->input.deadlineNs==packet.deadlineNs);
 const auto receipt=f.Suppress(demand);CHECK(receipt&&!HolsterSuppressionCurrent(*receipt,*demand));
 unsigned fire=1,reload=~0u;std::memcpy(&fire,f.cache.data()+8+4*8,4);std::memcpy(&reload,f.cache.data()+0x98,4);
 CHECK(fire==0&&!(reload&(1u<<29))&&!f.consumer.Authorization()&&f.consumer.Original()==original);
 auto owner=f.s.reserve.identity.owner;++owner.equipGeneration;CHECK(!f.consumer.RecoveryDemand(owner,f.s.input,f.s.nativeTick,f.s.cache));
 f.s.input.focused=true;f.s.input.tracked={true,true};for(unsigned n=0;n<4;++n)f.Send(false);
 CHECK(!f.consumer.BlocksActions()&&f.consumer.Recovered()==1&&f.consumer.Returned()==0);
 CHECK(f.s.reserve.loaded==30&&f.s.reserve.reserve==191&&f.Return());
 }return 0;}
}

int MissingPreReadCanOnlyBridgeCommittedUnexpiredCounts(){
 ConsumerFixture f;f.Send();f.Send(true);CHECK(f.consumer.Grabs()==1&&f.consumer.Authorization());
 const auto previous=*f.consumer.Authorization();f.preRead=false;f.Send(true,.065f);
 CHECK(f.consumer.Phase()==MagazineDetachPhase::Detached&&f.lastDemand&&f.consumer.Recovered()==0);
 CHECK(f.consumer.Authorization()->current.sequence>previous.current.sequence);
 CHECK(f.consumer.Authorization()->current.observedNs==f.s.reserve.observedNs);
 CHECK(f.consumer.Authorization()->current.deadlineNs==f.s.reserve.deadlineNs);
 CHECK(f.consumer.Original()==std::optional(previous.original));
 f.preRead=true;CHECK(f.FinishReturn()&&f.consumer.Returned()==1&&f.consumer.Recovered()==0);
 CHECK(f.s.reserve.loaded==30&&f.s.reserve.reserve==191);return 0;
}
int MissingPostReadRepeatsOnlyOriginalVisualWithoutSeats(){
 ConsumerFixture f;f.Send();f.Send(true);const auto original=*f.consumer.Original();
 const auto auth=*f.consumer.Authorization();f.preRead=f.postRead=false;
 for(unsigned n=0;n<3;++n){f.Send(true);CHECK(f.lastDemand&&f.consumer.Phase()==MagazineDetachPhase::Detached);
  CHECK(f.result.tracking.target&&f.result.tracking.retainedVisualSuppression&&!f.result.interaction.insertion.seat);
  CHECK(f.result.tracking.detach->current.observedNs==auth.current.observedNs&&f.result.tracking.detach->current.deadlineNs==auth.current.deadlineNs);
  CHECK(f.consumer.Authorization()->current.observedNs==auth.current.observedNs);
  CHECK(f.consumer.Authorization()->current.deadlineNs==auth.current.deadlineNs);
  CHECK(f.consumer.Original()==std::optional(original)&&f.consumer.Seats()==0&&f.consumer.Returned()==0);}
 f.preRead=f.postRead=true;CHECK(f.FinishReturn());return 0;
}
int MissingPreReadCannotStartOrRenewExpiredEvidence(){
 ConsumerFixture idle;idle.Send();idle.preRead=false;idle.Send(true);CHECK(idle.consumer.Grabs()==0&&!idle.consumer.BlocksActions()&&!idle.lastDemand);
 ConsumerFixture f;f.Send();f.Send(true);const auto old=*f.consumer.Authorization();f.preRead=f.postRead=false;
 for(unsigned n=0;n<12;++n)f.Send(true);
 CHECK(f.consumer.Phase()==MagazineDetachPhase::Recovering&&!f.consumer.Authorization());
 CHECK(f.consumer.Seats()==0&&f.consumer.Returned()==0&&!f.result.tracking.target);
 CHECK(f.lastDemand&&f.lastDemand->input.deadlineNs==old.suppression.input.deadlineNs);
 f.preRead=f.postRead=true;for(unsigned n=0;n<3;++n)f.Send(false);
 CHECK(!f.consumer.BlocksActions()&&f.consumer.Recovered()==1&&f.consumer.Returned()==0);return 0;
}
int PresentInvalidOrChangedEvidenceIsNeverReplacedByCache(){
 for(unsigned bad=0;bad<3;++bad){ConsumerFixture f;f.Send();f.Send(true);
  if(bad==0)f.invalidPre=true;
  if(bad==1)--f.s.reserve.loaded;
  if(bad==2)f.s.reserve.allThreeIdle=false;
  f.Send(true);CHECK(f.consumer.Phase()==MagazineDetachPhase::Recovering&&f.consumer.Returned()==0);
 }
 ConsumerFixture post;post.Send();post.Send(true);post.preRead=false;--post.s.reserve.loaded;post.Send(true);
 CHECK(post.consumer.Phase()==MagazineDetachPhase::Recovering&&!post.result.tracking.target&&post.consumer.Returned()==0);
 return 0;
}

int MissingPreWithOldPostCannotAuthorizeNewPose(){
 ConsumerFixture f;f.Send();f.Send(true);f.preRead=false;f.oldPost=true;f.Send(true);
 CHECK(f.lastDemand&&f.consumer.Phase()==MagazineDetachPhase::Recovering);
 CHECK(!f.consumer.Authorization()&&!f.result.tracking.target&&!f.result.interaction.insertion.seat&&f.consumer.Returned()==0);
 return 0;
}
int MissingPreDuringOwnerReplacementRetiresOnlyWithCurrentSuppression(){
 ConsumerFixture f;f.Send();f.Send(true);const auto original=*f.consumer.Original();
 auto& owner=f.s.reserve.identity.owner;owner.weapon=0x140000;++owner.equipGeneration;
 ++f.s.input.owner.equipGeneration;f.physical.nativeOwner=owner;f.s.family.verified=false;f.physical.meshes.reset();
 f.preRead=f.postRead=false;f.commitAllowed=false;f.Send(true);
 CHECK(f.lastDemand&&f.lastDemand->owner==owner&&f.consumer.BlocksActions()&&f.consumer.Original()==std::optional(original));
 CHECK(!f.result.tracking.target&&f.consumer.Returned()==0&&f.consumer.Recovered()==0);
 f.commitAllowed=true;f.Send(true);CHECK(!f.consumer.BlocksActions()&&!f.consumer.Original());
 CHECK(f.consumer.Recovered()==1&&f.consumer.Returned()==0&&!f.result.tracking.target);return 0;
}
int main(){for(const auto fn:{MissingPreWithOldPostCannotAuthorizeNewPose,MissingPreDuringOwnerReplacementRetiresOnlyWithCurrentSuppression,MissingPreReadCanOnlyBridgeCommittedUnexpiredCounts,MissingPostReadRepeatsOnlyOriginalVisualWithoutSeats,MissingPreReadCannotStartOrRenewExpiredEvidence,PresentInvalidOrChangedEvidenceIsNeverReplacedByCache,FullAndZeroReserveRequireActualCommit,NoReloadHoldOrPartialAuthority,SameOriginalReturnsOnlyAfterCurrentAttachedPair,
 PairCannotRenewOrCrossOperation,ChangedAmmoRequiresRecoveryNotOriginalSuccess,NewOwnerRetiresOnlyOldPresentation,
 ActualControllerPullReturnUsesNoReloadOperation,ActualControllerNoSuppressionOrPairNeverCompletes,NewSourceWatermarkRetiresBeforeNeutralRearm,
 NormalRoutesPreservePhysicalCycleAndEmptyFallback,ActiveDetachCannotFallThroughDuringMissingNativeRead,SameOwnerCannotRewindRecognitionAfterReturn,FocusRecoverySuppressesActualCacheWithoutRenewingPose})if(const auto result=fn())return result;
 std::cout<<"BC2 magazine detach: 19 actual-controller/committed-cache/idle/paired-restoration groups passed; runtime unverified\n";return 0;}
