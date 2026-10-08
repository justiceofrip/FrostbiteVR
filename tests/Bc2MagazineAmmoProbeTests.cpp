#include "Test.h"
#include "Bc2MagazineAmmoProbe.h"
using namespace fvr::bc2;
namespace {
constexpr std::int64_t Start=1000000000,First=Start+2100000000;
ReloadHoldInput Sample(int rounds=30,int reserve=183,unsigned branch=0,std::int64_t now=First){
    ReloadHoldInput i;i.identity.owner={0x11000,0x12000,0x13000,0x14000,1,2,3};
    i.identity.firing={0x21000,0x22000,0x23000};i.identity.serverPlayer=0x31000;i.identity.serverSoldier=0x32000;i.identity.serverItem=0x33000;
    i.branch=branch;i.verified=true;i.nowNs=i.contextObservedNs=now;i.leaseDeadlineNs=now+100000000;
    i.context.deltaSeconds=.016f;i.context.reloadTimeMultiplier=1;i.context.flags24Through28[0]=true;
    for(unsigned n=0;n<3;++n){auto& s=i.branches[n];s.address=i.identity.firing[n];s.wrapperOffset=n==0?0x3c:n==1?0x40:0x10;
        s.currentState=s.nextState=2;s.loaded=rounds;s.reserve=reserve;i.capacities[n]=rounds;}
    return i;
}
AmmoMoveProbeDecision Begin(MagazineAmmoRoundtripProbe& p,ReloadHoldInput i){
    AmmoMoveProbeDecision d;
    for(unsigned n=0;n<3;++n){i.branch=n;d=p.Before(i,n+1,true,Start,First);}
    return d;
}
int ReturnsSameRoundsAfterActualOwnCopies(){
    for(int capacity:{8,15,30,32,100,200})for(int reserve:{0,183}){
        MagazineAmmoRoundtripProbe p;auto i=Sample(capacity,reserve);auto d=Begin(p,i);
        CHECK(d.call&&d.plan.delta==-capacity&&d.identity==i.identity&&d.invocation==3);
        CHECK(!p.Before(i,4,true,Start,First).call); // dispatch cannot be repeated
        p.Called(d,true,First);CHECK(p.Calls()==1&&p.Phase()==AmmoMoveProbePhase::Removed);
        for(unsigned n=0;n<3;++n){auto s=i.branches[n];s.loaded=0;
            p.ObserveOwnUpdate(i.identity,n,s,10+n,true,First+1000);}
        CHECK(p.EmptyMask()==7);
        i.contextObservedNs=i.nowNs=First+350000000;i.leaseDeadlineNs=i.nowNs+100000000;i.branch=2;
        for(auto& s:i.branches)s.loaded=0;
        d=p.Before(i,20,true,Start,i.nowNs);CHECK(d.call&&d.plan.delta==capacity&&d.plan.reserve==reserve);
        p.Called(d,true,i.nowNs);CHECK(p.ExactCalls()==2);
        for(unsigned n=0;n<3;++n){auto s=i.branches[n];s.loaded=capacity;
            p.ObserveOwnUpdate(i.identity,n,s,30+n,true,i.nowNs+1000);}
        CHECK(p.Phase()==AmmoMoveProbePhase::Complete&&p.ReturnedMask()==7&&p.Failure()==0);
        CHECK(!p.Before(i,100,true,Start,i.nowNs).call&&p.Calls()==2);
    }return 0;
}
int NoClientOperationOrStaleAdmission(){
    for(unsigned reason=0;reason<10;++reason){MagazineAmmoRoundtripProbe p;auto i=Sample();
        if(reason==0)i.verified=false;
        if(reason==1)i.context.inputFlags=1;
        if(reason==2)i.context.flags24Through28[4]=true;
        if(reason==3)i.leaseDeadlineNs=First;
        if(reason==4)i.contextObservedNs=First-50000000;
        if(reason==5)i.branches[1].loaded=29;
        if(reason==6)i.branches[2].reserve=1;
        if(reason==7)i.identity.firing[1]=i.identity.firing[0];
        if(reason==8)i.context.deltaSeconds=.101f;
        if(reason==9)i.identity.owner.equipGeneration=0;
        CHECK(!Begin(p,i).call&&p.Calls()==0);
    }
    MagazineAmmoRoundtripProbe p;auto i=Sample();
    for(unsigned n=0;n<20;++n){i.branch=n%2;CHECK(!p.Before(i,n+1,true,Start,First).call);}
    return 0;
}
int ConvergenceCannotBeFabricated(){
    MagazineAmmoRoundtripProbe p;auto i=Sample();const auto d=Begin(p,i);CHECK(d.call);p.Called(d,true,First);
    auto s=i.branches[0];s.loaded=0;
    auto wrong=i.identity;wrong.owner.equipGeneration++;
    p.ObserveOwnUpdate(wrong,0,s,10,true,First+1);
    p.ObserveOwnUpdate(i.identity,0,s,11,false,First+1);
    p.ObserveOwnUpdate(i.identity,1,s,12,true,First+1);
    p.ObserveOwnUpdate(i.identity,0,s,13,true,First-1);
    CHECK(p.EmptyMask()==0);
    i.contextObservedNs=i.nowNs=First+1500000000;i.leaseDeadlineNs=i.nowNs+100000000;i.branch=2;
    for(auto& state:i.branches)state.loaded=0;
    const auto restore=p.Before(i,20,true,Start,i.nowNs);CHECK(restore.call); // restore even if replication never converged
    p.Called(restore,true,i.nowNs);
    for(unsigned n=0;n<3;++n){s=i.branches[n];s.loaded=30;p.ObserveOwnUpdate(i.identity,n,s,30+n,true,i.nowNs+1);}
    CHECK(p.ReturnedMask()==7&&p.Phase()==AmmoMoveProbePhase::Failed&&p.Failure()==8);
    return 0;
}
int NeverRetryAmbiguousSideEffects(){
    MagazineAmmoRoundtripProbe p;auto i=Sample();const auto d=Begin(p,i);CHECK(d.call);p.Called(d,false,First);
    CHECK(p.Phase()==AmmoMoveProbePhase::Failed&&p.ExactCalls()==0&&p.Calls()==1);
    CHECK(!p.Before(i,8,true,Start,First).call);
    return 0;
}
int RefillUsesFamilyCounts(){
    for(int type:{-1,0,1,2})for(int reserve:{0,183}){
        if((type==0||type==1)&&reserve>0)continue;
        MagazineAmmoRoundtripProbe p{true};auto i=Sample(30,reserve);i.config.reloadType=type;
        CHECK(!Begin(p,i).call&&p.Calls()==0);
    }
    for(int type:{0,1})for(int reserve:{1,23,183}){
        MagazineAmmoRoundtripProbe p{true};auto i=Sample(30,reserve);i.config.reloadType=type;
        const auto remove=Begin(p,i);CHECK(remove.call&&!remove.refill);p.Called(remove,true,First);
        for(unsigned n=0;n<3;++n){auto s=i.branches[n];s.loaded=0;p.ObserveOwnUpdate(i.identity,n,s,10+n,true,First+1);}
        i.branch=2;i.contextObservedNs=i.nowNs=First+350000000;i.leaseDeadlineNs=i.nowNs+100000000;
        for(auto& s:i.branches)s.loaded=0;
        const auto refill=p.Before(i,20,true,Start,i.nowNs);CHECK(refill.call&&refill.refill);
        CHECK(refill.refill->units==std::min(reserve,type==0?1:30));p.Called(refill,true,i.nowNs);
        for(unsigned n=0;n<3;++n){auto s=i.branches[n];s.loaded=p.ExpectedLoaded();s.reserve=p.ExpectedReserve();
            p.ObserveOwnUpdate(i.identity,n,s,30+n,true,i.nowNs+1);}
        CHECK(p.Phase()==AmmoMoveProbePhase::Complete&&p.Calls()==2&&p.ExactCalls()==2);
        CHECK(p.ExpectedLoaded()+p.ExpectedReserve()==reserve); // original rounds remain outside native ammo
    }return 0;
}
}
int main(){
    if(ReturnsSameRoundsAfterActualOwnCopies()||NoClientOperationOrStaleAdmission()||ConvergenceCannotBeFabricated()||NeverRetryAmbiguousSideEffects()||RefillUsesFamilyCounts())return 1;
    std::puts("MagazineAmmoProbe: bounded server-only experiment policy passed; native scheduling remains separate");return 0;
}
