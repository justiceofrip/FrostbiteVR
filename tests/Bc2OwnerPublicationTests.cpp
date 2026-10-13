#include "Bc2OwnerPublication.h"
#include "Bc2ReloadRequestCycle.h"
#include "Test.h"
#include <barrier>
#include <mutex>
#include <thread>
using namespace fvr::bc2;
namespace {
int PublicationArrivesAfterCallbackEntry(){
 // Force the real producer/consumer ordering: callback entry, then a newer
 // publication under the gate, then the callback acquires that same gate.
 struct Lease {std::int64_t observed=90,deadline=200;std::uint64_t revision=4;
  int loaded=7,reserve=24;};
 Lease published;std::optional<Lease> copied;
 std::mutex gate;std::unique_lock producer(gate);
 std::barrier entered(2),release(2);
 const auto current=[](const Lease& value,std::int64_t at){
  return value.revision==4&&at>=value.observed&&at<value.deadline;};
 std::thread callback([&]{
  const std::int64_t entry=100;
  entered.arrive_and_wait();release.arrive_and_wait();
  std::lock_guard hold(gate);
  copied=CopyPublishedOwnerAtAcquisition(published,entry,[]{return 120ll;},current);
 });
 entered.arrive_and_wait();published.observed=110;
 producer.unlock();release.arrive_and_wait();callback.join();
 CHECK(copied&&copied->observed==110&&copied->deadline==200);
 CHECK(copied->revision==4&&copied->loaded==7&&copied->reserve==24);
 // Reject expired, genuinely future, revoked and backwards-clock copies.
 for(unsigned mode=0;mode<5;++mode){
  Lease invalid=published;std::int64_t at=120,entry=100;
  if(mode==0)at=invalid.deadline;
  if(mode==1)invalid.observed=at+1;
  if(mode==2)++invalid.revision;
  if(mode==3)at=entry-1;
  if(mode==4)entry=0;
  CHECK(!CopyPublishedOwnerAtAcquisition(invalid,entry,[&]{return at;},current));
 }
 // Admission is not a renewal: the same original copy expires at200.
 CHECK(!CopyPublishedOwnerAtAcquisition(*copied,199,[]{return 200ll;},current));
 CHECK(copied->observed==110&&copied->deadline==200);
 return 0;
}
}
int main(){
 if(PublicationArrivesAfterCallbackEntry())return 1;
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
