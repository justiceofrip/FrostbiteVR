#include "Bc2PreholdCleanup.h"
#include "Bc2ReloadAbort.h"
#include <cstdio>
using namespace fvr::bc2;
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"line %d: %s\n",__LINE__,#x);return 1;}}while(false)
PreholdCleanupEvidence Source(){PreholdCleanupEvidence e;e.revision=1;e.operation=2;e.sequence=3;e.observedNs=100;e.deadlineNs=100000100;e.coherent=e.configVerified=e.callbacksExcluded=true;e.source.verified=true;e.source.nowNs=e.observedNs;e.source.leaseDeadlineNs=e.deadlineNs;e.source.identity.owner={1,2,3,4,5,6,7};e.source.identity.firing={1000,2000,3000};e.source.identity.serverPlayer=10;e.source.identity.serverSoldier=11;e.source.identity.serverItem=12;for(unsigned n=0;n<3;++n){auto& b=e.source.branches[n];b.address=e.source.identity.firing[n];b.currentState=b.nextState=2;b.loaded=0;b.reserve=20;e.source.capacities[n]=30;}return e;}
int main(){
 ReloadAbortCleanup legacy;ReloadRoundLease notHeld{};notHeld.identity=Source().source.identity;notHeld.cycle=2;notHeld.sequence=3;notHeld.loaded=0;notHeld.reserve=20;notHeld.capacity=30;notHeld.observedNs=100;notHeld.deadlineNs=100000100;notHeld.nativeBindingVerified=true;CHECK(!legacy.Arm(notHeld,1,101,true));
 auto e=Source();PreholdCleanupLedger ledger;CHECK(ledger.Prepare(e,101));e.source.branches[0].nextState=10;CHECK(ledger.ObserveEntry(e,0,102));CHECK(ledger.Cancel(2));
 // Fresh native confirmation after original controller expiry, no renewed
 // original source and no synthetic held receipt. Still no helper authority.
 e.sequence=4;e.observedNs=100000200;e.deadlineNs=200000200;e.source.nowNs=e.observedNs;e.source.leaseDeadlineNs=e.deadlineNs;
 auto candidate=ledger.ObserveCleanup(e,e.observedNs);CHECK(candidate&&candidate->loaded==0&&candidate->reserve==20&&candidate->enteredMask==1&&!candidate->dispatchEnabled);
 for(auto& b:e.source.branches){b.currentState=b.nextState=1;}CHECK(!ledger.ObserveCleanup(e,e.observedNs+1,true));CHECK(ledger.Phase()==PreholdCleanupPhase::Reconciled);CHECK(!ledger.Prepare(Source(),101));
 for(unsigned fault=0;fault<7;++fault){auto a=Source();PreholdCleanupLedger l;CHECK(l.Prepare(a,101));a.source.branches[0].nextState=10;CHECK(l.ObserveEntry(a,0,102));CHECK(l.Cancel(2));
  if(fault==0)++a.source.branches[1].loaded;if(fault==1)++a.source.branches[0].reserve;if(fault==2)++a.revision;if(fault==3)++a.source.config.primaryFire;if(fault==4)++a.source.identity.owner.space;if(fault==5)a.coherent=false;if(fault==6)a.deadlineNs=102;
  CHECK(!l.ObserveCleanup(a,103));CHECK(l.Phase()==PreholdCleanupPhase::Rejected);
 }
 auto bad=Source();bad.callbacksExcluded=false;PreholdCleanupLedger l;CHECK(!l.Prepare(bad,101));bad=Source();bad.source.branches[2].address=0;CHECK(!l.Prepare(bad,101));
 auto partial=Source();PreholdCleanupLedger p;CHECK(p.Prepare(partial,101));partial.source.branches[0].nextState=10;CHECK(p.ObserveEntry(partial,0,102));CHECK(p.Cancel(2));
 CHECK(!p.ObserveCancelledEntry(partial,1,103));CHECK(p.EnteredMask()==1);
 partial.source.branches[1].nextState=10;CHECK(p.ObserveCancelledEntry(partial,1,103));CHECK(p.EnteredMask()==3);
 for(auto& b:partial.source.branches)b.currentState=b.nextState=1;
 CHECK(!p.ObserveCleanup(partial,104,true));CHECK(p.Phase()==PreholdCleanupPhase::Reconciled);CHECK(p.EnteredMask()==3);
 auto delayed=Source();PreholdCleanupLedger delayedLedger;CHECK(delayedLedger.Prepare(delayed,101));
 delayed.source.branches[0].nextState=10;CHECK(delayedLedger.ObserveEntry(delayed,0,102));CHECK(delayedLedger.Cancel(2));
 for(auto& q:delayed.source.branches)q.currentState=q.nextState=1;
 CHECK(!delayedLedger.ObserveCleanup(delayed,103));CHECK(delayedLedger.Phase()==PreholdCleanupPhase::CleanupPending);
 delayed.source.branches[2].currentState=2;delayed.source.branches[2].nextState=10;
 CHECK(delayedLedger.ObserveCancelledEntry(delayed,2,104));CHECK(delayedLedger.EnteredMask()==5);
 CHECK(delayedLedger.ObserveCleanup(delayed,104));
 CHECK(!delayedLedger.ObserveCleanup(delayed,2000000101ll));CHECK(delayedLedger.Phase()==PreholdCleanupPhase::Expired);
 // Actual prehold-spas-02 events: prepare191934278389600;
 // Update1804 +1047900ns before2/2, nestedCommit1805 +1518400ns
 // before2/10 after10/10; Update exits10/11. Neutral Update1811
 // +18330800ns begins10/11, with7/24 capacity8 conserved.
 auto cadence=Source();cadence.source.branches[0].loaded=7;cadence.source.branches[0].reserve=24;
 for(unsigned n=0;n<3;++n){cadence.source.branches[n].loaded=7;cadence.source.branches[n].reserve=24;cadence.source.capacities[n]=8;}
 PreholdCleanupLedger c;CHECK(c.Prepare(cadence,101));
 cadence.source.branches[0].currentState=10;cadence.source.branches[0].nextState=11;
 CHECK(c.ObserveEntry(cadence,0,18330900));CHECK(c.EnteredMask()==1);CHECK(c.Cancel(2));
 CHECK(c.ObserveCleanup(cadence,18330901));
 for(unsigned next:{0u,2u,7u,12u,15u}){auto q=Source();PreholdCleanupLedger ql;CHECK(ql.Prepare(q,101));q.source.branches[0].currentState=10;q.source.branches[0].nextState=next;CHECK(!ql.ObserveEntry(q,0,102));}
 auto expired=Source();PreholdCleanupLedger ex;CHECK(ex.Prepare(expired,101));expired.source.branches[0].currentState=10;expired.source.branches[0].nextState=11;CHECK(!ex.ObserveEntry(expired,0,2000000101ll));
 // Actual XM8-01 source195185828699700, op369/rev3,22/191/cap30.
 // Top-level Update1802 +184.512ms enters11/12; siblings1807/1809
 // +202.289/+202.877ms likewise. No deadline/source authority is renewed.
 auto xm=Source();xm.operation=369;xm.revision=3;
 for(unsigned n=0;n<3;++n){xm.source.branches[n].loaded=22;xm.source.branches[n].reserve=191;xm.source.capacities[n]=30;}
 PreholdCleanupLedger xl;CHECK(xl.Prepare(xm,101));
 xm.sequence=4;xm.observedNs=184512100;xm.deadlineNs=284512100;xm.source.nowNs=xm.observedNs;xm.source.leaseDeadlineNs=xm.deadlineNs;
 xm.source.branches[0].currentState=11;xm.source.branches[0].nextState=12;
 CHECK(xl.ObserveEntry(xm,0,xm.observedNs));CHECK(xl.Cancel(369));
 xm.source.branches[1].currentState=11;xm.source.branches[1].nextState=12;
 xm.source.branches[2].currentState=11;xm.source.branches[2].nextState=12;
 CHECK(xl.ObserveCancelledEntry(xm,1,202289100));CHECK(xl.ObserveCancelledEntry(xm,2,202877100));
 CHECK(xl.EnteredMask()==7);CHECK(xl.ObserveCleanup(xm,202877101));
 CHECK(!xl.ObserveCleanup(xm,2000000101ll));CHECK(xl.Phase()==PreholdCleanupPhase::Expired);
 for(unsigned next:{0u,1u,2u,7u,10u,11u,13u,14u,15u}){
  auto badXm=Source();PreholdCleanupLedger badLedger;CHECK(badLedger.Prepare(badXm,101));
  badXm.source.branches[0].currentState=11;badXm.source.branches[0].nextState=next;
  CHECK(!badLedger.ObserveEntry(badXm,0,102));
 }
 // Actual ledger: skip native I/O only after terminal/expired, not early idle.
 auto gateSource=Source();PreholdCleanupLedger gate;
 CHECK(gate.Prepare(gateSource,101));CHECK(!gate.CleanupObservationOpen(102));
 gateSource.source.branches[0].nextState=10;CHECK(gate.ObserveEntry(gateSource,0,102));CHECK(gate.Cancel(2));
 unsigned reads=0;auto observe=[&](std::int64_t now){if(!gate.CleanupObservationOpen(now))return; ++reads;gate.ObserveCleanup(gateSource,now);};
 observe(103);CHECK(reads==1);for(auto& q:gateSource.source.branches)q.currentState=q.nextState=2;
 observe(104);CHECK(reads==2&&gate.Phase()==PreholdCleanupPhase::CleanupPending);
 gateSource.source.branches[2].nextState=10;CHECK(gate.ObserveCancelledEntry(gateSource,2,105));observe(105);
 CHECK(reads==3&&gate.EnteredMask()==5);
 const auto deadline=gate.OperationDeadlineNs();observe(deadline);observe(deadline+100);
 CHECK(reads==3&&gate.Phase()==PreholdCleanupPhase::Expired&&gate.OperationDeadlineNs()==deadline);
 CHECK(gate.Original().source.branches[0].reserve==20&&gate.EnteredMask()==5);
 CHECK(!p.CleanupObservationOpen(105));CHECK(p.Phase()==PreholdCleanupPhase::Reconciled);
 auto rejectedSource=Source();PreholdCleanupLedger rejected;CHECK(rejected.Prepare(rejectedSource,101));
 rejectedSource.source.branches[0].nextState=10;CHECK(rejected.ObserveEntry(rejectedSource,0,102));CHECK(rejected.Cancel(2));
 rejectedSource.revision++;CHECK(!rejected.ObserveCleanup(rejectedSource,103));CHECK(!rejected.CleanupObservationOpen(104));
 CHECK(rejected.Phase()==PreholdCleanupPhase::Rejected);
 std::puts("Prehold ledger conservation and authority boundary passed");return 0;
}


