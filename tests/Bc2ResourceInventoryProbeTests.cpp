#include "Test.h"
#include "ResourceInventoryNativeFixture.h"
#include "Bc2ResourceInventoryProbe.h"
#include "Bc2PumpCalibration225.h"
#include "Bc2PumpActions.h"
#include "fvr/interaction/ControllerInput.h"
#include "fvr/interaction/TrackedRig.h"
#include "fvr/interaction/ReloadGrip.h"
#include "fvr/interaction/MechanismAimContinuation.h"
#include <iostream>
#include <sstream>
#include <functional>
#include <memory>
using namespace fvr;using namespace bc2;using namespace interaction;using namespace reload_insertion_detail;
namespace {
constexpr std::int64_t Ms=1000000;
auto Pose(float x=0,float y=0,float z=0){auto p=Identity();p.values[3]={x,y,z,1};return p;}
auto Matrix(const math::Pose& p){return *InverseRigid(*math::MakeLhViewFromOpenXRPose(p));}
using NativeInventory=resource_inventory_test::NativeInventory;
// All acknowledgement producers below are test-only native/renderer boundaries.
// The input driver has no pointer to them; production policies stay persistent.

// Synthetic native pump boundary, independent of the input coordinator. All
// physical hand/cycle/support consumers and action mapping below are real.
struct PumpNativeBoundary {
 Bc2NativeCycleView view;Bc2NativeCycleControl control;std::optional<WeaponCycleRelease> pending;
 std::uint64_t sequence=0;std::int64_t firedAt=0,releasedAt=0;unsigned releases=0,acks=0;
 Bc2PhysicalPumpApi Api(){return {this,
  [](void* p,const Bc2NativeCycleControl& c)noexcept{auto& n=*static_cast<PumpNativeBoundary*>(p);n.control=c;
   if(c.release){if(n.pending||!n.view.held||c.release->cycle!=*n.view.held)return false;
    n.pending=c.release;n.releasedAt=c.input.nowNs;++n.releases;n.view.phase=Bc2NativeCyclePhase::Releasing;n.view.held.reset();}return true;},
  [](void* p,std::int64_t)noexcept->std::optional<Bc2NativeCycleView>{return static_cast<PumpNativeBoundary*>(p)->view;},
  [](void* p,const WeaponCycleReady& r)noexcept{auto& n=*static_cast<PumpNativeBoundary*>(p);
   if(!n.view.ready||n.view.ready->release!=r.release)return false;++n.acks;n.view.ready.reset();n.pending.reset();n.view.blocksFire=false;return true;},
  [](void* p)noexcept{auto& n=*static_cast<PumpNativeBoundary*>(p);if(n.view.blocksFire){n.view.phase=Bc2NativeCyclePhase::Cancelled;n.view.held.reset();}}};}
 void Tick(const ReloadHoldIdentity& native,int loaded,int reserve,std::int64_t now){
  if(firedAt&&now-firedAt>=120*Ms){firedAt=0;view.phase=Bc2NativeCyclePhase::Held;view.blocksFire=true;
   ++view.cycle;++view.shot;view.native=native;view.loaded=loaded;view.reserve=reserve;view.capacity=8;}
  if(view.phase==Bc2NativeCyclePhase::Held)view.held=WeaponCycleLease{control.input.owner,control.item,control.mechanism,view.cycle,view.shot,++sequence,now-1,now+50*Ms,true};
  if(pending&&now-releasedAt>=200*Ms){view.phase=Bc2NativeCyclePhase::Complete;if(!view.ready)view.ready=WeaponCycleReady{*pending,++sequence,now,now+50*Ms,true,true};}
 }
};
struct Loop {
 NativeInventory memory;Bc2BodyInventory inventory{true};Bc2BodyHolster holster{BodyInventoryHolsterAcceptance};
 AmmoResourceService service;AmmoResourceChannel channel;HandInteraction hands;ControllerActions actions;TrackedRig rig;
 std::optional<Bc2MagazinePhysicalReload> magazine;std::optional<Bc2PhysicalReload> shell;Bc2ResourceInventoryProbe driver;
 MagazinePhysicalResult magResult;PhysicalReloadResult shellResult;BodyHolsterResult bodyResult;ResourceInventoryObservation observed;
 ReloadHoldIdentity native;AmmoResourceBinding binding;AmmunitionCounts rifle{22,191,30};int shellLoaded=8,shellReserve=24;
 std::uint64_t sequence=0,intent=0,nativeSequence=0,invocation=0,physicalEquip=17,cycle=0;
 std::int64_t now=1000*Ms,shotAt=0,started=0,submittedAt=0,cancelledAt=0;unsigned calls=0,shots=0,shellSubmits=0,mappings=0,selectionCalls=0;
 bool shellApplied=false,shellAcked=false,allowSeatReceipt=true,allowVisibility=true,allowSelect=true,allowShot=true,allowRetirement=true,earlyMapping=false;
 std::optional<Bc2ReloadNativeRequest> shellRequest;std::vector<Bc2ReloadAckEvidence> shellReceipts;
 std::vector<AmmunitionReceipt> magReceipts;std::optional<AmmoResourceNativeCall> pending;
 HandInteractionSample hand;HandInteractionKey gun;InputFrame input;std::shared_ptr<SelectedMeshesSnapshot> meshes;
 MagazineRawContact magRaw;ReloadRawContact shellRaw;std::array<std::byte,InputBytes> cache{};
 std::optional<unsigned> selectedNext;unsigned selected=0;const MagazineEquipmentProfile* profile=&Xm8MagazineEquipment();
 std::int64_t processingNow=0;bool advancingShellClock=false,shellPairObserved=false,shellPairLost=false;
 bool refreshOwnershipClock=false,expireAfterAdoption=false,sourceRestamped=false;
 unsigned queuedSupportRejected=0,queuedSupportRetained=0;HandInteractionReason queuedSupportReason=HandInteractionReason::None;
 bool splitShellTransfer=false;unsigned submittedCohortWaits=0;
 bool manualPump=false,allowPumpPairs=true,allowPumpSupport=true,allowShellSupport=true,allowRifleShot=true;
 // Automatic native fire is deliberately independent of diagnostic phases.
 bool singleFireMagazine=false;
 bool loseSupportOnMappedFire=false;unsigned lateCancelledMappedFire=0;
 bool automaticRifle=false;std::int64_t rifleInterval=70*Ms,lastRifleFire=0,secondPublicationDelay=0,publicationUntil=0;
 std::optional<AmmunitionCounts> delayedRifleCounts;std::function<void(Loop&)> beforeDriver;
 unsigned pumpSupports=0,shellSupports=0,rifleShots=0;bool pumpFirePose=false,pumpRightJump=false;std::optional<math::Pose> previousPumpRight;SupportGrip support;
 PumpNativeBoundary pumpNative;std::optional<Bc2PhysicalPump> pump;Bc2PumpRawContact pumpRaw;Bc2PumpPackCounters pumpPacks;
 Bc2PumpCalibration calibration=MeasuredSpasPump225();Bc2PhysicalPumpResult pumpResult;
 MechanismAimContinuation pumpAim;InputFrame renderedInput{};bool guidePumpRendering=false;
 explicit Loop(bool withPump=false,unsigned pumpCycles=1,Bc2PumpControllerProbe::Scenario scenario=Bc2PumpControllerProbe::Scenario::None):manualPump(withPump){
  if(manualPump){if(!driver.EnablePump(calibration,scenario,pumpCycles))throw std::runtime_error("pump enable");pump.emplace(std::make_shared<Bc2PumpCalibration>(calibration),pumpNative.Api());}
  memory.Word(NativeInventory::ad+0x64,0x15000);memory.Word(NativeInventory::bd+0x64,0x15200);
  native={memory.owner,{0x50000,0x60000,0x70000},0x80000,0x90000,0xa0000};
  MagazinePhysicalApi ma;ma.context=this;
  ma.resourceRead=[](void* p,const ReloadStateOwner& o,std::int64_t n)noexcept{return static_cast<Loop*>(p)->channel.Read(o,n);};
  ma.resourceSubmit=[](void* p,const AmmoResourceRequest& r)noexcept{return static_cast<Loop*>(p)->channel.Submit(r);};
  ma.resourceOutcome=[](void* p,std::uint64_t id,const AmmoResourceContext& c)noexcept{return static_cast<Loop*>(p)->channel.Outcome(id,c);};
  auto pouch=ChestAmmoSupply();const auto ids=Bc2MagazinePhysicalReload::DefaultPouch();pouch.itemNamespace=ids.itemNamespace;pouch.pouch=ids.pouch;
  magazine.emplace(true,ma,pouch);magazine->EnableBodyAmmo();
  PhysicalReloadApi sa;sa.context=this;sa.clock=[](void* p)noexcept{auto& f=*static_cast<Loop*>(p);
   if(f.advancingShellClock&&!f.Rifle()&&(f.driver.State()==Bc2ResourceInventoryProbe::Phase::Shell||f.driver.State()==Bc2ResourceInventoryProbe::Phase::ShellSupport))return f.processingNow+=100;return f.now;};
  sa.reserve=[](void* p)noexcept{return static_cast<Loop*>(p)->Reserve();};
  sa.reserveObserved=[](void* p)noexcept{auto& f=*static_cast<Loop*>(p);
   if(f.splitShellTransfer&&f.submittedAt&&f.now-f.submittedAt>=180*Ms&&f.now-f.submittedAt<220*Ms){
    ++f.submittedCohortWaits;return ReloadReserveObservation{ReloadObservationResult::CohortGap,{}};}
   return ReloadReserveObservation{ReloadObservationResult::Available,f.Reserve()};};
  sa.identity=[](void* p)noexcept->std::optional<ReloadHoldIdentity>{return static_cast<Loop*>(p)->native;};
  sa.start=[](void* p,const ReloadCycleControl& c)noexcept{auto& f=*static_cast<Loop*>(p);if(f.manualPump&&f.pumpNative.view.blocksFire)return false;
    if(f.manualPump){f.pumpNative.view.phase=Bc2NativeCyclePhase::Watching;f.pumpNative.view.held.reset();}
    f.cycle=c.cycle;f.started=f.now;return true;};
  sa.keep=[](void*,const ReloadCycleControl&)noexcept{return true;};
  sa.lease=[](void* p,const ReloadHoldIdentity& id,std::uint64_t c)noexcept->std::optional<ReloadRoundLease>{auto& f=*static_cast<Loop*>(p);
    if(!f.started||f.now-f.started<150*Ms)return {};return ReloadRoundLease{id,c,f.sequence,f.now,f.now+((f.splitShellTransfer&&f.submittedAt)?15:100)*Ms,f.shellLoaded,f.shellReserve,8,true,!f.submittedAt||f.shellApplied};};
  sa.submit=[](void* p,const Bc2ReloadNativeRequest& r)noexcept{auto& f=*static_cast<Loop*>(p);++f.shellSubmits;f.shellRequest=r;f.submittedAt=f.now;f.shellApplied=f.shellAcked=false;return true;};
  sa.ack=[](void* p,const ReloadHoldIdentity& id,std::uint64_t c)noexcept->std::optional<Bc2ReloadAckEvidence>{auto& f=*static_cast<Loop*>(p);
    if(!f.shellApplied||f.shellAcked)return {};const auto& r=f.shellRequest->request;
    Bc2ReloadAckEvidence a{{{r.id,r.owner,r.operation,ReloadAcknowledgement::Applied},id,c,f.sequence,200+f.shellSubmits},f.now,f.now+100*Ms,true};
    f.shellAcked=true;f.shellReceipts.push_back(a);return a;};
  sa.cancel=[](void* p)noexcept{auto& f=*static_cast<Loop*>(p);f.started=0;f.cancelledAt=f.now;};
  sa.retire=[](void* p,const ReloadHoldIdentity& id,std::uint64_t cycle)noexcept->std::optional<ReloadCycleRetirement>{auto& f=*static_cast<Loop*>(p);
    if(!f.allowRetirement||!f.cancelledAt||f.now-f.cancelledAt<300*Ms||id!=f.native||cycle!=f.cycle)return {};
    return ReloadCycleRetirement{id,cycle,1000+cycle,f.cancelledAt+300*Ms,f.cancelledAt+500*Ms,true};};
  shell.emplace(true,sa,ChestAmmoSupply());shell->EnableBeltAmmo(true,SupplyAnchorFrame::RecenteredBody);
 }
 bool Rifle()const{return selected==0;}
 std::string_view Asset()const{return Rifle()?Xm8MagazineAsset:SpasReloadAsset;}
 std::optional<Bc2AmmoReserveLease> Reserve(){
  if(Rifle())return {};
  if(submittedAt&&!shellApplied&&now-submittedAt>=(splitShellTransfer?220:180)*Ms){++shellLoaded;--shellReserve;shellApplied=true;}
  return Bc2AmmoReserveLease{native,++nativeSequence,now,now+100*Ms,shellLoaded,shellReserve,8,true,true,(!manualPump||!pumpNative.view.blocksFire)&&now-shotAt>=1400*Ms&&!started&&(!cancelledAt||now-cancelledAt>=300*Ms)};
 }
 void Metadata(){meshes=std::make_shared<SelectedMeshesSnapshot>();meshes->owner=native.owner;meshes->sequence=sequence;
  meshes->observedNs=now;meshes->deadlineNs=now+100*Ms;meshes->weaponData=Rifle()?NativeInventory::ad:NativeInventory::bd;
  meshes->stateCount=1;meshes->soleConfiguredArray=0x45000;meshes->states[0].array=0x45000;meshes->states[0].count=Rifle()?2:1;
  const auto name=Asset();std::memcpy(meshes->weaponName.data(),name.data(),name.size());auto& m=meshes->states[0].meshes[0];
  m.kind=Rifle()?SelectedMeshKind::Xm8:SelectedMeshKind::Spas12;m.address=0x46000;m.namePointer=0x47000;m.typeInfo=0x48000;
  const auto path=Rifle()?profile->geometry->mesh:SpasReloadMesh;std::memcpy(m.assetPath.data(),path.data(),path.size());
  if(Rifle()){auto& optic=meshes->states[0].meshes[1];optic.kind=SelectedMeshKind::Acog4x;optic.address=0x46100;optic.namePointer=0x47100;optic.typeInfo=0x48100;
   constexpr char path[]="Objects/Weapons/Unlock/ACOG_4X/US_ACOG_4X_Mesh";std::memcpy(optic.assetPath.data(),path,sizeof(path));}
 }
 BodyHolsterSample BodySample(){BodyHolsterSample s;s.nativeOwner=native.owner;s.hand=hand;s.gun=gun;s.cache=0x49000;s.nativeTick=sequence;s.selected=meshes;
  if(allowVisibility&&bodyResult.visibility.enabled){const auto& v=bodyResult.visibility;auto p=std::make_shared<WeaponVisibilityPlan>();p->reason=WeaponVisibilityReason::None;
   p->rig.soldier=v.nativeOwner.soldier;p->rig.weak=v.nativeOwner.weak;p->rig.count=147;p->nativeOwner=v.nativeOwner;p->request=v.request;p->hidden=v.hide;
   p->inputSequence=v.input.sequence;p->physicalEquipGeneration=v.input.owner.equipGeneration;p->meshSequence=v.selected->sequence;p->selected=v.selected;
   p->observedNs=v.input.observedNs;p->deadlineNs=v.input.deadlineNs;p->inputDeadlineNs=v.input.deadlineNs;
   s.visibility=WeaponVisibilityReceipt{v.nativeOwner,p->rig,v.request,v.input.sequence,v.input.owner.equipGeneration,100+sequence,now,p->deadlineNs,3,v.hide,p};}
  if(const auto request=holster.Demand(s)){HolsterInputOverride patch;if(patch.Apply(cache,*request,{nullptr,[](void*,const HolsterSuppressionRequest&)noexcept{return true;}}))s.suppression=patch.Commit();}
  return s;
 }
 AmmunitionSnapshot Snapshot(){return {binding.context,++nativeSequence,now,now+100*Ms,delayedRifleCounts&&now<publicationUntil?*delayedRifleCounts:rifle,true};}
 AmmoResourceOwnUpdate Row(unsigned branch,const AmmoResourceNativeCall& c){AmmoResourceOwnUpdate r;r.beforeOwner=r.afterOwner=c.identity.owner;r.firing=c.identity.firing[branch];
  r.serverPlayer=c.identity.serverPlayer;r.serverSoldier=c.identity.serverSoldier;r.serverItem=c.identity.serverItem;r.invocation=c.invocation+100+branch;
  r.beginNs=now;r.endNs=now+10+branch;r.branch=branch;r.depth=1;r.loadedBefore=r.loadedAfter=c.after.loaded;r.reserveBefore=r.reserveAfter=c.after.reserve;
  r.current=r.next=2;r.finished=r.identityRetained=r.neutralContext=true;return r;}
 void NativeDrain(){if(const auto r=channel.Take())if(const auto c=service.Accept(*r,now))if(service.Dispatch(Snapshot(),now)){
   ++calls;pending=AmmoResourceNativeCall{native,c->id,(invocation+=10),now+1,now+2,c->before,c->after,true,true,true};rifle=c->after;service.Call(*pending);}
  if(pending&&(allowSeatReceipt||calls%2)){auto r=Row(2,*pending);r.invocation=pending->invocation;r.beginNs=pending->beginNs-1;r.endNs=pending->endNs+10;
   r.loadedBefore=pending->before.loaded;r.reserveBefore=pending->before.reserve;service.Observe(r,now+100);
   for(unsigned branch:{0,1,2})service.Observe(Row(branch,*pending),now+100);
   if(const auto outcome=service.Outcome();outcome&&outcome->receipt)magReceipts.push_back(*outcome->receipt);pending.reset();}
  channel.Publish(service.View(),service.Outcome());
 }
 void PublishBody(const BodyHolsterSample& sample){auto b=std::make_shared<BodyHolsterProbeSample>();b->nativeOwner=native.owner;b->sampledNs=now;b->input=input;b->hand=hand;b->physicalGun=gun;
  b->nativeTick=sequence;b->request=holster.RequestId();b->phase=holster.Phase();b->selectedSlot=inventory.AssignedSlot(native.owner.weapon);
  b->left=hands.Current(InteractionHand::Left);b->right=hands.Current(InteractionHand::Right);b->outcome=bodyResult;b->visibility=sample.visibility;b->suppression=sample.suppression;
  b->queuedTarget=selectedNext?(*selectedNext?NativeInventory::b:NativeInventory::a):0;observed.body=b;observed.display=inventory.Display(now);
 }
 void Tick(unsigned fault=0){now+=10*Ms;processingNow=now;++sequence;
  if(selectedNext){selected=*selectedNext;selectedNext.reset();memory.Select(selected);native.owner=memory.owner;++physicalEquip;magRaw={};shellRaw={};pumpRaw={};}
  input={};input.generation=sequence;input.spaceGeneration=native.owner.space;input.predictedNs=now;input.focused=input.headValid=true;
  for(auto& h:input.hands){h.active=Components;h.gripTracked=h.aimTracked=true;}input.hands[0].grip.position={-.2f,-.25f,-.45f};input.hands[1].grip.position={.15f,-.1f,-.2f};
  observed.reserve=Reserve();observed.magazine=magazine->ResourceProbeState(now);if(!observed.magazine.resource)observed.magazine.resource=channel.Read(native.owner,now);
  observed.shell=shell->ProbeState(now);observed.shellBlocks=shell->BlocksEquipment();observed.magazineRaw=magRaw;observed.shellRaw=shellRaw;if(manualPump){observed.pumpRaw=pumpRaw;observed.pumpNative=pumpNative.view;}
  if(fault==1)input.focused=false;if(fault==2)++native.owner.weak;
  if(earlyMapping){actions.Update(input,{native.owner.soldier,native.owner.equipGeneration,true,true},now);++mappings;}
  if(beforeDriver)beforeDriver(*this);
  driver.Prepare(input,native.owner,Asset(),observed,now,now+100*Ms,now);
  if(manualPump){const auto phase=driver.State();
   if(phase==Bc2ResourceInventoryProbe::Phase::Pump||phase==Bc2ResourceInventoryProbe::Phase::Shell||
      phase==Bc2ResourceInventoryProbe::Phase::ShellSupport||phase==Bc2ResourceInventoryProbe::Phase::ShellSettle){
    if(previousPumpRight){const auto a=previousPumpRight->position,b=input.hands[1].grip.position;
     if(std::hypot(a.x-b.x,a.y-b.y,a.z-b.z)>.0151f)pumpRightJump=true;}
    previousPumpRight=input.hands[1].grip;
   }
  }
  auto action=actions.Update(input,{native.owner.soldier,native.owner.equipGeneration,!driver.CancelConsumer(),true},now);++mappings;
  const bool pumpDebt=manualPump&&(pumpNative.view.blocksFire||pump->BlocksFire());ApplyPumpActionGate(action,pumpDebt);
  hand={{(std::uint64_t(native.owner.weak)<<32)|native.owner.soldier,native.owner.actorGeneration,physicalEquip,native.owner.space},sequence,now,now+100*Ms,now,input.focused,{true,true},{input.hands[0].squeeze<=.35f,false}};
  gun={Rifle()?0x15000u:native.owner.weapon,physicalEquip};hands.Update(hand);const HandContactProof proof{{1002,1},sequence,hand.deadlineNs,true};
  if(const auto g=hands.Current(InteractionHand::Right))hands.Renew(hand,g->token,proof);
  else if(bodyResult.allowAutomaticGunHold&&!holster.BlocksActions())hands.Acquire(hand,{hand.owner,InteractionHand::Right,HandClaimKind::GunHold,gun,proof,++intent,0});
  Metadata();
  MagazinePhysicalSample m;m.nativeOwner=native.owner;m.input=hand;m.weapon=gun;m.geometrySequence=sequence;m.trackingEpoch=native.owner.space;
  m.asset=Asset();m.meshes=meshes;m.raw=magRaw;if(magRaw.valid)m.originalHandEvidence=magRaw.inputEvidence;
  m.bodyFromHand=*BodyAnchorHandPose(input,InteractionHand::Left);m.gripPressed=ReloadGripActive(input.hands[0].squeeze,hand,hands.Current(InteractionHand::Left));
  m.cancel=pumpDebt||holster.BlocksActions()||shell->BlocksEquipment()||driver.CancelConsumer();m.actionFlagsKnown=true;m.actionHeld=action.held;m.actionPressed=action.pressed;
  if(Rifle()){m.family.binding={native.owner,gun,NativeInventory::inventory,0xc0000,2,profile};m.family.verified=true;m.family.observedNs=now;m.family.deadlineNs=now+100*Ms;
   if(const auto b=inventory.ResourceBinding(native.owner,now)){binding=*b;service.Select(binding,native,Snapshot(),now);channel.Publish(service.View(),service.Outcome());}}
  else channel.Publish({},service.Outcome());
  magResult=magazine->Tick(m,hands,intent);
  PhysicalReloadSample sh;sh.nativeOwner=native.owner;sh.input=hand;sh.weapon=gun;sh.geometrySequence=sequence;sh.trackingEpoch=native.owner.space;
  sh.asset=Asset();sh.meshes=meshes;sh.raw=shellRaw;if(shellRaw.valid)sh.originalHandEvidence=shellRaw.inputEvidence;sh.bodyFromHand=m.bodyFromHand;sh.gripPressed=m.gripPressed;
  sh.cancel=pumpDebt||holster.BlocksActions()||magResult.blocksWeaponActions||driver.CancelShell()||((action.held|action.pressed)&Fire);sh.actionFlagsKnown=true;sh.actionHeld=action.held;sh.actionPressed=action.pressed;
  if(Rifle())sh.replacementFamily=m.family;shellResult=shell->Tick(sh,hands,intent);
  if(magResult.tracking.target&&MagazineTargetFresh(magResult.tracking,now)){++packs.pairs;packs.copies+=2;const auto role=unsigned(magResult.tracking.target->role);++packs.rolePairs[role];packs.roleCopies[role]+=2;}
  observed.magazine=magazine->ResourceProbeState(now);observed.shell=shell->ProbeState(now);observed.shellBlocks=shell->BlocksEquipment();driver.Observe(observed,packs,now);

  if(manualPump){
   pumpNative.Tick(native,shellLoaded,shellReserve,now);
   Bc2PhysicalPumpSample p;p.nativeOwner=native.owner;p.input=hand;p.item=gun;p.asset=Asset();p.raw=pumpRaw;
   p.grip=ReloadGripActive(input.hands[0].squeeze,hand,hands.Current(InteractionHand::Left));
   p.cancel=holster.BlocksActions()||shell->BlocksEquipment()||magResult.blocksWeaponActions||driver.CancelConsumer();
   const auto shellClaim=hands.Current(InteractionHand::Left),gunClaim=hands.Current(InteractionHand::Right);
   const bool shellPair=advancingShellClock&&shellClaim&&gunClaim&&shellClaim->token.kind==HandClaimKind::AmmoObject;
   pumpResult=pump->Tick(p,hands,intent);
   if(shellPair){shellPairObserved=true;const auto afterLeft=hands.Current(InteractionHand::Left),afterRight=hands.Current(InteractionHand::Right);
    if(!afterLeft||!afterRight||afterLeft->token!=shellClaim->token||afterRight->token!=gunClaim->token)shellPairLost=true;}
   ApplyPumpActionGate(action,pumpResult.blocksFire);ApplyPumpResourceHandoffGate(action,shell->BlocksEquipment()||magResult.blocksWeaponActions);
   if(allowPumpPairs&&pumpResult.tracking.target&&PumpTargetFresh(pumpResult.tracking,now)){++pumpPacks.pairs;pumpPacks.copies+=2;}
   driver.ObservePump(pumpResult,pumpNative.view,pumpPacks,now);
  }
  renderedInput=input;
  if(manualPump||singleFireMagazine){
   SupportGripOwner so{hand.owner.actor,hand.owner.actorGeneration,native.owner.weapon};
   const auto source=Rifle()?magRaw.inputEvidence:pumpRaw.valid?pumpRaw.input:shellRaw.inputEvidence;
   const auto inverse=InverseRigid(shellRaw.weaponWorldMeters);
   float distance=10;if(!Rifle()&&shellRaw.valid&&inverse)distance=Distance(Multiply(shellRaw.rawLeftWristWorldMeters,*inverse),calibration.closedWrist);
   if(Rifle()&&observed.supportVerified)distance=Distance(magRaw.rawLeftWristWorldMeters,observed.supportTarget);
   const SupportGripContact contact{Rifle()?observed.supportVerified: shellRaw.valid,distance,{}};auto proposed=support;
   const bool busy=pumpResult.ownsHand||shellResult.ammoOwnsHand||magResult.ownsLeftHand||bodyResult.blockWeaponActions;
   std::optional<InputFrame> returnInput;
   const auto returningGun=hands.Current(InteractionHand::Right);
   if(pump&&!Rifle()&&returningGun&&!busy&&!driver.CancelConsumer()&&!hands.Current(InteractionHand::Left))
    if(const auto submitted=pump->SupportReturnStarted(hand,*returningGun))returnInput=pumpAim.ApplyReturning(input,hand,*returningGun,*submitted);
   auto returned=pump?pump->ContinueSupport(so,returnInput?*returnInput:input,contact,source,{2,native.owner.weapon},hands,proposed,intent,busy||driver.CancelConsumer()):std::optional<SupportGripResult>{};
   if(returned)++pumpSupports;
   auto shellCurrent=hand;shellCurrent.nowNs=processingNow;
   auto shellReturned=returned?std::optional<SupportGripResult>{}:shell->ContinueSupport(so,input,contact,source,{2,native.owner.weapon},hands,proposed,intent,busy||driver.CancelConsumer(),shellCurrent);
   if(shellReturned)++shellSupports;
   auto supported=returned?*returned:shellReturned?*shellReturned:proposed.Update(so,input,contact,driver.CancelConsumer(),busy);
   if(shellReturned&&expireAfterAdoption)processingNow=hand.deadlineNs+1;
   const auto originalCurrent=hand,originalSource=source;
   // The queued ownership helper runs after actual consumer adoption. Only
   // processing time advances; no renderer or controller evidence is renewed.
   if(refreshOwnershipClock)hand.nowNs=processingNow;
   if(hand.sequence!=originalCurrent.sequence||hand.observedNs!=originalCurrent.observedNs||
      hand.deadlineNs!=originalCurrent.deadlineNs||source.sequence!=originalSource.sequence||
      source.observedNs!=originalSource.observedNs||source.deadlineNs!=originalSource.deadlineNs)sourceRestamped=true;
   if(supported.holding){const auto left=hands.Current(InteractionHand::Left),right=hands.Current(InteractionHand::Right);
    HandContactProof proof{{2,native.owner.weapon},source.sequence,source.deadlineNs,true};bool owned=false;
    if(left&&left->token.kind==HandClaimKind::WeaponSupport){const auto renewed=hands.RenewFrom(hand,source,left->token,proof);owned=renewed.accepted;
     if(shellReturned&&!owned){++queuedSupportRejected;queuedSupportReason=renewed.reason;
      if(hands.Current(InteractionHand::Left)&&hands.Current(InteractionHand::Left)->token==left->token)++queuedSupportRetained;}}
    else if(supported.engaged&&right){HandClaimRequest request{hand.owner,InteractionHand::Left,HandClaimKind::WeaponSupport,gun,proof,++intent,right->token.id};
     owned=(left?hands.TransferFrom(hand,source,left->token,request):hands.AcquireFrom(hand,source,request)).accepted;}
    if(!owned){supported=support.Update(so,input,contact,true);proposed=support;const auto c=hands.Current(InteractionHand::Left);if(c&&c->token.kind==HandClaimKind::WeaponSupport)hands.Release(hand,c->token);}
   }else {const auto c=hands.Current(InteractionHand::Left);if(c&&c->token.kind==HandClaimKind::WeaponSupport)hands.Release(hand,c->token);}
   support=proposed;if(pump)pump->BindSupport(pumpResult.tracking,supported,hands);
   if(manualPump&&!Rifle()){
    const auto left=hands.Current(InteractionHand::Left),right=hands.Current(InteractionHand::Right);
    if(supported.holding&&left&&right)pumpAim.Capture(input,supported.input,hand,*left,*right);
    else if(pumpResult.ownsHand&&left&&right){if(const auto guided=pumpAim.Apply(input,hand,*left,*right))supported.input=*guided;}
    else if(returnInput)supported.input=*returnInput;
    else pumpAim.Reset();
   }else pumpAim.Reset();
   renderedInput=!Rifle()&&guidePumpRendering?supported.input:input;
   if(loseSupportOnMappedFire&&calls>=4&&((action.held|action.pressed)&Fire)){
    const auto left=hands.Current(InteractionHand::Left);if(left)hands.Release(hand,left->token);
   }
   if((driver.State()!=Bc2ResourceInventoryProbe::Phase::Pump||allowPumpSupport)&&
      (driver.State()!=Bc2ResourceInventoryProbe::Phase::ShellSupport||allowShellSupport))
    driver.ObserveSupport(pumpResult.tracking,supported,hand,source,hands.Current(InteractionHand::Left),hands.Current(InteractionHand::Right),shellReturned.has_value(),now);
  }
  if(advancingShellClock)hand.nowNs=processingNow; // Body processing follows support ownership.
  BodyDrawSample d;d.owner=native.owner;d.hand=hand;d.input=input;d.gun=gun;d.visible=BodyVisibleRig{native.owner,sequence,now,now+100*Ms};
  d.reloadBusy=pumpDebt||magazine->BlocksEquipment()||shell->BlocksEquipment();d.interaction=((action.held|action.pressed)&Fire)?BodyInteractionState::Suspended:BodyInteractionState::Available;
  auto bs=BodySample();bodyResult=inventory.TickHolster(memory.Memory(),d,bs,holster,hands,intent);PublishBody(bs);
  if(allowSelect&&bodyResult.select){const auto read=ReadBodyInventory(memory.Memory(),native.owner,true);if(read.snapshot&&ResolveBodyDraw(*read.snapshot,unsigned(bodyResult.select->id))){selectedNext=bodyResult.select->id==NativeInventory::b?1:0;++selectionCalls;}}
  if(driver.CancelConsumer()&&((action.held|action.pressed)&Fire))++lateCancelledMappedFire;
  driver.FilterCancelledFire(action); // Same final boundary as native Gather.
  if(allowShot&&!Rifle()&&(action.pressed&Fire)&&!bodyResult.blockWeaponActions){--shellLoaded;++shots;shotAt=now;if(manualPump){pumpNative.firedAt=now;const auto p=input.hands[1].grip.position;
   pumpFirePose=std::hypot(p.x,p.y+.40f,p.z)<.001f;}}
  if((manualPump||singleFireMagazine)&&allowRifleShot&&Rifle()&&!bodyResult.blockWeaponActions&&
     ((action.pressed&Fire)||(automaticRifle&&(action.held&Fire)&&now-lastRifleFire>=rifleInterval))){
   const auto before=rifle;--rifle.loaded;++rifleShots;lastRifleFire=now;
   if(rifleShots==2&&secondPublicationDelay){delayedRifleCounts=before;publicationUntil=now+secondPublicationDelay;}
  }
  NativeDrain();
  const auto body=Pose(5,2,3),left=Multiply(Pose(-.2f,-.25f,.45f),body),right=Multiply(Pose(.15f,-.1f,.2f),body);
  std::array<ArmAnchor,2> arms{{{{4.8f,1.8f,3},{0,1,0}},{{5.2f,1.8f,3},{0,1,0}}}};
  if(const auto posed=rig.Update({1,2,3,4},renderedInput,body,left,right,right,arms)){auto attachment=Pose();attachment.values[1][1]=attachment.values[2][2]=-1;
   auto wrist=Multiply(attachment,Multiply(Matrix(input.hands[0].grip),body));wrist.values[3]=posed->left.values[3];
   if(Rifle()){magRaw={};magRaw.valid=true;magRaw.owner=native.owner;magRaw.rigFingerprint=profile->geometry->rigFingerprint;magRaw.inputEvidence=hand;
    magRaw.weaponWorldMeters=posed->weapon;magRaw.trackingBodyWorldMeters=body;magRaw.rawLeftWristWorldMeters=wrist;magRaw.nativeMagazineAttached=!service.View()||!service.View()->wellEmpty;}
   else {shellRaw={};shellRaw.valid=true;shellRaw.owner=native.owner;shellRaw.rigFingerprint=SpasReloadRig;shellRaw.inputEvidence=hand;
    shellRaw.weaponWorldMeters=posed->weapon;shellRaw.trackingBodyWorldMeters=body;shellRaw.rawLeftWristWorldMeters=wrist;
    if(manualPump&&PumpTrackingFresh(pumpResult.tracking,now)){pumpRaw={};pumpRaw.valid=pumpRaw.mappingValid=true;pumpRaw.nativeOwner=native.owner;
     pumpRaw.rigFingerprint=calibration.rigFingerprint;pumpRaw.input=hand;pumpRaw.pointWrist={};pumpRaw.contact=Multiply(wrist,*InverseRigid(posed->weapon));
     pumpRaw.trackingBodyWorldMeters=body;pumpRaw.weaponWorldMeters=posed->weapon;pumpRaw.rawWristWorldMeters=wrist;++pumpPacks.contacts;
    }else pumpRaw={};}}
 }
 MagazinePackCounters packs{};
 std::string Report()const{std::ostringstream o;driver.Report(o);return o.str();}
 bool Run(bool showFailure=true){for(unsigned n=0;n<(manualPump?5900u:5600u)&&!driver.Completed()&&!driver.CancelConsumer();++n)Tick();if(showFailure&&!driver.Completed())std::cerr<<Report()<<'\n';return driver.Completed();}
};

std::string ProbeReport(const Bc2ResourceInventoryProbe& driver){std::ostringstream out;driver.Report(out);return out.str();}
bool FirstPumpShot(Loop& f){
 for(unsigned n=0;n<5000&&!f.shots&&!f.driver.CancelConsumer();++n)f.Tick();
 return f.shots==1&&f.driver.State()==Bc2ResourceInventoryProbe::Phase::Pump&&!f.driver.CancelConsumer();
}
Bc2NativeCycleView UnfinishedView(const Loop& f){Bc2NativeCycleView v;
 v.native=f.native;v.phase=Bc2NativeCyclePhase::ShotObserved;v.blocksFire=true;v.cycle=v.shot=1;
 v.loaded=f.shellLoaded;v.reserve=f.shellReserve;v.capacity=8;return v;
}
ReloadStateOwner NewActor(const Loop& f){auto owner=f.native.owner;owner.weak+=4;++owner.actorGeneration;
 owner.weapon=NativeInventory::a;++owner.equipGeneration;return owner;}
int RestartEquipChurnWaitsForRealActor(){
 auto storage=std::make_unique<Loop>(true);auto& f=*storage;
 CHECK(f.driver.EnableActorReplacement()&&FirstPumpShot(f));
 f.driver.ObservePump({},UnfinishedView(f),{},f.now);CHECK(f.driver.ActorReplacementWitnessed());
 const auto calls=f.calls,shots=f.shots,submits=f.shellSubmits;
 // scene268-recovery-01: witness838 at99865784654300; frame1056 at
 // 99869189407700 keeps the actor/weapon/space and advances equip2->3.
 // The old fixture failed18 before actor2 existed. These measured deltas
 // replay that boundary through the same Prepare policy, without native calls.
 auto owner=f.native.owner;++owner.equipGeneration;auto input=f.input;input.generation+=218;
 auto now=f.now+3404753400ll,observed=now-10570600ll,deadline=observed+100*Ms;
 input.predictedNs=now;input.hands[0].squeeze=input.hands[1].squeeze=input.hands[1].trigger=1;
 f.driver.Prepare(input,owner,SpasReloadAsset,{},observed,deadline,now);
 CHECK(!f.driver.CancelConsumer()&&!f.driver.ActorReplacementResumed()&&f.driver.State()==Bc2ResourceInventoryProbe::Phase::Pump);
 CHECK(!input.hands[0].squeeze&&!input.hands[1].squeeze&&!input.hands[1].trigger);
 const auto pending=f.Report();CHECK(pending.find("\"prior_report\":null")!=std::string::npos);
 auto duplicate=input;f.driver.Prepare(duplicate,owner,SpasReloadAsset,{},observed,deadline,now+1);
 CHECK(f.Report()==pending&&!f.driver.CancelConsumer());
 CHECK(f.calls==calls&&f.shots==shots&&f.shellSubmits==submits&&!f.pumpNative.releases&&!f.pumpNative.acks);
 for(unsigned fault=0;fault<8;++fault){auto candidate=std::make_unique<Bc2ResourceInventoryProbe>(f.driver);
  auto wrong=owner;auto next=input;++next.generation;const auto t=now+10*Ms;next.predictedNs=t;
  if(fault==0)--wrong.equipGeneration;
  if(fault==1)++wrong.weapon;
  if(fault==2){++wrong.space;next.spaceGeneration=wrong.space;}
  if(fault==3)++wrong.weak;
  if(fault==4)++wrong.actorGeneration;
  if(fault==5)next.focused=false;
  if(fault==6)next.hands[0].gripTracked=false;
  candidate->Prepare(next,wrong,SpasReloadAsset,{},t,fault==7?t:t+100*Ms,t);
  CHECK(candidate->CancelConsumer()&&!candidate->ActorReplacementResumed());
 }
 // Before an unfinished-shot witness, equip churn is still strict.
 auto missing=std::make_unique<Loop>(true);CHECK(missing->driver.EnableActorReplacement()&&FirstPumpShot(*missing));
 auto changed=missing->native.owner;++changed.equipGeneration;auto next=missing->input;++next.generation;
 missing->driver.Prepare(next,changed,SpasReloadAsset,{},missing->now+10*Ms,missing->now+110*Ms,missing->now+10*Ms);
 CHECK(missing->driver.CancelConsumer()&&!missing->driver.ActorReplacementWitnessed());
 // Only the later genuine actor lifetime change restarts the input script.
 auto fresh=NewActor(f);fresh.equipGeneration=owner.equipGeneration+1;++input.generation;input.predictedNs=now+20*Ms;
 f.driver.Prepare(input,fresh,Xm8MagazineAsset,{},now+20*Ms,now+120*Ms,now+20*Ms);
 CHECK(f.driver.ActorReplacementResumed()&&!f.driver.CancelConsumer()&&f.driver.State()==Bc2ResourceInventoryProbe::Phase::Setup);
 CHECK(f.Report().find("\"prior_report\":"+pending)!=std::string::npos);
 CHECK(f.calls==calls&&f.shots==shots&&f.shellSubmits==submits&&!f.pumpNative.releases&&!f.pumpNative.acks);
 std::cout<<"Recorded checkpoint equip-only churn stays neutral with exact old witness; only replacement actor resumes input script\n";
 return 0;
}
int ReplacementDriverOptInAndEvidence(){
 auto storage=std::make_unique<Loop>(true);auto& f=*storage;
 CHECK(f.driver.EnableActorReplacement()&&!f.driver.EnableActorReplacement());CHECK(FirstPumpShot(f));
 CHECK(!f.driver.EnableActorReplacement());const auto witness=UnfinishedView(f);
 const auto base=std::make_unique<Bc2ResourceInventoryProbe>(f.driver);
 for(unsigned fault=0;fault<12;++fault){auto candidate=std::make_unique<Bc2ResourceInventoryProbe>(*base);auto v=witness;auto now=f.now;
  if(fault==0)v.native.owner.weak+=4;if(fault==1)v.cycle=0;if(fault==2)v.shot=0;
  if(fault==3)v.blocksFire=false;if(fault==4)v.phase=Bc2NativeCyclePhase::Complete;
  if(fault==5)v.loaded++;if(fault==6)v.reserve++;if(fault==7)v.capacity=30;
  if(fault==8)v.native.firing[1]=v.native.firing[0];if(fault==9)v.native.serverSoldier=0;
  if(fault==10)now+=100*Ms;if(fault==11)v.ready=WeaponCycleReady{};
  candidate->ObservePump({},v,{},now);CHECK(!candidate->ActorReplacementWitnessed());
 }
 f.driver.ObservePump({},witness,{},f.now);CHECK(f.driver.ActorReplacementWitnessed()&&!f.driver.ActorReplacementResumed());
 CHECK(f.calls==0&&f.shellSubmits==0&&f.shots==1); // No repeated rifle acceptance work.
 const auto report=f.Report();const auto calls=f.calls,shots=f.shots,submits=f.shellSubmits,releases=f.pumpNative.releases,acks=f.pumpNative.acks;
 auto duplicate=f.input;duplicate.hands[0].squeeze=duplicate.hands[1].squeeze=duplicate.hands[1].trigger=1;
 f.driver.Prepare(duplicate,f.native.owner,f.Asset(),f.observed,f.now,f.now+100*Ms,f.now+1);
 CHECK(!f.driver.CancelConsumer()&&!duplicate.hands[0].squeeze&&!duplicate.hands[1].squeeze&&!duplicate.hands[1].trigger);
 CHECK(f.Report()==report); // Same packet cannot create another witness or script action.
 auto next=f.input;++next.generation;next.predictedNs=f.now+10*Ms;
 f.driver.Prepare(next,f.native.owner,f.Asset(),f.observed,f.now+10*Ms,f.now+110*Ms,f.now+10*Ms);
 CHECK(!next.hands[0].squeeze&&!next.hands[1].squeeze&&!next.hands[1].trigger);
 CHECK(f.calls==calls&&f.shots==shots&&f.shellSubmits==submits&&f.pumpNative.releases==releases&&f.pumpNative.acks==acks);
 auto disabledStorage=std::make_unique<Bc2ResourceInventoryProbe>();auto& disabled=*disabledStorage;CHECK(!disabled.EnableActorReplacement());
 CHECK(disabled.EnablePump(f.calibration));
 // The same ordinary sequence and owner change retains the default failure.
 auto normal=std::make_unique<Loop>(true);CHECK(FirstPumpShot(*normal));
 normal->driver.ObservePump({},UnfinishedView(*normal),{},normal->now);CHECK(!normal->driver.ActorReplacementWitnessed());
 auto input=normal->input;++input.generation;input.predictedNs=normal->now+10*Ms;
 normal->driver.Prepare(input,NewActor(*normal),Xm8MagazineAsset,{},normal->now+10*Ms,normal->now+110*Ms,normal->now+10*Ms);
 CHECK(normal->driver.CancelConsumer()&&!normal->driver.ActorReplacementResumed());return 0;
}
int ReplacementDriverOnlyRestartsInputOnce(){
 auto storage=std::make_unique<Loop>(true);auto& f=*storage;CHECK(f.driver.EnableActorReplacement()&&FirstPumpShot(f));
 f.driver.ObservePump({},UnfinishedView(f),{},f.now);CHECK(f.driver.ActorReplacementWitnessed());
 const auto prior=f.Report();const auto counts=f.rifle;const auto calls=f.calls,shots=f.shots,submits=f.shellSubmits;
 auto next=f.input;++next.generation;next.predictedNs=f.now+10*Ms;const auto owner=NewActor(f);
 f.driver.Prepare(next,owner,Xm8MagazineAsset,{},f.now+10*Ms,f.now+110*Ms,f.now+10*Ms);
 CHECK(f.driver.ActorReplacementResumed()&&f.driver.State()==Bc2ResourceInventoryProbe::Phase::Setup);
 CHECK(f.Report().find("\"prior_report\":"+prior)!=std::string::npos);
 CHECK(f.Report().find("\"resumed\":true")!=std::string::npos&&!f.driver.Completed());
 CHECK(!next.hands[0].squeeze&&!next.hands[1].trigger);
 CHECK(f.calls==calls&&f.shots==shots&&f.shellSubmits==submits&&f.rifle==counts&&!f.pumpNative.releases&&!f.pumpNative.acks);
 const auto accepted=f.Report();
 f.driver.Prepare(next,owner,Xm8MagazineAsset,{},f.now+10*Ms,f.now+110*Ms,f.now+11*Ms);
 CHECK(f.Report()==accepted&&!f.driver.CancelConsumer());
 // Independently construct the replacement scene's synthetic native boundary.
 // Transplant only the resumed input script. It performs the declared bounded
 // recovery scope through real consumers, inside the original58s session.
 for(const bool spasFirst:{true,false})for(const int loaded:{22,30}){
 auto replacement=std::make_unique<Loop>(true);replacement->now=f.now+11*Ms;replacement->sequence=next.generation;
 replacement->memory.owner=owner;replacement->native.owner=owner;
 replacement->memory.Word(NativeInventory::player+0xc54,owner.weak);
 replacement->memory.Word(owner.weak,owner.soldier+4);
 replacement->rifle={loaded,191,30};
 if(spasFirst){replacement->selected=1;replacement->memory.Select(1);replacement->native.owner=replacement->memory.owner;}
 replacement->driver=f.driver;
 CHECK(replacement->calls==0&&!replacement->shots&&!replacement->pumpNative.acks);
 CHECK(replacement->Run());
 CHECK(replacement->calls==2&&replacement->shots==1&&replacement->shellSubmits==1&&replacement->selectionCalls==(spasFirst?2u:1u));
 CHECK(replacement->pumpNative.releases==1&&replacement->pumpNative.acks==1&&replacement->rifleShots==0);
 CHECK(replacement->rifle==AmmunitionCounts(loaded,191,30)&&replacement->shellLoaded==8&&replacement->shellReserve==23);
 CHECK(replacement->pumpSupports==1&&replacement->shellSupports==1);
 CHECK(replacement->now<1010*Ms+58000*Ms);
 CHECK(replacement->Report().find("\"prior_report\":"+prior)!=std::string::npos);
 CHECK(f.calls==calls&&f.shots==shots&&f.shellSubmits==submits&&f.rifle==counts);
 std::cout<<"Replacement actor "<<(spasFirst?"SPAS-first shoulder bootstrap, ":"rifle-first, ")<<"original magazine "<<loaded<<"/30 conserved, pump/shell/support completed at "
          <<(replacement->now-1010*Ms)/Ms<<"ms within original58000ms bound\n";
 }
 // Same original 58s deadline, even though the local input scripts restarted.
 auto expired=std::make_unique<Bc2ResourceInventoryProbe>(f.driver);auto late=next;++late.generation;
 const auto end=1010*Ms+58000*Ms;late.predictedNs=end;
 expired->Prepare(late,owner,Xm8MagazineAsset,{},end,end+100*Ms,end);
 CHECK(expired->CancelConsumer()&&ProbeReport(*expired).find("\"failure\":1")!=std::string::npos);
 auto second=owner;second.weak+=4;++second.actorGeneration;++next.generation;next.predictedNs=f.now+20*Ms;
 f.driver.Prepare(next,second,Xm8MagazineAsset,{},f.now+20*Ms,f.now+120*Ms,f.now+20*Ms);
 CHECK(f.driver.CancelConsumer()&&f.Report().find("\"prior_report\":"+prior)!=std::string::npos);
 return 0;
}
int ReplacementDriverRejectsFalseOrStaleReplacement(){
 auto storage=std::make_unique<Loop>(true);auto& f=*storage;CHECK(f.driver.EnableActorReplacement()&&FirstPumpShot(f));
 const auto unwitnessed=std::make_unique<Bc2ResourceInventoryProbe>(f.driver);
 f.driver.ObservePump({},UnfinishedView(f),{},f.now);CHECK(f.driver.ActorReplacementWitnessed());
 for(unsigned fault=0;fault<13;++fault){auto candidate=std::make_unique<Bc2ResourceInventoryProbe>(fault==12?*unwitnessed:f.driver);
  auto owner=NewActor(f);auto input=f.input;++input.generation;auto now=f.now+10*Ms,observed=now,deadline=now+100*Ms;input.predictedNs=now;
  if(fault==0){owner=f.native.owner;++owner.equipGeneration;++owner.weapon;}
  if(fault==1){owner=f.native.owner;++owner.space;input.spaceGeneration=owner.space;}
  if(fault==2)owner.actorGeneration=f.native.owner.actorGeneration;
  if(fault==3)owner.weak=f.native.owner.weak;
  if(fault==4)input.generation=f.input.generation;
  if(fault==5)input.generation=f.input.generation-1;
  if(fault==6)deadline=now;
  if(fault==7)observed=now+1;
  if(fault==8)input.focused=false;
  if(fault==9)input.hands[0].gripTracked=false;
  if(fault==10)now=f.now-1;
  if(fault==11)observed=f.now-1;
  candidate->Prepare(input,owner,Xm8MagazineAsset,{},observed,deadline,now);
  CHECK(!candidate->ActorReplacementResumed()&&candidate->CancelConsumer());
 }
 auto completed=UnfinishedView(f);completed.phase=Bc2NativeCyclePhase::Complete;
 f.driver.ObservePump({},completed,{},f.now+1);CHECK(f.driver.CancelConsumer()&&!f.driver.ActorReplacementResumed());
 CHECK(f.Report().find("\"failure\":24")!=std::string::npos);return 0;
}
int ReplacementShoulderRequiresUniqueCurrentRifle(){
 Bc2InventoryReloadProbe ordinary;CHECK(!ordinary.PreferOtherInitialShoulder({1,{NativeInventory::a,1}}));
 Bc2InventoryReloadProbe inputOnly{false,true};CHECK(!inputOnly.PreferOtherInitialShoulder({}));
 CHECK(inputOnly.PreferOtherInitialShoulder({1,{NativeInventory::a,1}}));
 CHECK(!inputOnly.PreferOtherInitialShoulder({1,{NativeInventory::a,1}}));
 auto old=std::make_unique<Loop>(true);CHECK(old->driver.EnableActorReplacement()&&FirstPumpShot(*old));
 old->driver.ObservePump({},UnfinishedView(*old),{},old->now);
 const auto owner=NewActor(*old);auto input=old->input;++input.generation;input.predictedNs=old->now+10*Ms;
 old->driver.Prepare(input,owner,Xm8MagazineAsset,{},old->now+10*Ms,old->now+110*Ms,old->now+10*Ms);
 CHECK(old->driver.ActorReplacementResumed());
 for(unsigned fault=0;fault<4;++fault){auto replacement=std::make_unique<Loop>(true);
  replacement->now=old->now+11*Ms;replacement->sequence=input.generation;
  replacement->memory.owner=owner;
  replacement->memory.Word(NativeInventory::player+0xc54,owner.weak);replacement->memory.Word(owner.weak,owner.soldier+4);
  replacement->selected=1;replacement->memory.Select(1);replacement->native.owner=replacement->memory.owner;
  replacement->driver=old->driver;bool injected=false;
  replacement->beforeDriver=[&](Loop& f){if(!f.observed.display)return;auto& display=*f.observed.display;
   if(!BodyInventoryDisplayFresh(display,f.now))return;
   if(fault==0){ // A second shotgun is not an enrolled rifle target.
    for(unsigned n=0;n<display.count;++n)if(display.slots[n].native.weapon!=f.native.owner.weapon){
     auto& slot=display.slots[n];slot.native.category=1;display.carried.items[slot.native.slot]=slot.native;injected=true;}
   }else if(fault==1){ // No unique other shoulder can be chosen.
    for(unsigned n=0;n<display.count;++n)if(display.slots[n].native.weapon==f.native.owner.weapon){
     display.slots[0]=display.slots[n];display.count=1;injected=true;break;}
   }else if(fault==2){display.deadlineNs=f.now;injected=true;}
   else if(f.driver.State()==Bc2ResourceInventoryProbe::Phase::InitialDraw){
    for(unsigned n=0;n<display.count;++n)if(display.slots[n].native.weapon!=f.native.owner.weapon){
     ++display.slots[n].assignment.item.generation;injected=true;}
   }
  };
  CHECK(!replacement->Run(false)&&injected&&replacement->driver.CancelConsumer());
  CHECK(!replacement->calls&&!replacement->shots&&!replacement->shellSubmits&&!replacement->selectionCalls);
 }
 return 0;
}
int QueuedShellSupportUsesCurrentClock(){auto staleStorage=std::make_unique<Loop>(true);auto& stale=*staleStorage;stale.advancingShellClock=true;
 CHECK(!stale.Run(false));CHECK(stale.Report().find("\"failure\":22")!=std::string::npos);
 CHECK(stale.shellSubmits==1&&stale.shellSupports==1&&stale.pumpSupports==1&&stale.rifleShots==0);
 CHECK(stale.queuedSupportRejected==1&&stale.queuedSupportRetained==1&&stale.queuedSupportReason==HandInteractionReason::StaleInput);
 CHECK(stale.shellPairObserved&&!stale.shellPairLost&&!stale.sourceRestamped);
 std::cout<<"Original queued shell support renewal reproduces failure22 with one stale rejection and retained claim\n";
 return 0;}
int ExpiryDuringQueuedShellSupport(){auto fStorage=std::make_unique<Loop>(true);auto& f=*fStorage;f.advancingShellClock=f.refreshOwnershipClock=f.expireAfterAdoption=true;
 CHECK(!f.Run(false));CHECK(f.shellSupports==1&&f.shellSubmits==1&&f.rifleShots==0);
 CHECK(f.queuedSupportRejected==1&&f.queuedSupportRetained==0&&f.queuedSupportReason==HandInteractionReason::StaleInput);
 CHECK(!f.sourceRestamped);std::cout<<"Forward clock expiry rejects support renewal without restamping input or source\n";return 0;}
int AdvancingShellClockAfterAcknowledgedPump(){auto fStorage=std::make_unique<Loop>(true);auto& f=*fStorage;f.advancingShellClock=f.refreshOwnershipClock=true;CHECK(f.Run(false));
 CHECK(f.shellPairObserved&&!f.shellPairLost);CHECK(f.shellSubmits==1&&f.shellSupports==1&&f.pumpSupports==1&&f.rifleShots==1);
 CHECK(f.pumpNative.releases==1&&f.pumpNative.acks==1&&f.calls==6);CHECK(!f.queuedSupportRejected&&!f.sourceRestamped);return 0;}
int CombinedPartialNativeTransferAfterPriorLeaseExpiry(){auto fStorage=std::make_unique<Loop>(true);auto& f=*fStorage;
 f.advancingShellClock=f.refreshOwnershipClock=f.splitShellTransfer=true;CHECK(f.Run(false));
 CHECK(f.submittedCohortWaits==4&&f.shellSubmits==1&&f.shellSupports==1&&f.pumpSupports==1&&f.rifleShots==1);
 CHECK(f.pumpNative.releases==1&&f.pumpNative.acks==1&&f.calls==6&&!f.shellPairLost&&!f.sourceRestamped);
 CHECK(f.rifle==AmmunitionCounts(29,161,30)&&f.shellLoaded==8&&f.shellReserve==23);
 std::cout<<"Combined original pending shell survives partial native transfer after old lease expiry; exact completion/support/rifle return conserved\n";return 0;}
int PumpInspectionPreservesConsumers(){
 for(bool missing:{false,true}){
  auto f=std::make_unique<Loop>(true);
  CHECK(f->driver.UsePumpInspectionPose()&&!f->driver.UsePumpInspectionPose());
  CHECK(!f->driver.EnableActorReplacement());
  f->allowPumpPairs=!missing;bool sidewaysAtFire=false;
  f->beforeDriver=[&](Loop& loop){
   if(loop.shots&&loop.driver.State()==Bc2ResourceInventoryProbe::Phase::Pump&&loop.previousPumpRight){const auto& p=*loop.previousPumpRight;
    sidewaysAtFire|=std::hypot(p.position.x,p.position.y+.22f,p.position.z+.40f)<.001f&&std::abs(p.orientation.y-.7071067812f)<.001f;}
  };
  CHECK(f->Run(false)==!missing);
  CHECK(sidewaysAtFire&&!f->pumpRightJump);
  if(!missing){CHECK(f->pumpNative.releases==1&&f->pumpNative.acks==1&&f->calls==6&&f->shellSubmits==1&&f->rifleShots==1);
   CHECK(f->rifle==AmmunitionCounts(29,161,30)&&f->shellLoaded==8&&f->shellReserve==23);}
  else CHECK(!f->pumpNative.releases&&!f->shellSubmits&&!f->rifleShots);
  CHECK(!f->driver.UsePumpInspectionPose());
 }
 auto unconfiguredStorage=std::make_unique<Bc2ResourceInventoryProbe>();auto& unconfigured=*unconfiguredStorage;CHECK(!unconfigured.UsePumpInspectionPose());
 std::cout<<"Sideways pump inspection preserves real input/consumer completion and rejects missing native evidence\n";return 0;
}
int CombinedManualPump(){CHECK(!PumpInspectionPreservesConsumers());auto storage=std::make_unique<Loop>(true);auto& f=*storage;CHECK(f.Run());
 CHECK(f.calls==6&&f.shots==1&&f.shellSubmits==1&&f.selectionCalls==2&&f.rifleShots==1);
 CHECK(f.pumpFirePose&&!f.pumpRightJump);
 CHECK(f.pumpNative.releases==1&&f.pumpNative.acks==1&&f.pumpSupports==1&&f.shellSupports==1);
 CHECK(f.rifle==AmmunitionCounts(29,161,30)&&f.shellLoaded==8&&f.shellReserve==23);
 CHECK(f.Report().find("\"manual_pump\":true")!=std::string::npos&&f.Report().find("\"input_rows_dropped\":0")!=std::string::npos);
 std::cout<<"Combined pump: real persistent body, resource, shell, pump and support consumers; one physical pump, two support returns, final rifle shot\n";return 0;}
int CombinedRequiresRifleSupportBeforeReload(){
 CHECK(!RestartEquipChurnWaitsForRealActor());
 CHECK(!ReplacementShoulderRequiresUniqueCurrentRifle());
 CHECK(!ReplacementDriverOptInAndEvidence());
 CHECK(!ReplacementDriverOnlyRestartsInputOnce());
 CHECK(!ReplacementDriverRejectsFalseOrStaleReplacement());
 for(unsigned missing=0;missing<2;++missing){auto storage=std::make_unique<Loop>(true);auto& f=*storage;
  CHECK(f.driver.RequireRifleSupport());
  f.beforeDriver=[&](Loop& loop){
   loop.observed.supportVerified=!missing&&loop.Rifle()&&loop.magRaw.valid;
   loop.observed.supportRaw=loop.magRaw;
   loop.observed.supportTarget=Multiply(Pose(-.2f,-.25f,.45f),Pose(5,2,3));
  };
  CHECK(f.Run(false)==!missing);
  CHECK(f.Report().find("\"rifle_support_required\":true")!=std::string::npos);
  if(!missing){CHECK(f.Report().find("\"rifle_support_released\":true")!=std::string::npos);CHECK(f.calls==6);}
  else CHECK(f.calls==0&&f.Report().find("\"failure\":23")!=std::string::npos);
 }
 return 0;
}
int FinalAutomaticRifleShots(){
 // 70ms stock cadence under the unchanged100ms trigger reproduces actual245:
 // two native shots can precede the first coherent observation after release.
 for(const auto delay:{0ll,150*Ms}){auto storage=std::make_unique<Loop>(true);auto& f=*storage;f.automaticRifle=true;f.secondPublicationDelay=delay;
  bool sawOneDuringDwell=false,sawTwoDuringDwell=false;std::int64_t sawTwoAt=0;
  f.beforeDriver=[&](Loop& loop){if(loop.driver.State()==Bc2ResourceInventoryProbe::Phase::Dwell){
   if(loop.Report().find("\"rifle_shot_count\":1")!=std::string::npos)sawOneDuringDwell=true;
   if(loop.Report().find("\"rifle_shot_count\":2")!=std::string::npos){sawTwoDuringDwell=true;if(!sawTwoAt)sawTwoAt=loop.now;}
  }};
  CHECK(f.Run());CHECK(f.rifleShots==2&&f.rifle==AmmunitionCounts(28,161,30));
  CHECK(f.calls==6&&f.shellSubmits==1&&f.selectionCalls==2&&f.mappings==f.sequence);
  CHECK(sawTwoDuringDwell);if(delay)CHECK(sawOneDuringDwell&&f.now-sawTwoAt>=390*Ms);
  CHECK(f.Report().find("\"rifle_shot_count\":2")!=std::string::npos);
  CHECK(!f.input.hands[1].trigger&&!f.magazine->BlocksEquipment()&&!f.shell->BlocksEquipment());
 }
 auto singleStorage=std::make_unique<Loop>(true);auto& single=*singleStorage;CHECK(single.Run(false));CHECK(single.rifleShots==1&&single.rifle==AmmunitionCounts(29,161,30));
 CHECK(single.Report().find("\"rifle_shot_count\":1")!=std::string::npos);
 std::cout<<"Final rifle pulse accepts exact one/two-shot decreases, including delayed second-shot publication and stable dwell\n";return 0;
}
int FinalRifleRejectsForeignAmmunition(){
 for(unsigned fault=0;fault<10;++fault){auto storage=std::make_unique<Loop>(true);auto& f=*storage;bool injected=false;
  f.beforeDriver=[&](Loop& loop){
   if(loop.driver.State()!=Bc2ResourceInventoryProbe::Phase::Dwell||!loop.observed.magazine.resource)return;
   injected=true;auto& v=*loop.observed.magazine.resource;
   switch(fault){
   case 0:++v.snapshot.counts.reserve;break; // foreign grant
   case 1:--v.snapshot.counts.reserve;break; // reload/foreign consumption
   case 2:++v.snapshot.counts.capacity;break;
   case 3:v.snapshot.counts.loaded=30;break; // refill after accepted shot
   case 4:v.snapshot.counts.loaded=27;break; // unbounded extra shots
   case 5:++v.binding.context.resource.weaponGeneration;v.snapshot.context=v.binding.context;break;
   case 6:++v.binding.owner.equipGeneration;++v.identity.owner.equipGeneration;
          ++v.binding.context.equipGeneration;v.snapshot.context=v.binding.context;break;
   case 7:++v.binding.data;break;
   case 8:++v.request;break; // fresh counts cannot conceal another resource operation
   case 9:v.requestState=AmmoResourceRequestState::Dispatched;v.phase=AmmunitionLedgerPhase::Dispatched;break;
   }
  };
  CHECK(!f.Run(false)&&f.driver.CancelConsumer()&&injected);
  CHECK(f.Report().find("\"failure\":21")!=std::string::npos);CHECK(!f.input.hands[1].trigger);
  CHECK(f.calls==6&&f.rifleShots==1); // driver never writes ammo or adds a request
 }
 auto excessiveStorage=std::make_unique<Loop>(true);auto& excessive=*excessiveStorage;excessive.automaticRifle=true;excessive.rifleInterval=40*Ms;
 CHECK(!excessive.Run(false)&&excessive.driver.CancelConsumer()&&excessive.rifleShots==3);
 CHECK(excessive.Report().find("\"failure\":21")!=std::string::npos);
 std::cout<<"Final rifle rejects foreign owner/context/data/request, reload-like reserve/refill/capacity changes and excess rounds\n";return 0;
}
int FinalRifleRequiresFreshObservation(){
 for(unsigned fault=0;fault<3;++fault){auto storage=std::make_unique<Loop>(true);auto& f=*storage;bool injected=false;
  f.beforeDriver=[&](Loop& loop){
   const auto phase=loop.driver.State();
   if((phase!=Bc2ResourceInventoryProbe::Phase::RifleFire&&phase!=Bc2ResourceInventoryProbe::Phase::WaitRifleShot)||!loop.observed.magazine.resource)return;
   injected=true;auto& v=*loop.observed.magazine.resource;
   if(fault==0){v.snapshot.deadlineNs=loop.now;v.binding.deadlineNs=loop.now;}
   if(fault==1)v.snapshot.sequence=1;
   if(fault==2)v.snapshot.observedNs=loop.now+1;
  };
  CHECK(!f.Run(false)&&f.driver.CancelConsumer()&&injected);
  CHECK(f.Report().find("\"rifle_shot_count\":0")!=std::string::npos);
  CHECK(f.Report().find("\"failure\":13")!=std::string::npos);
 }
 return 0;
}
int FinalRifleCannotCompleteFromRegressedSnapshot(){
 auto storage=std::make_unique<Loop>(true);auto& f=*storage;bool injected=false;
 f.beforeDriver=[&](Loop& loop){if(loop.driver.State()==Bc2ResourceInventoryProbe::Phase::Dwell&&loop.observed.magazine.resource){
  injected=true;loop.observed.magazine.resource->snapshot.sequence=1;
 }};
 CHECK(!f.Run(false)&&f.driver.CancelConsumer()&&injected&&f.rifleShots==1);
 CHECK(f.Report().find("\"rifle_shot_count\":1")!=std::string::npos);
 return 0;
}
int CombinedPumpMissingEvidence(){for(unsigned fault=0;fault<4;++fault){auto fStorage=std::make_unique<Loop>(true);auto& f=*fStorage;
 if(fault==0)f.allowPumpPairs=false;if(fault==1)f.allowPumpSupport=false;if(fault==2)f.allowShellSupport=false;if(fault==3)f.allowRifleShot=false;
 CHECK(!f.Run(false)&&f.driver.CancelConsumer());
 if(fault<2)CHECK(f.shellSubmits==0&&f.selectionCalls==1);
 if(fault==2)CHECK(f.shellSubmits==1&&f.selectionCalls==1&&f.rifleShots==0);
 if(fault==3)CHECK(f.shellSubmits==1&&f.selectionCalls==2&&f.rifleShots==0);
 }return 0;}
int Combined(){auto fStorage=std::make_unique<Loop>();auto& f=*fStorage;CHECK(f.profile->Ready());CHECK(f.Run());CHECK(f.calls==6&&f.shots==1&&f.shellSubmits==1&&f.selectionCalls==2);
 CHECK(f.rifle==AmmunitionCounts(30,161,30));CHECK(f.shellLoaded==8&&f.shellReserve==23);CHECK(f.mappings==f.sequence);
 CHECK(f.magReceipts.size()==6&&f.shellReceipts.size()==1);CHECK(!f.magazine->BlocksEquipment()&&!f.shell->BlocksEquipment());
 CHECK(f.hands.Current(InteractionHand::Right)&&!f.hands.Current(InteractionHand::Left));CHECK(f.selected==0);
 const auto calls=f.calls;for(unsigned n=0;n<7200;++n)f.Tick();CHECK(f.driver.Completed()&&f.calls==calls&&f.shots==1&&f.shellSubmits==1);
 CHECK(f.Report().find("\"input_rows_dropped\":0")!=std::string::npos);
 // Only replace the input coordinator. Real consumer, service, inventory, hand,
 // native request and shell completion counters survive this second lap.
 f.driver=std::move(*std::make_unique<Bc2ResourceInventoryProbe>());CHECK(f.Run());CHECK(f.calls==12&&f.shots==2&&f.shellSubmits==2&&f.selectionCalls==4);
 CHECK(f.rifle==AmmunitionCounts(30,131,30)&&f.shellLoaded==8&&f.shellReserve==22);
 CHECK(f.shell->ProbeState(f.now).completed==2&&f.Report().find("\"counter_baseline\":{\"acquired\":1,\"submitted\":1,\"completed\":1}")!=std::string::npos);
 std::cout<<"Two persistent laps:12 native magazine operations,2 mapped shots,2 shell receipts,4 verified selections; final rifle30/131,SPAS8/22\n";return 0;}
int MissingEvidence(){for(unsigned failure=0;failure<6;++failure){auto fStorage=std::make_unique<Loop>();auto& f=*fStorage;
 if(failure==0)f.allowSeatReceipt=false;if(failure==1)f.allowVisibility=false;if(failure==2)f.allowSelect=false;
 if(failure==3)f.allowShot=false;if(failure==4)f.allowRetirement=false;if(failure==5)f.earlyMapping=true;
 CHECK(!f.Run(false)&&f.driver.CancelConsumer());CHECK(f.calls<=4&&f.selectionCalls<=1&&!f.driver.Completed());
 if(failure==0)CHECK(f.calls==2&&f.shots==0);if(failure==1||failure==2)CHECK(f.calls==4&&f.shots==0);
 if(failure==3||failure==5)CHECK(f.shots==0&&f.shellSubmits==0);if(failure==4)CHECK(f.shots==1&&f.shellSubmits==1&&f.shell->BlocksEquipment());
 std::cout<<"Rejected boundary fault "<<failure<<" after actual magazine calls="<<f.calls<<",shots="<<f.shots<<",shell submits="<<f.shellSubmits<<'\n';}
 return 0;}
int Interruptions(){for(unsigned failure=0;failure<4;++failure){auto fStorage=std::make_unique<Loop>();auto& f=*fStorage;
 const auto target=failure==3?Bc2ResourceInventoryProbe::Phase::Fire:Bc2ResourceInventoryProbe::Phase::FirstMagazine;
 for(unsigned n=0;n<4000&&f.driver.State()!=target&&!f.driver.CancelConsumer();++n)f.Tick();CHECK(f.driver.State()==target);
 if(failure<2)f.Tick(failure+1);
 else if(failure==2){auto duplicate=f.input;f.driver.Prepare(duplicate,f.native.owner,f.Asset(),f.observed,f.now+1,f.now+100*Ms,f.now+1);CHECK(!duplicate.hands[0].squeeze&&!duplicate.hands[1].trigger);}
 else {++f.native.owner.weapon;f.Tick();}
 CHECK(f.driver.CancelConsumer()&&!f.driver.Completed());}
 return 0;}
int BoundedInputJournal(){
 auto driverStorage=std::make_unique<Bc2ResourceInventoryProbe>();auto& driver=*driverStorage;ResourceInventoryObservation unavailable;ReloadStateOwner owner{1,2,3,4,5,6,7};
 InputFrame input;input.spaceGeneration=owner.space;input.focused=input.headValid=true;
 for(auto& h:input.hands){h.active=Components;h.gripTracked=h.aimTracked=true;}
 std::int64_t now=1000*Ms;
 auto tick=[&]{now+=10*Ms;++input.generation;input.predictedNs=now;driver.Prepare(input,owner,SpasReloadAsset,unavailable,now,now+100*Ms,now);};
 auto report=[&]{std::ostringstream o;driver.Report(o);return o.str();};
 auto rows=[&]{const auto s=report();const auto end=s.find("\"inventory\":");unsigned count=0;for(std::size_t p=0;(p=s.find("\"generation\":",p))<end;++p)++count;return count;};
 for(unsigned n=0;n<5400;++n)tick();CHECK(driver.State()==Bc2ResourceInventoryProbe::Phase::Setup);CHECK(rows()<60);
 // A weapon change is retained immediately between periodic setup samples.
 auto before=rows();owner.weapon=888777;tick();CHECK(rows()==before+1);
 for(unsigned n=0;n<1800;++n)tick();CHECK(driver.CancelConsumer());CHECK(report().find("\"input_rows_dropped\":0")!=std::string::npos);
 CHECK(report().find("\"phase\":12,\"weapon\":888777")!=std::string::npos);
 // Terminal neutral dwell is bounded, while unexpected owner/grip changes remain evidence.
 before=rows();for(unsigned n=0;n<7200;++n)tick();CHECK(rows()==before);
 input.hands[1].squeeze=.8f;tick();CHECK(rows()==before+1);owner.weapon=888778;tick();CHECK(rows()==before+2);
 CHECK(report().find("\"input_logging_version\":2")!=std::string::npos);return 0;
}
int QueuedPumpInterruptionPreservesTrackingOverride(){
 // This test exercises the real top-level same-generation replay. The native
 // boundary remains synthetic; stop after the first loss packet, so no native
 // recovery completion or grant is asserted by this wrapper-only regression.
 auto storage=std::make_unique<Loop>(true);auto& f=*storage;
 f.driver=std::move(*std::make_unique<Bc2ResourceInventoryProbe>());
 CHECK(f.driver.EnablePump(f.calibration,Bc2PumpControllerProbe::Scenario::LeftTrackingLoss));
 for(unsigned n=0;n<5000&&(n==0||f.input.hands[0].gripTracked)&&!f.driver.CancelConsumer();++n)f.Tick();
 CHECK(f.driver.State()==Bc2ResourceInventoryProbe::Phase::Pump&&!f.driver.CancelConsumer());
 CHECK(!f.input.hands[0].gripTracked&&!f.input.hands[0].aimTracked&&f.input.hands[0].squeeze==0);
 CHECK(f.input.hands[1].gripTracked&&f.input.hands[1].aimTracked&&f.shots==1&&f.pumpNative.releases==0);
 const auto source=f.input;const auto deadline=f.now+100*Ms;
 for(unsigned duplicate=1;duplicate<=3;++duplicate){
  auto replay=source;replay.hands[0].gripTracked=replay.hands[0].aimTracked=true;
  f.driver.Prepare(replay,f.native.owner,f.Asset(),f.observed,f.now,deadline,f.now+duplicate*Ms);
  CHECK(!replay.hands[0].gripTracked&&!replay.hands[0].aimTracked);
  CHECK(replay.hands[1].gripTracked&&replay.hands[1].aimTracked);
  CHECK(replay.hands[0].squeeze==0&&replay.hands[1].trigger==0);
  CHECK(replay.generation==source.generation&&replay.predictedNs==source.predictedNs);
 }
 auto expired=source;expired.hands[0].gripTracked=expired.hands[0].aimTracked=true;
 expired.hands[0].squeeze=expired.hands[1].trigger=1;
 f.driver.Prepare(expired,f.native.owner,f.Asset(),f.observed,f.now,deadline,deadline);
 CHECK(f.driver.CancelConsumer()&&expired.hands[0].squeeze==0&&expired.hands[1].trigger==0);
 CHECK(!expired.hands[0].gripTracked&&!expired.hands[0].aimTracked);
 std::cout<<"Top-level duplicate pump loss retains tracking overrides and original source time; expiry emits neutral actions\n";
 return 0;
}
}
int ThreePumpsTwoShellsAcrossPersistentConsumers(){
 auto f=std::make_unique<Loop>(true,3,Bc2PumpControllerProbe::Scenario::HeldSupportNatural);
 f->guidePumpRendering=true;CHECK(f->driver.EnableTwoShells()&&!f->driver.EnableTwoShells());
 CHECK(f->Run());CHECK(f->shots==3&&f->pumpNative.releases==3&&f->pumpNative.acks==3);
 CHECK(f->pumpSupports==3&&f->shellSupports==1&&f->shellSubmits==2&&f->shellReceipts.size()==2);
 CHECK(f->shellLoaded==7&&f->shellReserve==22&&f->calls==6&&f->rifleShots==1&&f->selectionCalls==2);
 CHECK(f->now<50000*Ms);
 CHECK(f->Report().find("\"requested_shells\":2")!=std::string::npos);
 CHECK(f->Report().find("\"requested_rounds\":2")!=std::string::npos);
 CHECK(!f->driver.EnableTwoShells());
 for(unsigned fault=0;fault<3;++fault){
  auto bad=std::make_unique<Loop>(true,3,Bc2PumpControllerProbe::Scenario::HeldSupportNatural);
  bad->guidePumpRendering=true;CHECK(bad->driver.EnableTwoShells());bool changed=false;
  bad->beforeDriver=[&](Loop& x){
   if(fault==2&&x.shellSubmits==2&&!x.shellApplied){x.submittedAt=x.now;changed=true;return;}
   if(changed)return;
   if((fault==0&&x.shellReceipts.size()==1)||(fault==1&&x.driver.State()==Bc2ResourceInventoryProbe::Phase::ShellSupport)){
    ++x.shellLoaded;--x.shellReserve;changed=true;
   }
  };
  CHECK(!bad->Run(false)&&changed&&bad->driver.CancelConsumer());
 }
 auto ordinaryStorage=std::make_unique<Bc2ResourceInventoryProbe>();auto& ordinary=*ordinaryStorage;CHECK(!ordinary.EnableTwoShells());
 CHECK(ordinary.EnablePump(f->calibration)&&!ordinary.EnableTwoShells());
 std::cout<<"Three pumps conserve8->5, two independent shell gestures conserve5->7/24->22; extra native fill and missing second receipt fail\n";
 return 0;
}
int EmptyTubeAcrossPersistentConsumers(){
 for(const auto scenario:{Bc2PumpControllerProbe::Scenario::None,Bc2PumpControllerProbe::Scenario::HeldSupportNatural}){
 auto storage=std::make_unique<Loop>(true,8,scenario);auto& f=*storage;f.rifle.loaded=30;
 f.guidePumpRendering=scenario==Bc2PumpControllerProbe::Scenario::HeldSupportNatural;
 CHECK(f.Run());CHECK(f.shots==8&&f.pumpNative.releases==8&&f.pumpNative.acks==8);
 CHECK(f.pumpSupports==8&&f.shellSupports==1&&f.shellSubmits==1);
 CHECK(f.shellLoaded==1&&f.shellReserve==23&&f.calls==2&&f.rifleShots==1&&f.selectionCalls==2);
 CHECK(f.rifle==AmmunitionCounts(29,191,30));
 CHECK(f.now<45000*Ms); // Leave time for real native/render scheduling inside the original session bound.
 CHECK(f.Report().find("\"requested_cycles\":8")!=std::string::npos);
 std::cout<<"Eight ordinary pump strokes, empty tube to one shell, support return and shoulder/rifle return at "<<f.now/Ms<<"ms\n";
 // An extra native round cannot be concealed by the final shell check.
 auto bad=std::make_unique<Loop>(true,8);bad->rifle.loaded=30;bool changed=false;
 bad->beforeDriver=[&](Loop& x){if(!changed&&x.driver.State()==Bc2ResourceInventoryProbe::Phase::Shell){++x.shellLoaded;changed=true;}};
 CHECK(!bad->Run(false)&&changed&&bad->driver.CancelConsumer());
 auto rejectedStorage=std::make_unique<Bc2ResourceInventoryProbe>();auto& rejected=*rejectedStorage;CHECK(!rejected.EnablePump(f.calibration,Bc2PumpControllerProbe::Scenario::LeftTrackingLoss,8));
 CHECK(!rejected.EnablePump(f.calibration,Bc2PumpControllerProbe::Scenario::None,9));
 CHECK(rejected.EnablePump(f.calibration,Bc2PumpControllerProbe::Scenario::None,8));
 CHECK(!rejected.EnableActorReplacement());
 }return 0;
}

