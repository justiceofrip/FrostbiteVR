#include "Bc2ReloadNativePolicy.h"
#include "Bc2ReloadAbort.h"
#include "Bc2ReloadRetirement.h"
#include "Bc2ReloadFlowRuntime.h"
#include "Test.h"
#include <bit>
#include <cstring>
#include "Bc2MagazineNativeFixture.h"
using namespace fvr::bc2;
using namespace fvr::interaction;
namespace {
int FrozenLookupAndExactDefault(){
 CHECK(ResolveMagazineNativeProfile(NativeMagazineProfileId::ScopedXm8)==&Xm8MagazineNativeProfile);
 CHECK(ResolveMagazineNativeProfile(NativeMagazineProfileId::AuthoredAek)==&AekMagazineNativeProfile);
 CHECK(!ResolveMagazineNativeProfile(static_cast<NativeMagazineProfileId>(255)));
 Bc2ReloadNativePolicy p;CHECK(p.MagazineProfileId()==NativeMagazineProfileId::ScopedXm8);
 CHECK(&p.MagazineProfile()==&Xm8MagazineNativeProfile);
 const auto in=test::Input();CHECK(p.MagazineProfile().Matches(in.config));
 CHECK(std::bit_cast<unsigned>(p.MagazineProfile().HoldCeiling())==std::bit_cast<unsigned>(2.1f));
 CHECK(p.MagazineProfile().TailCeiling()==2.8f*(1.f-.75f));
 CHECK(p.MagazineProfile().completionDeadlineNs==Bc2MagazineReloadCycle::AdvanceNs);
 const auto& a=AekMagazineNativeProfile.configuration.values;
 CHECK(a.fireLogicType==2&&a.reloadType==1&&a.fireInputAction==8&&a.reloadInputAction==29);
 CHECK(a.reloadTime==3.2f&&a.reloadThreshold==.75f&&!a.boltTime&&!a.boltDelay);
 CHECK(AekMagazineNativeProfile.Reviewed());return 0;
}
int UnreviewedSelectionNeverMutatesDispatch(){
 Bc2ReloadNativePolicy p;
 for(auto id:{static_cast<NativeMagazineProfileId>(255)}){
  CHECK(!p.SelectMagazineProfile(id,true,true));CHECK(!p.IsMagazine());
  CHECK(p.MagazineProfileId()==NativeMagazineProfileId::ScopedXm8);
 }
 CHECK(!p.SelectMagazineProfile(NativeMagazineProfileId::ScopedXm8,false,true));
 CHECK(p.SelectMagazineProfile(NativeMagazineProfileId::ScopedXm8,true,false));
 CHECK(p.IsMagazine()&&p.Phase()==ReloadRequestCyclePhase::Idle);
 CHECK(!p.SelectMagazineProfile(NativeMagazineProfileId::AuthoredAek,false,true));
 CHECK(p.SelectMagazineProfile(NativeMagazineProfileId::AuthoredAek,true,true));
 CHECK(p.IsMagazine()&&&p.MagazineProfile()==&AekMagazineNativeProfile);return 0;
}
int ActualHeldCycleCannotRebind(){
 test::Simulation f;CHECK(f.Arm());const auto old=f.Lease();CHECK(old&&old->allThreeHeld);
 CHECK(!f.policy.SelectProfile(Xm8MagazineNativeProfile,true,true));
 CHECK(!f.policy.SelectProfile(AekMagazineNativeProfile,true,true));
 CHECK(f.Lease()->cycle==old->cycle&&f.Lease()->loaded==old->loaded&&f.Lease()->reserve==old->reserve);
 CHECK(f.Tick());const auto d=f.Begin(0);CHECK(d.hold);
 f.policy.Cancel();CHECK(!f.policy.SelectProfile(Xm8MagazineNativeProfile,true,true));
 CHECK(f.policy.DrainCancelledInvocations(true));
 CHECK(!f.policy.SelectProfile(Xm8MagazineNativeProfile,false,true));
 CHECK(!f.policy.SelectProfile(Xm8MagazineNativeProfile,true,false));
 CHECK(f.policy.SelectProfile(Xm8MagazineNativeProfile,true,true));
 CHECK(!f.policy.Allows(d,f.input.nowNs));CHECK(!f.Lease());
 // Rebinding must preserve monotonic cycle and operation IDs, not reset them.
 CHECK(!f.policy.Start(f.control,{2,test::Owner(f.input.identity),ReloadOperation::UnseatMagazine,0,0},f.input.nowNs));
 ++f.control.cycle;
 CHECK(!f.policy.Start(f.control,{1,test::Owner(f.input.identity),ReloadOperation::UnseatMagazine,0,0},f.input.nowNs));
 CHECK(f.policy.Start(f.control,{2,test::Owner(f.input.identity),ReloadOperation::UnseatMagazine,0,0},f.input.nowNs));
 return 0;
}
int RuntimePolicyRetirementStillRequired(){
 auto in=test::Input();ReloadCycleControl c{in.identity,1,1,in.nowNs,in.leaseDeadlineNs,true};
 Bc2ReloadNativePolicy p;CHECK(p.Start(c,in.nowNs));
 CHECK(!p.SelectMagazineProfile(NativeMagazineProfileId::ScopedXm8,true,true));
 p.Cancel();CHECK(!p.SelectMagazineProfile(NativeMagazineProfileId::ScopedXm8,true,false));
 CHECK(CancelAndDrainReloadCycle(p,in.identity,1,true));
 CHECK(p.SelectMagazineProfile(NativeMagazineProfileId::ScopedXm8,true,true));
 CHECK(p.StartMagazine(c,{1,test::Owner(in.identity),ReloadOperation::UnseatMagazine,0,0},in.nowNs));
 CHECK(!p.SelectMagazineProfile(NativeMagazineProfileId::AuthoredAek,true,true));
 CHECK(!p.SelectFamily(ReloadNativeFamily::SpasTube,true,true));
 CHECK(p.Phase()==ReloadRequestCyclePhase::Arming);
 CHECK(CancelAndDrainReloadCycle(p,in.identity,1,true));
 CHECK(p.SelectFamily(ReloadNativeFamily::SpasTube,true,true));return 0;
}
ReloadRoundLease Lease(const test::Simulation& f){const auto& b=f.input.branches[0];return {f.input.identity,f.control.cycle,1,f.input.nowNs,f.input.leaseDeadlineNs,b.loaded,b.reserve,30,true,true};}
int AbortUsesSelectedProfileWithoutExtendingLease(){
 test::Simulation f;const auto lease=Lease(f);ReloadAbortCleanup original,profile;
 CHECK(original.Arm(lease,1,f.input.nowNs,true,ReloadAbortTiming::VerifiedXm8Magazine));
 CHECK(profile.ArmMagazine(lease,1,f.input.nowNs,true,Xm8MagazineNativeProfile));
 auto in=f.input;in.nowNs+=1000;
 const auto a=original.Claim(in,1,lease.cycle,true,9,true,in.nowNs);
 const auto b=profile.Claim(in,1,lease.cycle,true,9,true,in.nowNs);
 CHECK(a.call&&b.call&&a.deadlineNs==b.deadlineNs&&a.deadlineNs==lease.deadlineNs);
 auto after=b.before;after.currentState=after.nextState=2;after.phaseTimer=0;
 CHECK(profile.Finish(b,after,true,true,in.nowNs));CHECK(after.loaded==lease.loaded&&after.reserve==lease.reserve);
 ReloadAbortCleanup late;CHECK(late.ArmMagazine(lease,1,f.input.nowNs,true,Xm8MagazineNativeProfile));
 CHECK(!late.Claim(in,1,lease.cycle,true,9,true,lease.deadlineNs).call);
 CHECK(late.Failure()==ReloadAbortFailure::Expired);return 0;
}
int ProfileAbortCannotAdmitCandidateOrUnsafeCounts(){
 test::Simulation f;auto lease=Lease(f);ReloadAbortCleanup p;
 auto candidate=AekMagazineNativeProfile;candidate.cycleAdmission=MagazineCycleAdmission::Candidate;
 CHECK(!p.ArmMagazine(lease,1,f.input.nowNs,true,candidate));
 CHECK(!p.Active());lease.loaded=0;
 CHECK(!p.ArmMagazine(lease,1,f.input.nowNs,true,Xm8MagazineNativeProfile));
 lease=Lease(f);lease.reserve=0;
 CHECK(p.ArmMagazine(lease,1,f.input.nowNs,true,Xm8MagazineNativeProfile));
 auto in=f.input;in.nowNs+=1000;for(auto& b:in.branches)b.reserve=0;
 CHECK(p.Claim(in,1,lease.cycle,true,9,true,in.nowNs).call);
 ReloadAbortCleanup pending;CHECK(!pending.ArmMagazine(Lease(f),1,f.input.nowNs,false,Xm8MagazineNativeProfile));
 return 0;
}
int ParameterizedAbortTimingDoesNotBorrowXm8Ceiling(){
 // Same verified implementation with different authored timing. Geometry remains unbound.
 test::Simulation f;auto profile=AekMagazineNativeProfile;
 ReloadAbortCleanup shared,xm8;const auto lease=Lease(f);
 CHECK(shared.ArmMagazine(lease,1,f.input.nowNs,true,profile));
 CHECK(xm8.ArmMagazine(lease,1,f.input.nowNs,true,Xm8MagazineNativeProfile));
 auto in=f.input;in.nowNs+=1000;in.branches[0].phaseTimer=2.2f;
 CHECK(shared.Claim(in,1,lease.cycle,true,9,true,in.nowNs).call);
 CHECK(!xm8.Claim(in,1,lease.cycle,true,9,true,in.nowNs).call);
 CHECK(xm8.Failure()==ReloadAbortFailure::State);
 auto bad=profile;bad.configuration.values.reloadTime=std::numeric_limits<float>::infinity();
 ReloadAbortCleanup invalid;CHECK(!invalid.ArmMagazine(lease,1,f.input.nowNs,true,bad));return 0;
}
int DataRebindKeepsOriginalControlDeadline(){
 test::Simulation f;CHECK(f.Arm());const auto lease=f.Lease();CHECK(lease);
 f.policy.Cancel();CHECK(f.policy.DrainCancelledInvocations(true));
 CHECK(f.policy.SelectProfile(Xm8MagazineNativeProfile,true,true));
 auto c=f.control;++c.cycle;
 CHECK(!f.policy.Start(c,{2,test::Owner(f.input.identity),ReloadOperation::UnseatMagazine,0,0},c.deadlineNs));
 CHECK(!f.policy.KeepAlive(c,c.deadlineNs));CHECK(!f.policy.Lease(c.identity,c.cycle,c.deadlineNs));return 0;
}
}
int main(){CHECK(!FrozenLookupAndExactDefault());CHECK(!UnreviewedSelectionNeverMutatesDispatch());
 CHECK(!ActualHeldCycleCannotRebind());CHECK(!RuntimePolicyRetirementStillRequired());
 CHECK(!AbortUsesSelectedProfileWithoutExtendingLease());CHECK(!ProfileAbortCannotAdmitCandidateOrUnsafeCounts());
 CHECK(!ParameterizedAbortTimingDoesNotBorrowXm8Ceiling());CHECK(!DataRebindKeepsOriginalControlDeadline());
 std::puts("8 runtime magazine profile, retirement and abort groups passed");return 0;}
