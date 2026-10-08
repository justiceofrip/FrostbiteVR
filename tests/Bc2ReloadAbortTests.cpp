#include "Bc2ReloadAbort.h"
#include "Test.h"
#include <cstdio>
using namespace fvr::bc2;
namespace {
constexpr std::int64_t Start=1000000000;
ReloadRoundLease Lease(){ReloadRoundLease l;l.identity.owner={0x10000,0x20000,0x30000,0x40000,1,2,3};
    l.identity.firing={0x50000,0x60000,0x70000};l.identity.serverPlayer=0x80000;l.identity.serverSoldier=0x90000;l.identity.serverItem=0xa0000;
    l.cycle=1;l.sequence=25;l.observedNs=Start-1000000;l.deadlineNs=Start+80000000;l.loaded=5;l.reserve=18;l.capacity=8;l.nativeBindingVerified=l.allThreeHeld=true;return l;}
ReloadHoldInput Input(unsigned branch=0){ReloadHoldInput i;i.identity=Lease().identity;i.verified=true;i.branch=branch;i.nowNs=Start+1000000;i.leaseDeadlineNs=Start+100000000;
    i.context.deltaSeconds=.005f;i.context.reloadTimeMultiplier=1;i.context.flags24Through28[0]=true;
    for(unsigned n=0;n<3;++n){auto& b=i.branches[n];b.address=i.identity.firing[n];b.wrapperOffset=n==0?0x3c:n==1?0x40:0x10;
        b.currentState=11;b.nextState=12;b.phaseTimer=.21f;b.loaded=5;b.reserve=18;}return i;}
ReloadAbortDecision Claim(ReloadAbortCleanup& p,ReloadHoldInput i=Input()){return p.Claim(i,7,1,true,10+i.branch,true,i.nowNs);}
ReloadFiringObservation Exited(const ReloadAbortDecision& d){auto b=d.before;b.currentState=b.nextState=2;b.phaseTimer=0;return b;}
int ThreeIndependentBranches(){ReloadAbortCleanup p;CHECK(p.Arm(Lease(),7,Start,true));
    for(unsigned n=0;n<3;++n){auto input=Input(n);if(n)input.branches[n-1].currentState=input.branches[n-1].nextState=2;
        const auto d=Claim(p,input);CHECK(d.call&&d.branch==n);CHECK(p.Allows(d,7,1,true,input.nowNs));CHECK(!Claim(p,input).call);
        CHECK(p.Finish(d,Exited(d),true,true,input.nowNs+100));}
    CHECK(!p.Active()&&p.Completed()==7&&p.Failure()==ReloadAbortFailure::None);CHECK(!p.Arm(Lease(),7,Start+2000000,true));return 0;}
int NonrenewableDeadline(){ReloadAbortCleanup p;CHECK(p.Arm(Lease(),7,Start,true));const auto deadline=p.Deadline();auto renewed=Lease();renewed.sequence++;renewed.deadlineNs+=10000000;
    CHECK(!p.Arm(renewed,7,Start+2000000,true)&&p.Deadline()==deadline);
    auto i=Input();i.nowNs=deadline;CHECK(!Claim(p,i).call&&!p.Active()&&p.Failure()==ReloadAbortFailure::Expired);return 0;}
int UnsupportedArmingPendingEmptyOrFull(){for(unsigned n=0;n<6;++n){ReloadAbortCleanup p;auto l=Lease();bool held=true;
    if(n==0)held=false;if(n==1)l.loaded=0;if(n==2)l.allThreeHeld=false;if(n==3)l.nativeBindingVerified=false;if(n==4)l.deadlineNs=Start;if(n==5)l.observedNs=Start+1;
    CHECK(!p.Arm(l,7,Start,held));}return 0;}
int NewCycleAndOwnerChanges(){for(unsigned n=0;n<4;++n){ReloadAbortCleanup p;CHECK(p.Arm(Lease(),7,Start,true));auto i=Input();
    if(n==0)++i.identity.owner.equipGeneration;if(n==1)++i.identity.serverItem;
    CHECK(!p.Claim(i,n==2?8:7,n==3?2:1,true,10,true,i.nowNs).call);CHECK(!p.Active());}
    ReloadAbortCleanup p;CHECK(p.Arm(Lease(),7,Start,true));const auto d=Claim(p);CHECK(d.call);p.Abandon(ReloadAbortFailure::NewCycle);
    CHECK(!p.Allows(d,7,1,true,Start+2000000));auto next=Lease();next.cycle=2;CHECK(p.Arm(next,7,Start+2000000,true));
    CHECK(!p.Finish(d,Exited(d),true,true,Start+3000000));return 0;}
int FinalOwnerOrCycleCheck(){for(unsigned n=0;n<3;++n){ReloadAbortCleanup p;CHECK(p.Arm(Lease(),7,Start,true));const auto d=Claim(p);CHECK(d.call);
    CHECK(!p.Allows(d,n==0?8:7,n==1?2:1,n!=2,Start+2000000));CHECK(!p.Active());}return 0;}
int UnsafeInputAndState(){for(unsigned n=0;n<13;++n){ReloadAbortCleanup p;CHECK(p.Arm(Lease(),7,Start,true));auto i=Input();auto& b=i.branches[0];
    switch(n){case 0:i.context.inputFlags=1;break;case 1:i.context.inputFlags=4;break;case 2:i.context.flags24Through28[4]=true;break;
        case 3:i.context.deltaSeconds=0;break;case 4:i.context.deltaSeconds=.051f;break;case 5:b.loaded++;break;case 6:b.reserve--;break;
        case 7:b.flagsA8=16;break;case 8:b.currentState=7;break;case 9:b.nextState=7;break;case 10:b.address++;break;case 11:b.phaseTimer=-1;break;case 12:i.verified=false;break;}
    CHECK(!Claim(p,i).call&&!p.Active());}ReloadAbortCleanup p;CHECK(p.Arm(Lease(),7,Start,true));auto i=Input();CHECK(!p.Claim(i,7,1,true,10,false,i.nowNs).call);return 0;}
int ExactHelperContract(){std::array<std::byte,0xac> a{},b{};for(unsigned n=0;n<a.size();++n)a[n]=std::byte(n);b=a;
    const unsigned one=1,zero=0;std::memcpy(b.data()+0x44,&one,4);std::memcpy(b.data()+0x50,&zero,4);std::memcpy(b.data()+0x54,&zero,4);
    CHECK(ReloadAbortHelperPostcondition(a,b));
    for(unsigned n=0;n<b.size();++n){auto changed=b;changed[n]^=std::byte{1};CHECK(!ReloadAbortHelperPostcondition(a,changed));}return 0;}
int HelperAndOriginalPostconditions(){for(unsigned n=0;n<6;++n){ReloadAbortCleanup p;CHECK(p.Arm(Lease(),7,Start,true));const auto d=Claim(p);CHECK(d.call);auto after=Exited(d);
    if(n==0)after.loaded++;if(n==1)after.reserve--;if(n==2)after.currentState=12;
    if(n==5){after.currentState=6;after.nextState=7;--after.loaded;} // Combined helper+Fire remains unverified/rejected.
    CHECK(!p.Finish(d,after,n!=3,n!=4,Start+2000000));CHECK(!p.Active());}return 0;}
int NoFutureCallbackRequiredForRetirement(){ReloadAbortCleanup p;CHECK(p.Arm(Lease(),7,Start,true));
    // Runtime retirement never waits on this optional cleanup. Invalidation is
    // independent of observation arrival and can occur after its deadline.
    p.Abandon(ReloadAbortFailure::Owner);CHECK(!p.Active());CHECK(!Claim(p).call);return 0;}
int AlreadyExitedNoNativeCall(){ReloadAbortCleanup p;CHECK(p.Arm(Lease(),7,Start,true));
    for(unsigned n=0;n<3;++n){auto i=Input(n);i.branches[n].currentState=i.branches[n].nextState=2;i.branches[n].phaseTimer=0;CHECK(!Claim(p,i).call);}
    CHECK(!p.Active()&&p.Completed()==7);return 0;}
int FirePulseThenNeutral(){ReloadAbortCleanup p;CHECK(p.Arm(Lease(),7,Start,true));const auto originalDeadline=p.Deadline();
    for(unsigned n=0;n<3;++n){auto i=Input(n);i.context.inputFlags=1;i.context.fireRequested=true;
        CHECK(!Claim(p,i).call&&p.Active()&&!p.Claimed());CHECK(i.context.inputFlags==1&&i.context.fireRequested);}
    CHECK(p.Deadline()==originalDeadline&&p.Failure()==ReloadAbortFailure::None);
    for(unsigned n=0;n<3;++n){auto i=Input(n);i.nowNs+=5000000;const auto d=Claim(p,i);CHECK(d.call);
        CHECK(p.Allows(d,7,1,true,i.nowNs));CHECK(p.Finish(d,Exited(d),true,true,i.nowNs+100));}
    CHECK(!p.Active()&&p.Completed()==7);const auto& r=p.History(0);
    CHECK(r.claims==6&&r.fireDeferred==3&&r.deferredByBranch[0]==1&&r.deferredByBranch[1]==1&&r.deferredByBranch[2]==1);
    CHECK(r.first.context.fireRequested&&r.firstDeferred.context.fireRequested&&!r.last.context.fireRequested);
    CHECK(r.deadlineNs==originalDeadline&&r.terminalNs==Start+6000100&&r.claimed==7&&r.completed==7&&!r.active);return 0;}
int HeldFireCannotRenewOrClaim(){ReloadAbortCleanup p;CHECK(p.Arm(Lease(),7,Start,true));const auto deadline=p.Deadline();
    auto i=Input();i.context.inputFlags=1;i.context.fireRequested=true;
    for(unsigned n=1;n<80;++n){i.nowNs=Start+n*1000000ll;CHECK(!Claim(p,i).call&&p.Active()&&!p.Claimed()&&p.Deadline()==deadline);}
    auto renewed=Lease();renewed.sequence++;renewed.deadlineNs+=10000000;
    CHECK(!p.Arm(renewed,7,Start+79000000,true)&&p.Deadline()==deadline);
    i.nowNs=deadline;CHECK(!Claim(p,i).call&&!p.Active()&&p.Failure()==ReloadAbortFailure::Expired);
    CHECK(p.History(0).fireDeferred==79&&p.History(0).terminalNs==deadline&&p.History(0).lastAdmission==ReloadAbortAdmission::Rejected);
    i.context.inputFlags=0;i.context.fireRequested=false;i.nowNs++;CHECK(!Claim(p,i).call);return 0;}
int MixedOrMalformedFireStillRejected(){for(unsigned n=0;n<13;++n){ReloadAbortCleanup p;CHECK(p.Arm(Lease(),7,Start,true));
    auto i=Input();auto& c=i.context;c.inputFlags=1;c.fireRequested=true;
    switch(n){case 0:c.inputFlags=3;c.orderRequested=true;break;case 1:c.inputFlags=5;c.reloadRequested=true;break;
        case 2:c.inputFlags=9;break;case 3:c.fireRequested=false;break;case 4:c.inputFlags=0;break;
        case 5:c.flags24Through28[2]=true;break;case 6:c.flags24Through28[3]=true;break;case 7:c.flags24Through28[4]=true;break;
        case 8:c.deltaSeconds=0;break;case 9:c.deltaSeconds=.051f;break;case 10:c.deltaSeconds=std::numeric_limits<float>::quiet_NaN();break;
        case 11:c.reloadTimeMultiplier=0;break;case 12:c.reloadTimeMultiplier=2;break;}
    CHECK(!Claim(p,i).call&&!p.Active()&&p.Failure()==ReloadAbortFailure::Input&&!p.History(0).fireDeferred);
    CHECK(!Claim(p).call); // A genuinely unsupported input still abandons permanently.
    }return 0;}
int DeferredFireCannotMaskNativeChange(){for(unsigned n=0;n<8;++n){ReloadAbortCleanup p;CHECK(p.Arm(Lease(),7,Start,true));auto i=Input();
    i.context.inputFlags=1;i.context.fireRequested=true;CHECK(!Claim(p,i).call&&p.Active());
    i.context.inputFlags=0;i.context.fireRequested=false;i.nowNs+=1000000;auto& b=i.branches[0];
    switch(n){case 0:b.loaded++;b.reserve--;break;case 1:b.loaded--;b.currentState=6;b.nextState=7;break;
        case 2:b.reserve--;break;case 3:b.currentState=7;b.nextState=8;break;case 4:b.phaseTimer=1.1f;break;
        case 5:b.flagsA8=8;break;case 6:b.address++;break;case 7:b.wrapperOffset=0x40;break;}
    CHECK(!Claim(p,i).call&&!p.Active()&&p.Failure()==ReloadAbortFailure::State);
    CHECK(p.History(0).fireDeferred==1&&p.History(0).last.before.loaded==b.loaded&&p.History(0).last.before.reserve==b.reserve);
    }return 0;}
int DeferredFireCannotMaskOwnerOrCycle(){for(unsigned n=0;n<5;++n){ReloadAbortCleanup p;CHECK(p.Arm(Lease(),7,Start,true));auto i=Input();
    i.context.inputFlags=1;i.context.fireRequested=true;CHECK(!Claim(p,i).call&&p.Active());
    i.context.inputFlags=0;i.context.fireRequested=false;i.nowNs+=1000000;
    if(n==0)++i.identity.owner.equipGeneration;if(n==1)i.verified=false;if(n==4)i.leaseDeadlineNs=i.nowNs;
    CHECK(!p.Claim(i,n==2?8:7,n==3?2:1,true,25,true,i.nowNs).call&&!p.Active()&&!p.Claimed());
    CHECK(p.History(0).failure==p.Failure()&&p.History(0).terminalNs==i.nowNs);
    }return 0;}
int HistorySurvivesReplacementCycle(){ReloadAbortCleanup p;CHECK(p.Arm(Lease(),7,Start,true));auto i=Input();
    i.context.inputFlags=1;i.context.fireRequested=true;CHECK(!Claim(p,i).call&&p.Active());
    auto next=Lease();next.cycle=2;CHECK(p.Arm(next,7,Start+2000000,true));
    CHECK(p.HistoryTotal()==2&&p.HistoryCount()==2);const auto& first=p.History(0);
    CHECK(first.cycle==1&&first.fireDeferred==1&&first.failure==ReloadAbortFailure::NewCycle&&!first.active&&first.terminalNs==Start+2000000);
    i=Input();i.nowNs+=2000000;i.context.inputFlags=4;i.context.reloadRequested=true;
    CHECK(!p.Claim(i,7,2,true,20,true,i.nowNs).call&&!p.Active());
    CHECK(p.History(0).failure==ReloadAbortFailure::NewCycle&&p.History(1).failure==ReloadAbortFailure::Input);
    CHECK(p.History(1).first.context.inputFlags==4&&p.History(1).fireDeferred==0);return 0;}
int HistoryWrapIsBoundedAndOrdered(){ReloadAbortCleanup p;
    for(std::uint64_t n=1;n<=ReloadAbortCleanup::HistoryCapacity+3;++n){auto l=Lease();l.cycle=n;
        CHECK(p.Arm(l,7,Start,true));auto i=Input();i.context.inputFlags=4;
        CHECK(!p.Claim(i,7,n,true,n,true,i.nowNs).call&&!p.Active());}
    CHECK(p.HistoryTotal()==35&&p.HistoryCount()==32);
    for(std::uint64_t n=0;n<p.HistoryCount();++n){const auto& r=p.History(n);
        CHECK(r.cycle==n+4&&r.failure==ReloadAbortFailure::Input&&r.claims==1&&r.terminalNs==Start+1000000&&!r.active);}
    return 0;}
}
int main(){if(ThreeIndependentBranches()||NonrenewableDeadline()||UnsupportedArmingPendingEmptyOrFull()||NewCycleAndOwnerChanges()||FinalOwnerOrCycleCheck()||UnsafeInputAndState()||ExactHelperContract()||HelperAndOriginalPostconditions()||NoFutureCallbackRequiredForRetirement()||AlreadyExitedNoNativeCall()||FirePulseThenNeutral()||HeldFireCannotRenewOrClaim()||MixedOrMalformedFireStillRejected()||DeferredFireCannotMaskNativeChange()||DeferredFireCannotMaskOwnerOrCycle()||HistorySurvivesReplacementCycle()||HistoryWrapIsBoundedAndOrdered())return 1;
    std::puts("Seventeen bounded native-abort cleanup groups passed; Fire-only deferral is not combined native Fire/abort verification.");return 0;}