int PumpAgainAfterOtherWeapon(){
 auto storage=std::make_unique<Loop>(true);auto& f=*storage;
 CHECK(f.driver.EnablePumpRevisit());CHECK(!f.driver.EnablePumpRevisit());
 CHECK(!f.driver.UsePumpInspectionPose()&&!f.driver.EnableActorReplacement());
 f.guidePumpRendering=true;CHECK(f.Run());
 CHECK(f.shots==2&&f.pumpNative.releases==2&&f.pumpNative.acks==2&&f.pumpSupports==2);
 CHECK(f.shellSubmits==1&&f.shellSupports==1&&f.selectionCalls==4&&f.rifleShots==1);
 CHECK(f.shellLoaded==7&&f.shellReserve==23&&f.selected==0);
 CHECK(f.Report().find("\"pump_revisit\":{\"enabled\":true,\"completed\":true")!=std::string::npos);
 CHECK(!f.shell->BlocksEquipment()&&!f.magazine->BlocksEquipment()&&!f.pump->BlocksFire());
 CHECK(f.now<58000*Ms);
 std::cout<<"SPAS pump after rifle reload/fire and two shoulder round trips with persistent consumers at "<<f.now/Ms<<"ms\n";
 for(unsigned fault=0;fault<2;++fault){
  auto storage=std::make_unique<Loop>(true);auto& bad=*storage;
  CHECK(bad.driver.EnablePumpRevisit());bad.guidePumpRendering=true;bool injected=false;
  bad.beforeDriver=[&](Loop& x){
   if(!injected&&x.driver.State()==(fault?Bc2ResourceInventoryProbe::Phase::RevisitPump:Bc2ResourceInventoryProbe::Phase::RevisitDraw)){
    injected=true;if(fault)x.allowPumpPairs=false;else ++x.shellLoaded;
   }
  };
  CHECK(!bad.Run(false)&&injected&&bad.driver.CancelConsumer());
 }
 return 0;
}

