#include "Test.h"
#include "Bc2ReloadHold.h"
#include "Bc2PumpDiagnostic.h"
#include "NativeProbeConfig.h"
#include <cstring>
#include <limits>
#include <thread>
#include <vector>
#if defined(_MSC_VER)
#include <Windows.h>
#endif
using namespace fvr::bc2;
namespace {
ReloadHoldInput Input(){
    ReloadHoldInput i;i.verified=true;i.branch=0;i.nowNs=1000000000;i.leaseDeadlineNs=i.nowNs+100000000;
    i.identity.owner={0x10000,0x20000,0x30000,0x40000,1,2,3};i.identity.firing={0x50000,0x60000,0x70000};
    i.identity.serverPlayer=0x80000;i.identity.serverSoldier=0x90000;i.identity.serverItem=0xa0000;
    auto& c=i.config;std::memcpy(c.assetName.data(),"SPAS12_sp",sizeof("SPAS12_sp"));std::memcpy(c.assetPath.data(),"Objects/Weapons/Handheld/UL_shg_SPAS12/SP_shg_SPAS12",sizeof("Objects/Weapons/Handheld/UL_shg_SPAS12/SP_shg_SPAS12"));
    c.weaponData=0xb0000;c.firingData=0xc0000;c.primaryFire=0xd0000;c.ammoAddress=c.primaryFire+0x170;
    c.fireLogicType=1;c.reloadType=0;c.fireInputAction=8;c.reloadInputAction=29;c.baseCapacity=c.numberOfMagazines=4;
    c.reloadDelay=.06f;c.reloadTime=.72f;c.reloadThreshold=c.postReloadTime=1;c.boltDelay=.5f;
    i.context.deltaSeconds=.005f;i.context.reloadTimeMultiplier=1;i.context.flags24Through28[0]=true;
    for(unsigned n=0;n<3;++n){auto& b=i.branches[n];b.address=i.identity.firing[n];b.wrapperOffset=n==0?0x3c:n==1?0x40:0x10;
        b.currentState=11;b.nextState=12;b.phaseTimer=.2f;b.loaded=2;b.reserve=8;i.capacities[n]=8;}
    return i;
}
bool Arm(ReloadHoldProbe& p,ReloadHoldInput& i){if(!p.Enable())return false;
    for(unsigned n=0;n<3;++n){i.branch=n;i.nowNs+=1000;if(p.Evaluate(i)!=(n==2))return false;}return true;
}

ReloadHoldInput PumpInput(){auto i=Input();for(auto& b:i.branches){b.currentState=7;b.previousState=6;b.nextState=8;b.loaded=7;b.reserve=0;}return i;}
bool ArmPump(ReloadHoldProbe& p,ReloadHoldInput& i){if(!p.Enable(ReloadHoldTarget::SpasPump))return false;
    for(unsigned n=0;n<3;++n){i.branch=n;i.nowNs+=1000;if(p.Evaluate(i)!=(n==2))return false;}return true;}
int PumpTargetSeparateAndBounded(){
    auto i=PumpInput();ReloadHoldProbe p;CHECK(ArmPump(p,i)&&p.Target()==ReloadHoldTarget::SpasPump);
    CHECK(p.Loaded()==7&&p.Reserve()==0&&p.DeadlineNs()-p.BeginNs()==350000000);
    for(unsigned n=0;n<3;++n){i.branch=n;i.nowNs+=1000;CHECK(p.Evaluate(i));}
    CHECK(!p.Allows(p.DeadlineNs())&&p.Phase()==ReloadHoldPhase::Released);
    ReloadHoldProbe old;CHECK(old.Enable());i=PumpInput();for(unsigned n=0;n<3;++n){i.branch=n;CHECK(!old.Evaluate(i));}
    ReloadHoldProbe pump;CHECK(pump.Enable(ReloadHoldTarget::SpasPump));i=Input();for(unsigned n=0;n<3;++n){i.branch=n;CHECK(!pump.Evaluate(i));}
    return 0;
}
int PumpRejectsUnprovedCohorts(){
    for(unsigned test=0;test<12;++test){ReloadHoldProbe p;CHECK(p.Enable(ReloadHoldTarget::SpasPump));auto i=PumpInput();
        switch(test){case 0:for(auto& b:i.branches)b.loaded=0;break;case 1:i.branches[0].previousState=5;break;
        case 2:i.branches[1].currentState=8;break;case 3:i.branches[2].nextState=1;break;
        case 4:for(auto& b:i.branches)b.loaded=8;break;case 5:i.branches[1].reserve=1;break;
        case 6:i.context.fireRequested=true;break;case 7:i.context.reloadRequested=true;break;
        case 8:i.context.flags24Through28[3]=true;break;case 9:for(auto& b:i.branches)b.phaseTimer=.3f;break;
        case 10:i.config.boltDelay=.4f;break;case 11:i.branches[2].flagsA8=8;break;}
        for(unsigned n=0;n<3;++n){i.branch=n;CHECK(!p.Evaluate(i));}CHECK(p.Phase()==ReloadHoldPhase::Waiting);
    }
    ReloadHoldProbe p;auto i=PumpInput();CHECK(ArmPump(p,i));i.branches[1].currentState=8;
    CHECK(!p.Evaluate(i)&&p.Phase()==ReloadHoldPhase::Aborted);return 0;
}
int PumpSchemaAndTriggerIsolation(){
    NativeProbeConfig c;CHECK(sizeof(c)==1184&&c.bytes==1184&&c.pumpHoldDiagnostic==PumpHoldDiagnostic::Disabled);
    constexpr auto mode=PumpHoldDiagnostic::SpasOneShot;constexpr unsigned flags=9u|0x197800u|0x400000u;
    CHECK(ValidPumpHoldDiagnostic(mode,flags,15000));
    for(unsigned bit=0;bit<32;++bit)if(!(flags&(1u<<bit))&&bit!=9)CHECK(!ValidPumpHoldDiagnostic(mode,flags|(1u<<bit),15000));
    CHECK(!ValidPumpHoldDiagnostic(mode,flags,30000)&&!ValidPumpHoldDiagnostic(PumpHoldDiagnostic(2),flags,15000));
    CHECK(PumpDiagnosticTrigger(2999)==0&&PumpDiagnosticTrigger(3000)==1&&PumpDiagnosticTrigger(3079)==1&&PumpDiagnosticTrigger(3080)==0);
    CHECK(PumpDiagnosticTrigger(5999)==0&&PumpDiagnosticTrigger(6000)==1&&PumpDiagnosticTrigger(6080)==0&&PumpDiagnosticTrigger(UINT64_MAX)==0);
    return 0;
}

int DefaultOffAndOneShot(){
    ReloadHoldProbe p;auto i=Input();CHECK(!p.Evaluate(i)&&p.Phase()==ReloadHoldPhase::Disabled);
    CHECK(Arm(p,i));CHECK(!p.Enable());const auto begin=p.BeginNs(),deadline=p.DeadlineNs();CHECK(deadline-begin==350000000);
    CHECK(p.Identity()==i.identity&&p.Loaded()==2&&p.Reserve()==8);
    for(unsigned n=0;n<3;++n){i.branch=n;i.nowNs+=1000;CHECK(p.Evaluate(i)&&p.Targets(i.identity.firing[n]));}
    CHECK(p.Allows(deadline-1));CHECK(!p.Allows(deadline)&&p.Phase()==ReloadHoldPhase::Released&&p.Reason()==ReloadHoldReason::Expired);
    i.nowNs=deadline+1000;i.leaseDeadlineNs=i.nowNs+100000000;CHECK(!p.Evaluate(i)&&!p.Enable());return 0;
}
int RequiresThreeFreshSafeBranches(){
    ReloadHoldProbe p;CHECK(p.Enable());auto i=Input();
    CHECK(!p.Evaluate(i));i.branch=1;i.context.fireRequested=true;CHECK(!p.Evaluate(i));i.context.fireRequested=false;
    i.branch=2;CHECK(!p.Evaluate(i));i.branch=1;i.nowNs+=1000;CHECK(p.Evaluate(i));
    ReloadHoldProbe stale;CHECK(stale.Enable());i=Input();CHECK(!stale.Evaluate(i));i.branch=1;CHECK(!stale.Evaluate(i));
    i.nowNs+=ReloadHoldProbe::ContextFreshNs+1;i.leaseDeadlineNs=i.nowNs+100000000;i.branch=2;CHECK(!stale.Evaluate(i));
    for(unsigned n=0;n<2;++n){i.branch=n;const auto held=stale.Evaluate(i);CHECK(held==(n==1));}return 0;
}
int UnsafeConditionsAbortWithoutRearm(){
    for(unsigned test=0;test<15;++test){ReloadHoldProbe p;auto i=Input();CHECK(Arm(p,i));i.nowNs+=1000;
        switch(test){case 0:i.identity.owner.equipGeneration++;break;case 1:i.identity.serverItem++;break;
            case 2:i.context.inputFlags=1;break;case 3:i.context.flags24Through28[4]=true;break;
            case 4:i.branches[2].loaded++;break;case 5:i.branches[1].currentState=12;break;
            case 6:i.branches[0].flagsA8=16;break;case 7:i.branches[2].flagsA8=8;break;
            case 8:i.verified=false;break;case 9:i.leaseDeadlineNs=i.nowNs;break;
            case 10:i.config.assetName[0]='X';break;case 11:i.config.reloadTime=1;break;
            case 12:i.context.deltaSeconds=.1f;break;case 13:i.capacities[2]=4;break;case 14:i.context.deltaSeconds=std::numeric_limits<float>::quiet_NaN();break;}
        CHECK(!p.Evaluate(i)&&p.Phase()==ReloadHoldPhase::Aborted);i=Input();CHECK(!p.Evaluate(i)&&!p.Enable());}
    ReloadHoldProbe p;auto i=Input();CHECK(Arm(p,i));p.Stop();CHECK(p.Phase()==ReloadHoldPhase::Aborted&&!p.Allows(i.nowNs));return 0;
}
int ArmingWindowAndOwnerReset(){
    for(float timer:{.099f,.251f,0.f,-1.f}){ReloadHoldProbe p;CHECK(p.Enable());auto i=Input();
        for(auto& b:i.branches)b.phaseTimer=timer;for(unsigned n=0;n<3;++n){i.branch=n;CHECK(!p.Evaluate(i));}CHECK(p.Phase()==ReloadHoldPhase::Waiting);}
    ReloadHoldProbe p;CHECK(p.Enable());auto i=Input();CHECK(!p.Evaluate(i));i.identity.owner.actorGeneration++;i.branch=1;CHECK(!p.Evaluate(i));
    i.branch=2;CHECK(!p.Evaluate(i));i.branch=0;CHECK(p.Evaluate(i));return 0;
}
int SharedDeadlineAcrossThreads(){
    ReloadHoldProbe p;auto i=Input();CHECK(Arm(p,i));std::atomic<unsigned> good=0;std::vector<std::thread> threads;
    for(unsigned n=0;n<3;++n)threads.emplace_back([&,n]{for(unsigned k=0;k<1000;++k)if(p.Allows(i.nowNs+1000)&&p.Targets(i.identity.firing[n]))++good;});
    for(auto& t:threads)t.join();CHECK(good==3000);threads.clear();
    for(unsigned n=0;n<3;++n)threads.emplace_back([&]{if(p.Allows(p.DeadlineNs()))++good;});
    for(auto& t:threads)t.join();CHECK(good==3000&&p.Phase()==ReloadHoldPhase::Released);return 0;
}
struct DeltaFixture {
    unsigned words[12]{},calls=0;bool failCompare=false,failRestore=false,expectZero=true,matched=false;
    unsigned Bits()const{return words[6];}
    DeltaFixture(){float dt=.005f;std::memcpy(&words[6],&dt,4);words[4]=123;words[5]=456;words[7]=789;}
    ReloadDeltaAccess Access(){return {this,[](void* raw,unsigned expected,unsigned replacement,unsigned& observed){
        auto& f=*static_cast<DeltaFixture*>(raw);if(f.failCompare)return false;observed=f.words[6];if(observed==expected)f.words[6]=replacement;return true;
    },[](void* raw,unsigned value,unsigned& previous){auto& f=*static_cast<DeltaFixture*>(raw);if(f.failRestore)return false;
        previous=f.words[6];f.words[6]=value;return true;}};}
};
void Original(void* raw){auto& f=*static_cast<DeltaFixture*>(raw);++f.calls;f.matched=f.expectZero?f.words[6]==0:f.words[6]!=0;f.words[5]=0xdeadbeef;}
int ExactDeltaOriginalOnce(){
    for(bool request:{false,true}){DeltaFixture f;f.expectZero=request;const auto expected=f.Bits();auto access=f.Access();ReloadDeltaOverride t;
        RunReloadDeltaOverride(access,expected,request,Original,&f,t);
        CHECK(f.calls==1&&f.matched&&f.Bits()==expected&&f.words[4]==123&&f.words[5]==0xdeadbeef&&f.words[7]==789);
        CHECK(t.applied==request&&t.restored==request&&!t.unexpectedNativeWrite);CHECK(!t.Restore(access));}
    for(unsigned test=0;test<3;++test){DeltaFixture f;f.expectZero=false;const auto expected=f.Bits();auto access=f.Access();ReloadDeltaOverride t;
        if(test==0)f.failCompare=true;
        RunReloadDeltaOverride(access,test==1?expected+1:test==2?0:expected,true,Original,&f,t);
        CHECK(f.calls==1&&f.matched&&!t.applied&&f.Bits()==expected);}
    DeltaFixture f;auto access=f.Access();ReloadDeltaOverride t;const auto expected=f.Bits();CHECK(t.Apply(access,expected));
    f.words[6]=42;CHECK(!t.Restore(access)&&t.restored&&t.unexpectedNativeWrite&&f.Bits()==expected);return 0;
}
#if defined(_MSC_VER)
void RaisingOriginal(void* raw){Original(raw);RaiseException(0xe0424242,0,0,nullptr);}
int SehCleanup(){
    DeltaFixture f;const auto expected=f.Bits();auto access=f.Access();ReloadDeltaOverride t;bool caught=false;
    __try {RunReloadDeltaOverride(access,expected,true,RaisingOriginal,&f,t);}
    __except(GetExceptionCode()==0xe0424242?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH){caught=true;}
    CHECK(caught&&f.calls==1&&f.matched&&t.applied&&t.restored&&!t.unexpectedNativeWrite&&f.Bits()==expected&&f.words[5]==0xdeadbeef);return 0;
}
#endif
}
int main(){if(PumpTargetSeparateAndBounded()||PumpRejectsUnprovedCohorts()||PumpSchemaAndTriggerIsolation()||DefaultOffAndOneShot()||RequiresThreeFreshSafeBranches()||UnsafeConditionsAbortWithoutRearm()||ArmingWindowAndOwnerReset()||SharedDeadlineAcrossThreads()||ExactDeltaOriginalOnce())return 1;
#if defined(_MSC_VER)
    if(SehCleanup())return 1;
#endif
    std::printf("Ten diagnostic reload/pump-hold groups passed, including original-once exact delta restoration on native SEH.\n");return 0;
}
