#include "Bc2OwnerPublication.h"
#include "Bc2ReloadRequestCycle.h"
#include "Test.h"
using namespace fvr::bc2;
int main(){
 const std::int64_t observed=1000000000,deadline=1100000000;
 ReloadHoldIdentity id;id.owner={0x10000,0x20000,0x30000,0x40000,1,2,3};
 id.firing={0x50000,0x60000,0x70000};id.serverPlayer=0x80000;id.serverSoldier=0x90000;id.serverItem=0xa0000;
 ReloadCycleControl control{id,1,1,observed,deadline,true};
 Bc2ReloadRequestCycle cycle{true};CHECK(cycle.Start(control,observed));
 // Genuine native policy remains Arming through unavailable serialization;
 // no synthetic publication/identity, no KeepAlive and no deadline renewal.
 for(std::int64_t at=observed+10000000;at<deadline;at+=10000000){
  const auto unavailable=AdmitOwnerPublicationCopy(false,observed,deadline,at);
  CHECK(unavailable==OwnerCopyAdmission::Deferred&&!OwnerCopyUnavailableCancels(unavailable));
  if(OwnerCopyUnavailableCancels(unavailable))cycle.Cancel();
  CHECK(cycle.Phase()==ReloadRequestCyclePhase::Arming&&cycle.MissingEvidence(at));
 }
 CHECK(!cycle.MissingEvidence(deadline)&&cycle.Phase()==ReloadRequestCyclePhase::Cancelled);
 // Known expiry/clock-invalid source still rejects even while gate is busy.
 for(const auto at:{observed-1,deadline,deadline+1}){
  const auto invalid=AdmitOwnerPublicationCopy(false,observed,deadline,at);
  CHECK(invalid==OwnerCopyAdmission::Rejected&&OwnerCopyUnavailableCancels(invalid));
 }
 // Known server read rejection must not be concealed by serialization
 // contention. A fresh original lease does not change that native rejection.
 Bc2ReloadRequestCycle rejected{true};CHECK(rejected.Start(control,observed));
 const auto combined=AdmitOwnerPublicationCopy(false,observed,deadline,observed+1,true);
 CHECK(combined==OwnerCopyAdmission::Rejected&&OwnerCopyUnavailableCancels(combined));
 if(OwnerCopyUnavailableCancels(combined))rejected.Cancel();
 CHECK(rejected.Phase()==ReloadRequestCyclePhase::Cancelled);
 // Non-request diagnostic mode passes false; gate availability preserves the
 // existing under-gate invalid-server publication/cancellation path.
 CHECK(AdmitOwnerPublicationCopy(false,observed,deadline,observed+1,false)==OwnerCopyAdmission::Deferred);
 CHECK(AdmitOwnerPublicationCopy(true,observed,deadline,observed+1,true)==OwnerCopyAdmission::Acquired);
 CHECK(AdmitOwnerPublicationCopy(true,observed,deadline,observed+1)==OwnerCopyAdmission::Acquired);
 CHECK(AdmitOwnerPublicationCopy(true,observed,deadline,deadline)==OwnerCopyAdmission::Rejected);
 CHECK(AdmitOwnerPublicationCopy(false,observed,observed+250000001,observed)==OwnerCopyAdmission::Rejected);
 // Reproduce the previous unconditional gate-miss cancel against the same
 // real policy: it cancels immediately despite valid original input deadline.
 Bc2ReloadRequestCycle before{true};CHECK(before.Start(control,observed));before.Cancel();
 CHECK(before.Phase()==ReloadRequestCyclePhase::Cancelled);
 return 0;
}
