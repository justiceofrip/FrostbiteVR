#include "Test.h"
#include "ResourceInventoryNativeFixture.h"
#include "Bc2ResourceInventoryProbe.h"
#include "Bc2PumpCalibration225.h"
#include "Bc2PumpActions.h"
#include "fvr/interaction/ControllerInput.h"
#include "fvr/interaction/TrackedRig.h"
#include "fvr/interaction/ReloadGrip.h"
#include <iostream>
#include <sstream>
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
 unsigned pumpSupports=0,shellSupports=0,rifleShots=0;bool pumpFirePose=false,pumpRightJump=false;std::optional<math::Pose> previousPumpRight;SupportGrip support;
 PumpNativeBoundary pumpNative;std::optional<Bc2PhysicalPump> pump;Bc2PumpRawContact pumpRaw;Bc2PumpPackCounters pumpPacks;
 Bc2PumpCalibration calibration=MeasuredSpasPump225();Bc2PhysicalPumpResult pumpResult;
 explicit Loop(bool withPump=false):manualPump(withPump){
  if(manualPump){if(!driver.EnablePump(calibration))throw std::runtime_error("pump enable");pump.emplace(std::make_shared<Bc2PumpCalibration>(calibration),pumpNative.Api());}
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
 AmmunitionSnapshot Snapshot(){return {binding.context,++nativeSequence,now,now+100*Ms,rifle,true};}
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
   SupportGripOwner so{hand.owner.actor,hand.owner.actorGeneration,native.owner.weapon};
   const auto source=pumpRaw.valid?pumpRaw.input:shellRaw.inputEvidence;
   const auto inverse=InverseRigid(shellRaw.weaponWorldMeters);
   float distance=10;if(!Rifle()&&shellRaw.valid&&inverse)distance=Distance(Multiply(shellRaw.rawLeftWristWorldMeters,*inverse),calibration.closedWrist);
   const SupportGripContact contact{!Rifle()&&shellRaw.valid,distance,{}};auto proposed=support;
   const bool busy=pumpResult.ownsHand||shellResult.ammoOwnsHand||magResult.ownsLeftHand||bodyResult.blockWeaponActions;
   auto returned=pump->ContinueSupport(so,input,contact,source,{2,native.owner.weapon},hands,proposed,intent,busy||driver.CancelConsumer());
   if(returned)++pumpSupports;
   auto shellReturned=returned?std::optional<SupportGripResult>{}:shell->ContinueSupport(so,input,contact,source,{2,native.owner.weapon},hands,proposed,intent,busy||driver.CancelConsumer());
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
   support=proposed;pump->BindSupport(pumpResult.tracking,supported,hands);
   if((driver.State()!=Bc2ResourceInventoryProbe::Phase::Pump||allowPumpSupport)&&
      (driver.State()!=Bc2ResourceInventoryProbe::Phase::ShellSupport||allowShellSupport))
    driver.ObserveSupport(pumpResult.tracking,supported,hand,source,hands.Current(InteractionHand::Left),hands.Current(InteractionHand::Right),shellReturned.has_value(),now);
  }
  if(advancingShellClock)hand.nowNs=processingNow; // Body processing follows support ownership.
  BodyDrawSample d;d.owner=native.owner;d.hand=hand;d.input=input;d.gun=gun;d.visible=BodyVisibleRig{native.owner,sequence,now,now+100*Ms};
  d.reloadBusy=pumpDebt||magazine->BlocksEquipment()||shell->BlocksEquipment();d.interaction=((action.held|action.pressed)&Fire)?BodyInteractionState::Suspended:BodyInteractionState::Available;
  auto bs=BodySample();bodyResult=inventory.TickHolster(memory.Memory(),d,bs,holster,hands,intent);PublishBody(bs);
  if(allowSelect&&bodyResult.select){const auto read=ReadBodyInventory(memory.Memory(),native.owner,true);if(read.snapshot&&ResolveBodyDraw(*read.snapshot,unsigned(bodyResult.select->id))){selectedNext=bodyResult.select->id==NativeInventory::b?1:0;++selectionCalls;}}
  if(allowShot&&!Rifle()&&(action.pressed&Fire)&&!bodyResult.blockWeaponActions){--shellLoaded;++shots;shotAt=now;if(manualPump){pumpNative.firedAt=now;const auto p=input.hands[1].grip.position;
   pumpFirePose=std::hypot(p.x,p.y+.40f,p.z)<.001f;}}
  if(manualPump&&allowRifleShot&&Rifle()&&(action.pressed&Fire)&&!bodyResult.blockWeaponActions){--rifle.loaded;++rifleShots;}
  NativeDrain();
  const auto body=Pose(5,2,3),left=Multiply(Pose(-.2f,-.25f,.45f),body),right=Multiply(Pose(.15f,-.1f,.2f),body);
  std::array<ArmAnchor,2> arms{{{{4.8f,1.8f,3},{0,1,0}},{{5.2f,1.8f,3},{0,1,0}}}};
  if(const auto posed=rig.Update({1,2,3,4},input,body,left,right,right,arms)){auto attachment=Pose();attachment.values[1][1]=attachment.values[2][2]=-1;
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

int QueuedShellSupportUsesCurrentClock(){Loop stale(true);stale.advancingShellClock=true;
 CHECK(!stale.Run(false));CHECK(stale.Report().find("\"failure\":22")!=std::string::npos);
 CHECK(stale.shellSubmits==1&&stale.shellSupports==1&&stale.pumpSupports==1&&stale.rifleShots==0);
 CHECK(stale.queuedSupportRejected==1&&stale.queuedSupportRetained==1&&stale.queuedSupportReason==HandInteractionReason::StaleInput);
 CHECK(stale.shellPairObserved&&!stale.shellPairLost&&!stale.sourceRestamped);
 std::cout<<"Original queued shell support renewal reproduces failure22 with one stale rejection and retained claim\n";
 return 0;}
int ExpiryDuringQueuedShellSupport(){Loop f(true);f.advancingShellClock=f.refreshOwnershipClock=f.expireAfterAdoption=true;
 CHECK(!f.Run(false));CHECK(f.shellSupports==1&&f.shellSubmits==1&&f.rifleShots==0);
 CHECK(f.queuedSupportRejected==1&&f.queuedSupportRetained==0&&f.queuedSupportReason==HandInteractionReason::StaleInput);
 CHECK(!f.sourceRestamped);std::cout<<"Forward clock expiry rejects support renewal without restamping input or source\n";return 0;}
int AdvancingShellClockAfterAcknowledgedPump(){Loop f(true);f.advancingShellClock=f.refreshOwnershipClock=true;CHECK(f.Run(false));
 CHECK(f.shellPairObserved&&!f.shellPairLost);CHECK(f.shellSubmits==1&&f.shellSupports==1&&f.pumpSupports==1&&f.rifleShots==1);
 CHECK(f.pumpNative.releases==1&&f.pumpNative.acks==1&&f.calls==6);CHECK(!f.queuedSupportRejected&&!f.sourceRestamped);return 0;}
int CombinedPartialNativeTransferAfterPriorLeaseExpiry(){Loop f(true);
 f.advancingShellClock=f.refreshOwnershipClock=f.splitShellTransfer=true;CHECK(f.Run(false));
 CHECK(f.submittedCohortWaits==4&&f.shellSubmits==1&&f.shellSupports==1&&f.pumpSupports==1&&f.rifleShots==1);
 CHECK(f.pumpNative.releases==1&&f.pumpNative.acks==1&&f.calls==6&&!f.shellPairLost&&!f.sourceRestamped);
 CHECK(f.rifle==AmmunitionCounts(29,161,30)&&f.shellLoaded==8&&f.shellReserve==23);
 std::cout<<"Combined original pending shell survives partial native transfer after old lease expiry; exact completion/support/rifle return conserved\n";return 0;}
int CombinedManualPump(){Loop f(true);CHECK(f.Run());
 CHECK(f.calls==6&&f.shots==1&&f.shellSubmits==1&&f.selectionCalls==2&&f.rifleShots==1);
 CHECK(f.pumpFirePose&&!f.pumpRightJump);
 CHECK(f.pumpNative.releases==1&&f.pumpNative.acks==1&&f.pumpSupports==1&&f.shellSupports==1);
 CHECK(f.rifle==AmmunitionCounts(29,161,30)&&f.shellLoaded==8&&f.shellReserve==23);
 CHECK(f.Report().find("\"manual_pump\":true")!=std::string::npos&&f.Report().find("\"input_rows_dropped\":0")!=std::string::npos);
 std::cout<<"Combined pump: real persistent body, resource, shell, pump and support consumers; one physical pump, two support returns, final rifle shot\n";return 0;}
int CombinedPumpMissingEvidence(){for(unsigned fault=0;fault<4;++fault){Loop f(true);
 if(fault==0)f.allowPumpPairs=false;if(fault==1)f.allowPumpSupport=false;if(fault==2)f.allowShellSupport=false;if(fault==3)f.allowRifleShot=false;
 CHECK(!f.Run(false)&&f.driver.CancelConsumer());
 if(fault<2)CHECK(f.shellSubmits==0&&f.selectionCalls==1);
 if(fault==2)CHECK(f.shellSubmits==1&&f.selectionCalls==1&&f.rifleShots==0);
 if(fault==3)CHECK(f.shellSubmits==1&&f.selectionCalls==2&&f.rifleShots==0);
 }return 0;}
int Combined(){Loop f;CHECK(f.profile->Ready());CHECK(f.Run());CHECK(f.calls==6&&f.shots==1&&f.shellSubmits==1&&f.selectionCalls==2);
 CHECK(f.rifle==AmmunitionCounts(30,161,30));CHECK(f.shellLoaded==8&&f.shellReserve==23);CHECK(f.mappings==f.sequence);
 CHECK(f.magReceipts.size()==6&&f.shellReceipts.size()==1);CHECK(!f.magazine->BlocksEquipment()&&!f.shell->BlocksEquipment());
 CHECK(f.hands.Current(InteractionHand::Right)&&!f.hands.Current(InteractionHand::Left));CHECK(f.selected==0);
 const auto calls=f.calls;for(unsigned n=0;n<7200;++n)f.Tick();CHECK(f.driver.Completed()&&f.calls==calls&&f.shots==1&&f.shellSubmits==1);
 CHECK(f.Report().find("\"input_rows_dropped\":0")!=std::string::npos);
 // Only replace the input coordinator. Real consumer, service, inventory, hand,
 // native request and shell completion counters survive this second lap.
 f.driver=Bc2ResourceInventoryProbe{};CHECK(f.Run());CHECK(f.calls==12&&f.shots==2&&f.shellSubmits==2&&f.selectionCalls==4);
 CHECK(f.rifle==AmmunitionCounts(30,131,30)&&f.shellLoaded==8&&f.shellReserve==22);
 CHECK(f.shell->ProbeState(f.now).completed==2&&f.Report().find("\"counter_baseline\":{\"acquired\":1,\"submitted\":1,\"completed\":1}")!=std::string::npos);
 std::cout<<"Two persistent laps:12 native magazine operations,2 mapped shots,2 shell receipts,4 verified selections; final rifle30/131,SPAS8/22\n";return 0;}
int MissingEvidence(){for(unsigned failure=0;failure<6;++failure){Loop f;
 if(failure==0)f.allowSeatReceipt=false;if(failure==1)f.allowVisibility=false;if(failure==2)f.allowSelect=false;
 if(failure==3)f.allowShot=false;if(failure==4)f.allowRetirement=false;if(failure==5)f.earlyMapping=true;
 CHECK(!f.Run(false)&&f.driver.CancelConsumer());CHECK(f.calls<=4&&f.selectionCalls<=1&&!f.driver.Completed());
 if(failure==0)CHECK(f.calls==2&&f.shots==0);if(failure==1||failure==2)CHECK(f.calls==4&&f.shots==0);
 if(failure==3||failure==5)CHECK(f.shots==0&&f.shellSubmits==0);if(failure==4)CHECK(f.shots==1&&f.shellSubmits==1&&f.shell->BlocksEquipment());
 std::cout<<"Rejected boundary fault "<<failure<<" after actual magazine calls="<<f.calls<<",shots="<<f.shots<<",shell submits="<<f.shellSubmits<<'\n';}
 return 0;}
int Interruptions(){for(unsigned failure=0;failure<4;++failure){Loop f;
 const auto target=failure==3?Bc2ResourceInventoryProbe::Phase::Fire:Bc2ResourceInventoryProbe::Phase::FirstMagazine;
 for(unsigned n=0;n<4000&&f.driver.State()!=target&&!f.driver.CancelConsumer();++n)f.Tick();CHECK(f.driver.State()==target);
 if(failure<2)f.Tick(failure+1);
 else if(failure==2){auto duplicate=f.input;f.driver.Prepare(duplicate,f.native.owner,f.Asset(),f.observed,f.now+1,f.now+100*Ms,f.now+1);CHECK(!duplicate.hands[0].squeeze&&!duplicate.hands[1].trigger);}
 else {++f.native.owner.weapon;f.Tick();}
 CHECK(f.driver.CancelConsumer()&&!f.driver.Completed());}
 return 0;}
int BoundedInputJournal(){
 Bc2ResourceInventoryProbe driver;ResourceInventoryObservation unavailable;ReloadStateOwner owner{1,2,3,4,5,6,7};
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
}
int main(){if(CombinedPartialNativeTransferAfterPriorLeaseExpiry()||QueuedShellSupportUsesCurrentClock()||ExpiryDuringQueuedShellSupport()||AdvancingShellClockAfterAcknowledgedPump()||CombinedManualPump()||CombinedPumpMissingEvidence()||Combined()||MissingEvidence()||Interruptions()||BoundedInputJournal())return 1;std::puts("Persistent real body/resource/shell consumer composition and negative boundaries passed");return 0;}