int MagazineReplacementKeepsActualRemovedResource(){
 auto storage=std::make_unique<Loop>(true);auto& f=*storage;
 CHECK(f.driver.EnableActorReplacement(true)&&!f.driver.EnableActorReplacement(true));
 for(unsigned n=0;n<2200&&!f.driver.ActorReplacementWitnessed()&&!f.driver.CancelConsumer();++n)f.Tick();
 if(!f.driver.ActorReplacementWitnessed())std::cerr<<f.Report()<<'\n';
 CHECK(f.driver.ActorReplacementWitnessed()&&!f.driver.ActorReplacementResumed()&&!f.driver.CancelConsumer());
 CHECK(f.driver.State()==Bc2ResourceInventoryProbe::Phase::FirstMagazine&&f.calls==1&&!f.shots&&!f.shellSubmits);
 CHECK(f.rifle==AmmunitionCounts(0,191,30)&&f.magResult.ownsLeftHand&&f.packs.rolePairs[1]>0);
 CHECK(f.Report().find("\"original_rounds\":22")!=std::string::npos);
 const auto witness=f.Report();const auto oldCounts=f.rifle;
 for(unsigned n=0;n<40;++n)f.Tick();
 CHECK(f.calls==1&&f.rifle==oldCounts&&!f.driver.CancelConsumer());
 CHECK(f.input.hands[0].squeeze==1&&f.input.hands[1].squeeze==1&&!f.input.hands[1].trigger);
 // Fresh contradictory ammunition after the witness cannot pass as waiting.
 for(unsigned fault=0;fault<4;++fault){auto candidate=std::make_unique<Bc2ResourceInventoryProbe>(f.driver);
  auto obs=f.observed;CHECK(obs.magazine.resource);auto& v=*obs.magazine.resource;
  if(fault==0)v.wellEmpty=false;
  if(fault==1)++v.original->id;
  if(fault==2)++v.snapshot.counts.loaded;
  if(fault==3)++v.request;
  auto in=f.input;++in.generation;const auto now=f.now+1;
  candidate->Prepare(in,f.native.owner,f.Asset(),obs,f.now,f.now+100*Ms,now);
  CHECK(candidate->CancelConsumer()&&!candidate->ActorReplacementResumed());
 }
 // Owner, input time and tracking replacement rules are identical for the
 // magazine and pump witnesses; no controller input can create a new actor.
 for(unsigned fault=0;fault<12;++fault){auto candidate=std::make_unique<Bc2ResourceInventoryProbe>(f.driver);
  auto owner=NewActor(f);auto in=f.input;++in.generation;auto now=f.now+10*Ms,observed=now,deadline=now+100*Ms;
  if(fault==0){owner=f.native.owner;++owner.equipGeneration;++owner.weapon;}
  if(fault==1){owner=f.native.owner;++owner.space;in.spaceGeneration=owner.space;}
  if(fault==2)owner.actorGeneration=f.native.owner.actorGeneration;
  if(fault==3)owner.weak=f.native.owner.weak;
  if(fault==4)in.generation=f.input.generation;
  if(fault==5)--in.generation;
  if(fault==6)deadline=now;
  if(fault==7)observed=now+1;
  if(fault==8)in.focused=false;
  if(fault==9)in.hands[0].gripTracked=false;
  if(fault==10)now=f.now-1;
  if(fault==11)observed=f.now-1;
  in.predictedNs=now;
  candidate->Prepare(in,owner,Xm8MagazineAsset,{},observed,deadline,now);
  CHECK(!candidate->ActorReplacementResumed()&&candidate->CancelConsumer());
 }
 auto owner=NewActor(f);auto in=f.input;++in.generation;in.predictedNs=f.now+10*Ms;
 f.driver.Prepare(in,owner,Xm8MagazineAsset,{},f.now+10*Ms,f.now+110*Ms,f.now+10*Ms);
 CHECK(f.driver.ActorReplacementResumed()&&!f.driver.CancelConsumer()&&!in.hands[0].squeeze&&!in.hands[1].trigger);
 CHECK(f.calls==1&&f.rifle==oldCounts&&!f.shots&&!f.shellSubmits);
 // Only the input script is moved to independently initialized new-world
 // fixtures. These native receipts remain explicitly simulated.
 for(const bool spasFirst:{false,true}){
  auto replacement=std::make_unique<Loop>(true);replacement->now=f.now+11*Ms;replacement->sequence=in.generation;
  replacement->memory.owner=owner;replacement->native.owner=owner;
  replacement->memory.Word(NativeInventory::player+0xc54,owner.weak);
  replacement->memory.Word(owner.weak,owner.soldier+4);replacement->rifle={22,191,30};
  if(spasFirst){replacement->selected=1;replacement->memory.Select(1);replacement->native.owner=replacement->memory.owner;}
  replacement->driver=f.driver;CHECK(replacement->Run());
  CHECK(replacement->calls==2&&replacement->shots==1&&replacement->shellSubmits==1);
  CHECK(replacement->rifle==AmmunitionCounts(22,191,30)&&replacement->shellLoaded==8&&replacement->shellReserve==23);
  CHECK(replacement->pumpSupports==1&&replacement->shellSupports==1);
  CHECK(replacement->now<1010*Ms+58000*Ms);
 }
 auto late=f.input;late.generation+=2;const auto end=1010*Ms+58000*Ms;late.predictedNs=end;
 auto expired=std::make_unique<Bc2ResourceInventoryProbe>(f.driver);
 expired->Prepare(late,owner,Xm8MagazineAsset,{},end,end+100*Ms,end);CHECK(expired->CancelConsumer());
 std::cout<<"Removed original magazine waits under real custody; actor replacement resumes only the finite input driver, then magazine/pump/shell/support complete\n";
 return 0;
}

