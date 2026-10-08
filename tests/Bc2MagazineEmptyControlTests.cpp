#include "Bc2MagazineEmptyControl.h"
#include "Bc2MagazineEmptyDiagnostic.h"
#include "Bc2ReloadAbort.h"
#include "Bc2MagazineReloadCycle.h"
#include "Bc2ReloadInvocationEntry.h"
#include <cstring>
#include "Bc2MagazineNativeFixture.h"
#include "Test.h"
#include <stdexcept>
#include <iostream>
using namespace fvr::bc2;
namespace {
constexpr auto T=1000000000ll;
MagazineEmptyStep Sample(unsigned n=0){auto i=test::Input();MagazineEmptyStep s;
 s.identity=i.identity;s.sourceSequence=77;s.ownerRevision=8;s.observedNs=T;s.deadlineNs=T+100000000;s.branch=n;
 s.before=i.branches[n];s.before.currentState=s.before.nextState=2;s.before.phaseTimer=0;s.before.loaded=0;return s;}
int ScopedReadiness(){auto sample=Sample();auto i=test::Input();
 for(const int loaded:{0,1,30}){sample.before.loaded=loaded;
  CHECK(MagazineEmptyStepEligible(sample,i.config,ReloadRequestCyclePhase::Idle,i.context,T+1));}
 CHECK(MagazineEmptyStepEligible(sample,i.config,ReloadRequestCyclePhase::Arming,i.context,T+1));
 for(auto phase:{ReloadRequestCyclePhase::Holding,ReloadRequestCyclePhase::Advancing})
  CHECK(!MagazineEmptyStepEligible(sample,i.config,phase,i.context,T+1));
 i.context.inputFlags=1;i.context.fireRequested=true;
 CHECK(MagazineEmptyStepEligible(sample,i.config,ReloadRequestCyclePhase::Cancelled,i.context,T+1));
 i.context.inputFlags=4;i.context.fireRequested=false;i.context.reloadRequested=true;
 CHECK(MagazineEmptyStepEligible(sample,i.config,ReloadRequestCyclePhase::Finished,i.context,T+1));
 return 0;}
int StrictBoundaries(){for(unsigned bad=0;bad<14;++bad){auto s=Sample();auto i=test::Input();auto now=T+1;
 switch(bad){case 0:s.profile=static_cast<NativeMagazineProfileId>(255);break;
 case 1:i.config.assetName={};break;case 2:i.config.fireLogicType=4;break;
 case 3:i.config.reloadType=0;break;case 4:s.ownerRevision=0;break;
 case 5:s.identity.owner.space=0;break;case 6:s.before.currentState=6;break;
 case 7:s.before.nextState=10;break;case 8:i.context.flags24Through28[4]=true;break;
 case 9:i.context.orderRequested=true;i.context.inputFlags=2;break;
 case 10:now=s.deadlineNs;break;case 11:now=T-1;break;
 case 12:s.before.loaded=-1;break;case 13:s.before.flagsA8=8;break;}
 CHECK(!MagazineEmptyStepEligible(s,i.config,ReloadRequestCyclePhase::Idle,i.context,now));}return 0;}
struct Bytes {std::array<std::uint8_t,48> bytes{};unsigned calls=0;bool fail=false;
 MagazineEmptyByteAccess Access(){return {this,[](void* p,std::uint8_t expected,std::uint8_t replacement,std::uint8_t& seen)noexcept{
  auto& b=*static_cast<Bytes*>(p);if(b.fail)return false;seen=b.bytes[0x28];if(seen==expected)b.bytes[0x28]=replacement;return true;}};}};
int ExactByteAndExceptionalRestore(){for(unsigned original:{0u,1u}){Bytes b;for(unsigned n=0;n<48;++n)b.bytes[n]=std::uint8_t(n);b.bytes[0x28]=std::uint8_t(original);
 const auto saved=b.bytes;MagazineEmptyByteOverride p;
 RunMagazineEmptyByteOverride(b.Access(),std::uint8_t(original),true,[](void* c){auto& b=*static_cast<Bytes*>(c);++b.calls;b.fail=b.bytes[0x28]!=1;},&b,p);
 CHECK(!b.fail&&b.calls==1&&b.bytes==saved&&p.applied==(original==0)&&p.restored==(original==0));}
 Bytes b;MagazineEmptyByteOverride p;try {RunMagazineEmptyByteOverride(b.Access(),0,true,[](void* c){++static_cast<Bytes*>(c)->calls;throw std::runtime_error("original");},&b,p);}catch(const std::runtime_error&){}
 CHECK(b.calls==1&&b.bytes[0x28]==0&&p.applied&&p.restored);
 MagazineEmptyByteOverride q;b.bytes[0x28]=0;CHECK(q.Apply(b.Access(),0));b.bytes[0x28]=2;
 CHECK(!q.Restore(b.Access())&&q.unexpectedNativeWrite&&b.bytes[0x28]==2);return 0;}
int LastShotInputLoopAndExplicitReload(){
 // Model only the disassembled state-2 decision. Shot consumption happens
 // outside this scoped Step call and sees the original complete context.
 for(bool controlled:{false,true}){Bytes b;b.bytes[0x24]=1;b.bytes[0x2c]=1;int loaded=1;unsigned next=2;
  CHECK(b.bytes[0x28]==0);--loaded; // separate ordinary native shot callback
  struct Call {Bytes* b;int loaded;unsigned* next;} c{&b,loaded,&next};MagazineEmptyByteOverride p;
  RunMagazineEmptyByteOverride(b.Access(),0,controlled,[](void* value){auto& c=*static_cast<Call*>(value);++c.b->calls;
   if(c.b->bytes[0x24]&&!c.b->bytes[0x28]&&*c.next==2&&c.loaded==0)*c.next=10;},&c,p);
  CHECK(loaded==0&&next==(controlled?2u:10u)&&b.bytes[0x28]==0&&b.bytes[0x2c]==1);
  // A deliberate physical Reload pulse bypasses override and its native transition runs.
  next=2;RunMagazineEmptyByteOverride(b.Access(),0,false,[](void* value){auto& c=*static_cast<Call*>(value);if(!c.b->bytes[0x28])*c.next=10;},&c,p);
  CHECK(next==10&&loaded==0);
 }return 0;}
int OriginalReceiptIntersection(){MagazineEmptyControlReceipts receipts;MagazineEmptyByteOverride p;p.applied=p.restored=true;
 for(unsigned n=0;n<3;++n){auto s=Sample(n);s.deadlineNs-=std::int64_t(n)*1000000;CHECK(receipts.Observe(s,s.before,p,true,T+1));}
 const auto s=Sample();auto end=receipts.EmptyDeadline(s.identity,s.profile,s.ownerRevision,s.before.reserve,T+2);CHECK(end&&*end==T+98000000);
 CHECK(!receipts.EmptyDeadline(s.identity,s.profile,s.ownerRevision,s.before.reserve,*end));
 auto other=s.identity;++other.owner.space;CHECK(!receipts.EmptyDeadline(other,s.profile,s.ownerRevision,s.before.reserve,T+2));
 CHECK(!receipts.EmptyDeadline(s.identity,s.profile,s.ownerRevision+1,s.before.reserve,T+2));
 CHECK(!receipts.EmptyDeadline(s.identity,NativeMagazineProfileId::AuthoredAek,s.ownerRevision,s.before.reserve,T+2));
 CHECK(!receipts.EmptyDeadline(s.identity,s.profile,s.ownerRevision,s.before.reserve-1,T+2));
 auto after=s.before;++after.loaded;CHECK(!receipts.Observe(s,after,p,true,T+2));
 CHECK(!receipts.EmptyDeadline(s.identity,s.profile,s.ownerRevision,s.before.reserve,T+3));return 0;}
int ServerLeaseCannotRenewWithOwner(){MagazineEmptyControlReceipts receipts;MagazineEmptyByteOverride patch;patch.applied=patch.restored=true;
 for(unsigned n=0;n<3;++n){auto s=Sample(n);s.deadlineNs=MagazineEmptyEvidenceDeadline(s.observedNs,T+250000000);
  CHECK(s.deadlineNs==T+200000000&&receipts.Observe(s,s.before,patch,true,T+1));}
 const auto s=Sample();CHECK(receipts.EmptyDeadline(s.identity,s.profile,s.ownerRevision,s.before.reserve,T+199999999));
 // A newer same-owner publication does not renew the stored Step/server proof.
 CHECK(!receipts.EmptyDeadline(s.identity,s.profile,s.ownerRevision,s.before.reserve,T+200000000));
 for(unsigned n=0;n<3;++n){auto renewed=Sample(n);renewed.sourceSequence++;renewed.observedNs=T+150000000;
  renewed.deadlineNs=MagazineEmptyEvidenceDeadline(renewed.observedNs,T+400000000);
  CHECK(receipts.Observe(renewed,renewed.before,patch,true,T+200000000));}
 CHECK(receipts.EmptyDeadline(s.identity,s.profile,s.ownerRevision,s.before.reserve,T+200000001));
 auto invalid=s;invalid.deadlineNs=T+250000000;CHECK(!receipts.Observe(invalid,invalid.before,patch,true,T+1));
 CHECK(!MagazineEmptyEvidenceDeadline(INT64_MAX-1,INT64_MAX));return 0;}
int EmptyNativeHoldAndConservedTransfer(){test::Simulation f(0,7);CHECK(f.Arm());
 CHECK(f.Lease()&&f.Lease()->loaded==0&&f.Lease()->reserve==7&&f.Unseat());auto r=f.Request();CHECK(r.reservedUnits==7);
 CHECK(f.policy.Submit(r,f.input.nowNs));CHECK(f.Transfer(0,7));CHECK(f.Transfer(1,7,false));CHECK(f.Transfer(2,7));CHECK(f.Settle());
 const auto ack=f.policy.TakeAcknowledgement(f.input.identity,f.control.cycle,f.input.nowNs);CHECK(ack&&ack->verified);
 for(const auto& b:f.input.branches)CHECK(b.loaded==7&&b.reserve==0);return 0;}
int EmptyCancellationNeedsScopedAdapter(){test::Simulation f(0,83);CHECK(f.Arm());auto held=*f.Lease();
 ReloadRoundLease lease{held.identity,held.cycle,held.sequence,held.observedNs,held.deadlineNs,held.loaded,held.reserve,held.capacity,held.nativeBindingVerified,held.allThreeHeld};
 ReloadAbortCleanup noGate;CHECK(!noGate.ArmMagazine(lease,8,f.input.nowNs,true,Xm8MagazineNativeProfile));
 ReloadAbortCleanup p;CHECK(p.ArmMagazine(lease,8,f.input.nowNs,true,Xm8MagazineNativeProfile,true));
 for(unsigned n=0;n<3;++n){f.input.branch=n;const auto d=p.Claim(f.input,8,held.cycle,true,100+n,true,f.input.nowNs);CHECK(d.call);
  auto after=d.before;after.currentState=after.nextState=2;after.phaseTimer=0;CHECK(p.Finish(d,after,true,true,f.input.nowNs+1));}
 CHECK(p.Completed()==7&&!p.Active());ReloadAbortCleanup spas;CHECK(!spas.Arm(lease,8,f.input.nowNs,true));return 0;}
int NestedStepAdmissionAndDrain(){std::atomic<unsigned> active=0;std::atomic<std::uint64_t> revision=0;std::atomic_flag exclusive;
 CHECK(EnterReloadInvocation(active,revision,exclusive)); // actual outer Update
 const auto outerRevision=revision.load();CHECK(active==1);
 CHECK(EnterReloadInvocation(active,revision,exclusive)); // new nested Step
 CHECK(active==2&&revision>outerRevision);
 {ReloadInvocationExclusion start(exclusive,active,revision);CHECK(start.held&&!start.Quiet());
  // A new callback cannot borrow entry permission while a selection tries.
  CHECK(!EnterReloadInvocation(active,revision,exclusive));CHECK(active==3);ExitReloadInvocation(active,revision);
 }
 ExitReloadInvocation(active,revision);CHECK(active==1);ExitReloadInvocation(active,revision);CHECK(active==0);
 // Existing dispatcher can now register itself and prove drained originals.
 CHECK(EnterReloadInvocation(active,revision,exclusive));
 {ReloadInvocationExclusion stop(exclusive,active,revision);CHECK(stop.held&&stop.Quiet());}
 ExitReloadInvocation(active,revision);CHECK(active==0);return 0;}
}
int DiagnosticOriginalInputAndOwnerLease(){
 const auto sample=Sample();MagazineInteractionDiagnostic d{sample.identity.owner,sample.sourceSequence,T,T+100000000,true,false,true,7};
 CHECK(MagazineInteractionDiagnosticFresh(d,sample.identity.owner,sample.sourceSequence,T+1));
 CHECK(!MagazineInteractionDiagnosticFresh(d,sample.identity.owner,sample.sourceSequence,T+100000000));
 CHECK(!MagazineInteractionDiagnosticFresh(d,sample.identity.owner,sample.sourceSequence,T-1));
 CHECK(!MagazineInteractionDiagnosticFresh(d,sample.identity.owner,sample.sourceSequence-1,T+1));
 auto other=sample.identity.owner;++other.space;CHECK(!MagazineInteractionDiagnosticFresh(d,other,sample.sourceSequence,T+1));
 ++d.owner.equipGeneration;CHECK(!MagazineInteractionDiagnosticFresh(d,sample.identity.owner,sample.sourceSequence,T+1));
 d.owner=sample.identity.owner;d.deadlineNs=T+150000001;CHECK(!MagazineInteractionDiagnosticFresh(d,d.owner,d.sequence,T+1));return 0;
}
int DiagnosticBoundedTransitionsNotTimeRenewal(){
 MagazineEmptyDiagnosticJournal journal;MagazineEmptyDiagnosticRecord row;row.sample=Sample();row.hasBefore=true;row.nowNs=T;
 row.stage=MagazineEmptyDiagnosticStage::Eligibility;row.hasInteraction=true;row.interaction.supportHolding=false;
 journal.Observe(row);CHECK(journal.Size()==1);
 for(unsigned n=0;n<300;++n){row.nowNs++;row.sample.sourceSequence++;row.sample.observedNs++;row.sample.deadlineNs++;journal.Observe(row);}
 CHECK(journal.Size()==1&&journal.Observed()==301&&journal.Row(0).sample.sourceSequence==77&&journal.Row(0).sample.observedNs==T);
 row.interaction.supportHolding=true;journal.Observe(row);CHECK(journal.Size()==2);
 row.sample.before.nextState=10;journal.Observe(row);CHECK(journal.Size()==3);
 for(unsigned n=0;n<150;++n){row.sample.before.reserve=int(n);journal.Observe(row);}
 CHECK(journal.Size()==96&&journal.Overwritten()==57&&journal.Row(95).sample.before.reserve==149);
 CHECK(journal.Row(0).sample.before.reserve==54);return 0;
}
int DiagnosticCannotChangeEmptyAdmissionOrGrantReceipts(){
 auto input=test::Input();const auto before=Sample();const auto saved=input.context;
 MagazineEmptyDiagnosticJournal journal;MagazineEmptyDiagnosticRecord row;row.sample=before;row.context=input.context;row.hasBefore=row.hasContext=true;
 row.stage=MagazineEmptyDiagnosticStage::Eligibility;row.hasInteraction=true;row.interaction.supportHolding=true;
 journal.Observe(row);CHECK(std::memcmp(&input.context,&saved,sizeof(saved))==0);
 CHECK(MagazineEmptyStepEligible(before,input.config,ReloadRequestCyclePhase::Idle,input.context,T+1));
 input.context.orderRequested=true;input.context.inputFlags=2;row.context=input.context;journal.Observe(row);
 CHECK(!MagazineEmptyStepEligible(before,input.config,ReloadRequestCyclePhase::Idle,input.context,T+1));
 MagazineEmptyControlReceipts receipts;CHECK(!receipts.EmptyDeadline(before.identity,before.profile,before.ownerRevision,before.before.reserve,T+1));
 return 0;
}
int BoundedHitchKeepsOriginalAuthority(){
 auto sample=Sample();auto input=test::Input();
 for(float hitch:{.0596221f,.1f})for(unsigned branch=0;branch<3;++branch){
  sample=Sample(branch);input.context.deltaSeconds=hitch;
  CHECK(MagazineEmptyStepEligible(sample,input.config,ReloadRequestCyclePhase::Idle,input.context,T+1));
  CHECK(!MagazineEmptyStepEligible(sample,input.config,ReloadRequestCyclePhase::Idle,input.context,sample.deadlineNs));
  auto other=sample;++other.identity.owner.space;other.before.address+=4;
  CHECK(!MagazineEmptyStepEligible(other,input.config,ReloadRequestCyclePhase::Idle,input.context,T+1));
  CHECK(!MagazineEmptyStepEligible(sample,input.config,ReloadRequestCyclePhase::Holding,input.context,T+1));
  auto reload=input.context;reload.reloadRequested=true;reload.inputFlags=4;
  CHECK(!MagazineEmptyStepEligible(sample,input.config,ReloadRequestCyclePhase::Arming,reload,T+1));
 }
 for(float invalid:{0.f,-.01f,std::nextafter(.1f,1.f),std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()}){
  input.context.deltaSeconds=invalid;
  CHECK(!MagazineEmptyStepEligible(sample,input.config,ReloadRequestCyclePhase::Idle,input.context,T+1));
 }
 return 0;
}
int main(){if(BoundedHitchKeepsOriginalAuthority()||DiagnosticOriginalInputAndOwnerLease()||DiagnosticBoundedTransitionsNotTimeRenewal()||DiagnosticCannotChangeEmptyAdmissionOrGrantReceipts()||ScopedReadiness()||StrictBoundaries()||ExactByteAndExceptionalRestore()||LastShotInputLoopAndExplicitReload()||OriginalReceiptIntersection()||ServerLeaseCannotRenewWithOwner()||EmptyNativeHoldAndConservedTransfer()||EmptyCancellationNeedsScopedAdapter()||NestedStepAdmissionAndDrain())return 1;
 std::cout<<"13 empty native-control/diagnostic groups passed\n";}

