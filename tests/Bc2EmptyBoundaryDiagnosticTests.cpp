#include "Bc2MagazineEmptyDiagnostic.h"
#include "Test.h"
#include <bit>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
using namespace fvr::bc2;
namespace {
ManualEmptyDiagnosticContextBoundary Boundary(){return {true,true,true,true,true,true,1,0x50000,0x50000,0x90000,0x90000,0x6e9168,0x6e9168,7,0x80000,0xa0000};}
int Reads(){auto b=Boundary();CHECK(ManualEmptyDiagnosticContextReadAllowed(b));
 for(auto flag:{&ManualEmptyDiagnosticContextBoundary::combined,&ManualEmptyDiagnosticContextBoundary::request,&ManualEmptyDiagnosticContextBoundary::codeVerified,&ManualEmptyDiagnosticContextBoundary::entryAllowed,&ManualEmptyDiagnosticContextBoundary::parentPresent,&ManualEmptyDiagnosticContextBoundary::parentIsUpdate}){auto bad=b;bad.*flag=false;CHECK(!ManualEmptyDiagnosticContextReadAllowed(bad));}
 auto bad=b;bad.parentDepth=2;CHECK(!ManualEmptyDiagnosticContextReadAllowed(bad));bad=b;bad.parentFiring+=4;CHECK(!ManualEmptyDiagnosticContextReadAllowed(bad));
 bad=b;bad.parentInvocation=0;CHECK(!ManualEmptyDiagnosticContextReadAllowed(bad));bad=b;bad.parentContext+=4;CHECK(!ManualEmptyDiagnosticContextReadAllowed(bad));bad=b;bad.caller+=1;CHECK(!ManualEmptyDiagnosticContextReadAllowed(bad));
 bad=b;bad.stackLow=bad.context+1;CHECK(!ManualEmptyDiagnosticContextReadAllowed(bad));bad=b;bad.stackHigh=bad.context+0x2f;CHECK(!ManualEmptyDiagnosticContextReadAllowed(bad));bad=b;bad.context++;bad.parentContext=bad.context;CHECK(!ManualEmptyDiagnosticContextReadAllowed(bad));
 bad=b;bad.context=bad.parentContext=0xfffffffcu;bad.stackHigh=0xffffffffu;CHECK(!ManualEmptyDiagnosticContextReadAllowed(bad));return 0;
}
int Reasons(){using R=ManualEmptyBoundaryDiagnosticReason;ReloadUpdateContext ctx;ctx.deltaSeconds=.05f;ctx.reloadTimeMultiplier=1;ctx.flags24Through28[0]=true;
 CHECK(ManualEmptyBoundaryControlReason(.01f,ctx)==R::None);CHECK(ManualEmptyBoundaryControlReason(.05f,ctx)==R::None);
 CHECK(ManualEmptyBoundaryControlReason(std::nextafter(.1f,1.f),ctx)==R::StepDeltaTooLarge);
 CHECK(ManualEmptyBoundaryControlReason(0,ctx)==R::StepDeltaNonPositive);CHECK(ManualEmptyBoundaryControlReason(-.01f,ctx)==R::StepDeltaNonPositive);
 CHECK(ManualEmptyBoundaryControlReason(std::numeric_limits<float>::quiet_NaN(),ctx)==R::StepDeltaNonFinite);
 CHECK(ManualEmptyBoundaryControlReason(std::numeric_limits<float>::infinity(),ctx)==R::StepDeltaNonFinite);
 CHECK(ManualEmptyBoundaryControlReason(.01f,{})==R::ContextDecodeFailed);ctx.deltaSeconds=.005f;CHECK(ManualEmptyBoundaryControlReason(.01f,ctx)==R::StepDeltaExceedsContext);
 for(float hitch:{.0596221f,.1f}){ctx.deltaSeconds=hitch;CHECK(ManualEmptyBoundaryControlReason(hitch,ctx)==R::None);}
 ctx.deltaSeconds=std::nextafter(.1f,1.f);CHECK(ManualEmptyBoundaryControlReason(.01f,ctx)==R::ContextDeltaTooLarge);
 ctx.deltaSeconds=.02f;ctx.reloadTimeMultiplier=2;CHECK(ManualEmptyBoundaryControlReason(.01f,ctx)==R::ContextMultiplierNotOne);
 ctx.reloadTimeMultiplier=1;ctx.inputFlags=2;ctx.orderRequested=true;CHECK(ManualEmptyBoundaryControlReason(.01f,ctx)==R::UnsupportedInput);
 ctx.inputFlags=0;ctx.orderRequested=false;ctx.flags24Through28[2]=true;CHECK(ManualEmptyBoundaryControlReason(.01f,ctx)==R::UnsupportedContextFlags);
 return 0;
}
struct Native {unsigned calls=0,writes=0;std::uint8_t flag=0;};
bool Exchange(void* context,std::uint8_t expected,std::uint8_t replacement,std::uint8_t& observed){auto& n=*static_cast<Native*>(context);observed=n.flag;if(observed!=expected)return false;n.flag=replacement;++n.writes;return true;}
int OriginalUnchanged(){Native n;MagazineEmptyByteOverride patch;ReloadUpdateContext ctx;ctx.deltaSeconds=.05f;ctx.reloadTimeMultiplier=1;ctx.flags24Through28[0]=true;ctx.reloadTimeMultiplier=1;ctx.flags24Through28[0]=true;
 // A Step still cannot claim more elapsed time than its actual parent context.
 const float delta=.06f;const bool control=ValidManualReloadDelta(delta)&&delta<=ctx.deltaSeconds;
 CHECK(!control&&ManualEmptyDiagnosticContextReadAllowed(Boundary()));
 RunMagazineEmptyByteOverride({&n,Exchange},n.flag,control,[](void* context){++static_cast<Native*>(context)->calls;},&n,patch);
 CHECK(n.calls==1&&n.writes==0&&n.flag==0&&!patch.applied&&!patch.restored);return 0;
}
int Retention(){MagazineEmptyDiagnosticJournal j;MagazineEmptyDiagnosticRecord r;r.stage=MagazineEmptyDiagnosticStage::Policy;
 r.sample.branch=0;r.sample.identity.owner={0x10000,0x20000,0x30000,0x40000,1,1,1};r.sample.before.address=0x50000;
 r.hasBefore=r.hasAfter=true;r.sample.before.currentState=r.sample.before.nextState=2;r.sample.before.loaded=0;r.after=r.sample.before;r.after.nextState=10;
 r.boundaryReasonKnown=r.rawContextKnown=true;r.boundaryReason=unsigned(ManualEmptyBoundaryDiagnosticReason::StepDeltaTooLarge);
 r.stepDeltaBits=std::bit_cast<unsigned>(.06f);r.contextDeltaBits=std::bit_cast<unsigned>(.1f);j.Observe(r);
 for(unsigned n=0;n<10000;++n){MagazineEmptyDiagnosticRecord noise;noise.sample.before.address=0x80000000+n*4;j.Observe(noise);}
 CHECK(j.Size()==1&&j.Row(0).hasAfter&&j.Row(0).boundaryReason==unsigned(ManualEmptyBoundaryDiagnosticReason::StepDeltaTooLarge));
 auto undecoded=r;undecoded.boundaryReason=unsigned(ManualEmptyBoundaryDiagnosticReason::ContextDecodeFailed);undecoded.rawContextFlags[3]=2;j.Observe(undecoded);
 CHECK(j.Size()==2&&j.Row(1).rawContextFlags[3]==2);
 auto expired=r;expired.stage=MagazineEmptyDiagnosticStage::Boundary;expired.sample.identity={};expired.sample.branch=3;
 expired.contextMultiplierBits=123;expired.contextInputFlags=5;expired.rawContextFlags={1,2,3,4,5};j.Observe(expired);
 CHECK(j.Size()==3);const auto& stale=j.Row(2);CHECK(stale.leaseInvalidLastKnown);
 CHECK(!stale.boundaryReasonKnown&&!stale.rawContextKnown&&stale.boundaryReason==UINT32_MAX);
 CHECK(stale.stepDeltaBits==0&&stale.contextDeltaBits==0&&stale.contextMultiplierBits==0&&stale.contextInputFlags==0);
 for(auto flag:stale.rawContextFlags)CHECK(flag==0);
 CHECK(j.Row(0).boundaryReasonKnown&&j.Row(0).rawContextKnown&&j.Row(0).stepDeltaBits!=0);return 0;
}
}
int SelectedPumpUsesSameStrictStepBoundary(){
 auto b=Boundary();b.combined=false;
 CHECK(!ManualEmptyDiagnosticContextReadAllowed(b)); // Actual233 finite-mode omission.
 CHECK(!ManualEmptyDiagnosticAdmitted(false,true,false,false));
 b.nativeCycleTarget=true;CHECK(ManualEmptyDiagnosticContextReadAllowed(b));
 CHECK(ManualEmptyDiagnosticAdmitted(false,true,false,false,true));
 CHECK(!ManualEmptyDiagnosticAdmitted(false,false,false,false,true));
 for(auto flag:{&ManualEmptyDiagnosticContextBoundary::request,&ManualEmptyDiagnosticContextBoundary::codeVerified,
   &ManualEmptyDiagnosticContextBoundary::entryAllowed,&ManualEmptyDiagnosticContextBoundary::parentPresent,
   &ManualEmptyDiagnosticContextBoundary::parentIsUpdate,&ManualEmptyDiagnosticContextBoundary::nativeCycleTarget}){
  auto bad=b;bad.*flag=false;CHECK(!ManualEmptyDiagnosticContextReadAllowed(bad));}
 for(unsigned mutation=0;mutation<8;++mutation){auto bad=b;switch(mutation){
  case 0:bad.parentDepth=2;break;case 1:bad.parentFiring+=4;break;case 2:bad.parentInvocation=0;break;
  case 3:bad.parentContext+=4;break;case 4:bad.caller++;break;case 5:bad.stackLow=bad.context+1;break;
  case 6:bad.stackHigh=bad.context+0x2f;break;case 7:bad.context=bad.parentContext=0xfffffffcu;break;}
  CHECK(!ManualEmptyDiagnosticContextReadAllowed(bad));}
 return 0;
}
int main(){CHECK(SelectedPumpUsesSameStrictStepBoundary()==0);CHECK(Reads()==0);CHECK(Reasons()==0);CHECK(OriginalUnchanged()==0);CHECK(Retention()==0);std::cout<<"5 Step boundary diagnostic groups passed; bounded simulation delta, original evidence lifetime\n";}
