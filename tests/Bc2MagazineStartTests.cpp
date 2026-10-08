#include "Bc2MagazineStart.h"
#include "Bc2ReloadNativePolicy.h"
#include "Bc2ReloadInvocationEntry.h"
#include "Bc2ReloadRetirement.h"
#include "Test.h"
using namespace fvr::bc2;using Result=MagazineCycleStartResult;
namespace {
constexpr std::int64_t Now=1000000000;
ReloadHoldIdentity Identity(){ReloadHoldIdentity i;i.owner={0x10000,0x20000,0x30000,0x40000,1,2,3};
 i.firing={0x50000,0x60000,0x70000};i.serverPlayer=0x80000;i.serverSoldier=0x90000;i.serverItem=0xa0000;return i;}
ReloadCycleControl Control(std::uint64_t cycle=1){return {Identity(),cycle,1,Now,Now+100000000,true};}
fvr::interaction::ManualReloadRequest Unseat(){const auto o=Identity().owner;return {1,{o.soldier,o.actorGeneration,o.weapon,o.equipGeneration,o.space},fvr::interaction::ReloadOperation::UnseatMagazine,0,0};}
Result Inspect(const Bc2ReloadNativePolicy& p,std::uint64_t cycle=1,bool quiet=true){return InspectMagazineStartRegistration(quiet,p.IsMagazine(),p.Identity(),p.Cycle(),Identity(),cycle);}
int RejectedDoesNotInventCycle(){Bc2ReloadNativePolicy p;CHECK(p.ConfigureMagazine());
 auto bad=Control();bad.permitted=false;CHECK(!p.StartMagazine(bad,Unseat(),Now));
 CHECK(p.Cycle()==0&&Inspect(p)==Result::NotStarted);
 CHECK(!CancelAndDrainReloadCycle(p,Identity(),1,true)); // Reproduces permanent wait in old bool-only consumer.
 ReloadRetirementReceipts receipt;CHECK(!receipt.Observe(p,Identity(),1,Now,true));
 CHECK(p.StartMagazine(Control(2),Unseat(),Now));CHECK(p.Cycle()==2);return 0;}
int AcceptedThenCancelledRemainsRegistered(){Bc2ReloadNativePolicy p;CHECK(p.ConfigureMagazine());
 CHECK(p.StartMagazine(Control(),Unseat(),Now));CHECK(Inspect(p)==Result::RegisteredCancelled);
 p.Cancel(ReloadRequestCycleFailure::Owner);CHECK(Inspect(p)==Result::RegisteredCancelled);
 CHECK(CancelAndDrainReloadCycle(p,Identity(),1,true));ReloadRetirementReceipts receipt;
 const auto retired=receipt.Observe(p,Identity(),1,Now,true);CHECK(retired&&retired->verified);
 CHECK(Inspect(p)==Result::RegisteredCancelled);CHECK(Inspect(p,2)==Result::NotStarted);return 0;}
int NoGuessAfterCollisionOrWrongFamily(){Bc2ReloadNativePolicy p;
 CHECK(Inspect(p)==Result::Unknown);CHECK(p.ConfigureMagazine());CHECK(Inspect(p,0)==Result::Unknown);
 CHECK(p.StartMagazine(Control(9),Unseat(),Now));CHECK(Inspect(p,8)==Result::Unknown);
 auto other=Identity();++other.owner.actorGeneration;
 CHECK(InspectMagazineStartRegistration(true,true,p.Identity(),9,other,9)==Result::Unknown);
 CHECK(InspectMagazineStartRegistration(false,true,p.Identity(),9,Identity(),10)==Result::Unknown);
 CHECK(InspectMagazineStartRegistration(true,false,p.Identity(),9,Identity(),10)==Result::Unknown);return 0;}
int BusyThenQuietResolution(){
 Bc2ReloadNativePolicy p;CHECK(p.ConfigureMagazine());
 std::atomic<unsigned> active=0;std::atomic<std::uint64_t> revision=0;std::atomic_flag gate=ATOMIC_FLAG_INIT;
 CHECK(EnterReloadInvocation(active,revision,gate));CHECK(EnterReloadInvocation(active,revision,gate));
 {ReloadInvocationExclusion barrier(gate,active,revision);CHECK(!barrier.Quiet());CHECK(Inspect(p,1,barrier.Quiet())==Result::Unknown);}
 ExitReloadInvocation(active,revision);
 {ReloadInvocationExclusion barrier(gate,active,revision);CHECK(barrier.Quiet());CHECK(Inspect(p,1,barrier.Quiet())==Result::NotStarted);}
 ExitReloadInvocation(active,revision);return 0;
}
}
int main(){if(RejectedDoesNotInventCycle()||AcceptedThenCancelledRemainsRegistered()||NoGuessAfterCollisionOrWrongFamily()||BusyThenQuietResolution())return 1;
 std::puts("Four magazine startup registration, rejection, retirement and contention groups passed");return 0;}