int MagazineRefillThenFire(){
 auto f=std::make_unique<Loop>();f->singleFireMagazine=true;CHECK(f->driver.EnableMagazineOnly(false,true));
 CHECK(f->Run()&&f->driver.Completed()&&f->calls==2&&f->rifleShots==1);
 CHECK(f->rifle==AmmunitionCounts(29,161,30)&&!f->shots&&!f->shellSubmits&&!f->selectionCalls);
 auto denied=std::make_unique<Loop>();denied->singleFireMagazine=true;denied->allowRifleShot=false;
 CHECK(denied->driver.EnableMagazineOnly(false,true)&&!denied->Run(false));CHECK(denied->calls==2&&!denied->rifleShots);
 auto mixedStorage=std::make_unique<Bc2ResourceInventoryProbe>();auto& mixed=*mixedStorage;CHECK(!mixed.EnableMagazineOnly(true,true));
 return 0;
}
int EmptyMagazineOrdinaryInput(){
 auto f=std::make_unique<Loop>();f->singleFireMagazine=true;CHECK(f->driver.EnableMagazineOnly(true));
 CHECK(f->Run()&&f->driver.Completed());
 CHECK(f->calls==2&&f->rifleShots==22&&f->rifle==AmmunitionCounts(30,161,30));
 CHECK(!f->shellSubmits&&!f->shots&&!f->selectionCalls);
 CHECK(f->Report().find("\"shots\":22")!=std::string::npos);
 // Independent native boundary refuses the trigger: no shot may be inferred
 // from the input or from elapsed time, and no reload request is submitted.
 auto missing=std::make_unique<Loop>();missing->singleFireMagazine=true;missing->allowRifleShot=false;
 CHECK(missing->driver.EnableMagazineOnly(true)&&!missing->Run(false));CHECK(!missing->calls&&!missing->rifleShots);
 for(unsigned fault=0;fault<4;++fault){
  auto bad=std::make_unique<Loop>();bad->singleFireMagazine=true;CHECK(bad->driver.EnableMagazineOnly(true));bool changed=false;
  bad->beforeDriver=[&](Loop& v){
   if(changed||v.driver.State()!=Bc2ResourceInventoryProbe::Phase::EmptyDwell)return;
   changed=true;auto& view=*v.observed.magazine.resource;
   if(fault==0){v.rifle.loaded=v.rifle.capacity;view.snapshot.counts.loaded=v.rifle.loaded;}
   if(fault==1)--view.snapshot.counts.reserve;
   if(fault==2)++view.request;
   if(fault==3)++view.identity.firing[1];
  };
  CHECK(!bad->Run(false)&&changed&&bad->driver.CancelConsumer()&&!bad->calls);
 }
 return 0;
}
int MagazineOnlyRequiresSupport(){
 for(bool missing:{false,true}){
  auto f=std::make_unique<Loop>();f->singleFireMagazine=true;
  f->memory.Word(NativeInventory::array+4,0);
  CHECK(f->driver.EnableMagazineOnly(false,true,true));
  f->beforeDriver=[&](Loop& loop){
   loop.observed.supportVerified=!missing&&loop.Rifle()&&loop.magRaw.valid;
   loop.observed.supportRaw=loop.magRaw;
   loop.observed.supportTarget=Multiply(Pose(-.2f,-.25f,.45f),Pose(5,2,3));
  };
  unsigned heldTicks=0;
  for(unsigned n=0;n<5600&&!f->driver.Completed()&&!f->driver.CancelConsumer();++n){
   // Count actual arbiter custody before each ordinary input/consumer tick.
   const auto left=f->hands.Current(InteractionHand::Left);
   if(f->driver.State()==Bc2ResourceInventoryProbe::Phase::RifleSupport&&left&&left->token.kind==HandClaimKind::WeaponSupport)++heldTicks;
   f->Tick();
  }
  CHECK(f->driver.Completed()==!missing);
  if(!missing){CHECK(f->calls==4&&f->rifleShots==1);CHECK(f->Report().find("\"rifle_support_released\":true")!=std::string::npos);
   CHECK(f->Report().find("\"support_reacquired\":true")!=std::string::npos);}
  if(!missing)CHECK(heldTicks>=75);
  else CHECK(f->calls==0&&!f->rifleShots);
 }
 auto incompatibleStorage=std::make_unique<Bc2ResourceInventoryProbe>();auto& incompatible=*incompatibleStorage;CHECK(!incompatible.EnableMagazineOnly(true,false,true));
 for(unsigned fault=0;fault<2;++fault){
  auto f=std::make_unique<Loop>();f->singleFireMagazine=true;
  CHECK(f->driver.EnableMagazineOnly(false,true,true));
  unsigned heldTicks=0;bool interrupted=false;
  f->beforeDriver=[&](Loop& loop){
   loop.observed.supportVerified=loop.Rifle()&&loop.magRaw.valid;
   loop.observed.supportRaw=loop.magRaw;
   loop.observed.supportTarget=Multiply(Pose(-.2f,-.25f,.45f),Pose(5,2,3));
   const auto left=loop.hands.Current(InteractionHand::Left);
   if(loop.driver.State()==Bc2ResourceInventoryProbe::Phase::RifleSupport&&left&&left->token.kind==HandClaimKind::WeaponSupport&&++heldTicks==20){
    if(fault==0)loop.hands.Release(loop.hand,left->token);
    else loop.observed.supportVerified=false;
    interrupted=true;
   }
  };
  CHECK(!f->Run(false)&&interrupted&&f->driver.CancelConsumer()&&!f->calls&&!f->rifleShots);
  CHECK(f->Report().find("\"failure\":23")!=std::string::npos);
 }
 return 0;
}
int SingleCampaignMagazineWithAutomaticFire(){
 for(bool full:{false,true}){
  auto f=std::make_unique<Loop>();f->singleFireMagazine=f->automaticRifle=true;
  f->rifleInterval=50*Ms; // Faster than Nambu's100ms native period.
  f->memory.Word(NativeInventory::array+4,0);
  if(full)f->rifle.loaded=30;
  CHECK(f->driver.EnableMagazineOnly(false,true,true));
  const auto owner=f->native.owner;
  f->beforeDriver=[](Loop& loop){loop.observed.supportVerified=loop.Rifle()&&loop.magRaw.valid;
   loop.observed.supportRaw=loop.magRaw;loop.observed.supportTarget=Multiply(Pose(-.2f,-.25f,.45f),Pose(5,2,3));};
  CHECK(f->Run()&&f->calls==4&&f->magReceipts.size()==4);
  CHECK(f->rifleShots==(full?2u:1u)&&f->native.owner==owner);
  CHECK(f->rifle==AmmunitionCounts(29,161,30)&&!f->shots&&!f->shellSubmits&&!f->selectionCalls);
  const int partial=full?29:22;
  CHECK(f->magReceipts[0].before==AmmunitionCounts(partial,191,30));
  CHECK(f->magReceipts[1].after==AmmunitionCounts(partial,191,30));
  CHECK(f->magReceipts[2].before==AmmunitionCounts(partial,191,30));
  CHECK(f->magReceipts[3].after==AmmunitionCounts(30,161,30));
  CHECK(f->Report().find("\"support_reacquired\":true")!=std::string::npos);
  CHECK(f->now<31000*Ms);
 }
 for(unsigned fault=0;fault<3;++fault){
  auto f=std::make_unique<Loop>();f->singleFireMagazine=f->automaticRifle=true;
  f->memory.Word(NativeInventory::array+4,0);CHECK(f->driver.EnableMagazineOnly(false,true,true));
  bool applied=false;
  f->beforeDriver=[&](Loop& loop){
   loop.observed.supportVerified=loop.Rifle()&&loop.magRaw.valid;
   loop.observed.supportRaw=loop.magRaw;loop.observed.supportTarget=Multiply(Pose(-.2f,-.25f,.45f),Pose(5,2,3));
   if(loop.calls<4)return;
   if(fault==0){loop.observed.supportVerified=false;applied=true;}
   if(fault==1){loop.allowRifleShot=false;applied=true;}
   if(fault==2&&loop.driver.State()==Bc2ResourceInventoryProbe::Phase::WaitRifleShot){
    ++loop.observed.magazine.resource->identity.firing[1];applied=true;}
  };
  CHECK(!f->Run(false)&&applied&&f->calls==4&&f->driver.CancelConsumer());
  CHECK(f->rifleShots==(fault==2?1u:0u));
 }
 auto late=std::make_unique<Loop>();late->singleFireMagazine=late->automaticRifle=late->loseSupportOnMappedFire=true;
 late->memory.Word(NativeInventory::array+4,0);CHECK(late->driver.EnableMagazineOnly(false,true,true));
 late->beforeDriver=[](Loop& loop){loop.observed.supportVerified=loop.Rifle()&&loop.magRaw.valid;
  loop.observed.supportRaw=loop.magRaw;loop.observed.supportTarget=Multiply(Pose(-.2f,-.25f,.45f),Pose(5,2,3));};
 CHECK(!late->Run(false)&&late->calls==4&&late->lateCancelledMappedFire==1&&!late->rifleShots);
 CHECK(late->driver.CancelConsumer()&&late->Report().find("\"failure\":39")!=std::string::npos);
 ActionOutput unrelated;unrelated.active=true;unrelated.owner=123;unrelated.forward=.25f;
 unrelated.held=Fire|Use;unrelated.pressed=Fire|NextWeapon;unrelated.released=Reload;
 late->driver.FilterCancelledFire(unrelated);
 CHECK(unrelated.active&&unrelated.owner==123&&unrelated.forward==.25f&&unrelated.held==Use&&
       unrelated.pressed==NextWeapon&&unrelated.released==Reload);
 auto stalled=std::make_unique<Loop>();stalled->singleFireMagazine=true;
 stalled->memory.Word(NativeInventory::array+4,0);CHECK(stalled->driver.EnableMagazineOnly(false,true,true));
 stalled->beforeDriver=[](Loop& loop){loop.observed.supportVerified=loop.Rifle()&&loop.magRaw.valid;
  loop.observed.supportRaw=loop.magRaw;loop.observed.supportTarget=Multiply(Pose(-.2f,-.25f,.45f),Pose(5,2,3));};
 for(unsigned n=0;n<3000&&!stalled->input.hands[1].trigger&&!stalled->driver.CancelConsumer();++n)stalled->Tick();
 CHECK(stalled->input.hands[1].trigger==1&&!stalled->driver.CancelConsumer());
 const auto generation=stalled->input.generation;
 stalled->driver.Prepare(stalled->input,stalled->native.owner,stalled->Asset(),stalled->observed,
   stalled->now,stalled->now+100*Ms,stalled->now+30*Ms);
 CHECK(stalled->driver.CancelConsumer()&&stalled->input.hands[1].trigger==0&&stalled->input.generation==generation);
 CHECK(stalled->Report().find("\"failure\":40")!=std::string::npos);
 return 0;
}
int MagazineOnlySubset(){
 auto f=std::make_unique<Loop>();CHECK(f->driver.EnableMagazineOnly());
 CHECK(!f->driver.EnableMagazineOnly()&&!f->driver.RequireRifleSupport()&&!f->driver.EnablePump(f->calibration));
 CHECK(f->Run()&&f->calls==4&&!f->shots&&!f->shellSubmits&&!f->selectionCalls);
 CHECK(f->rifle==AmmunitionCounts(30,161,30)&&f->shellLoaded==8&&f->shellReserve==24);
 CHECK(f->Report().find("\"magazine_only\":true")!=std::string::npos);
 // A missing native receipt must still fail; the subset cannot declare success
 // merely because its gesture ended or because the other mechanism was omitted.
 auto missing=std::make_unique<Loop>();CHECK(missing->driver.EnableMagazineOnly());
 missing->allowSeatReceipt=false;CHECK(!missing->Run()&&!missing->driver.Completed());
 CHECK(!missing->shots&&!missing->shellSubmits);
 auto started=std::make_unique<Loop>();started->Tick();CHECK(!started->driver.EnableMagazineOnly());
 auto mixedStorage=std::make_unique<Bc2ResourceInventoryProbe>();auto& mixed=*mixedStorage;CHECK(mixed.RequireRifleSupport()&&!mixed.EnableMagazineOnly());
 return 0;
}

