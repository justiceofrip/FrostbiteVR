#include "Bc2MagazineEmptyControl.h"
#include "Bc2MagazineEmptyDiagnostic.h"
#include "Bc2MagazineReloadCycle.h"
#include <cstring>
#include "Bc2MagazineNativeFixture.h"
#include "Test.h"
#include <iostream>
#include <stdexcept>
using namespace fvr::bc2;
int ScopedHitchUsesDecodedContext(){
 // Exercise the production decoder, admission and byte scope together. This
 // verifies context preservation; native firing evidence is a separate run.
 for(float hitch:{.16f,.24f,.25f})for(unsigned branch=0;branch<3;++branch)
 for(bool brokenTls:{false,true})for(bool throwOriginal:{false,true}){
  auto i=test::Input();MagazineEmptyStep s;s.identity=i.identity;s.ownerRevision=8;s.sourceSequence=77;
  s.observedNs=1000000000ll;s.deadlineNs=s.observedNs+100000000;s.branch=branch;
  s.before=i.branches[branch];s.before.currentState=s.before.nextState=2;s.before.loaded=0;
  struct Call {std::array<std::byte,0x30> context{},expected{};unsigned originals=0,writes=0;bool exact=true,throws=false;} call;
  for(unsigned n=0;n<call.context.size();++n)call.context[n]=std::byte(0x80+n);
  const float multiplier=1;const unsigned flags=1;
  std::memcpy(call.context.data()+0x18,&hitch,sizeof(hitch));
  std::memcpy(call.context.data()+0x20,&multiplier,sizeof(multiplier));
  std::memcpy(call.context.data()+0x2c,&flags,sizeof(flags));
  for(unsigned n=0;n<5;++n)call.context[0x24+n]=std::byte(n==0);
  const auto original=call.context;const auto decoded=DecodeReloadUpdateContext(original);CHECK(decoded);
  CHECK(decoded->deltaSeconds==hitch&&!ValidManualReloadDelta(decoded->deltaSeconds));
  ManualEmptyDiagnosticContextBoundary boundary{true,true,true,true,true,true,1,s.before.address,s.before.address,
   0x90000,0x90000,0x6e9168,0x6e9168,7,0x80000,0xa0000};
  if(brokenTls)boundary.parentContext+=4;
  const bool control=ManualEmptyDiagnosticContextReadAllowed(boundary)&&
   ValidManualEmptyStepDelta(hitch,decoded->deltaSeconds)&&
   ManualEmptyFamilyStepEligible(s,ReloadNativeFamily::Xm8Magazine,i.config,ReloadRequestCyclePhase::Idle,*decoded,s.observedNs+1);
  CHECK(control==!brokenTls);call.expected=original;if(control)call.expected[0x28]=std::byte{1};call.throws=throwOriginal;
  const MagazineEmptyByteAccess access{&call,[](void* p,std::uint8_t expected,std::uint8_t replacement,std::uint8_t& seen)noexcept{
   auto& c=*static_cast<Call*>(p);seen=std::to_integer<std::uint8_t>(c.context[0x28]);
   if(seen==expected){c.context[0x28]=std::byte(replacement);++c.writes;}return true;}};
  MagazineEmptyByteOverride patch;bool caught=false;
  try {RunMagazineEmptyByteOverride(access,0,control,[](void* p){auto& c=*static_cast<Call*>(p);++c.originals;
   c.exact=c.context==c.expected;if(c.throws)throw std::runtime_error("original Step");},&call,patch);}
  catch(const std::runtime_error&){caught=true;}
  CHECK(caught==throwOriginal&&call.exact&&call.originals==1&&call.context==original);
  CHECK(call.writes==(control?2u:0u)&&patch.applied==control&&patch.restored==control);
  CHECK(s.sourceSequence==77&&s.observedNs==1000000000ll&&s.deadlineNs==1100000000ll);
 }
 return 0;
}
int main(){
 CHECK(ScopedHitchUsesDecodedContext()==0);
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


