#include "Bc2MagazineEmptyControl.h"
#include "Bc2MagazineEmptyDiagnostic.h"
#include "Bc2MagazineReloadCycle.h"
#include <cstring>
#include "Bc2MagazineNativeFixture.h"
#include "Test.h"
#include <iostream>
using namespace fvr::bc2;
int main(){
 CHECK(ManualEmptyDiagnosticAdmitted(true,true,false,true));
 CHECK(ManualEmptyDiagnosticAdmitted(true,true,true,false));
 CHECK(!ManualEmptyDiagnosticAdmitted(true,true,false,false));
 CHECK(!ManualEmptyDiagnosticAdmitted(false,true,false,true));
 CHECK(!ManualEmptyDiagnosticAdmitted(true,false,false,true));
 auto i=test::Input();MagazineEmptyStep s;s.identity=i.identity;s.profile=NativeMagazineProfileId::ScopedXm8;
 s.ownerRevision=8;s.sourceSequence=77;s.observedNs=1000000000ll;s.deadlineNs=1000000000ll+100000000;s.branch=0;
 s.before=i.branches[0];s.before.currentState=s.before.nextState=2;s.before.loaded=0;
 for(auto phase:{ReloadRequestCyclePhase::Idle,ReloadRequestCyclePhase::Finished,ReloadRequestCyclePhase::Cancelled}){
  CHECK(ManualEmptyFamilyStepEligible(s,ReloadNativeFamily::Xm8Magazine,i.config,phase,i.context,1000000000ll+1));
  CHECK(ManualEmptyFamilyStepEligible(s,ReloadNativeFamily::Xm8Magazine,i.config,phase,i.context,1000000000ll+1)==MagazineEmptyStepEligible(s,i.config,phase,i.context,1000000000ll+1));
 }
 CHECK(ManualEmptyFamilyStepEligible(s,ReloadNativeFamily::Xm8Magazine,i.config,ReloadRequestCyclePhase::Arming,i.context,1000000000ll+1));
 for(auto phase:{ReloadRequestCyclePhase::Holding,ReloadRequestCyclePhase::Advancing})CHECK(!ManualEmptyFamilyStepEligible(s,ReloadNativeFamily::Xm8Magazine,i.config,phase,i.context,1000000000ll+1));
 i.context.inputFlags=4;i.context.reloadRequested=true;
 CHECK(!ManualEmptyFamilyStepEligible(s,ReloadNativeFamily::Xm8Magazine,i.config,ReloadRequestCyclePhase::Arming,i.context,1000000000ll+1));
 CHECK(ManualEmptyFamilyStepEligible(s,ReloadNativeFamily::Xm8Magazine,i.config,ReloadRequestCyclePhase::Idle,i.context,1000000000ll+1));
 i.context.flags24Through28[4]=true;CHECK(!ManualEmptyFamilyStepEligible(s,ReloadNativeFamily::Xm8Magazine,i.config,ReloadRequestCyclePhase::Idle,i.context,1000000000ll+1));
 std::cout<<"shared magazine gate and pre-cycle diagnostic regression passed\n";
}