#include "SupportOnly373Checks.inc"
int UnreachablePoseKeepsRealThreePumpTwoShellConsumers(){
 auto f=std::make_unique<Loop>(true,3,Bc2PumpControllerProbe::Scenario::HeldSupportNatural);
 f->guidePumpRendering=true;
 CHECK(!f->driver.EnableUnreachablePumpPose());
 CHECK(f->driver.EnableTwoShells()&&f->driver.EnableUnreachablePumpPose());
 CHECK(!f->driver.EnableUnreachablePumpPose());
 unsigned alternate=0,returned=0;
 for(unsigned n=0;n<5900&&!f->driver.Completed()&&!f->driver.CancelConsumer();++n){
  f->Tick();const auto& p=f->input.hands[1].grip.position;
  if(f->driver.State()==Bc2ResourceInventoryProbe::Phase::Pump&&
     std::abs(p.x)<.001f&&std::abs(p.y+.22f)<.001f&&std::abs(p.z+.40f)<.001f)++alternate;
  if(f->driver.State()==Bc2ResourceInventoryProbe::Phase::Shell&&
     std::abs(p.x)<.001f&&std::abs(p.y+.40f)<.001f&&std::abs(p.z)<.001f)++returned;
 }
 if(!f->driver.Completed())std::cerr<<f->Report()<<'\n';
 CHECK(f->driver.Completed()&&alternate>0&&returned>0);
 CHECK(f->shots==3&&f->pumpNative.releases==3&&f->pumpNative.acks==3&&f->pumpSupports==3);
 CHECK(f->shellSubmits==2&&f->shellReceipts.size()==2&&f->shellLoaded==7&&f->shellReserve==22);
 CHECK(f->calls==6&&f->rifleShots==1&&f->selectionCalls==2&&!f->pumpRightJump);
 CHECK(!f->driver.EnableUnreachablePumpPose());
 auto ordinaryStorage=std::make_unique<Bc2ResourceInventoryProbe>();auto& ordinary=*ordinaryStorage;CHECK(!ordinary.EnableUnreachablePumpPose());
 CHECK(ordinary.EnablePump(f->calibration)&&!ordinary.EnableUnreachablePumpPose());
 std::puts("Raised-forward controller fixture retains actual consumer contact, three shots and two shells; mocked native/packs, no IK coverage claim");
 return 0;
}
int main(){if(SingleCampaignMagazineWithAutomaticFire()||UnreachablePoseKeepsRealThreePumpTwoShellConsumers()||ThreePumpsTwoShellsAcrossPersistentConsumers()||PumpAgainAfterOtherWeapon()||SupportWithoutMagazineEnrollment()||MagazineOnlyRequiresSupport()||MagazineRefillThenFire()||EmptyMagazineOrdinaryInput()||MagazineOnlySubset()||MagazineReplacementKeepsActualRemovedResource()||EmptyTubeAcrossPersistentConsumers()||CombinedRequiresRifleSupportBeforeReload()||QueuedPumpInterruptionPreservesTrackingOverride()||FinalAutomaticRifleShots()||FinalRifleCannotCompleteFromRegressedSnapshot()||FinalRifleRejectsForeignAmmunition()||FinalRifleRequiresFreshObservation()||CombinedPartialNativeTransferAfterPriorLeaseExpiry()||QueuedShellSupportUsesCurrentClock()||ExpiryDuringQueuedShellSupport()||AdvancingShellClockAfterAcknowledgedPump()||CombinedManualPump()||CombinedPumpMissingEvidence()||Combined()||MissingEvidence()||Interruptions()||BoundedInputJournal())return 1;std::puts("Persistent real body/resource/shell consumer composition and negative boundaries passed");return 0;}
