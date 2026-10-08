#include "Test.h"
#include "Bc2InventoryReloadProbe.h"
#include "Bc2MagazineReloadSession.h"
#include "Bc2BodyInventorySession.h"
#include "Bc2ReloadRecordWindow.h"
#include "Bc2BodyHolsterProbeFixture.h"
#include <sstream>
using namespace body_probe_test;
namespace {
struct Driver {
 Fixture f{true};Bc2InventoryReloadProbe probe;InputFrame input{};BodyInventoryDisplay display{};
 std::shared_ptr<BodyHolsterProbeSample> body;
 Driver(){input.focused=input.headValid=true;input.spaceGeneration=f.s.nativeOwner.space;
  for(auto& h:input.hands){h.active=Components;h.gripTracked=h.aimTracked=true;}
  input.hands[0].grip.position={-.2f,-.25f,-.45f};input.hands[1].grip.position={.15f,-.10f,-.20f};}
 void Publish(){body=std::make_shared<BodyHolsterProbeSample>();body->nativeOwner=f.s.nativeOwner;body->sampledNs=f.s.hand.nowNs;
  body->hand=f.s.hand;body->input=input;body->input.generation=f.s.hand.sequence;body->physicalGun=f.s.gun;body->nativeTick=f.s.nativeTick;
  body->phase=f.adapter.Phase();body->request=f.adapter.RequestId();body->selectedSlot=BodySlotAssignment{1,f.items[0].key};
  body->right=f.hands.Current(InteractionHand::Right);body->outcome=f.out;body->visibility=f.s.visibility;body->suppression=f.s.suppression;
  display.selectedOwner=f.s.nativeOwner;display.physicalOwner=f.s.hand.owner;display.physicalSelected=f.items[0].key;
  display.revision=display.cohort=1;display.sequence=f.s.hand.sequence;display.observedNs=f.s.hand.observedNs;display.deadlineNs=f.s.hand.deadlineNs;
  display.count=2;display.carried.inventory=0x120000;display.carried.switching=0x130000;display.carried.count=2;
  for(unsigned n=0;n<2;++n){display.slots[n].assignment={n+1,f.items[n].key};display.slots[n].native={unsigned(f.items[n].key.id),0x80000u+n*0x1000,1,0,n};display.carried.items[n]=display.slots[n].native;}}
 void Tick(unsigned mutation=0){f.Advance();f.Tick();Publish();input.generation=f.s.hand.sequence;input.predictedNs=f.s.hand.nowNs;
  if(mutation==1)body->right.reset();if(mutation==2)display.slots[1].assignment.item.generation++;
  if(mutation==3)body->hand.deadlineNs=f.s.hand.nowNs;
  probe.Prepare(input,f.s.nativeOwner,Xm8MagazineAsset,body,display,{},{},f.s.hand.observedNs,f.s.hand.deadlineNs,f.s.hand.nowNs);}
 void Start(){for(unsigned n=0;n<250&&probe.State()==Bc2InventoryReloadProbe::Phase::Warmup;++n)Tick();}
};
int Config(){constexpr auto flags=0x12197809u;
 CHECK(ValidMagazineReloadSession(8,flags,60000)&&MagazineDetachedSessionEnabled(8));
 CHECK(!ValidMagazineReloadSession(8,flags,30000)&&!ValidMagazineReloadSession(8,flags|0x400u,60000));
 CHECK(ValidMagazineReloadSession(7,flags,60000)&&ValidBodyInventoryConfig(flags)&&MagazineDetachedSessionEnabled(7));
 CHECK(!ValidMagazineReloadSession(7,flags,30000)&&!ValidMagazineReloadSession(7,flags&~0x10000000u,60000));
 for(auto bit:{0x400u,0x200000u,0x4000000u,0x80000000u,0x100u})CHECK(!ValidMagazineReloadSession(7,flags|bit,60000));return 0;}
int RecordWindow(){ReloadRecordWindow normal;CHECK(!normal.OpenDeferred(1));normal.Start(1000);
 CHECK(normal.StartNs()==1000&&normal.Contains(1000)&&!normal.Contains(999)&&!normal.Defer());
 CHECK(!normal.Contains(1000+ReloadRecordWindow::Duration));normal.Start(2000);CHECK(normal.StartNs()==1000);
 ReloadRecordWindow deferred;CHECK(deferred.Defer());deferred.Start(1000);CHECK(!deferred.Contains(5000000000ll));
 CHECK(!deferred.OpenDeferred(0)&&deferred.OpenDeferred(5000000000ll));CHECK(deferred.StartNs()==5000000000ll);
 CHECK(deferred.OpenDeferred(6000000000ll)&&deferred.StartNs()==5000000000ll);
 CHECK(!deferred.Contains(5000000000ll+ReloadRecordWindow::Duration));
 ReloadRecordWindow recovery;CHECK(recovery.Defer(true)&&!recovery.Defer());recovery.Start(1000);
 CHECK(recovery.DurationNs()==40000000000ll&&recovery.OpenDeferred(5000000000ll));
 CHECK(recovery.Contains(25000000000ll)&&!recovery.Contains(45000000000ll));
 CHECK(recovery.OpenDeferred(44000000000ll)&&recovery.StartNs()==5000000000ll&&!recovery.Defer(true));
 ReloadRecordWindow combo;CHECK(!combo.CombinedPump());CHECK(combo.Defer(true)&&combo.CombinedPump());
 CHECK(combo.DurationNs()==60000000000ll&&!combo.CombinedPump()&&!combo.Pump(8));
 combo.Start(1000);CHECK(!combo.CombinedPump()&&!combo.Contains(9000));CHECK(combo.OpenDeferred(5000000000ll));
 CHECK(combo.Contains(64000000000ll)&&!combo.Contains(65000000000ll));
 CHECK(combo.OpenDeferred(64000000000ll)&&combo.StartNs()==5000000000ll&&!combo.CombinedPump());
 ReloadRecordWindow shortDeferred;CHECK(shortDeferred.Defer()&&!shortDeferred.CombinedPump());return 0;}
int MissingNativeAcknowledgement(){Driver d;d.Start();CHECK(d.probe.State()==Bc2InventoryReloadProbe::Phase::ReachStow);
 for(unsigned n=0;n<700&&!d.probe.CancelConsumer();++n)d.Tick();
 CHECK(d.probe.CancelConsumer()&&!d.probe.Completed());std::ostringstream o;d.probe.Report(o);
 CHECK(o.str().find("\"other_claim\":0")!=std::string::npos);return 0;}
int MissingOrStaleClaim(){for(unsigned mode:{1u,3u}){Driver d;for(unsigned n=0;n<650&&!d.probe.CancelConsumer();++n)d.Tick(mode);
 CHECK(d.probe.CancelConsumer()&&!d.probe.Completed());}return 0;}
int ChangedInventory(){Driver d;d.Start();CHECK(d.probe.State()==Bc2InventoryReloadProbe::Phase::ReachStow);d.Tick(2);
 CHECK(d.probe.CancelConsumer());return 0;}
int StartupAndEmptyContinuity(){Driver d;d.Tick();CHECK(d.probe.State()==Bc2InventoryReloadProbe::Phase::Warmup);
 // The first Held observation is transient during ordinary startup stow.
 CHECK(d.f.Empty());
 for(unsigned n=0;n<220&&d.probe.State()==Bc2InventoryReloadProbe::Phase::Warmup;++n){
  d.f.Cycle();d.Publish();d.input.generation=d.f.s.hand.sequence;d.input.predictedNs=d.f.s.hand.nowNs;
  d.probe.Prepare(d.input,d.f.s.nativeOwner,Xm8MagazineAsset,d.body,d.display,{},{},d.f.s.hand.observedNs,d.f.s.hand.deadlineNs,d.f.s.hand.nowNs);}
 CHECK(d.probe.State()==Bc2InventoryReloadProbe::Phase::ReachInitial);
 Driver h;h.Start();for(unsigned n=0;n<100&&h.probe.State()!=Bc2InventoryReloadProbe::Phase::Stow;++n)h.Tick();
 CHECK(h.probe.State()==Bc2InventoryReloadProbe::Phase::Stow);CHECK(h.f.Empty());h.Publish();
 h.input.generation=h.f.s.hand.sequence;h.input.predictedNs=h.f.s.hand.nowNs;
 h.probe.Prepare(h.input,h.f.s.nativeOwner,Xm8MagazineAsset,h.body,h.display,{},{},h.f.s.hand.observedNs,h.f.s.hand.deadlineNs,h.f.s.hand.nowNs);
 CHECK(h.probe.State()==Bc2InventoryReloadProbe::Phase::ClearEmpty);
 // Recorded first live-run failure: the gun reappeared before clearing the shoulder.
 // A successful reload afterwards must not turn that into a passed holster run.
 h.f.Advance();h.Publish();h.body->phase=BodyHolsterPhase::Held;h.input.generation=h.f.s.hand.sequence;
 h.probe.Prepare(h.input,h.f.s.nativeOwner,Xm8MagazineAsset,h.body,h.display,{},{},h.f.s.hand.observedNs,h.f.s.hand.deadlineNs,h.f.s.hand.nowNs);
 CHECK(h.probe.CancelConsumer()&&!h.probe.Completed());return 0;}
int DuplicateAndActor(){for(unsigned bad=0;bad<3;++bad){Driver d;d.Tick();const auto original=d.input;
 auto owner=d.f.s.nativeOwner;auto observed=d.f.s.hand.observedNs,deadline=d.f.s.hand.deadlineNs;
 if(bad==0)++deadline;if(bad==1)++owner.weak;if(bad==2)d.input.focused=false;
 d.probe.Prepare(d.input,owner,Xm8MagazineAsset,d.body,d.display,{},{},observed,deadline,d.f.s.hand.nowNs);
 CHECK(d.probe.CancelConsumer()&&d.input.hands[1].squeeze==0);}return 0;}
}
int main(){if(StartupAndEmptyContinuity()||RecordWindow()||Config()||MissingNativeAcknowledgement()||MissingOrStaleClaim()||ChangedInventory()||DuplicateAndActor())return 1;
 std::puts("Combined inventory/reload driver: config, real-policy baseline, missing receipts, identity and freshness checks passed; live sequence unverified");}
